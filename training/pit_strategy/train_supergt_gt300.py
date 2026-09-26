#!/usr/bin/env python3
"""Extract a research-only GT3 lap dataset and descriptive pace baseline."""

import argparse
import csv
from collections import Counter, defaultdict
import hashlib
import json
import math
from pathlib import Path
import re
import statistics
from urllib.request import urlopen

from pypdf import PdfReader

from vehicle_category import vehicle_category


ROOT = Path(__file__).resolve().parents[2]
OUT_DIR = ROOT / "training/pit_strategy/.local_train/supergt_gt300"
# Vehicle numbers come from the official event GT300 entry lists. The lap-chart
# Type labels omit "GT3" for several manufacturers, so type-string matching is incomplete.
EVENTS = {
    2024: {
        "event_id": "super_gt_2024_r05_suzuka_300km",
        "pdf_url": "https://www.suzukacircuit.jp/result_s/2024/supergt/1208_gt_laptimechart.pdf",
        "entry_url": "https://supergt.net/en/teams_and_drivers?gt_class=gt300&round_id=143&series=2024",
        "gt300_rank_total": 27,
        "gt300_cars": {
            "2", "4", "5", "6", "7", "9", "11", "18", "20", "22", "25", "30", "31", "45",
            "48", "50", "52", "56", "60", "61", "62", "65", "87", "88", "96", "360", "777",
        },
        "gt3_cars": {
            "4", "6", "7", "9", "18", "22", "45", "48", "50", "56", "62", "65", "87", "88",
            "96", "360", "777",
        },
        "pdf_name": "1208_gt_laptimechart_2024.pdf",
        "csv_name": "supergt_suzuka2024_gt300_gt3_laps.csv",
        "report_name": "dataset_audit_2024.json",
    },
    2025: {
        "event_id": "super_gt_2025_r05_suzuka_300km",
        "pdf_url": "https://www.suzukacircuit.jp/result_s/2025/supergt/0824_gt_laptimechart.pdf",
        "entry_url": "https://supergt.net/en/teams_and_drivers?gt_class=gt300&round=Round5&series=2025",
        "gt300_rank_total": 28,
        "gt300_cars": {
            "0", "2", "4", "5", "6", "7", "9", "11", "18", "20", "22", "25", "26", "30",
            "31", "45", "48", "52", "56", "60", "61", "62", "65", "87", "96", "360", "666", "777",
        },
        "gt3_cars": {
            "0", "4", "6", "7", "9", "18", "22", "26", "45", "48", "56", "62", "65", "87",
            "96", "360", "666", "777",
        },
        "pdf_name": "0824_gt_laptimechart.pdf",
        "csv_name": "supergt_suzuka2025_gt300_gt3_laps.csv",
        "report_name": "dataset_audit_2025.json",
    },
}

MAIN_CAR_RE = re.compile(r"^No\s+(\S+)\s+Best Time\b")
DRIVER_RE = re.compile(r"^No\s+(\S+)\s+\(Driver([12])\)\s+Best Time\b")
TYPE_RE = re.compile(r"^Type\s+(.*?)\s+Today's Rank\s+(\d+)\s*/\s*(\d+)")
TEAM_RE = re.compile(r"^Team\s+(.*?)\s+Average Lap Time\b")
LAP_RE = re.compile(r"^(\d+)\.\s+([\d:']+\.\d{3})(?:\s+(Pit))?$")
TIME_RE = re.compile(r"^(?:\d+:)?\d+'\d{2}\.\d{3}$|^\d+\.\d{3}$")
FIELDS = (
    "event_id", "class", "vehicle_category", "car", "team", "car_type", "driver", "driver_number",
    "lap", "lap_time_s", "lap_time_source", "sector1_s", "sector2_s", "sector3_s",
    "sector4_s", "max_speed_kmh", "passing_time_s", "pit_after", "stint",
    "follows_pit_marker", "pace_candidate",
)


