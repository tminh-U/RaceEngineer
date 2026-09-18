#!/usr/bin/env python3
"""Deterministic validation and coverage reporting for Piper corpus JSONL."""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
import unicodedata
from collections import Counter, defaultdict
from difflib import SequenceMatcher
from pathlib import Path


CATEGORIES = {
    "critical_warning",
    "engineer_update",
    "strategy_call",
    "telemetry_report",
    "lap_analysis",
    "confirmation_encouragement",
    "pit_instruction",
    "system_session",
}
BUCKET_RANGES = {
    "very_short": (1, 4),
    "short": (5, 8),
    "medium": (9, 15),
    "long": (16, 24),
}
REQUIRED_FIELDS = {
    "id", "text", "spoken_text", "category", "length_bucket", "tags", "template_family"
}
TERM_PATTERNS = {
    "drs": r"\bdrs\b|đê e-rờ ét",
    "ers": r"\bers\b|i e-rờ ét",
    "pit": r"\bpit\b|\bpít\b|vào pít",
    "box": r"\bbox\b|vào pít",
    "delta": r"\bdelta\b|chênh lệch",
    "sector": r"\bsector\b",
    "stint": r"\bstint\b|lượt chạy",
    "understeer": r"\bundersteer\b|thiếu lái",
    "oversteer": r"\boversteer\b|thừa lái",
    "tyre": r"\btyre(?:s)?\b|\blốp\b",
    "brake": r"\bbrake(?:s)?\b|\bphanh\b",
    "fuel": r"\bfuel\b|nhiên liệu",
    "engine": r"\bengine\b|động cơ",
    "damage": r"\bdamage\b|hư hại",
    "gap": r"\bgap\b|khoảng cách",
    "position": r"\bposition\b|vị trí",
    "lap": r"\blap\b|\bvòng\b",
    "penalty": r"\bpenalty\b|án phạt",
    "yellow_flag": r"yellow flag|cờ vàng",
    "blue_flag": r"blue flag|cờ xanh dương",
    "track_limits": r"track limits|giới hạn đường đua",
    "pit_limiter": r"pit limiter|giới hạn tốc độ pít",
}
UNIT_PATTERNS = {
    "seconds": r"\bgiây\b",
    "litres": r"\blít\b",
    "degrees": r"\bđộ c\b|độ xê",
    "psi": r"\bpsi\b|pi ét ai",
    "kpa": r"\bkpa\b|ki lô pascal",
    "percent": r"phần trăm",
    "positive": r"\bdương\b",
    "negative": r"\bâm\b",
    "decimal": r"\bphẩy\b",
}
VIETNAMESE_SPECIALS = "ăâêôơưđ"
TONE_GROUPS = {
    "ngang": "aăâeêioôơuưy",
    "sac": "áắấéếíóốớúứý",
    "huyen": "àằầèềìòồờùừỳ",
    "hoi": "ảẳẩẻểỉỏổởủửỷ",
    "nga": "ãẵẫẽễĩõỗỡũữỹ",
    "nang": "ạặậẹệịọộợụựỵ",
}
TONELESS_GROUPS = {
    "a": "aáàảãạ", "ă": "ăắằẳẵặ", "â": "âấầẩẫậ", "e": "eéèẻẽẹ",
    "ê": "êếềểễệ", "i": "iíìỉĩị", "o": "oóòỏõọ", "ô": "ôốồổỗộ",
    "ơ": "ơớờởỡợ", "u": "uúùủũụ", "ư": "ưứừửữự", "y": "yýỳỷỹỵ",
}
TONELESS_TRANSLATION = str.maketrans({char: base for base, chars in TONELESS_GROUPS.items() for char in chars})
RIMES = ("iê", "ươ", "uô", "uyê", "oă", "uâ", "ươi", "ương", "iêng")
FINALS = ("ch", "nh", "ng", "c", "m", "n", "p", "t")
DISALLOWED_MARKUP = re.compile(r"(?:https?://|www\.|```|[#*_]{2,}|<[^>]+>)", re.I)
ASCII_DIGIT = re.compile(r"[0-9]")
CONTROL_OR_SURROGATE = re.compile(r"[\u0000-\u0008\u000b\u000c\u000e-\u001f\ud800-\udfff]")
EMOJI = re.compile("[\U0001F000-\U0001FAFF\U00002700-\U000027BF]")
ALLOWED_PUNCT = set(".,?!;:…-'–—()/%+°")


def words(text: str) -> list[str]:
    return re.findall(r"[\wÀ-ỹ]+(?:[-'][\wÀ-ỹ]+)?", text, re.UNICODE)


def normalized_key(text: str) -> str:
    text = unicodedata.normalize("NFC", text).casefold()
    return " ".join(re.findall(r"[\wÀ-ỹ]+", text, re.UNICODE))


def toneless(text: str) -> str:
    return text.casefold().translate(TONELESS_TRANSLATION)


def trigram_set(text: str) -> set[str]:
    value = f"  {normalized_key(text)}  "
    return {value[i:i + 3] for i in range(max(0, len(value) - 2))}


