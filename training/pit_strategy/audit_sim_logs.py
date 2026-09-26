#!/usr/bin/env python3
"""Audit RaceEngineer recorder JSONL without deriving pit-strategy labels."""

import argparse
from collections import Counter, defaultdict, deque
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import sys
import tempfile

from vehicle_category import vehicle_category


EVENTS = {
    "pit_enter": "pit_enter", "pit_exit": "pit_exit",
    "pit_box_enter": "box_enter", "pit_box_exit": "box_exit",
}
WEAR = [f"tyre_wear_{i}_at_sample" for i in range(4)]
LIMITATIONS = [
    "Vehicle category uses source class or car-model text; unknown/conflicting categories need review before training.",
    "Box events show a visit, not whether fuel, tyres, or other service was completed.",
    "Missing optional samples are not zero; coverage does not validate ACC tyreWear as a wear signal.",
    "Pit observations are not candidate-quality or optimal-stop labels.",
]


def _reject_constant(value):
    raise ValueError(f"non-standard JSON number {value}")


def _number(value):
    try:
        return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)
    except (OverflowError, TypeError, ValueError):
        return False


def _time(value):
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
        return parsed.astimezone(timezone.utc).timestamp() if parsed.tzinfo else None
    except (AttributeError, ValueError, OverflowError):
        return None


def _read(paths):
    sources, rows = [], []
    for file_index, path in enumerate(paths):
        source = {"path": str(path), "lines": 0, "malformed_json": [], "invalid_records": []}
        sources.append(source)
        try:
            with path.open(encoding="utf-8-sig") as stream:
                for line, raw in enumerate(stream, 1):
                    source["lines"] += 1
                    try:
                        row = json.loads(raw, parse_constant=_reject_constant)
                    except ValueError as error:
                        source["malformed_json"].append({"line": line, "error": str(error)})
                        continue
                    if not isinstance(row, dict):
                        source["invalid_records"].append({"line": line, "error": "expected JSON object"})
                        continue
                    kind, session = row.get("record_type"), row.get("session_id")
                    if not isinstance(kind, str) or not kind.strip() or not isinstance(session, str) or not session.strip():
                        source["invalid_records"].append({"line": line, "error": "missing record_type or session_id"})
                        continue
                    rows.append({"row": row, "session": session, "source": str(path),
                                 "file_index": file_index, "line": line})
        except (OSError, UnicodeError) as error:
            source["read_error"] = str(error)
    return sources, rows


def _coverage(rows, fields):
    if isinstance(fields, str):
        fields = [fields]
    present = sum(any(_number(record["row"].get(field)) for field in fields) for record in rows)
    invalid = sum(any(field in record["row"] and record["row"][field] is not None
                      and not _number(record["row"][field]) for field in fields) for record in rows)
    return {"present": present, "missing": len(rows) - present, "invalid_values": invalid}


def _pairs(events, entry, exit_):
    pending, pairs, unmatched_exits = deque(), [], 0
    for index, event in enumerate(events):
        if event["kind"] == entry:
            pending.append(index)
        elif event["kind"] == exit_:
            if pending:
                pairs.append((pending.popleft(), index))
            else:
                unmatched_exits += 1
    return pairs, len(pending), unmatched_exits


def _duplicates(rows):
    groups = defaultdict(list)
    for record in rows:
        row, kind = record["row"], record["row"].get("record_type")
        if kind == "lap":
            lap = row.get("completed_lap")
            if _number(lap) and lap >= 1 and int(lap) == lap:
                key = ("lap", int(lap))
            else:
                continue
        elif kind in EVENTS and _time(row.get("captured_utc")) is not None:
            key = ("event", kind, row["captured_utc"])
        else:
            continue
        groups[key].append({"source": record["source"], "line": record["line"]})
    return [
        {"kind": key[0], "key": key[1:], "locations": locations}
        for key, locations in groups.items() if len(locations) > 1
    ]


def _vehicle_category(row):
    recorded = row.get("car_category")
    inferred = vehicle_category(car_model=row.get("car_model"),
                                class_label=row.get("car_class"),
                                homologation=row.get("homologation"),
                                source_domain=row.get("source_domain"))
    if recorded in {"f1", "formula", "gt", "prototype", "touring", "road",
                    "cup", "rally", "track_only", "drift", "time_attack", "unknown"}:
        if recorded != "unknown" and inferred != "unknown" and recorded != inferred:
            return "conflict"
        return recorded if recorded != "unknown" else inferred
    return inferred


