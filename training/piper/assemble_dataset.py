#!/usr/bin/env python3
"""Assemble validated AGY batches and create leakage-safe corpus splits."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from collections import Counter
from pathlib import Path


def load(paths: list[Path]) -> list[dict]:
    records = []
    for path in paths:
        with path.open("r", encoding="utf-8") as stream:
            records.extend(json.loads(line) for line in stream if line.strip())
    return records


def split_for(family: str, seed: str) -> str:
    value = int(hashlib.sha256(f"{seed}:{family}".encode()).hexdigest()[:8], 16) % 100
    if value < 5:
        return "test"
    if value < 12:
        return "validation"
    return "train"


def write_jsonl(path: Path, records: list[dict]) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        for record in records:
            stream.write(json.dumps(record, ensure_ascii=False, separators=(",", ":")) + "\n")


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser()
    parser.add_argument("inputs", nargs="+", type=Path)
    parser.add_argument("--out-dir", type=Path, default=Path("training/piper/corpus"))
    parser.add_argument("--seed", default="raceengineer-piper-v1")
    args = parser.parse_args()
    records = load(args.inputs)
    ids = [item["id"] for item in records]
    if len(ids) != len(set(ids)):
        raise SystemExit("Duplicate IDs found; validate inputs before assembly")
    records.sort(key=lambda item: item["id"])
    splits = {"train": [], "validation": [], "test": []}
    for record in records:
        splits[split_for(record["template_family"], args.seed)].append(record)
    args.out_dir.mkdir(parents=True, exist_ok=True)
    for name, items in splits.items():
        write_jsonl(args.out_dir / f"{name}.jsonl", items)
    report = {
        "total": len(records),
        "split_counts": {name: len(items) for name, items in splits.items()},
        "category_counts": dict(sorted(Counter(item["category"] for item in records).items())),
        "template_families": len({item["template_family"] for item in records}),
        "split_seed": args.seed,
    }
    (args.out_dir / "assembly_summary.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