def seconds(token):
    if ":" in token:
        hours, rest = token.split(":", 1)
        minutes, sec = rest.split("'", 1)
        return int(hours) * 3600 + int(minutes) * 60 + float(sec)
    if "'" in token:
        minutes, sec = token.split("'", 1)
        return int(minutes) * 60 + float(sec)
    return float(token)


def sector_row(line):
    parts = line.strip().split()
    if parts and parts[0] == "B":
        parts = parts[1:]
    if len(parts) not in (6, 8) or parts[-1] not in ("1", "2"):
        return None
    sector_tokens = parts[-6:-2]
    if not all(TIME_RE.fullmatch(token) for token in sector_tokens):
        return None
    try:
        speed = float(parts[-2])
    except ValueError:
        return None
    lap_time = next((token for token in parts[:-6] if TIME_RE.fullmatch(token)), None)
    return {
        "driver_number": int(parts[-1]),
        "max_speed_kmh": speed,
        "sectors": [seconds(token) for token in sector_tokens],
        "lap_time_s": seconds(lap_time) if lap_time else None,
        "lap_time_source": "reported" if lap_time else "sector_sum",
    }


def parse_pdf(path):
    reader = PdfReader(str(path))
    cars, rows = {}, []
    active_car = active_driver_slot = None
    pending = None
    pages_without_car_header = 0

    for page in reader.pages:
        text = page.extract_text() or ""
        if not any(MAIN_CAR_RE.match(line.strip()) or DRIVER_RE.match(line.strip())
                   for line in text.splitlines()):
            pages_without_car_header += 1
        for raw in text.splitlines():
            line = raw.strip()
            match = DRIVER_RE.match(line)
            if match:
                active_car, active_driver_slot = match.group(1), int(match.group(2))
                cars.setdefault(active_car, {"drivers": {}})
                continue
            match = MAIN_CAR_RE.match(line)
            if match:
                active_car, active_driver_slot = match.group(1), 0
                cars.setdefault(active_car, {"drivers": {}})
                pending = None
                continue
            if not active_car:
                continue

            if line.startswith("Name ") and active_driver_slot in (1, 2):
                cars[active_car]["drivers"][active_driver_slot] = line[5:].split("  Total Time", 1)[0].strip()
            team = TEAM_RE.match(line)
            if team and active_driver_slot == 0:
                cars[active_car]["team"] = team.group(1).strip()
            car_type = TYPE_RE.match(line)
            if car_type and active_driver_slot == 0:
                cars[active_car]["car_type"] = car_type.group(1).strip()
                cars[active_car]["class_rank"] = int(car_type.group(2))
                cars[active_car]["class_entries"] = int(car_type.group(3))

            timing = sector_row(line)
            if timing:
                pending = timing
                continue
            lap = LAP_RE.match(line)
            if lap and pending:
                lap_number = int(lap.group(1))
                lap_time = pending["lap_time_s"]
                rows.append({
                    "car": active_car,
                    "lap": lap_number,
                    "lap_time_s": lap_time if lap_time is not None else round(sum(pending["sectors"]), 3),
                    "lap_time_source": pending["lap_time_source"],
                    "sector1_s": pending["sectors"][0],
                    "sector2_s": pending["sectors"][1],
                    "sector3_s": pending["sectors"][2],
                    "sector4_s": pending["sectors"][3],
                    "max_speed_kmh": pending["max_speed_kmh"],
                    "driver_number": pending["driver_number"],
                    "passing_time_s": seconds(lap.group(2)),
                    "pit_after": lap.group(3) == "Pit",
                })
                pending = None

    return reader, cars, rows, pages_without_car_header


def select_gt3(cars, event):
    # The lap-chart Type field omits "GT3" for several actual FIA GT3 models.
    return {
        car for car, meta in cars.items()
        if car in event["gt3_cars"] and meta.get("class_entries") == event["gt300_rank_total"]
    }