def _session(session_id, rows):
    categories = {_vehicle_category(r["row"]) for r in rows} - {"unknown"}
    conflict = "conflict" in categories or len(categories) > 1
    category = next(iter(categories)) if len(categories) == 1 and not conflict else "unknown"
    lap_rows = [r for r in rows if r["row"].get("record_type") == "lap"]
    lap_numbers = {int(r["row"]["completed_lap"]) for r in lap_rows
                   if _number(r["row"].get("completed_lap")) and r["row"]["completed_lap"] >= 1
                   and int(r["row"]["completed_lap"]) == r["row"]["completed_lap"]}
    events = [dict(r, kind=EVENTS[r["row"]["record_type"]]) for r in rows
              if r["row"].get("record_type") in EVENTS]
    times = [_time(e["row"].get("captured_utc")) for e in events]
    timed = bool(events) and all(t is not None for t in times)
    events.sort(key=(lambda e: (_time(e["row"].get("captured_utc")), e["file_index"], e["line"]))
                if timed else (lambda e: (e["file_index"], e["line"])))
    pit_pairs, open_pit, unmatched_pit = _pairs(events, "pit_enter", "pit_exit")
    box_pairs, open_box, unmatched_box = _pairs(events, "box_enter", "box_exit")

    def has_box(pit_pair):
        start, end = pit_pair
        return any(start < box_start < box_end < end for box_start, box_end in box_pairs)

    pre_race = [pair for pair in pit_pairs
                if _number(events[pair[0]]["row"].get("current_lap"))
                and events[pair[0]]["row"]["current_lap"] <= 0]
    in_race = [pair for pair in pit_pairs
               if _number(events[pair[0]]["row"].get("current_lap"))
               and events[pair[0]]["row"]["current_lap"] > 0]
    duplicates = _duplicates(rows)
    counts = Counter(r["row"]["record_type"] for r in rows)
    fuel_rows = _coverage(lap_rows, ["fuel_used_l", "fuel_at_sample_l"])
    coverage = {
        "lap_records": len(lap_rows),
        "fuel_any": fuel_rows,
        "fuel_used_l": _coverage(lap_rows, "fuel_used_l"),
        "fuel_at_sample_l": _coverage(lap_rows, "fuel_at_sample_l"),
        "gap_ahead_at_sample_s": _coverage(lap_rows, "gap_ahead_at_sample_s"),
        "gap_behind_at_sample_s": _coverage(lap_rows, "gap_behind_at_sample_s"),
        "tyre_wear_corners": {field: _coverage(lap_rows, field) for field in WEAR},
        "tyre_wear_any_corner": sum(any(_number(r["row"].get(f)) for f in WEAR) for r in lap_rows),
        "tyre_wear_all_four_corners": sum(all(_number(r["row"].get(f)) for f in WEAR) for r in lap_rows),
    }
    return {
        "session_id": session_id,
        "vehicle_category": category,
        "category_conflict": conflict,
        "car_models": sorted({r["row"]["car_model"] for r in rows
                              if isinstance(r["row"].get("car_model"), str) and r["row"]["car_model"]}),
        "source_files": sorted({r["source"] for r in rows}),
        "simulators": sorted({str(r["row"]["simulator"]) for r in rows if r["row"].get("simulator") not in (None, "")}),
        "record_counts": dict(sorted(counts.items())),
        "laps": {"records": len(lap_rows), "unique": len(lap_numbers),
                 "first": min(lap_numbers) if lap_numbers else None,
                 "last": max(lap_numbers) if lap_numbers else None,
                 "duplicate_groups": sum(d["kind"] == "lap" for d in duplicates)},
        "pit_stops": {
            "pit_enters": sum(e["kind"] == "pit_enter" for e in events),
            "pit_exits": sum(e["kind"] == "pit_exit" for e in events),
            "paired_visits": len(pit_pairs),
            "pre_race_visits_current_lap_le_zero": len(pre_race),
            "in_race_visits_current_lap_positive": len(in_race),
            "visits_unknown_entry_lap": len(pit_pairs) - len(pre_race) - len(in_race),
            "pit_enters_without_exit": open_pit, "pit_exits_without_enter": unmatched_pit,
            "box_enters": sum(e["kind"] == "box_enter" for e in events),
            "box_exits": sum(e["kind"] == "box_exit" for e in events),
            "paired_box_cycles": len(box_pairs),
            "box_enters_without_exit": open_box, "box_exits_without_enter": unmatched_box,
            "visits_with_box_cycle": sum(has_box(pair) for pair in pit_pairs),
            "in_race_visits_with_box_cycle": sum(has_box(pair) for pair in in_race),
            "timeline_basis": "captured_utc" if timed else "input_order",
            "events_missing_or_invalid_timestamp": sum(t is None for t in times),
            "duplicate_event_groups": sum(d["kind"] == "event" for d in duplicates),
        },
        "telemetry_coverage": coverage,
        "duplicates": duplicates,
    }


def audit(paths):
    sources, rows = _read(paths)
    groups = defaultdict(list)
    for row in rows:
        groups[row["session"]].append(row)
    sessions = [_session(key, group) for key, group in sorted(groups.items())]
    return {
        "report": "sim_log_audit_v1",
        "interpretation": "Descriptive recorder audit only; no strategy labels are created.",
        "sources": sources,
        "totals": {"files": len(sources), "lines": sum(s["lines"] for s in sources),
                   "records": len(rows),
                   "malformed_json": sum(len(s["malformed_json"]) for s in sources),
                   "invalid_records": sum(len(s["invalid_records"]) for s in sources),
                   "sessions": len(sessions)},
        "sessions": sessions,
        "limitations": LIMITATIONS,
    }


