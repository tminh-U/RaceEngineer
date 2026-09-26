"""Audit and, when the data supports it, forecast AC simulator tyre condition."""

import argparse
import hashlib
import json
import re
import sys
import urllib.request
from pathlib import Path

import duckdb
import numpy as np

from vehicle_category import vehicle_category


ROOT = Path(__file__).resolve().parent / ".local_train" / "ac_wear"
WHEELS = ("fl", "fr", "rl", "rr")
HORIZON_S = 60
DATASETS = (
    ("Laguna-Seca-Lap-data", "ks_audi_r8_lms_2016", "ks_laguna_seca", 10),
    ("Silverstone-1967-Lap-data", "bmw_z4_gt3", "ks_silverstone1967", 15),
    ("Silverstone-GP-lap-data", "ks_porsche_911_gt3_r_2016", "ks_silverstone", 12),
    ("Nordschleife-Lap-data", "mercedes_sls_gt3", "ks_nordschleife", 3),
)
COMMON = ("speed_kmh", "g_lat", "g_lon", "slip_angle", "throttle", "brake", "steer_angle")


def _get(url):
    request = urllib.request.Request(url, headers={"User-Agent": "RaceEngineer-AC-wear-research/1.0"})
    with urllib.request.urlopen(request, timeout=180) as response:
        return response.read()


def _sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _download(url, path):
    temporary = path.with_suffix(path.suffix + ".part")
    request = urllib.request.Request(url, headers={"User-Agent": "RaceEngineer-AC-wear-research/1.0"})
    try:
        with urllib.request.urlopen(request, timeout=180) as response, temporary.open("wb") as output:
            while chunk := response.read(1024 * 1024):
                output.write(chunk)
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def _source(slug, offline):
    repo = f"Nasim435/{slug}"
    folder = ROOT / "raw" / slug
    folder.mkdir(parents=True, exist_ok=True)
    record_path = folder / "source.json"
    previous = json.loads(record_path.read_text(encoding="utf-8")) if record_path.exists() else {}
    if offline:
        if not previous.get("revision"):
            raise FileNotFoundError(f"No cached Hugging Face source record for {repo}")
        info = previous
        if "files" not in info:
            info["files"] = info.get("hashes", {})
    else:
        info = json.loads(_get(f"https://huggingface.co/api/datasets/{repo}"))
        revision = info["sha"]
        files = {item["rfilename"] for item in info["siblings"]}
        for filename in ("README.md", "session_info.json", "telemetry.parquet"):
            if filename not in files:
                raise ValueError(f"{repo} is missing {filename}")
            path = folder / filename
            if previous.get("revision") != revision or not path.is_file():
                _download(f"https://huggingface.co/datasets/{repo}/resolve/{revision}/{filename}", path)
        info = {
            "repo": repo,
            "revision": revision,
            "license": info.get("cardData", {}).get("license"),
            "files": {
                filename: {"bytes": (folder / filename).stat().st_size, "sha256": _sha256(folder / filename)}
                for filename in ("README.md", "session_info.json", "telemetry.parquet")
            },
        }
        record_path.write_text(json.dumps(info, indent=2), encoding="utf-8")
    for filename in ("README.md", "session_info.json", "telemetry.parquet"):
        path = folder / filename
        if not path.is_file():
            raise FileNotFoundError(path)
        expected_hash = info.get("files", {}).get(filename, {}).get("sha256")
        if offline and expected_hash and _sha256(path) != expected_hash:
            raise ValueError(f"Cached {repo}/{filename} does not match its recorded SHA-256")
    return folder, info


def _query(db, parquet, sql):
    path = str(parquet.resolve()).replace("'", "''")
    return db.execute(sql.format(path=path))


