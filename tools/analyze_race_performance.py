#!/usr/bin/env python3
"""Validate Windows collector CSVs and compare matched AC/ACC captures; stdlib only.

No simulated replay, resource-only run, failed capture or empty frame CSV establishes
game readiness. Reports are descriptive, never an automatic '8 GB/no stutter' pass.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path
from statistics import mean, median


class CaptureError(ValueError):
    pass


def number(value, field, *, integer=False):
    try:
        result = int(value) if integer else float(value)
    except (ValueError, TypeError, OverflowError) as error:
        raise CaptureError(f"Invalid numeric {field}: {value!r}") from error
    if not math.isfinite(result) or result < 0:
        raise CaptureError(f"Expected finite, nonnegative {field}")
    return result


def percentile(values, fraction):
    ordered = sorted(values)
    rank = (len(ordered) - 1) * fraction
    low = math.floor(rank)
    return ordered[low] + (ordered[math.ceil(rank)] - ordered[low]) * (rank - low)


def data_file(root, name):
    if not isinstance(name, str) or not name:
        raise CaptureError("Capture data filename is missing")
    path = (root / name).resolve()
    if path.parent != root.resolve() or not path.is_file():
        raise CaptureError(f"Missing or nonlocal capture data file: {name}")
    return path


def rows(path):
    with path.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        if not reader.fieldnames or len(set(reader.fieldnames)) != len(reader.fieldnames):
            raise CaptureError(f"Missing or duplicate CSV columns in {path.name}")
        result = list(reader)
    if not result or any(None in row or any(value is None for value in row.values()) for row in result):
        raise CaptureError(f"Empty or incomplete CSV: {path.name}")
    return result


def process_resources(samples, prefix, logical_processors):
    times = [number(row["elapsed_s"], "elapsed_s") for row in samples]
    cpu = [number(row[prefix + "_cpu_ms"], prefix + "_cpu_ms") for row in samples]
    if any(b < a for a, b in zip(cpu, cpu[1:])):
        raise CaptureError(f"{prefix} CPU counter regressed (process restart or invalid trace)")
    interval_cpu = [(b - a) / ((end - start) * 10. * logical_processors)
                    for a, b, start, end in zip(cpu, cpu[1:], times, times[1:])]
    result = {
        "cpu_machine_pct_mean": (cpu[-1] - cpu[0]) / ((times[-1] - times[0]) * 10. * logical_processors),
        "cpu_machine_pct_p95_1s": percentile(interval_cpu, .95),
    }
    if prefix != "collector":
        working = [number(row[prefix + "_working_set_bytes"], "working_set") / 1048576. for row in samples]
        private = [number(row[prefix + "_private_commit_bytes"], "private_commit") / 1048576. for row in samples]
        result.update(working_set_peak_mib=max(working), working_set_median_mib=median(working),
                      private_commit_peak_mib=max(private), private_commit_growth_mib=private[-1] - private[0])
        if prefix + "_peak_lifetime_working_set_bytes" in samples[0]:
            result["lifetime_working_set_peak_mib"] = max(number(row[prefix + "_peak_lifetime_working_set_bytes"],
                "lifetime working-set peak") for row in samples) / 1048576.
    elif "collector_working_set_bytes" in samples[0]:
        result["working_set_peak_mib"] = max(number(row["collector_working_set_bytes"], "collector RAM")
                                                for row in samples) / 1048576.
    return result


def frames_summary(path, manifest):
    frame_rows = rows(path)
    columns = frame_rows[0]
    qpc_column = next((name for name in ("QPCTime", "CPUStartQPC") if name in columns), None)
    required = {"ProcessID", "SwapChainAddress", "MsBetweenPresents"}
    if not qpc_column or not required.issubset(columns):
        raise CaptureError("Expected PresentMon v1 metrics with --qpc_time: ProcessID, SwapChainAddress, MsBetweenPresents and QPCTime/CPUStartQPC")
    frequency = number(manifest["clock_frequency"], "clock_frequency", integer=True)
    start = number(manifest["capture_start_qpc"], "capture_start_qpc", integer=True)
    end = number(manifest["capture_end_qpc"], "capture_end_qpc", integer=True)
    if frequency <= 0 or end <= start:
        raise CaptureError("Invalid QPC capture clock")
    expected_pid = number(manifest["game_process"]["pid"], "game PID", integer=True)
    streams = {}
    rejected = {"other_process": 0, "outside_capture": 0, "zero_or_initial_boundary": 0}
    for row in frame_rows:
        if number(row["ProcessID"], "frame PID", integer=True) != expected_pid:
            rejected["other_process"] += 1
            continue
        qpc = number(row[qpc_column], qpc_column, integer=True)
        if not start <= qpc <= end:
            rejected["outside_capture"] += 1
            continue
        interval = number(row["MsBetweenPresents"], "MsBetweenPresents")
        if interval == 0 or (qpc - start + 1) / frequency * 1000. < interval:
            rejected["zero_or_initial_boundary"] += 1
            continue
        key = row["SwapChainAddress"].strip()
        if not key:
            raise CaptureError("Empty swap-chain identity")
        streams.setdefault(key, []).append((qpc, interval))
    if not streams:
        raise CaptureError("No game frames inside the measured capture window")
    key, selected = max(streams.items(), key=lambda pair: len(pair[1]))
    if any(b[0] <= a[0] for a, b in zip(selected, selected[1:])):
        raise CaptureError("Present timestamps are duplicated or not monotonic for the selected swap chain")
    duration = (end - start) / frequency
    if abs(duration - manifest["actual_duration_s"]) > max(1., duration * .05):
        raise CaptureError("QPC frame window does not match the resource capture duration")
    span = (selected[-1][0] - selected[0][0]) / frequency
    if len(selected) < 30 or span < duration * .8:
        raise CaptureError("Insufficient frame coverage: at least 30 frames spanning 80% of capture are required")
    intervals = [entry[1] for entry in selected]
    return {
        "metric": "render_present_interval_ms_not_display_latency",
        "swap_chain": key, "frames": len(intervals), "covered_s": span,
        "other_swap_chain_frames": sum(len(s) for k, s in streams.items() if k != key),
        "rejected_rows": rejected, "mean_ms": mean(intervals),
        "p50_ms": median(intervals), "p95_ms": percentile(intervals, .95),
        "p99_ms": percentile(intervals, .99), "max_ms": max(intervals),
        "mean_fps_from_intervals": 1000. / mean(intervals),
        "p99_interval_reciprocal_fps": 1000. / percentile(intervals, .99),
        "frames_over_33_3ms": sum(v > 1000. / 30 for v in intervals),
        "frames_over_50ms": sum(v > 50 for v in intervals),
        "frames_over_100ms": sum(v > 100 for v in intervals),
    }


def analyze(root, *, allow_resources_only=False):
    root = Path(root)
    try:
        manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8-sig"))
        if manifest.get("schema") != 1 or manifest.get("kind") != "windows_live_capture":
            raise CaptureError("This is not a Windows live-capture manifest")
        resource_only = manifest.get("status") == "resources_only"
        if manifest.get("status") != "complete" and not (resource_only and allow_resources_only):
            raise CaptureError("Capture did not complete with frametimes (resources-only requires explicit opt-in)")
        if manifest["game"] not in ("ac", "acc") or manifest["mode"] not in ("GameOnly", "Normal", "LowResource"):
            raise CaptureError("Unknown game or profile mode")
        if not isinstance(manifest["scenario"], str) or not manifest["scenario"].strip():
            raise CaptureError("Scenario label is required for repeatable comparisons")
        logical = number(manifest["machine"]["logical_processors"], "logical processors", integer=True)
        requested = number(manifest["duration_requested_s"], "requested duration")
        actual = number(manifest["actual_duration_s"], "actual duration")
        if logical < 1 or requested < 30 or actual < requested * .95 or actual > requested * 1.25:
            raise CaptureError("Incomplete or invalid capture duration")
        sample_rows = rows(data_file(root, manifest["files"]["resources"]))
        if len(sample_rows) != manifest["sample_count"] or len(sample_rows) < requested * .8:
            raise CaptureError("Resource sample count is incomplete")
        times = [number(row["elapsed_s"], "elapsed_s") for row in sample_rows]
        if times[0] > 2 or times[-1] < requested * .9 or times[-1] > actual + 1. or any(b <= a for a, b in zip(times, times[1:])):
            raise CaptureError("Resource timestamps do not cover the capture monotonically")
        is_control = manifest["mode"] == "GameOnly"
        if is_control != (manifest["app_process"] is None):
            raise CaptureError("GameOnly/app process identity mismatch")
        if is_control and any(row["app_cpu_ms"].strip() for row in sample_rows):
            raise CaptureError("GameOnly control contains app process samples")
        if not resource_only and (not manifest.get("presentmon") or manifest["presentmon"].get("exit_code") != 0):
            raise CaptureError("PresentMon did not exit successfully")
        installed = number(manifest["machine"]["installed_physical_bytes"], "installed memory", integer=True)
        available = [number(row["system_available_physical_bytes"], "available RAM") / 1048576. for row in sample_rows]
        commit_pct = []
        for row in sample_rows:
            total = number(row["system_total_physical_bytes"], "total RAM")
            limit = number(row["system_commit_limit_bytes"], "commit limit")
            commit = number(row["system_commit_bytes"], "commit")
            if limit <= 0 or commit > limit or number(row["system_available_physical_bytes"], "available RAM") > total:
                raise CaptureError("Impossible system-memory sample")
            commit_pct.append(commit / limit * 100.)
        result = {
            "schema": 1, "kind": "windows_capture_analysis", "capture_data_valid": True,
            "game": manifest["game"], "mode": manifest["mode"], "scenario": manifest["scenario"],
            "manifest": manifest, "duration_s": actual,
            "hardware_in_8gib_class": abs(installed - 8 * 1073741824) <= .25 * 1073741824,
            "app": None if is_control else process_resources(sample_rows, "app", logical),
            "game_process": process_resources(sample_rows, "game", logical),
            "collector": process_resources(sample_rows, "collector", logical),
            "system": {"available_physical_min_mib": min(available), "commit_pct_peak": max(commit_pct),
                       "low_available_ram_below_512mib": min(available) < 512},
            "frametimes": None if resource_only else frames_summary(data_file(root, manifest["files"]["frames"]), manifest),
            "game_readiness_verdict": "not_automatically_assessed",
            "limitations": ["Scenario and active app profile are user-declared.",
                            "Working sets include shared pages: do not sum app/game RAM.",
                            "No GPU VRAM or hard-page-fault measurement; collector/ETW overhead remains.",
                            "Frame percentiles describe present intervals, not display latency or a guaranteed no-stutter result."],
        }
        if resource_only:
            result["limitations"].append("Resource-only capture: game frametimes are unmeasured.")
        return result
    except (KeyError, TypeError, OSError, json.JSONDecodeError, ZeroDivisionError) as error:
        raise CaptureError(f"Invalid capture: {error}") from error


def compare(reference, candidate):
    a, b = reference["manifest"], candidate["manifest"]
    for field in ("game", "scenario", "duration_requested_s", "machine"):
        if a[field] != b[field]:
            raise CaptureError(f"Cannot compare captures with different {field}")
    if a["game_process"]["image_sha256"] != b["game_process"]["image_sha256"]:
        raise CaptureError("Cannot compare different game binaries")
    if abs(reference["duration_s"] - candidate["duration_s"]) > max(1., reference["duration_s"] * .05):
        raise CaptureError("Cannot compare captures with materially different actual durations")
    if reference["frametimes"] is None or candidate["frametimes"] is None:
        raise CaptureError("Frametime comparison requires two complete frame captures")
    differences = {}
    for field in ("mean_ms", "p95_ms", "p99_ms", "max_ms", "frames_over_50ms", "frames_over_100ms"):
        old, new = reference["frametimes"][field], candidate["frametimes"][field]
        differences[field] = {"reference": old, "candidate": new, "delta": new - old,
                              "delta_pct": (new - old) / old * 100. if old else None}
    return {"reference_mode": a["mode"], "candidate_mode": b["mode"], "frametime_differences": differences,
            "interpretation": "Descriptive matched-run comparison; repeat ABBA runs before attributing a difference to the app."}


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("capture", type=Path)
    cli.add_argument("--baseline", type=Path)
    cli.add_argument("--output", type=Path)
    cli.add_argument("--allow-resources-only", action="store_true")
    args = cli.parse_args()
    try:
        result = analyze(args.capture, allow_resources_only=args.allow_resources_only)
        if args.baseline:
            result["comparison"] = compare(analyze(args.baseline), result)
        output = json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False) + "\n"
        if args.output:
            args.output.write_text(output, encoding="utf-8")
        else:
            print(output, end="")
        return 0
    except (CaptureError, OSError) as error:
        cli.exit(2, f"Capture rejected: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
