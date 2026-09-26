#!/usr/bin/env python3
"""Process local RaceEngineer lap logs and train a research-only pace regressor."""

from __future__ import annotations

import argparse
from collections import defaultdict
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import statistics
import sys
import tempfile
import uuid

from vehicle_category import vehicle_category


SCHEMA_VERSION = "raceengineer_pace_pairs_v3"
VALID_CATEGORIES = {
    "f1", "formula", "gt", "prototype", "touring", "road", "cup",
    "rally", "track_only", "drift", "time_attack",
}
PIT_EVENTS = {"pit_enter", "pit_exit", "pit_box_enter", "pit_box_exit"}
MIN_LAP_TIME_S = 10.0
MAX_LAP_TIME_S = 600.0
MIN_SESSIONS = 3
MIN_ROWS = 60
MIN_TRAIN_ROWS = 40
MIN_VALIDATION_ROWS = 10
WHEELS = range(4)
SIGNAL_FIELDS = (
    "fuel_at_sample_l",
    *(f"tyre_wear_{wheel}_at_sample" for wheel in WHEELS),
    *(f"tyre_temp_{wheel}_at_sample" for wheel in WHEELS),
)
SNAPSHOT_SIGNAL_FIELDS = tuple(
    f"{name}_{boundary}"
    for name in SIGNAL_FIELDS
    for boundary in ("current", "next")
)
MIN_CORE_TEMP_C = -50.0
MAX_CORE_TEMP_C = 250.0
WEAR_JUMP_MEDIAN_MULTIPLIER = 10.0
WEAR_JUMP_RANGE_FRACTION = 0.5


def _finite_number(value):
    return (isinstance(value, (int, float)) and not isinstance(value, bool)
            and math.isfinite(value))


def _integer(value, minimum=None):
    if not _finite_number(value) or int(value) != value:
        return None
    result = int(value)
    return result if minimum is None or result >= minimum else None


def _sample_signals(row):
    return {name: float(row[name]) if _finite_number(row.get(name)) else None
            for name in SIGNAL_FIELDS}


def _reject_json_constant(value):
    raise ValueError(f"non-standard JSON value: {value}")