def _ratio(data):
    total = data["present"] + data["missing"]
    return f"{data['present']}/{total}" if total else "n/a"


def print_human(report):
    t = report["totals"]
    print(f"Files {t['files']}; lines {t['lines']}; records {t['records']}; "
          f"malformed {t['malformed_json']}; invalid {t['invalid_records']}; sessions {t['sessions']}")
    for source in report["sources"]:
        print(f"Source: {source['path']} ({source['lines']} lines)")
        for key in ("malformed_json", "invalid_records"):
            for issue in source[key]:
                print(f"  {key} line {issue['line']}: {issue['error']}")
        if source.get("read_error"):
            print(f"  read error: {source['read_error']}")
    for s in report["sessions"]:
        lap, pit, cov = s["laps"], s["pit_stops"], s["telemetry_coverage"]
        span = f"{lap['first']}-{lap['last']}" if lap["first"] is not None else "none"
        print(f"Session {s['session_id']} [{s['vehicle_category']}]: laps {lap['unique']}/{lap['records']} ({span}); "
              f"pit in/out {pit['pit_enters']}/{pit['pit_exits']} paired {pit['paired_visits']} "
              f"(pre-race {pit['pre_race_visits_current_lap_le_zero']}, "
              f"in-race {pit['in_race_visits_current_lap_positive']}); "
              f"unmatched pit {pit['pit_enters_without_exit']}/{pit['pit_exits_without_enter']}; "
              f"box in/out {pit['box_enters']}/{pit['box_exits']} cycles {pit['paired_box_cycles']}, "
              f"visits with box {pit['visits_with_box_cycle']}")
        print(f"  Coverage: fuel {_ratio(cov['fuel_any'])}; gaps ahead/behind "
              f"{_ratio(cov['gap_ahead_at_sample_s'])}/{_ratio(cov['gap_behind_at_sample_s'])}; "
              f"tyre wear any/all4 {cov['tyre_wear_any_corner']}/"
              f"{cov['tyre_wear_all_four_corners']}/{lap['records']}; corners "
              f"{','.join(_ratio(cov['tyre_wear_corners'][field]) for field in WEAR)}")
        for duplicate in s["duplicates"]:
            locations = ", ".join(f"{loc['source']}:{loc['line']}" for loc in duplicate["locations"])
            print(f"  Duplicate {duplicate['kind']} {duplicate['key']}: {locations}")
    for limitation in report["limitations"]:
        print(f"Limitation: {limitation}")


def self_check():
    rows = [
        {"record_type": "lap", "session_id": "demo", "completed_lap": 1,
         "fuel_used_l": 2.0, "gap_ahead_at_sample_s": 1.5, **{WEAR[i]: i / 10 for i in range(4)}},
        {"record_type": "lap", "session_id": "demo", "completed_lap": 1},
        {"record_type": "pit_enter", "session_id": "demo", "current_lap": 0, "captured_utc": "2026-01-01T00:00:00+00:00"},
        {"record_type": "pit_box_enter", "session_id": "demo", "captured_utc": "2026-01-01T00:00:01+00:00"},
        {"record_type": "pit_box_exit", "session_id": "demo", "captured_utc": "2026-01-01T00:00:02+00:00"},
        {"record_type": "pit_exit", "session_id": "demo", "current_lap": 0, "captured_utc": "2026-01-01T00:00:03+00:00"},
        {"record_type": "pit_enter", "session_id": "demo", "current_lap": 4, "captured_utc": "2026-01-01T00:10:00+00:00"},
        {"record_type": "pit_exit", "session_id": "demo", "current_lap": 4, "captured_utc": "2026-01-01T00:10:03+00:00"},
    ]
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "sample.jsonl"
        path.write_text("\n".join(json.dumps(row) for row in rows) + "\n{bad json\n", encoding="utf-8")
        report = audit([path])
    s = report["sessions"][0]
    assert report["totals"]["malformed_json"] == 1
    assert s["laps"]["unique"] == 1 and s["laps"]["duplicate_groups"] == 1
    assert s["pit_stops"]["pre_race_visits_current_lap_le_zero"] == 1
    assert s["pit_stops"]["in_race_visits_current_lap_positive"] == 1
    assert s["pit_stops"]["in_race_visits_with_box_cycle"] == 0
    assert s["telemetry_coverage"]["tyre_wear_all_four_corners"] == 1
    assert "labels" not in report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="*", type=Path, help="local RaceEngineer JSONL files")
    parser.add_argument("--format", choices=("human", "json"), default="human")
    parser.add_argument("--self-check", action="store_true")
    args = parser.parse_args(argv)
    if args.self_check:
        self_check()
        print("self-check passed", file=sys.stderr if args.logs else sys.stdout)
    if not args.logs:
        if args.self_check:
            return 0
        parser.error("provide one or more local JSONL files")
    report = audit(args.logs)
    if args.format == "json":
        print(json.dumps(report, ensure_ascii=False, indent=2))
    else:
        print_human(report)
    return int(any(s.get("read_error") or s["malformed_json"] or s["invalid_records"]
                   for s in report["sources"]))


if __name__ == "__main__":
    raise SystemExit(main())