def _audit(db, slug, car, track, expected_laps, folder, source):
    parquet = folder / "telemetry.parquet"
    card = (folder / "README.md").read_text(encoding="utf-8")
    session = json.loads((folder / "session_info.json").read_text(encoding="utf-8"))
    card_class = re.search(r">\s*Class\s*</[^>]+>\s*<[^>]+>\s*([^<]+)", card, re.IGNORECASE)
    category = vehicle_category(car_model=car,
                                class_label=card_class.group(1) if card_class else "")
    if category != "gt":
        raise ValueError(f"{slug} has no verified GT class in its dataset card or car model")
    card_laps = re.search(r"(\d+)\s+consecutive laps", card, re.IGNORECASE)
    if session.get("car") != car or session.get("track") != track:
        raise ValueError(f"{slug} session metadata does not match the expected car/track")
    if card_laps is None or int(card_laps.group(1)) != expected_laps:
        raise ValueError(f"{slug} dataset card does not verify the expected {expected_laps} laps")
    if (source.get("license") or "").lower() != "mit":
        raise ValueError(f"{slug} dataset license is not verified as MIT")

    schema = _query(db, parquet, "DESCRIBE SELECT * FROM read_parquet('{path}')").fetchall()
    columns = {row[0] for row in schema}
    required = {"timestamp", "fuel", *COMMON, "lap_progress"}
    required.update(f"{base}_{wheel}" for base in ("wear", "tyre_core", "psi", "slip", "load") for wheel in WHEELS)
    if not required <= columns:
        raise ValueError(f"{slug} is missing telemetry fields: {sorted(required - columns)}")
    pit_fields = sorted(
        name for name in columns
        if name.lower() in {"pit", "in_pit", "pit_lane", "pit_box", "pit_stop", "pit_event", "service", "service_event"}
    )
    lap_summary = {}
    for field in ("completed_laps", "lap_progress"):
        lap_summary[field] = (
            dict(zip(("distinct", "min", "max"), _query(
                db, parquet,
                f"SELECT COUNT(DISTINCT {field}), MIN({field}), MAX({field}) FROM read_parquet('{{path}}')",
            ).fetchone())) if field in columns else None
        )
    base = _query(
        db, parquet,
        """WITH d AS (
             SELECT timestamp, LAG(timestamp) OVER (ORDER BY timestamp) AS previous
             FROM read_parquet('{path}')
           )
           SELECT COUNT(*), MIN(timestamp), MAX(timestamp), COUNT(*) FILTER (WHERE timestamp < previous),
                  QUANTILE_CONT(timestamp - previous, 0.5), MAX(timestamp - previous)
           FROM d""",
    ).fetchone()
    wear = {}
    for wheel in WHEELS:
        field = f"wear_{wheel}"
        values = _query(
            db, parquet,
            f"""SELECT COUNT(DISTINCT {field}), MIN({field}), MAX({field}),
                       QUANTILE_CONT({field}, 0.05), QUANTILE_CONT({field}, 0.95),
                       FIRST({field} ORDER BY timestamp), LAST({field} ORDER BY timestamp)
                FROM read_parquet('{{path}}')""",
        ).fetchone()
        wear[field] = dict(zip(("distinct", "min", "max", "p05", "p95", "first", "last"), values))

    event_query = _query(
        db, parquet,
        """WITH d AS (
             SELECT timestamp, fuel, LAG(fuel) OVER (ORDER BY timestamp) AS previous_fuel,
                    GREATEST(COALESCE(wear_fl - LAG(wear_fl) OVER (ORDER BY timestamp), 0),
                             COALESCE(wear_fr - LAG(wear_fr) OVER (ORDER BY timestamp), 0),
                             COALESCE(wear_rl - LAG(wear_rl) OVER (ORDER BY timestamp), 0),
                             COALESCE(wear_rr - LAG(wear_rr) OVER (ORDER BY timestamp), 0)) AS wear_increase
             FROM read_parquet('{path}')
           )
           SELECT timestamp, fuel - previous_fuel, wear_increase
           FROM d WHERE fuel - previous_fuel > 5 OR wear_increase > 0.1 ORDER BY timestamp""",
    ).fetchall()
    final_timestamp = float(base[2])
    events = [
        {"timestamp": float(row[0]), "fuel_increase_l": float(row[1] or 0),
         "wear_increase_pp": float(row[2] or 0), "at_tail": final_timestamp - float(row[0]) <= 1.0}
        for row in event_query
    ]
    # Never let a future target cross a fuel/wear reset-like record.
    reset_cutoff = min((event["timestamp"] for event in events), default=None)
    lap_labels_valid = (
        lap_summary.get("completed_laps") is not None
        and lap_summary["completed_laps"]["distinct"] > 1
    ) or (
        lap_summary["lap_progress"] is not None
        and lap_summary["lap_progress"]["distinct"] > 10
        and float(lap_summary["lap_progress"]["max"] or 0) - float(lap_summary["lap_progress"]["min"] or 0) > 0.9
    )
    return {
        "dataset": f"Nasim435/{slug}", "revision": source.get("revision"), "license": source.get("license"),
        "car": car, "track": track, "vehicle_category": category,
        "card_laps": expected_laps, "session_info": session,
        "row_count": int(base[0]), "duration_s": float(base[2] - base[1]),
        "timestamp_backward_steps": int(base[3]), "timestamp_step_median_s": float(base[4]),
        "timestamp_step_max_s": float(base[5]), "columns": len(columns), "pit_fields": pit_fields,
        "lap_fields": lap_summary, "lap_labels_valid": bool(lap_labels_valid), "wear": wear,
        "fuel_or_wear_reset_events": events, "reset_cutoff_timestamp": reset_cutoff,
        "pit_interpretation": "No explicit pit/service fields; reset-like fuel/wear jumps do not prove a pit service.",
        "files": source.get("files", {}),
    }


