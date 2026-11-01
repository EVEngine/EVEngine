"""Benchmark evidence must reject incomplete, mismatched or nonfinite samples."""

import csv
import math
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import benchmark_path_tracing as benchmark


class PathBenchmarkEvidenceTests(unittest.TestCase):
    def read(self, changes=None, count=2, moving=False, spatial=False, runtime=False, instances=1, texture_size=0, pbr_material=False, normal_map=False, temporal=False, scene="planar_grid", moving_camera=False):
        rows = [{"schema_version": "1", "scene": scene, "triangles": "10000",
                 "instances": str(instances), "width": "1920", "height": "1080", "spp": "1",
                 "bounces": "4", "repeat": "0", "frame": str(i), "prepare_ms": "2",
                 "batch_wall_ms": str(i + 1), "instance_motion": "translate_x" if moving else "static",
                 "instance_update_ms": ".25" if moving else "0",
                 "denoise": "temporal" if temporal else "spatial" if spatial else "none"} for i in range(count)]
        if moving_camera:
            for i, row in enumerate(rows):
                row.update(camera_motion="sine_x_look_at_origin", camera_x=str(.35 * math.sin((120 + i) * .01)))
        if runtime:
            for row in rows:
                row.update(schema_version="2", path_active="1", vsync="0", readback="0", present_mode="immediate")
                row["frame_wall_ms"] = row.pop("batch_wall_ms")
                row.pop("instance_update_ms")
        if normal_map:
            for row in rows:
                row["normal_map"] = "1"
        if pbr_material:
            for row in rows:
                row["pbr_material"] = "1"
        if texture_size:
            for row in rows:
                row["texture_size"] = str(texture_size)
        if changes and any(name in changes for name in ("motion_ms", "render_ms", "present_ui_ms")):
            for row in rows:
                row.update(motion_ms="0", render_ms=row["frame_wall_ms"], present_ui_ms="0")
        if changes and any(name in changes for name in ("gpu_available", "gpu_last_completed_ms")):
            for row in rows:
                row.update(gpu_available="1", gpu_last_completed_ms="0.75")
        if changes:
            rows[-1].update(changes)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "samples.csv"
            with path.open("w", encoding="utf-8", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=list(dict.fromkeys(key for row in rows for key in row)))
                writer.writeheader()
                writer.writerows(rows)
            return benchmark.read_samples(path, 10000, 1920, 1080, 2, 1, moving, spatial, runtime, instances, texture_size, pbr_material, normal_map, temporal, scene, moving_camera)

    def test_normal_metadata_must_match_requested_workload(self):
        self.assertEqual(self.read(runtime=True, texture_size=512, pbr_material=True, normal_map=True), [[1., 2.]])
        with self.assertRaisesRegex(ValueError, "normal map"):
            self.read({"normal_map": "0"}, runtime=True, pbr_material=True, normal_map=True)
        with self.assertRaisesRegex(ValueError, "normal map"):
            self.read({"normal_map": "1"}, runtime=True)

    def test_pbr_metadata_must_match_requested_workload(self):
        self.assertEqual(self.read(runtime=True, texture_size=512, pbr_material=True), [[1., 2.]])
        with self.assertRaisesRegex(ValueError, "PBR material"):
            self.read({"pbr_material": "0"}, runtime=True, texture_size=512, pbr_material=True)
        with self.assertRaisesRegex(ValueError, "PBR material"):
            self.read({"pbr_material": "1"}, runtime=True)

    def test_texture_metadata_must_match_requested_workload(self):
        self.assertEqual(self.read(runtime=True, texture_size=512), [[1., 2.]])
        with self.assertRaisesRegex(ValueError, "texture size"):
            self.read({"texture_size": "0"}, runtime=True, texture_size=512)
        with self.assertRaisesRegex(ValueError, "texture size"):
            self.read({"texture_size": "512"}, runtime=True)

    def test_camera_motion_requires_measured_trajectory(self):
        self.assertEqual(self.read(runtime=True, moving_camera=True), [[1., 2.]])
        for change in ({"camera_motion": "static"}, {"camera_x": "0"}, {"camera_x": "nan"}):
            with self.assertRaisesRegex(ValueError, "camera"):
                self.read(change, runtime=True, moving_camera=True)

    def test_scene_identity_must_match_requested_workload(self):
        self.assertEqual(self.read(runtime=True, scene="corrugated_grid"), [[1., 2.]])
        with self.assertRaisesRegex(ValueError, "configuration"):
            self.read({"scene": "planar_grid"}, runtime=True, scene="corrugated_grid")

    def test_temporal_mode_must_match_requested_measurement(self):
        self.assertEqual(self.read(runtime=True, temporal=True), [[1., 2.]])
        with self.assertRaisesRegex(ValueError, "denoise mode"):
            self.read({"denoise": "spatial"}, runtime=True, temporal=True)
        with self.assertRaisesRegex(ValueError, "denoise mode"):
            self.read({"denoise": "temporal"}, runtime=True, spatial=True)

    def test_denoise_mode_must_match_requested_measurement(self):
        self.assertEqual(self.read(spatial=True), [[1., 2.]])
        with self.assertRaisesRegex(ValueError, "denoise mode"):
            self.read({"denoise": "none"}, spatial=True)

    def test_complete_samples_and_interpolated_percentiles(self):
        self.assertEqual(self.read(), [[1., 2.]])
        self.assertEqual(benchmark.percentile([3., 1., 2.], .5), 2.)
        self.assertAlmostEqual(benchmark.percentile([1., 2.], .95), 1.95)

    def test_rejects_missing_samples(self):
        with self.assertRaisesRegex(ValueError, "incomplete sample count"):
            self.read(count=1)

    def test_rejects_schema_configuration_and_duplicate_frames(self):
        for change in ({"schema_version": "2"}, {"triangles": "100000"}, {"frame": "0"}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                self.read(change)

    def test_rejects_nonfinite_and_nonpositive_times(self):
        for value in ("nan", "inf", "-1", "0"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.read({"batch_wall_ms": value})

    def test_motion_requires_matching_mode_and_valid_update_interval(self):
        self.assertEqual(self.read(moving=True), [[1., 2.]])
        for change in ({"instance_motion": "static"}, {"instance_update_ms": "nan"},
                       {"instance_update_ms": "0"}, {"instance_update_ms": "3"}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                self.read(change, moving=True)
        with self.assertRaises(ValueError):
            self.read({"instance_update_ms": ".25"})

    def test_runtime_requires_active_path_and_unthrottled_nonreadback_frames(self):
        self.assertEqual(self.read(runtime=True, spatial=True), [[1., 2.]])
        self.assertEqual(self.read(runtime=True, spatial=True, moving=True), [[1., 2.]])
        for change in ({"path_active": "0"}, {"vsync": "1"}, {"readback": "1"},
                       {"schema_version": "1"}, {"frame_wall_ms": "nan"}, {"present_mode": "fifo"}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                self.read(change, runtime=True, spatial=True)

    def test_instance_count_must_match_requested_scene(self):
        self.assertEqual(self.read(runtime=True, instances=100), [[1., 2.]])
        with self.assertRaisesRegex(ValueError, "configuration"):
            self.read({"instances": "1"}, runtime=True, instances=100)

    def test_runtime_stage_times_must_be_complete_and_sum_to_frame(self):
        for stages in ({"motion_ms": ""},
                       {"motion_ms": "nan", "render_ms": "1", "present_ui_ms": "1"},
                       {"motion_ms": "0.1", "render_ms": "1", "present_ui_ms": "3"}):
            with self.subTest(stages=stages), self.assertRaisesRegex(ValueError, "stage times"):
                self.read(stages, runtime=True)

    def test_gpu_timing_is_explicitly_available_and_delayed(self):
        self.assertEqual(self.read({"gpu_available": "1", "gpu_last_completed_ms": "3"}, runtime=True), [[1., 2.]])
        self.assertEqual(self.read({"gpu_available": "0", "gpu_last_completed_ms": "0"}, runtime=True), [[1., 2.]])
        for change in ({"gpu_last_completed_ms": "nan"}, {"gpu_last_completed_ms": "0"},
                       {"gpu_available": "0"}, {"gpu_available": "yes"}):
            with self.subTest(change=change), self.assertRaisesRegex(ValueError, "GPU timing"):
                self.read(change, runtime=True)
