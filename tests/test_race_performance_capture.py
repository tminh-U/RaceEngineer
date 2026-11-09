"""Synthetic input validates the analyzer, never claims a real Windows/game capture."""
import csv
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("race_performance", ROOT / "tools/analyze_race_performance.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class CaptureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.manifest = {
            "schema": 1, "kind": "windows_live_capture", "status": "complete",
            "game": "ac", "mode": "LowResource", "scenario": "synthetic-unit-fixture",
            "machine": {"logical_processors": 4, "installed_physical_bytes": 8 * 1073741824},
            "duration_requested_s": 30, "actual_duration_s": 30., "sample_count": 31,
            "clock_frequency": 1_000_000, "capture_start_qpc": 1_000_000, "capture_end_qpc": 31_000_000,
            "game_process": {"pid": 123, "image_sha256": "unit-test-game-hash"},
            "app_process": {"pid": 456}, "presentmon": {"exit_code": 0},
            "files": {"resources": "resources.csv", "frames": "frames.csv"},
        }
        self.samples = [{
            "elapsed_s": str(i), "app_cpu_ms": str(100 + 20 * i),
            "app_working_set_bytes": str(512 * 1048576), "app_private_commit_bytes": str(768 * 1048576),
            "game_cpu_ms": str(200 + 1000 * i), "game_working_set_bytes": str(4096 * 1048576),
            "game_private_commit_bytes": str(6000 * 1048576), "collector_cpu_ms": str(20 + 2 * i),
            "system_available_physical_bytes": str(1024 * 1048576),
            "system_total_physical_bytes": str(8 * 1073741824),
            "system_commit_bytes": str(8 * 1073741824), "system_commit_limit_bytes": str(16 * 1073741824),
        } for i in range(31)]
        self.frames = [{"ProcessID": "123", "SwapChainAddress": "0xprimary",
                        "QPCTime": str(1_000_000 + int(i * 1_000_000 / 60)),
                        "MsBetweenPresents": str(1000. / 60)} for i in range(1, 1801)]

    def write(self):
        (self.root / "manifest.json").write_text(json.dumps(self.manifest), encoding="utf-8")
        for filename, data in (("resources.csv", self.samples), ("frames.csv", self.frames)):
            with (self.root / filename).open("w", encoding="utf-8-sig", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=list(data[0]))
                writer.writeheader(); writer.writerows(data)

    def analyze(self, **options):
        self.write()
        return module.analyze(self.root, **options)

    def test_units_cpu_and_frame_metrics(self):
        result = self.analyze()
        self.assertAlmostEqual(result["app"]["cpu_machine_pct_mean"], .5)
        self.assertEqual(result["app"]["working_set_peak_mib"], 512)
        self.assertEqual(result["system"]["commit_pct_peak"], 50)
        self.assertAlmostEqual(result["frametimes"]["mean_fps_from_intervals"], 60)
        self.assertTrue(result["hardware_in_8gib_class"])
        self.assertEqual(result["game_readiness_verdict"], "not_automatically_assessed")

    def test_acc_also_supported(self):
        self.manifest["game"] = "acc"
        self.assertEqual(self.analyze()["game"], "acc")

    def test_stutters_are_retained_not_filtered_as_outliers(self):
        self.frames[600]["MsBetweenPresents"] = "150"
        result = self.analyze()["frametimes"]
        self.assertEqual(result["max_ms"], 150)
        self.assertEqual(result["frames_over_100ms"], 1)

    def test_never_merge_swapchains_or_other_processes(self):
        self.frames.extend([{**row, "SwapChainAddress": "0xoverlay"} for row in self.frames[:300]])
        self.frames.extend([{**row, "ProcessID": "999"} for row in self.frames[:200]])
        frames = self.analyze()["frametimes"]
        self.assertLessEqual(frames["frames"], 1800)
        self.assertEqual(frames["other_swap_chain_frames"], 300)
        self.assertEqual(frames["rejected_rows"]["other_process"], 200)

    def test_failed_or_simulated_run_rejected(self):
        for field, value in (("status", "failed"), ("kind", "synthetic_native_replay_not_game")):
            original = self.manifest[field]; self.manifest[field] = value
            with self.assertRaises(module.CaptureError): self.analyze()
            self.manifest[field] = original

    def test_missing_or_wrong_game_frames_rejected(self):
        for row in self.frames: row["ProcessID"] = "999"
        with self.assertRaises(module.CaptureError): self.analyze()

    def test_partial_frame_coverage_rejected(self):
        self.frames = self.frames[:300]
        with self.assertRaises(module.CaptureError): self.analyze()

    def test_stale_qpc_window_rejected(self):
        for row in self.frames: row["QPCTime"] = str(int(row["QPCTime"]) + 100_000_000)
        with self.assertRaises(module.CaptureError): self.analyze()

    def test_wrong_units_unknown_frame_schema_rejected(self):
        self.frames = [{"ProcessID": "123", "SwapChainAddress": "x", "TimeInSeconds": "1", "FrameTime": "16"}]
        with self.assertRaises(module.CaptureError): self.analyze()

    def test_resources_only_requires_explicit_opt_in_and_has_no_frametime_verdict(self):
        self.manifest["status"] = "resources_only"
        with self.assertRaises(module.CaptureError): self.analyze()
        result = self.analyze(allow_resources_only=True)
        self.assertIsNone(result["frametimes"])
        with self.assertRaises(module.CaptureError): module.compare(result, result)

    def test_cpu_counter_reset_or_missing_samples_rejected(self):
        self.samples[20]["app_cpu_ms"] = "0"
        with self.assertRaises(module.CaptureError): self.analyze()
        self.samples.pop()
        with self.assertRaises(module.CaptureError): self.analyze()

    def test_nan_and_nonmonotonic_samples_rejected(self):
        self.samples[2]["elapsed_s"] = "NaN"
        with self.assertRaises(module.CaptureError): self.analyze()
        self.samples[2]["elapsed_s"] = "1"
        with self.assertRaises(module.CaptureError): self.analyze()

    def test_comparison_requires_same_game_scene_hardware_and_duration(self):
        reference = self.analyze()
        for field, value in (("game", "acc"), ("scenario", "other replay"), ("duration_requested_s", 35),
                             ("machine", {"logical_processors": 8, "installed_physical_bytes": 8 * 1073741824})):
            old = self.manifest[field]; self.manifest[field] = value
            candidate = {**reference, "manifest": json.loads(json.dumps(self.manifest))}
            with self.assertRaises(module.CaptureError): module.compare(reference, candidate)
            self.manifest[field] = old

    def test_control_run_cannot_contain_app_data(self):
        self.manifest["mode"] = "GameOnly"; self.manifest["app_process"] = None
        with self.assertRaises(module.CaptureError): self.analyze()
        for row in self.samples: row["app_cpu_ms"] = ""
        self.assertIsNone(self.analyze()["app"])

    def test_16gb_capture_is_not_reported_as_8gb(self):
        self.manifest["machine"]["installed_physical_bytes"] = 16 * 1073741824
        self.assertFalse(self.analyze()["hardware_in_8gib_class"])


if __name__ == "__main__": unittest.main()
