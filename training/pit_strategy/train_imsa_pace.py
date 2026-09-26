"""Train a research-only IMSA GT3 one-lap-ahead pace regressor."""

from __future__ import annotations

import hashlib
import json
import urllib.request
from pathlib import Path

import duckdb
import numpy as np
import pandas as pd
import xgboost as xgb

from vehicle_category import vehicle_category


ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "training/pit_strategy/.local_train/imsa_gt3"
DATABASE = OUT / "imsa.duckdb"
PAIRS_CSV = OUT / "imsa_gt3_lap_pairs.csv"
MODEL_PATH = OUT / "imsa_gt3_pace_delta_xgb.json"
MANIFEST_PATH = OUT / "manifest.json"
DATASET_REVISION = "3bf21c58542845eb82637e1ea7f78ac3c6af88cc"
DATASET_URL = (
    "https://huggingface.co/datasets/tobil/imsa/resolve/"
    f"{DATASET_REVISION}/imsa.duckdb"
)
DATASET_PAGE = "https://huggingface.co/datasets/tobil/imsa"
DATASET_SHA256 = "e3fb664b15d5348c464a81d441b609d4ac962f66649b8e12906e2a1fdeea3561"
FEATURES = ["current_lap_time_s", "rolling_3_lap_s", "stint_lap"]


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def ensure_database() -> str:
    OUT.mkdir(parents=True, exist_ok=True)
    if DATABASE.is_file() and sha256_file(DATABASE) == DATASET_SHA256:
        return DATASET_SHA256

    partial = DATABASE.with_suffix(".duckdb.partial")
    digest = hashlib.sha256()
    try:
        with urllib.request.urlopen(DATASET_URL, timeout=60) as response, partial.open("wb") as target:
            while chunk := response.read(1 << 20):
                target.write(chunk)
                digest.update(chunk)
        actual = digest.hexdigest()
        if actual != DATASET_SHA256:
            raise ValueError(f"Dataset SHA-256 mismatch: expected {DATASET_SHA256}, got {actual}")
        partial.replace(DATABASE)
    finally:
        partial.unlink(missing_ok=True)
    return DATASET_SHA256


def build_pairs(raw: pd.DataFrame) -> tuple[pd.DataFrame, dict[str, int]]:
    raw["year"] = pd.to_numeric(raw["year"], errors="coerce")
    for column in ("lap", "lap_time", "stint_number", "stint_lap", "bpillar_quartile"):
        raw[column] = pd.to_numeric(raw[column], errors="coerce")
    raw["lap_time_s"] = raw["lap_time"].astype(float)
    raw["event_id"] = (
        raw["year"].astype("Int64").astype(str)
        + "|" + raw["event"].astype("string")
        + "|" + raw["start_date"].astype("string")
        + "|" + raw["session_id"].astype("string")
    )
    driver_id = raw["driver_id"].astype("string").str.strip().str.casefold()
    driver_name = raw["driver_name"].astype("string").str.strip().str.casefold()
    raw["_driver_key"] = driver_id.mask(driver_id.isna() | driver_id.eq(""), driver_name)
    raw["car"] = raw["car"].astype("string").str.strip()
    raw = raw.dropna(subset=["year", "event", "start_date", "session_id", "car",
                             "_driver_key", "lap", "lap_time_s", "stint_number", "stint_lap"])
    raw = raw[(raw["car"] != "") & (raw["_driver_key"] != "")].copy()
    raw["lap"] = raw["lap"].astype("int64")
    raw["stint_number"] = raw["stint_number"].astype("int64")
    raw["stint_lap"] = raw["stint_lap"].astype("int64")
    raw["_clean"] = (
        raw["flags"].astype("string").str.strip().str.upper().eq("GF")
        & raw["bpillar_quartile"].isin([1, 2])
        & raw["lap_time_s"].between(60.0, 300.0)
        & raw["stint_lap"].ge(1)
    )

    keys = ["event_id", "car", "_driver_key", "stint_number"]
    raw = (raw.sort_values(keys + ["lap"], kind="stable")
           .drop_duplicates(keys + ["lap"], keep="last")
           .reset_index(drop=True))
    grouped = raw.groupby(keys, sort=False, dropna=False)
    previous_lap = grouped["lap"].shift(1)
    previous2_lap = grouped["lap"].shift(2)
    previous_time = grouped["lap_time_s"].shift(1)
    previous2_time = grouped["lap_time_s"].shift(2)
    previous_clean = grouped["_clean"].shift(1).fillna(False).astype(bool)
    previous2_clean = grouped["_clean"].shift(2).fillna(False).astype(bool)
    current_clean = raw["_clean"]

    use_previous = previous_clean & previous_lap.eq(raw["lap"] - 1)
    use_previous2 = (
        use_previous & previous2_clean
        & previous2_lap.eq(previous_lap - 1)
    )
    rolling_sum = raw["lap_time_s"] + previous_time.where(use_previous, 0.0)
    rolling_count = 1 + use_previous.astype("int8")
    rolling_sum += previous2_time.where(use_previous2, 0.0)
    rolling_count += use_previous2.astype("int8")
    raw["rolling_3_lap_s"] = rolling_sum / rolling_count

    next_lap = grouped["lap"].shift(-1)
    next_time = grouped["lap_time_s"].shift(-1)
    next_stint_lap = grouped["stint_lap"].shift(-1)
    next_clean = grouped["_clean"].shift(-1).fillna(False).astype(bool)
    keep = (
        current_clean & next_clean
        & next_lap.eq(raw["lap"] + 1)
        & next_stint_lap.eq(raw["stint_lap"] + 1)
        & next_time.notna()
    )

    pairs = raw.loc[keep, [
        "event_id", "year", "event", "start_date", "session_id", "class", "homologation", "car",
        "driver_id", "driver_name", "stint_number", "lap", "stint_lap",
        "lap_time_s", "rolling_3_lap_s",
    ]].copy()
    pairs = pairs.rename(columns={"lap": "current_lap", "lap_time_s": "current_lap_time_s"})
    pairs["next_lap"] = next_lap.loc[keep].astype("int64")
    pairs["next_lap_time_s"] = next_time.loc[keep].astype(float)
    pairs["target_delta_s"] = pairs["next_lap_time_s"] - pairs["current_lap_time_s"]
    pairs["year"] = pairs["year"].astype("int64")

    years = sorted(int(year) for year in pairs["year"].unique())
    if len(years) < 3:
        raise RuntimeError(f"Need at least three event years for train/validation/test; found {years}")
    validation_year, test_year = years[-2:]
    pairs["split"] = np.select(
        [pairs["year"].eq(validation_year), pairs["year"].eq(test_year)],
        ["validation", "test"], default="train",
    )

    assert pairs["next_lap"].eq(pairs["current_lap"] + 1).all()
    assert pairs["next_lap_time_s"].between(60.0, 300.0).all()
    split_events = pairs.groupby("split")["event_id"].apply(set)
    assert all(split_events[a].isdisjoint(split_events[b])
               for a, b in (("train", "validation"), ("train", "test"), ("validation", "test")))
    return pairs, {"validation_year": validation_year, "test_year": test_year}