def jaccard(left: set[str], right: set[str]) -> float:
    union = left | right
    return len(left & right) / len(union) if union else 1.0


def invalid_chars(text: str) -> list[str]:
    invalid = []
    for char in text:
        category = unicodedata.category(char)
        if char.isspace() or char.isalnum() or char in ALLOWED_PUNCT:
            continue
        if category.startswith("M"):
            continue
        invalid.append(char)
    return sorted(set(invalid))


def record_errors(record: dict, seen_ids: set[str]) -> list[str]:
    errors: list[str] = []
    missing = sorted(REQUIRED_FIELDS - set(record))
    if missing:
        return ["missing_fields:" + ",".join(missing)]
    if not isinstance(record["id"], str) or not record["id"].strip():
        errors.append("invalid_id")
    elif record["id"] in seen_ids:
        errors.append("duplicate_id")
    if record["category"] not in CATEGORIES:
        errors.append("invalid_category")
    if record["length_bucket"] not in BUCKET_RANGES:
        errors.append("invalid_length_bucket")
    if not isinstance(record["tags"], list) or not record["tags"] or not all(
        isinstance(tag, str) and tag.strip() for tag in record["tags"]
    ):
        errors.append("invalid_tags")
    if not isinstance(record["template_family"], str) or not record["template_family"].strip():
        errors.append("invalid_template_family")
    for field in ("text", "spoken_text"):
        value = record[field]
        if not isinstance(value, str) or not value.strip():
            errors.append(f"invalid_{field}")
            continue
        if value != unicodedata.normalize("NFC", value):
            errors.append(f"not_nfc:{field}")
        if CONTROL_OR_SURROGATE.search(value):
            errors.append(f"control_character:{field}")
        if EMOJI.search(value):
            errors.append(f"emoji:{field}")
        if DISALLOWED_MARKUP.search(value):
            errors.append(f"markup_or_url:{field}")
        bad = invalid_chars(value)
        if bad:
            errors.append(f"invalid_chars:{field}:{''.join(bad)}")
    spoken = record.get("spoken_text", "")
    if isinstance(spoken, str):
        if ASCII_DIGIT.search(spoken):
            errors.append("spoken_text_contains_digit")
        count = len(words(spoken))
        if count > 24:
            errors.append("too_long")
        bucket = record.get("length_bucket")
        if bucket in BUCKET_RANGES:
            low, high = BUCKET_RANGES[bucket]
            if not low <= count <= high:
                errors.append(f"bucket_mismatch:{count}")
    return errors


def load_jsonl(paths: list[Path]) -> tuple[list[dict], list[dict]]:
    records: list[dict] = []
    malformed: list[dict] = []
    for path in paths:
        with path.open("r", encoding="utf-8") as stream:
            for line_number, line in enumerate(stream, 1):
                if not line.strip():
                    continue
                try:
                    record = json.loads(line)
                    if not isinstance(record, dict):
                        raise ValueError("JSON value is not an object")
                    record["_source"] = str(path)
                    record["_line"] = line_number
                    records.append(record)
                except (json.JSONDecodeError, ValueError) as exc:
                    malformed.append({
                        "source": str(path), "line": line_number, "error": str(exc), "raw": line.rstrip()
                    })
    return records, malformed