def _atomic_json(path: Path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as stream:
            json.dump(value, stream, ensure_ascii=False, indent=2, allow_nan=False)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def _atomic_text(path: Path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(text)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def _remove_temp_dir(path: Path, parent: Path):
    if path.exists():
        if path.resolve().parent != parent.resolve():
            raise RuntimeError(f"Refusing to remove unexpected temporary directory: {path}")
        shutil.rmtree(path)


def _source_files(data_dir: Path):
    paths = [data_dir / "pit_strategy_laps.jsonl", data_dir / "pit_strategy_laps.jsonl.old"]
    archive_dir = data_dir / "recordings"
    if archive_dir.is_dir():
        paths.extend(archive_dir.glob("pit_strategy_laps-*.jsonl"))
    unique = {path.resolve(): path for path in paths if path.is_file()}
    return sorted(unique.values(), key=lambda path: str(path).casefold())


def _source_signature(data_dir: Path, paths):
    result = []
    for path in paths:
        stat = path.stat()
        result.append({
            "path": path.relative_to(data_dir).as_posix(),
            "bytes": stat.st_size,
            "mtime_ns": stat.st_mtime_ns,
        })
    return result


def _category(row):
    recorded = row.get("car_category")
    if isinstance(recorded, str):
        recorded = recorded.strip().casefold()
        if recorded in VALID_CATEGORIES:
            return recorded
    return vehicle_category(
        car_model=row.get("car_model", ""),
        class_label=row.get("car_subclass", ""),
    )


def _identity(row):
    simulator = row.get("simulator")
    session = row.get("session_id")
    car = row.get("car_model")
    track = row.get("track")
    category = _category(row)
    subclass = row.get("car_subclass")
    if simulator not in {"sim_ac", "sim_acc"} or category not in VALID_CATEGORIES:
        return None
    if not all(isinstance(value, str) and value.strip() for value in (session, car, track)):
        return None
    if not isinstance(subclass, str) or not subclass.strip():
        subclass = "unknown"
    return (simulator, category, subclass.strip().casefold(), car.strip(), track.strip(), session.strip())


def _read_raw(data_dir: Path, paths):
    counts = {
        "files": len(paths), "lines": 0, "parsed_records": 0, "malformed_json": 0,
        "invalid_records": 0, "valid_lap_records": 0, "invalid_lap_records": 0,
        "unconfirmed_setup_laps": 0,
        "legacy_laps_without_exclusion_flag": 0, "pit_events": 0,
        "explicitly_excluded_laps": 0, "pit_adjacent_laps": 0,
        "standing_start_laps_excluded": 0,
        "duplicate_laps_removed": 0, "conflicting_duplicate_groups_removed": 0,
        "duplicate_sample_signal_conflicts": 0,
        "missing_group_identity": 0, "nonconsecutive_lap_pairs": 0,
        "pairs_written": 0,
    }
    laps, events = [], []
    sources = []
    for path in paths:
        source = {"path": path.relative_to(data_dir).as_posix(), "lines": 0, "read_error": None}
        sources.append(source)
        try:
            with path.open("r", encoding="utf-8-sig") as stream:
                for line_number, line in enumerate(stream, 1):
                    source["lines"] += 1
                    counts["lines"] += 1
                    try:
                        row = json.loads(line, parse_constant=_reject_json_constant)
                    except (ValueError, UnicodeError):
                        counts["malformed_json"] += 1
                        continue
                    if not isinstance(row, dict):
                        counts["invalid_records"] += 1
                        continue
                    record_type = row.get("record_type")
                    session_id = row.get("session_id")
                    if not isinstance(session_id, str) or not session_id.strip():
                        counts["invalid_records"] += 1
                        continue
                    counts["parsed_records"] += 1
                    if row.get("realism_confirmed") is not True:
                        if record_type == "lap":
                            counts["unconfirmed_setup_laps"] += 1
                        continue
                    location = {"path": source["path"], "line": line_number}
                    if record_type in PIT_EVENTS:
                        lap = _integer(row.get("current_lap"), 1)
                        if lap is not None:
                            events.append((session_id.strip(), lap))
                            counts["pit_events"] += 1
                        continue
                    if record_type != "lap":
                        counts["invalid_records"] += 1
                        continue
                    lap = _integer(row.get("completed_lap"), 1)
                    lap_time = row.get("lap_time_s")
                    if (lap is None or not _finite_number(lap_time)
                            or not MIN_LAP_TIME_S <= lap_time <= MAX_LAP_TIME_S):
                        counts["invalid_lap_records"] += 1
                        continue
                    if "lap_excluded" in row and not isinstance(row["lap_excluded"], bool):
                        counts["invalid_lap_records"] += 1
                        continue
                    identity = _identity(row)
                    if identity is None:
                        counts["missing_group_identity"] += 1
                        continue
                    counts["valid_lap_records"] += 1
                    if "lap_excluded" not in row:
                        counts["legacy_laps_without_exclusion_flag"] += 1
                    laps.append({
                        "identity": identity,
                        "lap": lap,
                        "lap_time_s": float(lap_time),
                        "total_laps": _integer(row.get("total_laps"), 1),
                        "captured_utc": row.get("captured_utc") if isinstance(row.get("captured_utc"), str) else "",
                        "in_pit": row.get("in_pit_at_sample") is True,
                        "excluded": row.get("lap_excluded") is True,
                        "exclusion_flag_present": "lap_excluded" in row,
                        "signals": _sample_signals(row),
                        "location": location,
                    })
        except (OSError, UnicodeError) as error:
            source["read_error"] = str(error)
    return laps, events, sources, counts


def _build_pairs(data_dir: Path, paths):
    laps, events, sources, counts = _read_raw(data_dir, paths)
    blocked_by_session = defaultdict(set)
    for session_id, event_lap in events:
        # Pit entry/exit and box events do not reveal a clean lap on either side.
        blocked_by_session[session_id].update(range(max(1, event_lap - 1), event_lap + 2))

    # Identical copies are deduplicated; disagreement is ambiguous and dropped as a group.
    duplicate_groups = defaultdict(list)
    for lap in laps:
        simulator, _, _, _, _, session = lap["identity"]
        duplicate_groups[(simulator, session, lap["lap"])].append(lap)
    unique_laps = []
    for records in duplicate_groups.values():
        if len(records) == 1:
            unique_laps.append(records[0])
        elif (len({record["identity"] for record in records}) == 1
              and max(record["lap_time_s"] for record in records)
              - min(record["lap_time_s"] for record in records) <= 0.001):
            merged = dict(records[0])
            merged["in_pit"] = any(record["in_pit"] for record in records)
            merged["excluded"] = any(record["excluded"] for record in records)
            merged["exclusion_flag_present"] = all(record["exclusion_flag_present"] for record in records)
            merged["signals"] = {}
            for name in SIGNAL_FIELDS:
                values = [record["signals"][name] for record in records]
                if all(_finite_number(value) for value in values) and all(value == values[0] for value in values[1:]):
                    merged["signals"][name] = values[0]
                else:
                    merged["signals"][name] = None
                    if any(_finite_number(value) for value in values):
                        counts["duplicate_sample_signal_conflicts"] += 1
            unique_laps.append(merged)
            counts["duplicate_laps_removed"] += len(records) - 1
        else:
            counts["conflicting_duplicate_groups_removed"] += 1
            counts["duplicate_laps_removed"] += len(records)

    for lap in unique_laps:
        session = lap["identity"][-1]
        if lap["lap"] == 1:
            blocked_by_session[session].add(1)
            counts["standing_start_laps_excluded"] += 1
        if lap["in_pit"]:
            blocked_by_session[session].update(range(max(1, lap["lap"] - 1), lap["lap"] + 2))

    grouped = defaultdict(list)
    for lap in unique_laps:
        session = lap["identity"][-1]
        blocked = lap["lap"] in blocked_by_session[session]
        if lap["excluded"]:
            counts["explicitly_excluded_laps"] += 1
            blocked = True
        if blocked:
            counts["pit_adjacent_laps"] += 1
            continue
        grouped[lap["identity"]].append(lap)

    pairs = []
    for identity, session_laps in grouped.items():
        session_laps.sort(key=lambda item: item["lap"])
        for index, current in enumerate(session_laps[:-1]):
            following = session_laps[index + 1]
            if following["lap"] != current["lap"] + 1:
                counts["nonconsecutive_lap_pairs"] += 1
                continue
            history = [current]
            for previous_index in (index - 1, index - 2):
                if previous_index < 0:
                    break
                previous = session_laps[previous_index]
                expected_lap = history[-1]["lap"] - 1
                if previous["lap"] != expected_lap:
                    break
                history.append(previous)
            rolling_3 = sum(item["lap_time_s"] for item in history) / len(history)
            simulator, category, subclass, car, track, session = identity
            pair = {
                "schema": SCHEMA_VERSION,
                "simulator": simulator,
                "category": category,
                "subclass": subclass,
                "car_model": car,
                "track": track,
                "session_id": session,
                "captured_utc": current["captured_utc"],
                "current_lap": current["lap"],
                "next_lap": following["lap"],
                "current_lap_time_s": current["lap_time_s"],
                "next_lap_time_s": following["lap_time_s"],
                "rolling_3_lap_s": rolling_3,
                "total_laps": current["total_laps"],
                "legacy_exclusion_metadata": not (
                    current["exclusion_flag_present"] and following["exclusion_flag_present"]
                ),
                "target_delta_s": following["lap_time_s"] - current["lap_time_s"],
            }
            for name in SIGNAL_FIELDS:
                pair[f"{name}_current"] = current["signals"][name]
                pair[f"{name}_next"] = following["signals"][name]
            pairs.append(pair)
    pairs.sort(key=lambda row: (
        row["simulator"], row["category"], row["subclass"], row["car_model"],
        row["track"], row["session_id"], row["current_lap"],
    ))
    counts["pairs_written"] = len(pairs)
    signal_coverage = _signal_coverage(pairs)
    summary = {
        "schema": SCHEMA_VERSION,
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "source_files": sources,
        "source_signatures": _source_signature(data_dir, paths),
        "counts": counts,
        "tyre_signal_coverage": signal_coverage,
        "status": "partial" if any(source["read_error"] for source in sources) else "success",
        "sessions": len({row["session_id"] for row in pairs}),
        "simulators": sorted({row["simulator"] for row in pairs}),
        "categories": sorted({row["category"] for row in pairs}),
        "limitations": [
            "Pace targets are observed next-lap time changes; tyre changes use source-native samples without scale conversion.",
            "Tyre wear signal semantics are unverified; these snapshots do not establish calibrated physical wear or pit readiness.",
            "No optimal pit-lap labels or pit recommendations are inferred.",
            "The filter removes lap 1, recorded lap_excluded laps, in-pit laps and pit-adjacent laps; legacy rows without lap_excluded cannot be filtered for recorded yellow/red/black flags.",
            "The recorder does not currently identify session type; held-out splits use its unique session_id.",
        ],
    }
    return pairs, summary


def _signal_coverage(pairs):
    def measure(name):
        current = sum(_finite_number(row.get(f"{name}_current")) for row in pairs)
        following = sum(_finite_number(row.get(f"{name}_next")) for row in pairs)
        labeled = sum(
            _finite_number(row.get(f"{name}_current")) and _finite_number(row.get(f"{name}_next"))
            for row in pairs
        )
        return {
            "current_boundary": current,
            "next_boundary": following,
            "labeled_pair_count": labeled,
            "missing_current": len(pairs) - current,
            "missing_next": len(pairs) - following,
            "missing_pair_label": len(pairs) - labeled,
        }

    return {
        "fuel_l": measure("fuel_at_sample_l"),
        "tyre_wear_by_wheel": {
            str(wheel): measure(f"tyre_wear_{wheel}_at_sample") for wheel in WHEELS
        },
        "core_temp_c_by_wheel": {
            str(wheel): measure(f"tyre_temp_{wheel}_at_sample") for wheel in WHEELS
        },
    }


def _process(data_dir: Path, output_dir: Path):
    paths = _source_files(data_dir)
    pairs, summary = _build_pairs(data_dir, paths)
    run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8]
    processed_root = output_dir / "processed"
    processed_root.mkdir(parents=True, exist_ok=True)
    temporary_dir = Path(tempfile.mkdtemp(prefix=f".{run_id}-", dir=processed_root))
    final_dir = processed_root / run_id
    try:
        pair_text = "".join(json.dumps(row, ensure_ascii=False, separators=(",", ":"), allow_nan=False) + "\n"
                            for row in pairs)
        _atomic_text(temporary_dir / "pace_pairs.jsonl", pair_text)
        summary["process_id"] = run_id
        summary["dataset_file"] = "pace_pairs.jsonl"
        _atomic_json(temporary_dir / "summary.json", summary)
        temporary_dir.replace(final_dir)
    except Exception:
        _remove_temp_dir(temporary_dir, processed_root)
        raise

    _atomic_json(processed_root / "latest.json", {"process_id": run_id})
    status = summary["status"] if pairs else ("partial" if summary["status"] == "partial" else "no_data")
    if status == "partial":
        message = (f"Đã xử lý một phần: {len(pairs)} cặp vòng; có tệp log không đọc được. "
                   "Xem chi tiết trong summary.json.")
    elif pairs:
        message = f"Đã xử lý {len(pairs)} cặp vòng qua bộ lọc từ {summary['counts']['files']} tệp log."
    else:
        message = "Chưa có đủ hai vòng liên tiếp qua bộ lọc để tạo dữ liệu pace."
    result = {
        "action": "process",
        "status": status,
        "message": message,
        "output_dir": str(output_dir.resolve()),
        "process_id": run_id,
        "processed_dir": str(final_dir.resolve()),
        "dataset_path": str((final_dir / "pace_pairs.jsonl").resolve()),
        "summary_path": str((final_dir / "summary.json").resolve()),
        "counts": summary["counts"],
    }
    _atomic_json(output_dir / "last_result.json", result)
    return result


def _load_snapshot(output_dir: Path):
    latest_path = output_dir / "processed" / "latest.json"
    if not latest_path.is_file():
        raise ValueError("Chưa có dữ liệu đã xử lý; chạy Process trước.")
    latest = json.loads(latest_path.read_text(encoding="utf-8"))
    process_id = latest.get("process_id")
    if not isinstance(process_id, str) or not re.fullmatch(r"\d{8}T\d{6}Z-[a-f0-9]{8}", process_id):
        raise ValueError("Con trỏ dữ liệu đã xử lý không hợp lệ.")
    snapshot_dir = output_dir / "processed" / process_id
    summary_path = snapshot_dir / "summary.json"
    pairs_path = snapshot_dir / "pace_pairs.jsonl"
    if not summary_path.is_file() or not pairs_path.is_file():
        raise ValueError("Bản dữ liệu đã xử lý chưa đầy đủ.")
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    if summary.get("process_id") != process_id or summary.get("schema") != SCHEMA_VERSION:
        raise ValueError("Phiên bản dữ liệu đã xử lý không khớp.")
    rows = []
    with pairs_path.open("r", encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            try:
                row = json.loads(line, parse_constant=_reject_json_constant)
            except ValueError as error:
                raise ValueError(f"Dòng {line_number} trong dữ liệu đã xử lý bị lỗi: {error}") from error
            if (not isinstance(row, dict) or row.get("schema") != SCHEMA_VERSION
                    or row.get("simulator") not in {"sim_ac", "sim_acc"}
                    or row.get("category") not in VALID_CATEGORIES
                    or not isinstance(row.get("session_id"), str)
                    or _integer(row.get("current_lap"), 1) is None
                    or _integer(row.get("next_lap"), 1) != row.get("current_lap") + 1
                    or not all(_finite_number(row.get(name)) for name in
                               ("current_lap_time_s", "next_lap_time_s", "rolling_3_lap_s", "target_delta_s"))
                    or row["current_lap_time_s"] <= 0 or row["next_lap_time_s"] <= 0
                or abs(row["target_delta_s"] - (row["next_lap_time_s"] - row["current_lap_time_s"])) > 1e-6
                or any(name not in row or (row[name] is not None and not _finite_number(row[name]))
                       for name in SNAPSHOT_SIGNAL_FIELDS)):
                raise ValueError(f"Dòng {line_number} trong dữ liệu đã xử lý không hợp lệ.")
            rows.append(row)
    return process_id, summary, rows


def _current_source_signatures(data_dir: Path):
    try:
        return _source_signature(data_dir, _source_files(data_dir))
    except OSError:
        return []


def _session_order(rows):
    latest_by_session = defaultdict(str)
    for row in rows:
        timestamp = row.get("captured_utc", "")
        if isinstance(timestamp, str) and timestamp > latest_by_session[row["session_id"]]:
            latest_by_session[row["session_id"]] = timestamp
    return sorted(latest_by_session, key=lambda session: (latest_by_session[session], session))


def _metrics_mae(expected, predicted, np):
    return float(np.mean(np.abs(expected - predicted)))


def _train_group(rows, np, XGBRegressor, model_path):
    sessions = _session_order(rows)
    total_rows = len(rows)
    reason = None
    if len(sessions) < MIN_SESSIONS:
        reason = f"cần ít nhất {MIN_SESSIONS} phiên độc lập; hiện có {len(sessions)}"
    elif total_rows < MIN_ROWS:
        reason = f"cần ít nhất {MIN_ROWS} cặp vòng; hiện có {total_rows}"
    if reason:
        return {"status": "insufficient_data", "reason": reason, "sessions": len(sessions), "rows": total_rows}

    holdout_count = max(1, math.ceil(len(sessions) * 0.2))
    validation_sessions = sessions[-holdout_count:]
    training_sessions = sessions[:-holdout_count]
    train_rows = [row for row in rows if row["session_id"] in set(training_sessions)]
    validation_rows = [row for row in rows if row["session_id"] in set(validation_sessions)]
    # Add recent whole sessions to validation until it is useful, retaining two train sessions.
    while len(validation_rows) < MIN_VALIDATION_ROWS and len(training_sessions) > 2:
        validation_sessions.insert(0, training_sessions.pop())
        train_rows = [row for row in rows if row["session_id"] in set(training_sessions)]
        validation_rows = [row for row in rows if row["session_id"] in set(validation_sessions)]
    if len(train_rows) < MIN_TRAIN_ROWS or len(validation_rows) < MIN_VALIDATION_ROWS:
        return {
            "status": "insufficient_data",
            "reason": f"cần ≥{MIN_TRAIN_ROWS} cặp train, ≥{MIN_VALIDATION_ROWS} cặp validation và giữ nguyên phiên; hiện {len(train_rows)}/{len(validation_rows)}",
            "sessions": len(sessions), "rows": total_rows,
            "training_rows": len(train_rows), "validation_rows": len(validation_rows),
        }

    subclasses = sorted({str(row.get("subclass") or "unknown") for row in rows})
    feature_names = ["current_lap_time_s", "rolling_3_lap_s", "current_lap"]
    feature_names.extend(f"subclass::{subclass}" for subclass in subclasses)

    def matrix(selected):
        return np.asarray([
            [float(row["current_lap_time_s"]), float(row["rolling_3_lap_s"]), float(row["current_lap"]),
             *[float((row.get("subclass") or "unknown") == subclass) for subclass in subclasses]]
            for row in selected
        ], dtype=np.float32)

    train_x, train_y = matrix(train_rows), np.asarray([row["target_delta_s"] for row in train_rows], dtype=np.float32)
    valid_x, valid_y = matrix(validation_rows), np.asarray([row["target_delta_s"] for row in validation_rows], dtype=np.float32)
    model = XGBRegressor(
        objective="reg:squarederror", tree_method="hist", n_estimators=100,
        max_depth=3, learning_rate=0.05, min_child_weight=5, subsample=0.8,
        colsample_bytree=0.9, reg_lambda=5.0, n_jobs=1, random_state=42, verbosity=0,
    )
    model.fit(train_x, train_y)
    predicted = model.predict(valid_x)
    mae = _metrics_mae(valid_y, predicted, np)
    baseline_mae = _metrics_mae(valid_y, np.zeros_like(valid_y), np)
    improvement_pct = ((baseline_mae - mae) / baseline_mae * 100.0) if baseline_mae > 0 else None

    # Evaluation stays on held-out sessions; the saved research artifact is refit on all observed pairs.
    all_x, all_y = matrix(rows), np.asarray([row["target_delta_s"] for row in rows], dtype=np.float32)
    final_model = XGBRegressor(
        objective="reg:squarederror", tree_method="hist", n_estimators=100,
        max_depth=3, learning_rate=0.05, min_child_weight=5, subsample=0.8,
        colsample_bytree=0.9, reg_lambda=5.0, n_jobs=1, random_state=42, verbosity=0,
    )
    final_model.fit(all_x, all_y)
    final_model.save_model(str(model_path))
    digest = hashlib.sha256(model_path.read_bytes()).hexdigest()
    return {
        "status": "trained_research_only",
        "sessions": len(sessions),
        "rows": total_rows,
        "training_sessions": training_sessions,
        "validation_sessions": validation_sessions,
        "training_rows": len(train_rows),
        "validation_rows": len(validation_rows),
        "validation_mae_s": mae,
        "previous_lap_baseline_mae_s": baseline_mae,
        "improvement_over_previous_lap_pct": improvement_pct,
        "feature_order": feature_names,
        "model_file": model_path.name,
        "model_sha256": digest,
    }


def _train(data_dir: Path, output_dir: Path):
    process_id, process_summary, rows = _load_snapshot(output_dir)
    source_stale = _current_source_signatures(data_dir) != process_summary.get("source_signatures", [])
    if rows:
        try:
            import numpy as np
            import xgboost
            from xgboost import XGBRegressor
        except (ImportError, OSError) as error:
            raise RuntimeError("Train cần numpy và xgboost; cài bằng `python -m pip install numpy xgboost`.") from error
    else:
        np = None
        xgboost = None
        XGBRegressor = None

    by_group = defaultdict(list)
    for row in rows:
        by_group[(row["simulator"], row["category"])].append(row)

    run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8]
    runs_root = output_dir / "runs"
    runs_root.mkdir(parents=True, exist_ok=True)
    temporary_dir = Path(tempfile.mkdtemp(prefix=f".{run_id}-", dir=runs_root))
    final_dir = runs_root / run_id
    group_metrics, trained = [], 0
    try:
        for simulator, category in sorted(by_group):
            key = f"{simulator}_{category}"
            model_path = temporary_dir / f"pace_delta_{key}.json"
            metrics = _train_group(by_group[(simulator, category)], np, XGBRegressor, model_path)
            metrics.update({"simulator": simulator, "category": category})
            if metrics["status"] == "trained_research_only":
                trained += 1
            group_metrics.append(metrics)
        status = "success" if trained else "insufficient_data"
        source_partial = process_summary.get("status") == "partial"
        if trained:
            message = f"Đã train {trained} model pace XGBoost; kết quả chỉ dùng nghiên cứu."
        else:
            group_counts = "; ".join(
                f"{item['simulator']}/{item['category']}: {item['sessions']} phiên, {item['rows']} cặp"
                for item in group_metrics
            ) or "chưa có nhóm dữ liệu"
            message = (f"Chưa đủ dữ liệu: cần ít nhất {MIN_SESSIONS} phiên và {MIN_ROWS} cặp vòng "
                       f"cho mỗi nhóm simulator/category ({group_counts}).")
        source_warnings = []
        if source_stale:
            source_warnings.append("Có log mới hơn snapshot; cần chạy Process lại để đưa chúng vào lần sau.")
            message += " Đã có log mới; hãy Process lại để đưa chúng vào lần sau."
        if source_partial:
            source_warnings.append("Snapshot được tạo một phần do có tệp log không đọc được.")
            message += " Bản đã xử lý thiếu tệp log đọc được; xem process summary."
        manifest = {
            "schema": "raceengineer_local_pace_model_v1",
            "run_id": run_id,
            "created_utc": datetime.now(timezone.utc).isoformat(),
            "source_process_id": process_id,
            "source_process_status": process_summary.get("status", "unknown"),
            "source_snapshot_stale": source_stale,
            "model_type": "XGBRegressor",
            "target": "next accepted observed lap time minus current accepted observed lap time, seconds",
            "grouped_by": ["simulator", "category"],
            "feature_policy": "Observed lap times, rolling recent pace, lap number, and subclass; no inferred wear or pit-optimal labels.",
            "deployment_ready": False,
            "cpu_threads": 1,
            "training_environment": {
                "python": sys.version.split()[0],
                "numpy": np.__version__ if np is not None else None,
                "xgboost": xgboost.__version__ if xgboost is not None else None,
            },
            "minimums": {
                "sessions_per_model": MIN_SESSIONS,
                "pairs_per_model": MIN_ROWS,
                "training_pairs": MIN_TRAIN_ROWS,
                "held_out_pairs": MIN_VALIDATION_ROWS,
            },
            "models": group_metrics,
            "limitations": [
                "Local models predict next-lap pace change, not the optimal pit lap or whether a pit stop is needed.",
                "Validation holds out complete recorder session_id groups; the recorder does not identify session type.",
                "Models stay in versioned local research runs and are not copied into the app deployment bundle.",
                "Training uses the processed snapshot named above; newer logs require Process again.",
            ],
        }
        run_summary = {
            "status": status,
            "message": message,
            "run_id": run_id,
            "source_process_id": process_id,
            "source_process_status": process_summary.get("status", "unknown"),
            "source_snapshot_stale": source_stale,
            "models_trained": trained,
            "models_skipped": sum(item["status"] != "trained_research_only" for item in group_metrics),
            "models": group_metrics,
            "source_warnings": source_warnings,
            "deployment_ready": False,
        }
        _atomic_json(temporary_dir / "manifest.json", manifest)
        _atomic_json(temporary_dir / "metrics.json", {"models": group_metrics})
        _atomic_json(temporary_dir / "summary.json", run_summary)
        temporary_dir.replace(final_dir)
    except Exception:
        _remove_temp_dir(temporary_dir, runs_root)
        raise

    message = run_summary["message"]
    result = {
        "action": "train",
        "status": status,
        "message": message,
        "output_dir": str(output_dir.resolve()),
        "run_id": run_id,
        "run_dir": str(final_dir.resolve()),
        "summary_path": str((final_dir / "summary.json").resolve()),
        "metrics_path": str((final_dir / "metrics.json").resolve()),
        "manifest_path": str((final_dir / "manifest.json").resolve()),
        "source_process_id": process_id,
        "source_process_status": process_summary.get("status", "unknown"),
        "source_snapshot_stale": source_stale,
        "models_trained": trained,
        "models_skipped": run_summary["models_skipped"],
        "deployment_ready": False,
    }
    _atomic_json(output_dir / "last_result.json", result)
    return result


def _tyre_examples(rows, target):
    wear_target = target == "wear_change"
    prefix = "tyre_wear" if wear_target else "tyre_temp"
    candidates = len(rows) * 4
    coverage = {
        "candidate_wheel_lap_pairs": candidates,
        "missing_current": 0,
        "missing_next": 0,
        "missing_both": 0,
        "invalid_absolute_values": 0,
        "labeled_wheel_lap_pairs": 0,
        "labeled_lap_pairs": 0,
        "sessions_with_labels": 0,
        "sessions_without_target_change": [],
        "sessions_with_reset_like_jumps": [],
        "reset_like_jump_rows": 0,
    }
    examples = []
    for row in rows:
        for wheel in WHEELS:
            name = f"{prefix}_{wheel}_at_sample"
            current, following = row.get(f"{name}_current"), row.get(f"{name}_next")
            if not _finite_number(current) or not _finite_number(following):
                coverage["missing_current"] += not _finite_number(current)
                coverage["missing_next"] += not _finite_number(following)
                coverage["missing_both"] += not _finite_number(current) and not _finite_number(following)
                continue
            if wear_target and (current < 0 or following < 0):
                coverage["invalid_absolute_values"] += 1
                continue
            if not wear_target and not all(MIN_CORE_TEMP_C <= value <= MAX_CORE_TEMP_C
                                           for value in (current, following)):
                coverage["invalid_absolute_values"] += 1
                continue
            delta = following - current
            if not _finite_number(delta):
                coverage["invalid_absolute_values"] += 1
                continue
            current_wear = row.get(f"tyre_wear_{wheel}_at_sample_current")
            current_temp = row.get(f"tyre_temp_{wheel}_at_sample_current")
            fuel = row.get("fuel_at_sample_l_current")
            examples.append({
                "session_id": row["session_id"],
                "captured_utc": row.get("captured_utc", ""),
                "current_lap": row["current_lap"],
                "car_model": row.get("car_model", ""),
                "wheel_index": wheel,
                "current_value": float(current),
                "next_value": float(following),
                "target": float(delta),
                "features": {
                    "current_lap_time_s": row["current_lap_time_s"],
                    "rolling_3_lap_s": row["rolling_3_lap_s"],
                    "current_lap": row["current_lap"],
                    "current_fuel_l": fuel if _finite_number(fuel) and fuel >= 0 else None,
                    "current_wear": current_wear if _finite_number(current_wear) and current_wear >= 0 else None,
                    "current_core_temp_c": current_temp if _finite_number(current_temp)
                    and MIN_CORE_TEMP_C <= current_temp <= MAX_CORE_TEMP_C else None,
                    "wheel_index": wheel,
                },
            })
    if wear_target and examples:
        buckets = defaultdict(list)
        for example in examples:
            buckets[(example["session_id"], example["car_model"], example["wheel_index"])].append(example)
        rejected = set()
        bad_sessions = set()
        for bucket in buckets.values():
            deltas = [item["target"] for item in bucket]
            median_delta = statistics.median(deltas)
            median_step = statistics.median(abs(value) for value in deltas)
            readings = [value for item in bucket for value in (item["current_value"], item["next_value"])]
            span = max(readings) - min(readings)
            threshold = max(WEAR_JUMP_MEDIAN_MULTIPLIER * median_step,
                            WEAR_JUMP_RANGE_FRACTION * span)
            for item in bucket:
                reverse = median_delta != 0 and item["target"] * median_delta < 0
                isolated = median_delta == 0 and item["target"] != 0
                if (reverse or isolated) and abs(item["target"]) > threshold:
                    rejected.add(id(item))
                    bad_sessions.add(item["session_id"])
        if rejected:
            examples = [item for item in examples if id(item) not in rejected]
        coverage["reset_like_jump_rows"] = len(rejected)
        coverage["sessions_with_reset_like_jumps"] = sorted(bad_sessions)
    pair_ids = {(item["session_id"], item["current_lap"]) for item in examples}
    coverage["labeled_wheel_lap_pairs"] = len(examples)
    coverage["labeled_lap_pairs"] = len(pair_ids)
    coverage["sessions_with_labels"] = len({item["session_id"] for item in examples})
    by_session = defaultdict(list)
    for item in examples:
        by_session[item["session_id"]].append(item)
    coverage["sessions_without_target_change"] = sorted(
        session for session, items in by_session.items()
        if all(item["target"] == 0 for item in items)
    )
    return examples, coverage


def _tyre_split(examples):
    sessions = _session_order(examples)
    pair_count = len({(row["session_id"], row["current_lap"]) for row in examples})
    if len(sessions) < MIN_SESSIONS:
        return None, None, sessions, pair_count, f"need {MIN_SESSIONS} independent sessions; found {len(sessions)}"
    if pair_count < MIN_ROWS:
        return None, None, sessions, pair_count, f"need {MIN_ROWS} labeled lap pairs; found {pair_count}"
    holdout_count = max(1, math.ceil(len(sessions) * 0.2))
    validation_sessions = sessions[-holdout_count:]
    training_sessions = sessions[:-holdout_count]
    pair_keys = lambda selected: {(row["session_id"], row["current_lap"]) for row in examples
                                  if row["session_id"] in selected}
    while len(pair_keys(validation_sessions)) < MIN_VALIDATION_ROWS and len(training_sessions) > 2:
        validation_sessions.insert(0, training_sessions.pop())
    training = [row for row in examples if row["session_id"] in training_sessions]
    validation = [row for row in examples if row["session_id"] in validation_sessions]
    train_pairs, validation_pairs = len(pair_keys(training_sessions)), len(pair_keys(validation_sessions))
    if train_pairs < MIN_TRAIN_ROWS or validation_pairs < MIN_VALIDATION_ROWS:
        return None, None, sessions, pair_count, (
            f"need {MIN_TRAIN_ROWS} training and {MIN_VALIDATION_ROWS} held-out lap pairs; "
            f"found {train_pairs} and {validation_pairs}"
        )
    return (training, validation, training_sessions, validation_sessions), None, sessions, pair_count, None


def _fit_tyre_target(examples, target, np, XGBRegressor, model_path):
    split, _, sessions, pair_count, reason = _tyre_split(examples)
    if reason:
        return {"status": "insufficient_data", "reason": reason, "sessions": len(sessions),
                "labeled_lap_pairs": pair_count, "labeled_wheel_lap_pairs": len(examples)}
    training, validation, training_sessions, validation_sessions = split
    feature_names = ["current_lap_time_s", "rolling_3_lap_s", "current_lap", "current_fuel_l",
                     "current_wear", "current_core_temp_c", "wheel_index"]

    def matrix(selected):
        return np.asarray([
            [float(item["features"][name]) if _finite_number(item["features"].get(name)) else np.nan
             for name in feature_names]
            for item in selected
        ], dtype=np.float32)

    train_x = matrix(training)
    train_y = np.asarray([item["target"] for item in training], dtype=np.float32)
    valid_x = matrix(validation)
    valid_y = np.asarray([item["target"] for item in validation], dtype=np.float32)
    model = XGBRegressor(objective="reg:squarederror", tree_method="hist", n_estimators=100,
                         max_depth=3, learning_rate=0.05, min_child_weight=5, subsample=0.8,
                         colsample_bytree=0.9, reg_lambda=5.0, n_jobs=1, random_state=42, verbosity=0)
    model.fit(train_x, train_y)
    predicted = model.predict(valid_x)
    mae = _metrics_mae(valid_y, predicted, np)
    persistence_mae = _metrics_mae(valid_y, np.zeros_like(valid_y), np)
    final_model = XGBRegressor(objective="reg:squarederror", tree_method="hist", n_estimators=100,
                               max_depth=3, learning_rate=0.05, min_child_weight=5, subsample=0.8,
                               colsample_bytree=0.9, reg_lambda=5.0, n_jobs=1, random_state=42, verbosity=0)
    all_y = np.asarray([item["target"] for item in examples], dtype=np.float32)
    final_model.fit(matrix(examples), all_y)
    final_model.save_model(str(model_path))
    return {
        "status": "trained_research_only_signal_unverified" if target == "wear_change" else "trained_research_only",
        "sessions": len(sessions), "labeled_lap_pairs": pair_count,
        "labeled_wheel_lap_pairs": len(examples),
        "training_sessions": training_sessions, "validation_sessions": validation_sessions,
        "training_lap_pairs": len({(item["session_id"], item["current_lap"]) for item in training}),
        "validation_lap_pairs": len({(item["session_id"], item["current_lap"]) for item in validation}),
        "validation_mae": mae, "persistence_baseline_mae": persistence_mae,
        "persistence_prediction": "zero change from current boundary",
        "target_units": "unverified source-native units" if target == "wear_change" else "degrees Celsius",
        "feature_order": feature_names, "model_file": model_path.name,
        "model_sha256": hashlib.sha256(model_path.read_bytes()).hexdigest(),
    }


def _train_tyres(data_dir: Path, output_dir: Path):
    process_id, process_summary, rows = _load_snapshot(output_dir)
    source_stale = _current_source_signatures(data_dir) != process_summary.get("source_signatures", [])
    grouped = defaultdict(list)
    for row in rows:
        grouped[(row["simulator"], row["category"])].append(row)
    prepared = []
    for (simulator, category), group_rows in sorted(grouped.items()):
        for target in ("wear_change", "core_temp_change"):
            examples, coverage = _tyre_examples(group_rows, target)
            metric = {
                "simulator": simulator, "category": category, "target": target,
                "signal_status": "signal_unverified" if target == "wear_change" else "not_independently_calibrated",
                "calibration_status": "not_calibrated", "coverage": coverage,
                "deployment_ready": False,
            }
            if not examples:
                metric.update({"status": "no_valid_labels", "reason": "no confirmed clean boundary pairs with valid samples"})
            elif target == "wear_change" and all(item["target"] == 0 for item in examples):
                metric.update({"status": "constant_signal", "reason": "wear samples do not change across labeled pairs"})
            else:
                _, _, sessions, pair_count, reason = _tyre_split(examples)
                if reason:
                    metric.update({"status": "insufficient_data", "reason": reason,
                                   "sessions": len(sessions), "labeled_lap_pairs": pair_count,
                                   "labeled_wheel_lap_pairs": len(examples)})
                else:
                    metric.update({"status": "ready_to_train", "sessions": len(sessions),
                                   "labeled_lap_pairs": pair_count,
                                   "labeled_wheel_lap_pairs": len(examples)})
            prepared.append((metric, examples, target))
    needs_training = any(metric["status"] == "ready_to_train" for metric, _, _ in prepared)
    np = xgboost = XGBRegressor = None
    if needs_training:
        try:
            import numpy as np
            import xgboost
            from xgboost import XGBRegressor
        except (ImportError, OSError) as error:
            raise RuntimeError("Train-tyres cần numpy và xgboost; cài bằng `python -m pip install numpy xgboost`.") from error
    run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8]
    runs_root = output_dir / "tyre_runs"
    runs_root.mkdir(parents=True, exist_ok=True)
    temporary_dir = Path(tempfile.mkdtemp(prefix=f".{run_id}-", dir=runs_root))
    final_dir = runs_root / run_id
    group_metrics = []
    try:
        for metric, examples, target in prepared:
            if metric["status"] == "ready_to_train":
                key = f"{metric['simulator']}_{metric['category']}"
                prefix = "tyre_wear_change" if target == "wear_change" else "core_temp_change"
                fitted = _fit_tyre_target(examples, target, np, XGBRegressor,
                                          temporary_dir / f"{prefix}_{key}.json")
                metric.update(fitted)
            group_metrics.append(metric)
        trained = sum(bool(item.get("model_file")) for item in group_metrics)
        if trained:
            status = "trained_research_only"
            message = (f"Đã train {trained} model lốp chỉ dùng nghiên cứu; wear source-native chưa xác minh/calibrate. "
                       "Kết quả không chọn vòng pit.")
        elif not any(item["coverage"]["labeled_wheel_lap_pairs"] for item in group_metrics):
            status = "no_valid_tyre_labels"
            message = "Snapshot chưa có nhãn lốp hợp lệ; không tạo model. Xem độ phủ trong summary.json."
        else:
            status = "insufficient_data"
            message = "Chưa đủ phiên/cặp lốp để train; xem lý do và độ phủ trong summary.json."
        source_warnings = []
        if source_stale:
            source_warnings.append("Newer logs exist; run Process to include them.")
        if process_summary.get("status") == "partial":
            source_warnings.append("The Process snapshot has unreadable source logs.")
        manifest = {
            "schema": "raceengineer_local_tyre_model_v1", "run_id": run_id,
            "created_utc": datetime.now(timezone.utc).isoformat(), "source_process_id": process_id,
            "source_snapshot_stale": source_stale, "model_type": "XGBRegressor",
            "targets": {"wear_change": "next boundary minus current boundary, source-native units",
                        "core_temp_change": "next boundary minus current boundary, degrees Celsius"},
            "wear_signal_status": "signal_unverified", "calibration_status": "not_calibrated",
            "grouped_by": ["simulator", "category"],
            "feature_policy": "All features use the current lap boundary; next samples are targets only.",
            "minimums": {"sessions_per_model": MIN_SESSIONS, "labeled_lap_pairs_per_model": MIN_ROWS,
                          "training_lap_pairs": MIN_TRAIN_ROWS, "held_out_lap_pairs": MIN_VALIDATION_ROWS},
            "cpu_threads": 1, "deployment_ready": False,
            "process_tyre_signal_coverage": process_summary.get("tyre_signal_coverage", {}),
            "training_environment": {"python": sys.version.split()[0],
                                     "numpy": np.__version__ if np is not None else None,
                                     "xgboost": xgboost.__version__ if xgboost is not None else None},
            "models": group_metrics,
            "limitations": ["Wear signal semantics and scale are unverified; metrics use source-native units.",
                            "No pit-lap, tyre-life, or pit-readiness claim is produced."],
        }
        run_summary = {
            "status": status, "message": message, "run_id": run_id, "source_process_id": process_id,
            "source_process_status": process_summary.get("status", "unknown"),
            "source_snapshot_stale": source_stale, "models_trained": trained,
            "models_skipped": len(group_metrics) - trained, "models": group_metrics,
            "process_tyre_signal_coverage": process_summary.get("tyre_signal_coverage", {}),
            "source_warnings": source_warnings, "wear_signal_status": "signal_unverified",
            "calibration_status": "not_calibrated", "deployment_ready": False,
        }
        _atomic_json(temporary_dir / "manifest.json", manifest)
        _atomic_json(temporary_dir / "metrics.json", {"models": group_metrics})
        _atomic_json(temporary_dir / "summary.json", run_summary)
        temporary_dir.replace(final_dir)
    except Exception:
        _remove_temp_dir(temporary_dir, runs_root)
        raise
    result = {
        "action": "train-tyres", "status": status, "message": message,
        "output_dir": str(output_dir.resolve()), "run_id": run_id, "run_dir": str(final_dir.resolve()),
        "summary_path": str((final_dir / "summary.json").resolve()),
        "metrics_path": str((final_dir / "metrics.json").resolve()),
        "manifest_path": str((final_dir / "manifest.json").resolve()),
        "source_process_id": process_id, "models_trained": trained,
        "models_skipped": len(group_metrics) - trained, "deployment_ready": False,
    }
    _atomic_json(output_dir / "last_result.json", result)
    return result


def main(argv=None):
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="backslashreplace")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--action", required=True, choices=("process", "train", "train-tyres"))
    parser.add_argument("--data-dir", required=True, type=Path, help="AppLocalDataLocation containing recorder logs")
    parser.add_argument("--output-dir", required=True, type=Path, help="Local processed data and model runs directory")
    args = parser.parse_args(argv)
    for name in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS", "NUMEXPR_NUM_THREADS"):
        os.environ[name] = "1"
    try:
        if args.action == "process":
            result = _process(args.data_dir, args.output_dir)
        elif args.action == "train":
            result = _train(args.data_dir, args.output_dir)
        else:
            result = _train_tyres(args.data_dir, args.output_dir)
    except Exception as error:
        result = {
            "status": "error",
            "message": str(error),
            "output_dir": str(args.output_dir.resolve()),
        }
        print(json.dumps(result, ensure_ascii=False, separators=(",", ":")))
        return 1
    print(json.dumps(result, ensure_ascii=False, separators=(",", ":"), allow_nan=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
