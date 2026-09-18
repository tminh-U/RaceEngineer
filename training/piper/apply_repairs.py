#!/usr/bin/env python3
"""Apply reviewed repair records to a raw AGY batch without altering the raw file."""

import argparse
import json
from pathlib import Path


def read(path: Path) -> list[dict]:
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("batch", type=Path)
    parser.add_argument("repairs", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    batch = read(args.batch)
    repairs = {item["id"]: item for item in read(args.repairs)}
    batch_ids = {item["id"] for item in batch}
    missing = set(repairs) - batch_ids
    if missing:
        raise SystemExit(f"Repair IDs absent from batch: {sorted(missing)}")
    merged = [repairs.get(item["id"], item) for item in batch]
    if len(merged) != len(batch) or len({item["id"] for item in merged}) != len(merged):
        raise SystemExit("Repair changed record count or introduced duplicate IDs")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        "".join(json.dumps(item, ensure_ascii=False, separators=(",", ":")) + "\n" for item in merged),
        encoding="utf-8",
    )
    print(json.dumps({"records": len(merged), "repairs_applied": len(repairs)}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