def find_near_duplicates(records: list[dict], accepted_indices: set[int]) -> dict[int, list[str]]:
    issues: dict[int, list[str]] = defaultdict(list)
    buckets: dict[tuple[str, int], list[int]] = defaultdict(list)
    grams = {}
    for index in accepted_indices:
        text = records[index]["spoken_text"]
        token_count = len(words(text))
        grams[index] = trigram_set(text)
        buckets[(records[index]["category"], token_count // 3)].append(index)
    compared: set[tuple[int, int]] = set()
    for index in sorted(accepted_indices):
        token_count = len(words(records[index]["spoken_text"]))
        category = records[index]["category"]
        candidates = []
        for size_key in range(max(0, token_count // 3 - 1), token_count // 3 + 2):
            candidates.extend(buckets.get((category, size_key), []))
        for other in candidates:
            if other >= index or (other, index) in compared:
                continue
            compared.add((other, index))
            a = records[other]["spoken_text"]
            b = records[index]["spoken_text"]
            jac = jaccard(grams[other], grams[index])
            ratio = SequenceMatcher(None, normalized_key(a), normalized_key(b), autojunk=False).ratio()
            if jac >= 0.88 or ratio >= 0.92:
                issues[index].append(
                    f"near_duplicate:{records[other]['id']}:jaccard={jac:.3f}:ratio={ratio:.3f}"
                )
    return issues


def coverage(records: list[dict]) -> dict:
    spoken = " ".join(str(record["spoken_text"]).casefold() for record in records)
    runtime = " ".join(str(record["text"]).casefold() for record in records)
    all_text = runtime + " " + spoken
    category_counts = Counter(record["category"] for record in records)
    length_counts = Counter(record["length_bucket"] for record in records)
    template_counts = Counter(record["template_family"] for record in records)
    tone_counts = {name: sum(spoken.count(char) for char in chars) for name, chars in TONE_GROUPS.items()}
    special_counts = {char: spoken.count(char) for char in VIETNAMESE_SPECIALS}
    rime_text = toneless(spoken)
    rime_counts = {rime: rime_text.count(rime) for rime in RIMES}
    final_counts = {
        final: sum(1 for word in words(spoken) if word.casefold().endswith(final)) for final in FINALS
    }
    term_counts = {name: len(re.findall(pattern, all_text, re.I)) for name, pattern in TERM_PATTERNS.items()}
    unit_counts = {name: len(re.findall(pattern, spoken, re.I)) for name, pattern in UNIT_PATTERNS.items()}
    gaps = {
        "tones": [name for name, value in tone_counts.items() if value == 0],
        "vietnamese_specials": [name for name, value in special_counts.items() if value == 0],
        "rimes": [name for name, value in rime_counts.items() if value == 0],
        "finals": [name for name, value in final_counts.items() if value == 0],
        "racing_terms": [name for name, value in term_counts.items() if value == 0],
        "number_units": [name for name, value in unit_counts.items() if value == 0],
        "categories": [name for name in sorted(CATEGORIES) if category_counts[name] == 0],
        "length_buckets": [name for name in BUCKET_RANGES if length_counts[name] == 0],
    }
    return {
        "accepted_count": len(records),
        "category_counts": dict(sorted(category_counts.items())),
        "length_counts": dict(sorted(length_counts.items())),
        "template_family_unique": len(template_counts),
        "template_family_max_count": max(template_counts.values(), default=0),
        "tone_counts": tone_counts,
        "vietnamese_special_counts": special_counts,
        "rime_counts": rime_counts,
        "final_counts": final_counts,
        "term_counts": term_counts,
        "unit_counts": unit_counts,
        "coverage_gaps": gaps,
    }


def write_jsonl(path: Path, records: list[dict]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        for record in records:
            stream.write(json.dumps(record, ensure_ascii=False, separators=(",", ":")) + "\n")


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser()
    parser.add_argument("inputs", nargs="+", type=Path)
    parser.add_argument("--reference", action="append", default=[], type=Path,
                        help="Existing accepted JSONL used for cross-batch duplicate checks")
    parser.add_argument("--out-dir", type=Path, default=Path("training/piper/reports/latest"))
    parser.add_argument("--strict-near-duplicates", action="store_true",
                        help="Reject near duplicates instead of only flagging them")
    args = parser.parse_args()

    new_records, malformed = load_jsonl(args.inputs)
    reference_records, reference_malformed = load_jsonl(args.reference)
    records = reference_records + new_records
    reference_count = len(reference_records)
    rejection_reasons: dict[int, list[str]] = defaultdict(list)
    seen_ids: set[str] = set()
    exact_owner: dict[str, int] = {}

    for index, record in enumerate(records):
        clean = {key: value for key, value in record.items() if not key.startswith("_")}
        rejection_reasons[index].extend(record_errors(clean, seen_ids))
        if isinstance(record.get("id"), str):
            seen_ids.add(record["id"])
        if isinstance(record.get("spoken_text"), str):
            key = normalized_key(record["spoken_text"])
            if key in exact_owner:
                rejection_reasons[index].append(f"exact_duplicate:{records[exact_owner[key]].get('id', '?')}")
            else:
                exact_owner[key] = index

    structurally_valid = {i for i in range(len(records)) if not rejection_reasons[i]}
    near_issues = find_near_duplicates(records, structurally_valid)
    suspicious = []
    for index, reasons in near_issues.items():
        suspicious.append({"id": records[index]["id"], "reasons": reasons})
        if args.strict_near_duplicates:
            rejection_reasons[index].extend(reasons)

    accepted = []
    rejected = []
    for index in range(reference_count, len(records)):
        clean = {key: value for key, value in records[index].items() if not key.startswith("_")}
        reasons = rejection_reasons[index]
        if reasons:
            rejected.append({"record": clean, "rejection_reasons": sorted(set(reasons))})
        else:
            accepted.append(clean)

    args.out_dir.mkdir(parents=True, exist_ok=True)
    write_jsonl(args.out_dir / "accepted.jsonl", accepted)
    write_jsonl(args.out_dir / "rejected.jsonl", rejected)
    report = coverage(accepted)
    report.update({
        "input_count": len(new_records),
        "reference_count": reference_count,
        "rejected_count": len(rejected),
        "malformed_count": len(malformed),
        "reference_malformed_count": len(reference_malformed),
        "suspicious_near_duplicate_count": len(suspicious),
        "rejection_reason_counts": dict(sorted(Counter(
            reason.split(":", 1)[0] for item in rejected for reason in item["rejection_reasons"]
        ).items())),
    })
    (args.out_dir / "summary.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    (args.out_dir / "suspicious.json").write_text(
        json.dumps(suspicious, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    (args.out_dir / "malformed.json").write_text(
        json.dumps(malformed, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 1 if malformed or rejected else 0


if __name__ == "__main__":
    raise SystemExit(main())
