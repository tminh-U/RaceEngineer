import datetime
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "training" / "pit_strategy" / "local_training.py"
HAS_XGBOOST = importlib.util.find_spec("numpy") and importlib.util.find_spec("xgboost")


class LocalTrainingCliTests(unittest.TestCase):
    def _lap(self, session_index, lap_number):
        captured = datetime.datetime(2026, 1, 1, tzinfo=datetime.timezone.utc) + datetime.timedelta(
            days=session_index, seconds=lap_number * 100
        )
        return {
            "record_type": "lap",
            "session_id": f"session-{session_index}",
            "captured_utc": captured.isoformat(),
            "simulator": "sim_acc",
            "track": "spa",
            "car_model": "mclaren_720s_gt3_evo",
            "car_category": "gt",
            "car_subclass": "gt3",
            "total_laps": 40,
            "completed_lap": lap_number,
            "lap_time_s": 100.0 + session_index * 0.25 + lap_number * 0.02,
            "lap_excluded": False,
            "in_pit_at_sample": False,
        }

    def _write_logs(self, data_dir):
        data_dir.mkdir()
        rows = [self._lap(session, lap) for session in range(4) for lap in range(1, 36)]
        rows[25 - 1 + 35]["lap_excluded"] = True  # session 1, lap 25
        rows[35 * 2 + 8 - 1]["in_pit_at_sample"] = True  # session 2, lap 8
        rows[35 * 3 + 35 - 1].pop("lap_excluded")  # legacy metadata
        rows.append({"record_type": "pit_enter", "session_id": "session-0", "current_lap": 18})
        current = "".join(json.dumps(row) + "\n" for row in rows)
        (data_dir / "pit_strategy_laps.jsonl").write_text(current, encoding="utf-8")

        safe_duplicate = self._lap(0, 5)
        unsafe_duplicate = self._lap(1, 10)
        unsafe_duplicate["lap_excluded"] = True
        invalid = {
            "record_type": "lap", "session_id": "bad", "simulator": "sim_acc",
            "completed_lap": 1, "lap_time_s": 0,
        }
        (data_dir / "pit_strategy_laps.jsonl.old").write_text(
            json.dumps(safe_duplicate) + "\n" + json.dumps(unsafe_duplicate) + "\n"
            + json.dumps(invalid) + "\n{broken json\n",
            encoding="utf-8",
        )

        archive_dir = data_dir / "recordings"
        archive_dir.mkdir()
        conflict = self._lap(3, 12)
        conflict["lap_time_s"] += 1.5
        (archive_dir / "pit_strategy_laps-20260101-archive.jsonl").write_text(
            json.dumps(conflict) + "\n", encoding="utf-8"
        )

    def _run(self, action, data_dir, output_dir):
        completed = subprocess.run(
            [sys.executable, str(SCRIPT), "--action", action,
             "--data-dir", str(data_dir), "--output-dir", str(output_dir)],
            capture_output=True, text=True, encoding="utf-8", errors="replace", check=False,
        )
        self.assertEqual(completed.returncode, 0, (completed.stdout or "") + (completed.stderr or ""))
        lines = completed.stdout.strip().splitlines()
        self.assertEqual(len(lines), 1, completed.stdout)
        return json.loads(lines[0])

    def test_process_filters_and_publishes_a_snapshot(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            data_dir, output_dir = root / "logs", root / "output"
            self._write_logs(data_dir)
            result = self._run("process", data_dir, output_dir)

            self.assertEqual(result["status"], "success")
            counts = result["counts"]
            self.assertEqual(counts["malformed_json"], 1)
            self.assertEqual(counts["invalid_lap_records"], 1)
            self.assertEqual(counts["duplicate_laps_removed"], 4)
            self.assertEqual(counts["conflicting_duplicate_groups_removed"], 1)
            self.assertEqual(counts["standing_start_laps_excluded"], 4)
            self.assertEqual(counts["legacy_laps_without_exclusion_flag"], 1)

            snapshot = Path(result["processed_dir"])
            pairs = [json.loads(line) for line in (snapshot / "pace_pairs.jsonl").read_text(encoding="utf-8").splitlines()]
            self.assertTrue(pairs)
            self.assertTrue(all(row["current_lap"] >= 2 for row in pairs))
            for row in pairs:
                self.assertAlmostEqual(
                    row["target_delta_s"], row["next_lap_time_s"] - row["current_lap_time_s"]
                )
                if row["session_id"] == "session-0":
                    self.assertTrue({row["current_lap"], row["next_lap"]}.isdisjoint({17, 18, 19}))
                if row["session_id"] == "session-1":
                    self.assertTrue({row["current_lap"], row["next_lap"]}.isdisjoint({10, 25}))
                if row["session_id"] == "session-2":
                    self.assertTrue({row["current_lap"], row["next_lap"]}.isdisjoint({7, 8, 9}))
                if row["session_id"] == "session-3":
                    self.assertTrue({row["current_lap"], row["next_lap"]}.isdisjoint({12}))
            self.assertEqual(json.loads((output_dir / "last_result.json").read_text(encoding="utf-8"))["process_id"], result["process_id"])

    @unittest.skipUnless(HAS_XGBOOST, "numpy and xgboost are optional local training dependencies")
    def test_train_holds_out_sessions_and_saves_research_artifacts(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            data_dir, output_dir = root / "logs", root / "output"
            self._write_logs(data_dir)
            processed = self._run("process", data_dir, output_dir)

            # The saved processed snapshot remains the training source; newly recorded rows are reported.
            with (data_dir / "pit_strategy_laps.jsonl").open("a", encoding="utf-8") as stream:
                stream.write("{}\n")
            trained = self._run("train", data_dir, output_dir)
            self.assertEqual(trained["status"], "success")
            self.assertEqual(trained["source_process_id"], processed["process_id"])
            self.assertTrue(trained["source_snapshot_stale"])
            self.assertFalse(trained["deployment_ready"])

            summary = json.loads(Path(trained["summary_path"]).read_text(encoding="utf-8"))
            model = summary["models"][0]
            self.assertEqual(model["status"], "trained_research_only")
            self.assertGreaterEqual(model["sessions"], 3)
            self.assertGreaterEqual(model["rows"], 60)
            self.assertTrue(set(model["training_sessions"]).isdisjoint(model["validation_sessions"]))
            self.assertTrue((Path(trained["run_dir"]) / model["model_file"]).is_file())
            self.assertFalse(json.loads((output_dir / "last_result.json").read_text(encoding="utf-8"))["deployment_ready"])


if __name__ == "__main__":
    unittest.main()
