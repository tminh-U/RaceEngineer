# RaceEngineer Vietnamese Piper corpus specification

## Scope

- Target: 3,900 accepted utterances (3,600 train, 200 validation, 100 locked test).
- Generate 12–15 AGY batches of 250–350 records; validate every batch before the next.
- Vietnamese-first Formula-style team radio, with limited real motorsport code-switching.
- `text` is the runtime-facing form. `spoken_text` is the fully expanded training transcript.
- UTF-8 and Unicode NFC only. `spoken_text` must contain no Arabic digits.

## JSONL schema

```json
{"id":"agy_b01_0001","text":"DRS đã bật, gap còn 1.8 giây.","spoken_text":"Đê e-rờ ét đã bật, khoảng cách còn một phẩy tám giây.","category":"telemetry_report","length_bucket":"short","tags":["drs","decimal","seconds","code_switch"],"template_family":"drs_gap_update"}
```

Required fields: `id`, `text`, `spoken_text`, `category`, `length_bucket`, `tags`, `template_family`.

Allowed categories and target shares:

| Category | Share |
|---|---:|
| critical_warning | 12% |
| engineer_update | 16% |
| strategy_call | 16% |
| telemetry_report | 18% |
| lap_analysis | 12% |
| confirmation_encouragement | 8% |
| pit_instruction | 10% |
| system_session | 8% |

Length buckets use `spoken_text`: very_short 1–4 words (15%), short 5–8 (40%), medium 9–15 (35%), long 16–24 (10%).

## Phonetic coverage

Cover all six tones; `ă â ê ô ơ ư`; common and difficult rimes (`iê`, `ươ`, `uô`, `uyê`, `oă`, `uâ`, `ươi`, `ương`, `iêng`); initials (`b c/k/q ch d đ g/gh gi h kh l m n ng/ngh nh p ph r s t th tr v x`); and finals (`c ch m n ng nh p t`). Prioritize natural contexts containing `tr/ch`, `s/x`, `d/gi/r`, `n/ng/nh`, hỏi/ngã/nặng and checked finals.

## Motorsport terminology and spoken normalization

| Runtime form | Spoken form |
|---|---|
| DRS | đê e-rờ ét |
| ERS | i e-rờ ét |
| ABS | a bê ét |
| TC | tê xê |
| pit / pit lane | pít / làn pít |
| box | vào pít |
| delta | chênh lệch |
| sector | sector |
| stint | lượt chạy |
| understeer / oversteer | thiếu lái / thừa lái |
| tyre / brake / fuel / engine | lốp / phanh / nhiên liệu / động cơ |
| damage / gap / position / lap | hư hại / khoảng cách / vị trí / vòng |
| yellow flag / blue flag | cờ vàng / cờ xanh dương |
| track limits | giới hạn đường đua |
| pit limiter | giới hạn tốc độ pít |

## Numeric coverage

Cover integers, signed decimals, tenths/hundredths/thousandths, P1–P30, lap and sector numbers, seconds, litres, °C, PSI, kPa, percentages and minute/second lap times. Expand every value in `spoken_text`, for example `-0.32` → `âm không phẩy ba hai`, `P12` → `vị trí mười hai`, and `1:42.583` → `một phút bốn mươi hai phẩy năm tám ba giây`.

## Quality constraints

- One clear, speakable radio message; no assistant prose.
- No emoji, Markdown, URL, malformed Vietnamese, gratuitous punctuation or unsafe technical advice.
- No shallow variants that differ only by number, tyre corner, temperature or position.
- Maximum four records per `template_family` per batch and 1.5% in the accepted corpus.
- Every record must add phonetic, terminology, numeric, prosodic or radio-cadence value.
- Batch acceptance: 100% schema/NFC/character validity, no exact duplicates, near duplicates filtered below 3%, category and length targets within five percentage points, and manual audit error below 2%.