def _samples(db, slug, folder, audit):
    parquet = folder / "telemetry.parquet"
    wear_fields = [f"wear_{wheel}" for wheel in WHEELS]
    wheel_fields = [f"{base}_{wheel}" for wheel in WHEELS for base in ("tyre_core", "psi", "slip", "load")]
    fields = wear_fields + wheel_fields + list(COMMON)
    selected = [
        "FLOOR(timestamp)::BIGINT AS second",
        *[f"LAST({field} ORDER BY timestamp) AS {field}" for field in fields],
    ]
    path = str(parquet.resolve()).replace("'", "''")
    where = f"WHERE timestamp < {float(audit['reset_cutoff_timestamp'])}" if audit["reset_cutoff_timestamp"] is not None else ""
    rows = db.execute(
        f"SELECT {', '.join(selected)} FROM read_parquet('{path}') {where} "
        "GROUP BY FLOOR(timestamp) ORDER BY second"
    ).fetchall()
    names = ["second", *fields]
    data = {name: np.asarray([row[index] for row in rows], dtype=np.float64) for index, name in enumerate(names)}
    times = data["second"]
    cutoff = audit["reset_cutoff_timestamp"]
    features, targets = [], []
    wheel_flags = [[float(index == selected) for index in range(len(WHEELS))] for selected in range(len(WHEELS))]
    feature_names = [
        "current_wear", "wear_change_last_10s", "tyre_core", "psi", "slip", "load",
        *COMMON, *(f"wheel_{wheel}" for wheel in WHEELS),
    ]
    for index, second in enumerate(times):
        target_index = int(np.searchsorted(times, second + HORIZON_S, side="left"))
        history_index = int(np.searchsorted(times, second - 10, side="right")) - 1
        if target_index >= len(times) or history_index < 0:
            continue
        if times[target_index] - second - HORIZON_S > 2 or second - times[history_index] > 12:
            continue
        if cutoff is not None and times[target_index] >= cutoff:
            continue
        for wheel_index, wheel in enumerate(WHEELS):
            current = data[f"wear_{wheel}"][index]
            past = data[f"wear_{wheel}"][history_index]
            target = data[f"wear_{wheel}"][target_index]
            feature = [current, current - past]
            feature.extend(data[f"{base}_{wheel}"][index] for base in ("tyre_core", "psi", "slip", "load"))
            feature.extend(data[name][index] for name in COMMON)
            feature.extend(wheel_flags[wheel_index])
            if np.isfinite(feature).all() and np.isfinite(target):
                features.append(feature)
                targets.append(target)
    if not features:
        return None
    return {"group": slug, "car": audit["car"], "features": np.asarray(features),
            "targets": np.asarray(targets), "feature_names": feature_names}


