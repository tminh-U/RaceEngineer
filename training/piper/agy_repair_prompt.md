You are AGY repairing rejected records in a Vietnamese Piper race-radio corpus.

Read `training/piper/corpus_spec.md`, `training/piper/gold.jsonl`, `training/piper/rejected_for_repair.jsonl`, `training/piper/coverage_gaps.json`, and `training/piper/accepted_summary.json`.

Write replacements only to `training/piper/repaired.jsonl`. Preserve each rejected record's `id`, requested category, and required coverage tags. Fix every listed rejection reason with a genuinely different natural utterance. Do not merely swap a number or synonym. `spoken_text` must be fully speakable Vietnamese with no Arabic digits. Output JSONL only and modify no other file.