def main() -> None:
    dataset_sha256 = ensure_database()
    connection = duckdb.connect(str(DATABASE), read_only=True)
    try:
        raw = connection.execute("""
            SELECT year, event, start_date, session_id, class, homologation, car, driver_id, driver_name,
                   lap, lap_time, flags, bpillar_quartile, stint_number, stint_lap
            FROM laps
            WHERE lower(series_code) = 'imsa' AND lower(session) = 'race'
              AND upper(homologation) = 'GT3' AND upper(class) IN ('GTD', 'GTDPRO')
        """).fetchdf()
        source_domains = connection.execute("""
            SELECT series_code, year, count(*) AS race_lap_rows,
                   count(DISTINCT (year, event, start_date, session_id)) AS race_sessions
            FROM laps
            WHERE upper(homologation) = 'GT3' AND lower(session) = 'race'
              AND upper(class) IN ('GTD', 'GTDPRO', 'GT', 'LMGT3')
            GROUP BY series_code, year ORDER BY series_code, year
        """).fetchdf()
    finally:
        connection.close()

    if raw.empty:
        raise RuntimeError("No IMSA GTD/GTDPRO GT3 race rows in the pinned source database")
    raw_rows = len(raw)
    clean_rows = int((raw["flags"].astype("string").str.strip().str.upper().eq("GF")
                      & pd.to_numeric(raw["bpillar_quartile"], errors="coerce").isin([1, 2])
                      & pd.to_numeric(raw["lap_time"], errors="coerce").between(60, 300)
                      & pd.to_numeric(raw["stint_lap"], errors="coerce").ge(1)).sum())
    pairs, split_years = build_pairs(raw)
    pairs["vehicle_category"] = [
        vehicle_category(class_label=label, homologation=homologation)
        for label, homologation in zip(pairs["class"], pairs["homologation"])
    ]
    if set(pairs["vehicle_category"]) != {"gt"}:
        raise ValueError("IMSA GT3 source contains an unexpected vehicle category")
    counts = pairs["split"].value_counts().to_dict()
    if any(counts.get(split, 0) == 0 for split in ("train", "validation", "test")):
        raise RuntimeError(f"Empty temporal split; pair counts: {counts}")

    pairs.to_csv(PAIRS_CSV, index=False)
    train = pairs[pairs["split"].eq("train")]
    model = xgb.XGBRegressor(
        objective="reg:squarederror", tree_method="hist", device="cpu", n_jobs=1,
        n_estimators=300, max_depth=5, learning_rate=0.04, min_child_weight=20,
        subsample=0.8, colsample_bytree=0.9, reg_lambda=5, random_state=42,
    )
    model.fit(train[FEATURES], train["target_delta_s"], verbose=False)
    model.save_model(MODEL_PATH)

    def evaluate(frame: pd.DataFrame) -> dict[str, float | int]:
        actual = frame["next_lap_time_s"].to_numpy()
        predicted = frame["current_lap_time_s"].to_numpy() + model.predict(frame[FEATURES])
        mae = lambda estimate: float(np.mean(np.abs(actual - estimate)))
        return {
            "n": int(len(frame)),
            "xgboost_mae_s": mae(predicted),
            "previous_lap_mae_s": mae(frame["current_lap_time_s"].to_numpy()),
            "rolling_3_lap_mae_s": mae(frame["rolling_3_lap_s"].to_numpy()),
        }

    validation = pairs[pairs["split"].eq("validation")]
    test = pairs[pairs["split"].eq("test")]
    metrics = {"validation": evaluate(validation), "test": evaluate(test)}
    counts_by_year = {str(int(year)): int(count) for year, count in pairs.groupby("year").size().items()}
    event_counts_by_split = {
        split: int(frame["event_id"].nunique())
        for split, frame in pairs.groupby("split")
    }
    domain_counts = [
        {"series_code": str(row.series_code), "year": int(row.year),
         "race_lap_rows": int(row.race_lap_rows), "race_sessions": int(row.race_sessions)}
        for row in source_domains.itertuples(index=False)
    ]
    manifest = {
        "schema_version": 1,
        "artifact_kind": "imsa_gt3_one_lap_ahead_pace_regression_research",
        "vehicle_category": "gt",
        "deployment_ready": False,
        "blocked_by": [
            "Research data from public timing only; not validated on AC/ACC telemetry",
            "Pace prediction does not label or optimize pit strategy",
            "Estimated tire age is heuristic, not measured wear",
            "Post-race pace quartiles select training/evaluation laps and are unavailable live",
        ],
        "target": "next consecutive clean green lap time minus current clean green lap time (seconds)",
        "features": FEATURES,
        "feature_notes": {
            "rolling_3_lap_s": "Mean of current and up to two immediately preceding consecutive clean laps in the same car/driver/stint.",
            "stint_lap": "Source stint lap counter; a stint proxy only, not measured tire wear.",
            "est_tire_age": "Source field is a post-race score-and-allocate heuristic using pit/stint, pace, race phase, and tire-budget assumptions; not measured wear and excluded from features to avoid future-context leakage.",
            "bpillar_quartile": "Used only for post-race clean-lap selection, never as a model feature; this selection is not available live.",
        },
        "source": {
            "url": DATASET_PAGE,
            "file_url": DATASET_URL,
            "repository_revision": DATASET_REVISION,
            "database_file": DATABASE.name,
            "database_size_bytes": DATABASE.stat().st_size,
            "database_sha256": dataset_sha256,
            "coverage_note": "Pinned DuckDB includes IMSA 2026 race data, while the dataset card describes 2021-2025; the 2026 test season is partial in this snapshot (8 IMSA race sessions).",
            "raw_imsa_gtd_gt3_race_rows": raw_rows,
            "clean_green_lap_rows": clean_rows,
            "pair_rows": int(len(pairs)),
            "pair_rows_by_year": counts_by_year,
            "pair_rows_by_split": {key: int(value) for key, value in counts.items()},
            "events_by_split": event_counts_by_split,
            "gt3_race_source_counts_by_series_year": domain_counts,
            "filter": "series_code=imsa, session=race, homologation=GT3, class in GTD/GTDPRO; both laps flags=GF and bpillar_quartile in (1,2), 60-300 seconds, consecutive lap and stint_lap, same event/car/driver/stint",
        },
        "split": {
            "method": "chronological whole-event year split; no event appears in more than one split",
            "train_years": sorted(int(year) for year in train["year"].unique()),
            "validation_year": split_years["validation_year"],
            "test_year": split_years["test_year"],
            "test_coverage_note": "The 2026 test year has 8 race sessions in the pinned snapshot and is a partial-season result.",
            "train_examples": int(len(train)),
            "validation_examples": int(len(validation)),
            "test_examples": int(len(test)),
        },
        "metrics": metrics,
        "xgboost": {
            "version": xgb.__version__, "objective": "reg:squarederror",
            "tree_method": "hist", "device": "cpu", "n_jobs": 1,
            "n_estimators": 300, "max_depth": 5, "learning_rate": 0.04,
            "min_child_weight": 20, "subsample": 0.8,
            "colsample_bytree": 0.9, "reg_lambda": 5, "random_state": 42,
        },
        "lap_pairs_csv": PAIRS_CSV.name,
        "lap_pairs_csv_sha256": sha256_file(PAIRS_CSV),
        "model_file": MODEL_PATH.name,
        "model_sha256": sha256_file(MODEL_PATH),
    }
    MANIFEST_PATH.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(json.dumps({"manifest": str(MANIFEST_PATH), "metrics": metrics,
                      "pair_rows_by_split": manifest["source"]["pair_rows_by_split"],
                      "events_by_split": event_counts_by_split}, indent=2))


if __name__ == "__main__":
    main()