def normalize(rows, cars, gt3_cars, event_id):
    by_car = defaultdict(list)
    for row in rows:
        if row["car"] in gt3_cars:
            by_car[row["car"]].append(row)

    normalized = []
    for car, laps in sorted(by_car.items(), key=lambda item: int(item[0])):
        meta = cars[car]
        laps.sort(key=lambda row: row["lap"])
        stint, follows_pit = 1, False
        for row in laps:
            row["stint"] = stint
            row["follows_pit_marker"] = follows_pit
            row["pace_candidate"] = (
                row["lap"] > 1 and not follows_pit and not row["pit_after"]
                and 100.0 <= row["lap_time_s"] <= 180.0
                and row["max_speed_kmh"] >= 200.0
            )
            row.update({
                "event_id": event_id,
                "class": "GT300-GT3",
                "vehicle_category": vehicle_category(class_label="GT300-GT3"),
                "team": meta.get("team", ""),
                "car_type": meta.get("car_type", ""),
                "driver": meta.get("drivers", {}).get(row["driver_number"], ""),
            })
            normalized.append(row)
            follows_pit = row["pit_after"]
            if row["pit_after"]:
                stint += 1
    return normalized


def summarize(rows, all_rows, cars, gt3_cars, pages, pages_without_header, event):
    per_car = []
    for car in sorted(gt3_cars, key=int):
        laps = [row for row in rows if row["car"] == car]
        candidates = [row for row in laps if row["pace_candidate"]]
        per_car.append({
            "car": car,
            "team": cars[car].get("team"),
            "car_type": cars[car].get("car_type"),
            "laps": len(laps),
            "lap_span": [min((r["lap"] for r in laps), default=None), max((r["lap"] for r in laps), default=None)],
            "stints": len({r["stint"] for r in laps}),
            "drivers": sorted({r["driver"] for r in laps if r["driver"]}),
            "pit_markers": sum(r["pit_after"] for r in laps),
            "pace_candidates": len(candidates),
        })

    pairs, prior = [], {}
    for row in sorted(rows, key=lambda item: (int(item["car"]), item["lap"])):
        old = prior.get(row["car"])
        if (old and old["pace_candidate"] and row["pace_candidate"]
                and row["lap"] == old["lap"] + 1):
            pairs.append((old["lap_time_s"], row["lap_time_s"]))
        prior[row["car"]] = row
    errors = [abs(predicted - actual) for predicted, actual in pairs]
    denominator_counts = Counter(str(meta.get("class_entries", "unknown")) for meta in cars.values())
    sector_deltas = [abs(row["lap_time_s"] - sum(row[f"sector{i}_s"] for i in range(1, 5)))
                     for row in rows if row["lap_time_source"] == "reported"]
    rank_total = event["gt300_rank_total"]
    gt300_headers = sum(meta.get("class_entries") == rank_total for meta in cars.values())
    return {
        "report": "supergt_gt300_gt3_descriptive_v1",
        "vehicle_category": "gt",
        "event_id": event["event_id"],
        "sources": {"lap_chart_pdf": event["pdf_url"], "official_gt300_entry_list": event["entry_url"]},
        "pages": pages,
        "pages_without_car_header": pages_without_header,
        "car_pages": len(cars),
        "class_rank_denominators": dict(sorted(denominator_counts.items())),
        "gt300_car_headers": gt300_headers,
        "gt300_non_gt3_car_headers": gt300_headers - len(gt3_cars),
        "official_gt300_entry_cars": len(event["gt300_cars"]),
        "official_gt300_cars_without_lap_chart": sorted(event["gt300_cars"] - cars.keys(), key=int),
        "official_gt3_car_numbers": sorted(event["gt3_cars"], key=int),
        "official_gt3_cars_without_lap_chart": sorted(event["gt3_cars"] - cars.keys(), key=int),
        "gt300_gt3_cars": len(gt3_cars),
        "gt300_gt3_car_numbers": sorted(gt3_cars, key=int),
        "all_car_lap_rows": len(all_rows),
        "lap_rows": len(rows),
        "reported_lap_time_rows": sum(r["lap_time_source"] == "reported" for r in rows),
        "sector_sum_lap_time_rows": sum(r["lap_time_source"] == "sector_sum" for r in rows),
        "rows_with_four_sectors": sum(all(math.isfinite(r[f"sector{i}_s"]) for i in range(1, 5)) for r in rows),
        "reported_lap_time_sector_sum_max_abs_delta_s": max(sector_deltas, default=None),
        "pit_markers": sum(r["pit_after"] for r in rows),
        "stint_groups": sum(item["stints"] for item in per_car),
        "duplicate_car_laps": sum(count > 1 for count in Counter((r["car"], r["lap"]) for r in rows).values()),
        "pace_candidate_rule": "after race lap 1; 100-180 s; max speed >=200 km/h; no pit marker and not immediately after a pit marker; heuristic, not verified clean laps",
        "pace_candidate_laps": sum(r["pace_candidate"] for r in rows),
        "previous_lap_baseline": {
            "evaluation_pairs": len(pairs),
            "mae_s": statistics.mean(errors) if errors else None,
            "median_absolute_error_s": statistics.median(errors) if errors else None,
            "scope": "descriptive adjacent-lap pairs where both measured laps pass the heuristic; not a held-out or runtime prediction test",
        },
        "per_car": per_car,
        "limitations": [
            "One Suzuka race event per year; no multi-track/series generalization or deployment claim.",
            "The report provides a descriptive previous-lap baseline, not a fitted pace model.",
            "Pit markers indicate the chart's Pit annotation; service type or completion is unknown.",
            "The chart has no reliable safety-car, caution, or traffic flag to label these effects.",
            "The source exposes no tyre wear or compound data; pace candidates are not confirmed clean laps.",
            "No open-data license for the official timing PDF was verified; retain the downloaded PDF locally.",
        ],
    }


