# Vietnamese Piper corpus

Current accepted corpus: 1,500 AGY-generated utterances in `accepted/b01.jsonl` through
`accepted/b05.jsonl`. The 64-record `gold.jsonl` is a locked reference set and is
not included in training splits.

## Batch workflow

1. Ask AGY Flash High to read `agy_bulk_prompt.md`, `corpus_spec.md`, `gold.jsonl`
   and every existing file under `accepted/`, then create one raw batch in `batches/`.
2. Validate it against gold and all earlier accepted batches:

   `python validate_corpus.py batches/bNN.jsonl --reference gold.jsonl --reference accepted/b01.jsonl --strict-near-duplicates --out-dir reports/bNN`

3. Audit `reports/bNN/accepted.jsonl` with `make_audit_sample.py`. Put rejected
   records and reasons in a repair JSONL and have AGY regenerate only those IDs.
4. Validate repairs, then apply them with `apply_repairs.py`. Never overwrite the
   raw AGY batch.
5. Rebuild deterministic splits with `assemble_dataset.py` after each accepted batch.

The planned text target is 3,900 accepted utterances. Audio synthesis and Piper
fine-tuning start only after the text corpus passes cumulative validation and audit.
