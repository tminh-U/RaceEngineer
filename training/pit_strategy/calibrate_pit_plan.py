#!/usr/bin/env python3
"""Calibrate research-only pace and pit-loss estimates from RaceEngineer JSONL."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import statistics
import tempfile
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path


LAP_TYPES = {"lap"}
PIT_TYPES = {"pit_enter", "pit_box_enter", "pit_box_exit", "pit_exit"}
STOP_SEQUENCE = ("pit_enter", "pit_box_enter", "pit_box_exit", "pit_exit")


def _finite(value):
    try:
        return (isinstance(value, (int, float)) and not isinstance(value, bool)
                and math.isfinite(value))
    except (OverflowError, TypeError, ValueError):
        return False


def _integer(value):
    return _finite(value) and int(value) == value


def _fuel_value(row):
    fuel, capacity = row.get("fuel_at_sample_l"), row.get("fuel_capacity_l")
    return (_finite(fuel) and fuel >= 0
            and (not _finite(capacity) or capacity <= 0 or fuel <= capacity + 0.1))


def _reject_constant(value):
    raise ValueError(f"invalid JSON number: {value}")


def _timestamp(value):
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
        if parsed.tzinfo is None:
            return None
        return parsed.astimezone(timezone.utc).timestamp()
    except (AttributeError, OverflowError, ValueError):
        return None


def _profile(path):
    try:
        data = json.loads(path.read_text(encoding="utf-8-sig"), parse_constant=_reject_constant)
    except (OSError, UnicodeError, ValueError) as error:
        raise ValueError(f"cannot read profile {path}: {error}") from error
    if not isinstance(data, dict):
        raise ValueError("profile must be a JSON object")
    for name in ("profile_id", "simulator", "track", "car_model"):
        if not isinstance(data.get(name), str) or not data[name].strip():
            raise ValueError(f"profile.{name} must be a non-empty string")
    if not _integer(data.get("total_laps")) or data["total_laps"] < 1:
        raise ValueError("profile.total_laps must be a positive integer")
    return data


def _matches_profile(row, profile):
    return (all(row.get(key) == profile[key] for key in
                ("simulator", "track", "car_model"))
            and _integer(row.get("total_laps"))
            and row["total_laps"] == profile["total_laps"])


def _read_logs(paths, profile):
    sessions = defaultdict(lambda: {"laps": [], "events": []})
    quality = {"lines": 0, "malformed_json": 0, "unmatched_profile": 0,
               "unconfirmed_records": 0, "invalid_records": 0}
    for path in paths:
        try:
            with path.open(encoding="utf-8-sig") as stream:
                for raw in stream:
                    quality["lines"] += 1
                    try:
                        row = json.loads(raw, parse_constant=_reject_constant)
                    except (TypeError, ValueError):
                        quality["malformed_json"] += 1
                        continue
                    if not isinstance(row, dict):
                        quality["invalid_records"] += 1
                        continue
                    record_type = row.get("record_type")
                    if not isinstance(record_type, str) or record_type not in LAP_TYPES | PIT_TYPES:
                        continue
                    if not _matches_profile(row, profile):
                        quality["unmatched_profile"] += 1
                        continue
                    if row.get("realism_confirmed") is not True:
                        quality["unconfirmed_records"] += 1
                        continue
                    session = row.get("session_id")
                    if not isinstance(session, str) or not session.strip():
                        quality["invalid_records"] += 1
                        continue
                    if record_type == "lap":
                        lap = row.get("completed_lap")
                        lap_time = row.get("lap_time_s")
                        if (not _integer(lap) or lap < 1 or lap > profile["total_laps"]
                                or not _finite(lap_time) or not 10 <= lap_time <= 600):
                            quality["invalid_records"] += 1
                            continue
                        sessions[session]["laps"].append(row)
                    else:
                        if (not _integer(row.get("current_lap")) or row["current_lap"] < 0
                                or _timestamp(row.get("captured_utc")) is None):
                            quality["invalid_records"] += 1
                            continue
                        sessions[session]["events"].append(row)
        except (OSError, UnicodeError) as error:
            raise ValueError(f"cannot read log {path}: {error}") from error
    return sessions, quality


def _deduplicate(rows, key):
    groups = defaultdict(list)
    for row in rows:
        groups[key(row)].append(row)
    unique, conflicts = [], 0
    for items in groups.values():
        if all(item == items[0] for item in items[1:]):
            unique.append(items[0])
        else:
            conflicts += 1
    return unique, conflicts


def _pit_visits(events, profile):
    by_type = defaultdict(list)
    for row in events:
        by_type[row.get("record_type")].append((row, _timestamp(row.get("captured_utc"))))
    ordered = sorted((item for items in by_type.values() for item in items if item[1] is not None),
                     key=lambda item: item[1])
    visits, incomplete = [], 0
    index = 0
    while index < len(ordered):
        if ordered[index][0].get("record_type") != "pit_enter":
            index += 1
            continue
        sequence = ordered[index:index + len(STOP_SEQUENCE)]
        types = tuple(item[0].get("record_type") for item in sequence)
        if (len(sequence) != len(STOP_SEQUENCE) or types != STOP_SEQUENCE
                or any(sequence[i][1] >= sequence[i + 1][1] for i in range(len(sequence) - 1))):
            incomplete += 1
            index += 1
            continue
        rows = [item[0] for item in sequence]
        laps = [row.get("current_lap") for row in rows]
        if (not all(_integer(lap) for lap in laps) or laps[0] < 1
                or laps[-1] > profile["total_laps"] or laps[-1] - laps[0] not in (0, 1)
                or any(laps[i] > laps[i + 1] for i in range(len(laps) - 1))):
            incomplete += 1
            index += 1
            continue

        box_in, box_out = rows[1], rows[2]
        fuel_before, fuel_after = box_in.get("fuel_at_sample_l"), box_out.get("fuel_at_sample_l")
        capacity = box_in.get("fuel_capacity_l")
        if not _finite(capacity) or capacity <= 0:
            capacity = box_out.get("fuel_capacity_l")
        if not _finite(capacity) or capacity <= 0:
            capacity = None
        fuel_added = None
        if (_finite(fuel_before) and _finite(fuel_after) and fuel_before >= 0 and fuel_after >= 0
                and (not _finite(capacity) or max(fuel_before, fuel_after) <= capacity + 0.1)):
            fuel_added = fuel_after - fuel_before
        fuel_service = (profile.get("refuel_allowed") is True
                        and profile.get("refuel_amount_control_validated") is True
                        and _finite(fuel_added)
                        and fuel_added > max(0.05, capacity * 0.002 if _finite(capacity) else 0.05))

        before_wear = [box_in.get(f"tyre_wear_{i}_at_sample") for i in range(4)]
        after_wear = [box_out.get(f"tyre_wear_{i}_at_sample") for i in range(4)]
        tyre_reset = (profile.get("tyre_change_validated") is True
                      and all(_finite(a) and _finite(b) and a >= 0 and b >= 0
                              and b <= a - max(0.01, abs(a) * 0.1)
                              for a, b in zip(before_wear, after_wear)))
        services = [name for name, present in (("refuel", fuel_service), ("tyre_change", tyre_reset))
                    if present]
        visits.append({"lap": int(laps[0]), "rows": rows,
                       "times": [item[1] for item in sequence], "services": services,
                       "fuel_added_l": fuel_added, "service_observed": bool(services),
                       "tyres_changed": tyre_reset})
        index += len(STOP_SEQUENCE)
    return visits, incomplete


def _calibration_rows(sessions, profile):
    result, raw_laps, visits_by_session = [], {}, {}
    quality = {"duplicate_laps_removed": 0, "conflicting_lap_groups_removed": 0,
               "duplicate_events_removed": 0, "conflicting_event_groups_removed": 0,
               "incomplete_pit_sequences": 0, "complete_pit_visits": 0,
               "service_observed_visits": 0, "laps_excluded": 0,
               "pit_adjacent_laps_excluded": 0, "unknown_stint_age_laps": 0}
    for session, records in sessions.items():
        laps, conflicts = _deduplicate(records["laps"], lambda row: row["completed_lap"])
        quality["conflicting_lap_groups_removed"] += conflicts
        quality["duplicate_laps_removed"] += len(records["laps"]) - len(laps) - conflicts
        events, conflicts = _deduplicate(
            records["events"],
            lambda row: (row.get("record_type"), row.get("captured_utc"), row.get("current_lap")))
        quality["conflicting_event_groups_removed"] += conflicts
        quality["duplicate_events_removed"] += len(records["events"]) - len(events) - conflicts
        events = [row for row in events if row.get("record_type") in PIT_TYPES]
        visits, incomplete = _pit_visits(events, profile)
        quality["incomplete_pit_sequences"] += incomplete
        quality["complete_pit_visits"] += len(visits)
        quality["service_observed_visits"] += sum(visit["service_observed"] for visit in visits)
        visits_by_session[session] = visits
        raw_laps[session] = laps

        blocked = {int(row["current_lap"]) + offset
                   for row in events if _integer(row.get("current_lap"))
                   for offset in (-1, 0, 1) if int(row["current_lap"]) + offset > 0}
        first_lap = min((int(row["completed_lap"]) for row in laps), default=None)
        known_start = (profile.get("start_fresh_tyres_validated") is True
                       and first_lap is not None and first_lap <= 2)
        service_laps = sorted(visit["lap"] for visit in visits if visit["tyres_changed"])
        for row in sorted(laps, key=lambda item: item["completed_lap"]):
            lap = int(row["completed_lap"])
            if (row.get("lap_excluded") is not False or row.get("in_pit_at_sample") is not False
                    or lap == 1):
                quality["laps_excluded"] += 1
                continue
            if lap in blocked:
                quality["pit_adjacent_laps_excluded"] += 1
                continue
            prior_service = [stop_lap for stop_lap in service_laps if stop_lap < lap]
            if prior_service:
                age = lap - prior_service[-1]
            elif known_start:
                age = lap
            else:
                quality["unknown_stint_age_laps"] += 1
                continue
            result.append({"session": session, "lap": lap, "age": age,
                           "time": float(row["lap_time_s"]), "row": row})
    return result, raw_laps, visits_by_session, quality


def _fit(rows):
    # ponytail: fixed-effects linear trend; add model complexity only if independent held-out races justify it.
    by_session = defaultdict(list)
    for item in rows:
        by_session[item["session"]].append(item)
    by_session = {session: items for session, items in by_session.items()
                  if any(item["age"] <= 2 for item in items)}
    rows = [item for items in by_session.values() for item in items]
    x_values = [item["age"] - 1 for item in rows]
    x_range = len(set(x_values))
    sxx = sxy = 0.0
    total = 0
    for session, items in by_session.items():
        xm = statistics.mean(item["age"] - 1 for item in items)
        ym = statistics.mean(item["time"] for item in items)
        for item in items:
            dx, dy = item["age"] - 1 - xm, item["time"] - ym
            sxx += dx * dx
            sxy += dx * dy
            total += 1
    if total < 10 or x_range < 3 or sxx < 1e-9:
        return None
    age_slope = sxy / sxx

    fuel_rows = [item for item in rows if _fuel_value(item["row"])]
    fuel_slope, fuel_ref = None, None
    if (len(fuel_rows) >= 30 and len({item["session"] for item in fuel_rows}) >= 3
            and max(item["row"]["fuel_at_sample_l"] for item in fuel_rows)
            - min(item["row"]["fuel_at_sample_l"] for item in fuel_rows) >= 5):
        fuel_groups = defaultdict(list)
        for item in fuel_rows:
            fuel_groups[item["session"]].append(item)
        fuel_ref = statistics.median(item["row"]["fuel_at_sample_l"] for item in fuel_rows)
        fxx = fxy = fyy = fx = fy = 0.0
        for items in fuel_groups.values():
            xm = statistics.mean(item["age"] - 1 for item in items)
            fm = statistics.mean(item["row"]["fuel_at_sample_l"] for item in items)
            ym = statistics.mean(item["time"] for item in items)
            for item in items:
                dx = item["age"] - 1 - xm
                df = item["row"]["fuel_at_sample_l"] - fm
                dy = item["time"] - ym
                fxx += dx * dx
                fyy += df * df
                fxy += dx * df
                fx += dx * dy
                fy += df * dy
        determinant = fxx * fyy - fxy * fxy
        if determinant > max(1e-9, fxx * fyy * 1e-4):
            candidate_age = (fx * fyy - fy * fxy) / determinant
            candidate_fuel = (fy * fxx - fx * fxy) / determinant
            if candidate_fuel > 0:
                age_slope, fuel_slope = candidate_age, candidate_fuel
    if fuel_slope is None:
        fuel_ref = None

    model_rows = fuel_rows if fuel_slope is not None else rows
    model_groups = defaultdict(list)
    for item in model_rows:
        model_groups[item["session"]].append(item)
    intercepts = {}
    for session, items in model_groups.items():
        intercepts[session] = statistics.mean(
            item["time"] - age_slope * (item["age"] - 1)
            - (fuel_slope or 0) * ((item["row"].get("fuel_at_sample_l") or 0) - (fuel_ref or 0))
            for item in items)
    return {"age_slope": age_slope, "fuel_slope": fuel_slope, "fuel_ref": fuel_ref,
            "intercepts": intercepts, "fresh": statistics.median(intercepts.values()),
            "sessions": len(by_session), "rows": len(rows)}


def _predict(fit, item):
    fuel = item["row"].get("fuel_at_sample_l")
    if fit["fuel_slope"] is not None and not _fuel_value(item["row"]):
        return None
    return (fit["fresh"] + fit["age_slope"] * (item["age"] - 1)
            + (fit["fuel_slope"] or 0) *
            ((fuel if _finite(fuel) else fit["fuel_ref"] or 0) - (fit["fuel_ref"] or 0)))


def _validate(rows):
    grouped = defaultdict(list)
    for item in rows:
        grouped[item["session"]].append(item)
    errors, baseline_errors, held_out = [], [], set()
    for session, test in grouped.items():
        training = [item for other, items in grouped.items() if other != session for item in items]
        fit = _fit(training)
        if fit is None:
            continue
        previous = {item["lap"]: item for item in test}
        evaluated = []
        for item in test:
            predicted = _predict(fit, item)
            if predicted is None:
                continue
            prior = previous.get(item["lap"] - 1)
            if prior is not None and prior["age"] == item["age"] - 1:
                evaluated.append((abs(item["time"] - predicted),
                                  abs(item["time"] - prior["time"])))
        errors.extend(model_error for model_error, _ in evaluated)
        baseline_errors.extend(baseline_error for _, baseline_error in evaluated)
        if evaluated:
            held_out.add(session)
    return errors, baseline_errors, len(held_out)


def _fuel_use(rows):
    samples = defaultdict(list)
    for item in rows:
        used = item["row"].get("fuel_used_l")
        if _finite(used) and 0 < used <= 30:
            samples[item["session"]].append(float(used))
    if sum(map(len, samples.values())) < 5 or len(samples) < 2:
        samples = defaultdict(list)
        by_session = defaultdict(list)
        for item in rows:
            fuel = item["row"].get("fuel_at_sample_l")
            if _fuel_value(item["row"]):
                by_session[item["session"]].append((item["lap"], float(fuel)))
        for session, readings in by_session.items():
            readings.sort()
            for (lap_a, fuel_a), (lap_b, fuel_b) in zip(readings, readings[1:]):
                drop = fuel_a - fuel_b
                if lap_b == lap_a + 1 and 0 < drop <= 30:
                    samples[session].append(drop)
    values = [value for readings in samples.values() for value in readings]
    if len(values) < 5 or len(samples) < 2:
        return None, len(values), len(samples)
    return statistics.median(values), len(values), len(samples)


def _pit_loss(visits, raw_laps, fit):
    residuals, sessions, cross_lap_stops = [], set(), 0
    for session, stops in visits.items():
        if session not in fit["intercepts"]:
            continue
        laps = {int(row["completed_lap"]): row for row in raw_laps.get(session, [])}
        for stop in stops:
            if not stop["service_observed"]:
                continue
            event_start, event_finish = stop["times"][0], stop["times"][-1]
            crossed_line = any(
                (timestamp := _timestamp(row.get("captured_utc"))) is not None
                and event_start < timestamp < event_finish
                for row in laps.values())
            if (stop["rows"][0]["current_lap"] != stop["rows"][-1]["current_lap"]
                    or crossed_line):
                cross_lap_stops += 1
                continue
            lap = stop["lap"]
            pit_lap, prior = laps.get(lap), laps.get(lap - 1)
            if (pit_lap is None or prior is None or pit_lap.get("lap_excluded") is not True
                    or not _finite(pit_lap.get("lap_time_s"))
                    or prior.get("lap_excluded") is not False
                    or prior.get("in_pit_at_sample") is not False):
                continue
            start, finish = _timestamp(prior.get("captured_utc")), _timestamp(pit_lap.get("captured_utc"))
            if start is None or finish is None or not (start < stop["times"][0] < stop["times"][-1] < finish):
                continue
            fuel = stop["rows"][0].get("fuel_at_sample_l")
            if fit["fuel_slope"] is not None and not _fuel_value(stop["rows"][0]):
                continue
            expected = (fit["intercepts"][session] + fit["age_slope"] * (lap - 1)
                        + (fit["fuel_slope"] or 0) *
                        ((fuel if _finite(fuel) else fit["fuel_ref"] or 0) - (fit["fuel_ref"] or 0)))
            residual = float(pit_lap["lap_time_s"]) - expected
            residuals.append((session, residual))
            sessions.add(session)
    if cross_lap_stops or len(residuals) < 2 or len(sessions) < 2:
        return None, residuals, cross_lap_stops
    estimate = statistics.median(value for _, value in residuals)
    return (estimate if estimate > 0 else None), residuals, cross_lap_stops


def _sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _source_hashes(paths):
    combined, files = hashlib.sha256(), []
    for path in paths:
        size = path.stat().st_size
        combined.update(size.to_bytes(8, "big"))
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
                combined.update(block)
        files.append({"path": str(path), "sha256": digest.hexdigest(), "bytes": size})
    return files, combined.hexdigest()


def _write_json(path, value):
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


def calibrate(profile_path, paths):
    profile = _profile(profile_path)
    paths = sorted({path.resolve() for path in paths}, key=lambda path: str(path).casefold())
    if not paths or any(not path.is_file() for path in paths):
        raise ValueError("provide existing JSONL session log files")
    sessions, read_quality = _read_logs(paths, profile)
    rows, raw_laps, visits, quality = _calibration_rows(sessions, profile)
    reasons, parameter_quality = [], {}
    session_count = len({item["session"] for item in rows})
    source_files, source_sha256 = _source_hashes(paths)
    ages_by_session = defaultdict(set)
    for item in rows:
        ages_by_session[item["session"]].add(item["age"])
    fresh_supported = sum(any(age <= 2 for age in ages) for ages in ages_by_session.values()) >= 2
    fit = _fit(rows) if fresh_supported else None

    if fit is not None:
        fresh = fit["fresh"] - (fit["fuel_slope"] or 0) * (fit["fuel_ref"] or 0)
        age_penalty = fit["age_slope"]
        parameter_quality["fresh_lap_s"] = "research_only; observed clean laps at stint age 1-2"
        parameter_quality["stint_age_penalty_s_per_lap"] = "research_only; within-session linear slope"
    else:
        fresh = age_penalty = None
        reason = ("need at least ten clean laps, age-1/2 laps in two sessions, and three observed stint ages"
                  if fresh_supported else "need clean age-1/2 laps in at least two sessions")
        reasons.append(reason)
        parameter_quality["fresh_lap_s"] = parameter_quality["stint_age_penalty_s_per_lap"] = reason

    fuel_penalty = fit["fuel_slope"] if fit is not None else None
    if fuel_penalty is None:
        reason = "fuel penalty unsupported: need independent fuel variation across three sessions and a positive effect"
        reasons.append(reason)
        parameter_quality["fuel_penalty_s_per_liter"] = reason
    else:
        parameter_quality["fuel_penalty_s_per_liter"] = "research_only; full-rank within-session regression"

    fuel_use, fuel_use_rows, fuel_use_sessions = _fuel_use(rows)
    if fuel_use is None:
        reason = "need at least five measured or adjacent-lap fuel-use samples from two sessions"
        reasons.append(reason)
        parameter_quality["fuel_use_l_per_lap"] = reason
    else:
        parameter_quality["fuel_use_l_per_lap"] = "research_only; median of observed consumption"

    errors, baseline_errors, held_out_sessions = _validate(rows)
    pace_mae = statistics.mean(errors) if errors else None
    baseline_mae = statistics.mean(baseline_errors) if baseline_errors else None
    uncertainty = None
    if pace_mae is None or baseline_mae is None:
        reasons.append("whole-session held-out pace validation or previous-lap baseline has insufficient samples")
    reasons.append("race event identity is unverified; held-out sessions are not counted as independent races")

    pit_loss, pit_residuals, cross_lap_stops = (
        _pit_loss(visits, raw_laps, fit) if fit is not None else (None, [], 0))
    if pit_loss is None:
        reasons.append("pit loss requires fitted clean pace and two complete, service-observed timed stops in separate sessions")
        if cross_lap_stops:
            reasons.append("cross-line pit stops were observed; total loss across both affected laps is unsupported")
        parameter_quality["pit_loss_s"] = "insufficient_data"
    else:
        parameter_quality["pit_loss_s"] = "research_only; median pit-affected lap residual"
    reasons.append("whole-race pairwise uncertainty is unavailable from lap-level residuals")
    parameter_quality["uncertainty_s"] = "insufficient_data: no held-out whole-race pairwise residuals"
    parameter_quality["decision_margin_s"] = "insufficient_data: no counterfactual legal-choice labels"
    reasons.append("decision margin unavailable without counterfactual legal-choice labels")

    profile_flags_present = all(isinstance(profile.get(name), bool) for name in
                                 ("refuel_allowed", "tyre_change_validated", "fixed_pit_loss_validated",
                                  "start_fresh_tyres_validated"))
    profile_prerequisites_met = (profile_flags_present
                                 and profile.get("start_fresh_tyres_validated") is True
                                 and (profile.get("refuel_allowed") is not True
                                      or profile.get("refuel_amount_control_validated") is True))
    if not profile_prerequisites_met:
        reasons.append("profile must validate fresh race-start tyres and required service controls")
    artifact = {
        "schema_version": 1,
        "profile_id": profile["profile_id"],
        "simulator": profile["simulator"],
        "track": profile["track"],
        "car_model": profile["car_model"],
        "total_laps": int(profile["total_laps"]),
        "source_sha256": source_sha256,
        "session_count": session_count,
        "fresh_lap_s": fresh,
        "stint_age_penalty_s_per_lap": age_penalty,
        "fuel_penalty_s_per_liter": fuel_penalty,
        "fuel_use_l_per_lap": fuel_use,
        "pit_loss_s": pit_loss,
        "uncertainty_s": uncertainty,
        "decision_margin_s": None,
        "deployment_ready": False,
        "metrics": {
            "held_out_races": 0,
            "prospective_races": 0,
            "pace_mae_s": pace_mae,
            "baseline_pace_mae_s": baseline_mae,
            "legal_choice_rate": 0,
            "improvement_ci95_lower_s": 0,
        },
        "status": "insufficient_data",
        "quality": {
            **quality,
            "matching_sessions": session_count,
            "clean_laps_used": len(rows),
            "held_out_sessions": held_out_sessions,
            "held_out_lap_predictions": len(errors),
            "baseline_lap_predictions": len(baseline_errors),
            "fuel_use_samples": fuel_use_rows,
            "fuel_use_sessions": fuel_use_sessions,
            "pit_loss_residuals": len(pit_residuals),
            "pit_loss_cross_lap_stops": cross_lap_stops,
            "parameter_status": parameter_quality,
            "insufficient_data_reasons": sorted(set(reasons)),
            "read": read_quality,
            "profile_calibration_flags_present": profile_flags_present,
            "profile_prerequisites_met": profile_prerequisites_met,
            "decision_metrics_status": "not_evaluated; conservative zero values retained by schema",
        },
        "provenance": {
            "profile_path": str(profile_path.resolve()),
            "profile_sha256": _sha256(profile_path),
            "source_files": source_files,
            "source_hash_basis": "sha256 over sorted JSONL bytes, prefixed by each file's 8-byte size",
            "source_sessions": sorted(sessions),
            "pace_model": "lap_time = fresh_lap_s + stint_age_penalty_s_per_lap*(stint_age-1) + fuel_penalty_s_per_liter*fuel_l",
            "fresh_lap_semantics": "pace intercept at zero fuel; matches runtime predictor formula",
            "fuel_reference_l": fit["fuel_ref"] if fit is not None else None,
            "pit_loss_basis": "timed pit-affected lap residual against fitted clean-lap pace",
        },
    }
    return artifact


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", required=True, type=Path, help="exact strategy profile JSON")
    parser.add_argument("--output", required=True, type=Path, help="plan_calibration.json output path")
    parser.add_argument("logs", nargs="+", type=Path, help="RaceEngineer JSONL session logs")
    args = parser.parse_args(argv)
    try:
        input_paths = {args.profile.resolve(), *(path.resolve() for path in args.logs)}
        if args.output.resolve() in input_paths:
            raise ValueError("output path must not overwrite the profile or an input log")
        artifact = calibrate(args.profile, args.logs)
        _write_json(args.output, artifact)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(json.dumps({"status": artifact["status"], "output": str(args.output.resolve()),
                      "clean_laps": artifact["quality"]["clean_laps_used"],
                      "held_out_sessions": artifact["quality"]["held_out_sessions"]},
                     ensure_ascii=False, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