def _train(samples):
    from xgboost import XGBRegressor

    groups = list(samples)
    predictions, labels, persistence = [], [], []
    folds = []
    for test_group in groups:
        train_groups = [group for group in groups if group != test_group]
        train_x = np.concatenate([samples[group]["features"] for group in train_groups])
        train_y = np.concatenate([samples[group]["targets"] for group in train_groups])
        test_x = samples[test_group]["features"]
        test_y = samples[test_group]["targets"]
        model = XGBRegressor(
            objective="reg:squarederror", tree_method="hist", n_estimators=120, max_depth=3,
            learning_rate=0.05, subsample=0.8, colsample_bytree=0.85, reg_lambda=5,
            n_jobs=1, random_state=42, verbosity=0,
        )
        model.fit(train_x, train_y)
        prediction = model.predict(test_x)
        baseline = test_x[:, 0]
        predictions.extend(prediction.tolist())
        labels.extend(test_y.tolist())
        persistence.extend(baseline.tolist())
        folds.append({
            "held_out_dataset": test_group, "held_out_car": samples[test_group]["car"],
            "train_datasets": train_groups, "train_rows": len(train_y), "test_rows": len(test_y),
            "persistence_mae_pp": float(np.mean(np.abs(test_y - baseline))),
            "xgboost_mae_pp": float(np.mean(np.abs(test_y - prediction))),
        })
    y = np.asarray(labels)
    xgb_mae = float(np.mean(np.abs(y - np.asarray(predictions))))
    persistence_mae = float(np.mean(np.abs(y - np.asarray(persistence))))
    improvement = 100 * (persistence_mae - xgb_mae) / persistence_mae if persistence_mae else 0.0
    return {
        "split": "leave-one-dataset-session-and-car-out; no row-random split",
        "target": f"wear_* after {HORIZON_S}s; Assetto Corsa simulator condition scale, percentage points",
        "features": samples[groups[0]]["feature_names"], "folds": folds,
        "persistence_mae_pp": persistence_mae, "xgboost_mae_pp": xgb_mae,
        "improvement_over_persistence_pct": improvement,
        "groups": groups, "xgboost_params": {"n_estimators": 120, "max_depth": 3, "n_jobs": 1, "tree_method": "hist"},
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--offline", action="store_true", help="use cached, revision-pinned files only")
    args = parser.parse_args()
    ROOT.mkdir(parents=True, exist_ok=True)
    db = duckdb.connect()
    audits, sample_data, source_records = [], {}, []
    for slug, car, track, laps in DATASETS:
        folder, source = _source(slug, args.offline)
        audit = _audit(db, slug, car, track, laps, folder, source)
        audits.append(audit)
        source_records.append({
            "dataset": audit["dataset"], "url": f"https://huggingface.co/datasets/{audit['dataset']}/tree/{audit['revision']}",
            "revision": audit["revision"], "license": audit["license"], "files": audit["files"],
        })
        sample = _samples(db, slug, folder, audit)
        if sample:
            sample_data[slug] = sample
        print(f"{slug}: {audit['row_count']:,} rows; card {laps} laps; "
              f"completed_laps={audit['lap_fields'].get('completed_laps')}; "
              f"lap_progress={audit['lap_fields']['lap_progress']}; pit_fields={audit['pit_fields']}; "
              f"wear_fl={audit['wear']['wear_fl']['min']:.3f}..{audit['wear']['wear_fl']['max']:.3f}")
    db.close()

    # Four groups, 500 samples per group, and measurable wear variation are the
    # minimum for a leave-one-session-and-car-out research comparison.
    has_signal = all(
        min(audit["wear"][f"wear_{wheel}"]["p95"] - audit["wear"][f"wear_{wheel}"]["p05"] for wheel in WHEELS) >= 0.05
        for audit in audits
    )
    trainable = (
        len(sample_data) >= 4 and len({audit["car"] for audit in audits}) >= 4
        and min((len(sample["targets"]) for sample in sample_data.values()), default=0) >= 500
        and has_signal
    )
    metrics = _train(sample_data) if trainable else {
        "status": "skipped",
        "reason": "insufficient independent session/car groups, 60-second samples, or observed wear signal",
    }
    xgboost_version = None
    if trainable:
        import xgboost
        xgboost_version = xgboost.__version__
    model_saved = False
    if trainable:
        # Keep a research artifact only when group-held-out CV beats the persistence baseline.
        if metrics["improvement_over_persistence_pct"] >= 5:
            from xgboost import XGBRegressor
            x = np.concatenate([sample_data[group]["features"] for group in sample_data])
            y = np.concatenate([sample_data[group]["targets"] for group in sample_data])
            final_model = XGBRegressor(
                objective="reg:squarederror", tree_method="hist", n_estimators=120, max_depth=3,
                learning_rate=0.05, subsample=0.8, colsample_bytree=0.85, reg_lambda=5,
                n_jobs=1, random_state=42, verbosity=0,
            ).fit(x, y)
            final_model.save_model(str(ROOT / "wear_60s_xgb.json"))
            model_saved = True
        metrics["status"] = "research_model_saved" if model_saved else "did_not_beat_baseline_gate"
    (ROOT / "audit.json").write_text(json.dumps(audits, indent=2), encoding="utf-8")
    (ROOT / "metrics.json").write_text(json.dumps(metrics, indent=2), encoding="utf-8")
    manifest = {
        "project": "RaceEngineer AC tyre condition research",
        "vehicle_category": "gt",
        "simulator": "Assetto Corsa; wear_* is simulator condition telemetry, not physical tread depth or ACC tyreWear",
        "deployment_ready": False,
        "status": metrics["status"],
        "model": "wear_60s_xgb.json" if model_saved else None,
        "model_sha256": _sha256(ROOT / "wear_60s_xgb.json") if model_saved else None,
        "feature_order": metrics.get("features", []),
        "target": metrics.get("target"),
        "training_environment": {
            "python": sys.version.split()[0], "duckdb": duckdb.__version__,
            "numpy": np.__version__, "xgboost": xgboost_version, "cpu_threads": 1,
        },
        "sources": source_records,
        "lap_labels_valid": all(audit["lap_labels_valid"] for audit in audits),
        "limitations": [
            "Cards document lap counts, but completed_laps is constant or absent and lap_progress is constant in the telemetry; no lap-based target is valid.",
            "One source session per car/track means car and track generalization are confounded.",
            "BMW, Porsche, and SLS show one fuel/wear jump at the final record; Audi has none. No jump proves a pit service.",
            "All artifacts are research-only; four sessions are too few for deployment approval.",
        ],
        "metrics_file": "metrics.json",
        "audit_file": "audit.json",
    }
    (ROOT / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    if trainable:
        print(f"LOSO MAE: XGBoost {metrics['xgboost_mae_pp']:.6f} vs persistence "
              f"{metrics['persistence_mae_pp']:.6f} pp ({metrics['improvement_over_persistence_pct']:.1f}% improvement)")
    else:
        print(f"Training skipped: {metrics['reason']}")
    print(f"Audit and deployment_ready=false manifest: {ROOT}")


if __name__ == "__main__":
    main()