def write_outputs(rows, report, csv_path, report_path):
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    with csv_path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=FIELDS, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)
    report_path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--year", type=int, choices=sorted(EVENTS), default=2025)
    parser.add_argument("--pdf", type=Path, help="local official lap-chart PDF")
    args = parser.parse_args(argv)
    event = EVENTS[args.year]
    pdf_path = args.pdf or OUT_DIR / event["pdf_name"]
    csv_path = OUT_DIR / event["csv_name"]
    report_path = OUT_DIR / event["report_name"]
    pdf_path.parent.mkdir(parents=True, exist_ok=True)
    if not pdf_path.exists():
        with urlopen(event["pdf_url"], timeout=60) as response, pdf_path.open("wb") as stream:
            stream.write(response.read())

    pdf, cars, all_rows, pages_without_header = parse_pdf(pdf_path)
    gt3_cars = select_gt3(cars, event)
    rows = normalize(all_rows, cars, gt3_cars, event["event_id"])
    if not rows:
        parser.error("PDF parsing found no GT300 GT3 laps; inspect the source layout before using results")
    report = summarize(rows, all_rows, cars, gt3_cars, len(pdf.pages), pages_without_header, event)
    report["sources"]["lap_chart_pdf_sha256"] = hashlib.sha256(pdf_path.read_bytes()).hexdigest()
    write_outputs(rows, report, csv_path, report_path)
    print(json.dumps({key: report[key] for key in (
        "pages", "car_pages", "class_rank_denominators", "gt300_car_headers",
        "gt300_gt3_cars", "gt300_gt3_car_numbers", "lap_rows", "reported_lap_time_rows",
        "sector_sum_lap_time_rows", "pit_markers", "stint_groups", "pace_candidate_laps",
        "previous_lap_baseline",
    )}, indent=2))
    print(f"CSV: {csv_path}")
    print(f"Audit: {report_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
