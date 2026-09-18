#!/usr/bin/env python3
"""Select a stable random audit sample plus records likely to need human review."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--rate", type=float, default=0.05)
    parser.add_argument("--seed", default="raceengineer-audit-v1")
    args = parser.parse_args()
    records = [json.loads(line) for line in args.input.read_text(encoding="utf-8").splitlines() if line.strip()]
    selected = {}
    threshold = int(max(0.0, min(1.0, args.rate)) * 10_000)
    for record in records:
        digest = int(hashlib.sha256(f"{args.seed}:{record['id']}".encode()).hexdigest()[:8], 16) % 10_000
        reasons = []
        if digest < threshold:
            reasons.append("random_sample")
        word_count = len(re.findall(r"\w+", record["spoken_text"], re.UNICODE))
        if word_count >= 23:
            reasons.append("very_long")
        english_terms = re.findall(
            r"\b(?:DRS|ERS|MGU-K|ABS|TC|pit|box|delta|sector|stint|understeer|oversteer|tyre|brake|fuel|engine|damage|gap|position|lap|penalty|podium|map|mode|restart|push)\b",
            record["text"], re.I,
        )
        if len(english_terms) >= 3:
            reasons.append("heavy_code_switch")
        if len(record.get("tags", [])) >= 7:
            reasons.append("many_tags")
        if reasons:
            selected[record["id"]] = {"record": record, "audit_reasons": reasons}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        "".join(json.dumps(item, ensure_ascii=False) + "\n" for item in selected.values()), encoding="utf-8"
    )
    print(json.dumps({"input": len(records), "selected": len(selected)}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
