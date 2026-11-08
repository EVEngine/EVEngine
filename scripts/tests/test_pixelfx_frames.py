"""Spatial differences must remain visible even when channel sums cancel."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import compare_pixelfx_frames as compare  # noqa: E402
import compare_pixelfx_rig as rig  # noqa: E402


class PixelFxRigComparisonTests(unittest.TestCase):
    def snapshot(self):
        return {"schema": "eve.pixelfx.rig-probe/v1", "joints": {"handN": [0.0, -2.0, 3.0]},
                "shortfall": 0.0, "z": 1.0, "xs": [0, 1], "ys": [-1, -1],
                "color": [0xff112233, 0xff445566], "src": [1, 2], "ink": [0, 1], "bounds": [0, -1, 1, -1]}

    def test_exact_rig_passes(self):
        self.assertTrue(rig.compare(self.snapshot(), self.snapshot())["passed"])

    def test_equal_sums_with_swapped_cells_fail(self):
        a, b = self.snapshot(), self.snapshot()
        b["color"].reverse()
        report = rig.compare(a, b)
        self.assertFalse(report["passed"])
        self.assertEqual(report["differentSpatialCells"], 2)

    def test_source_mapping_and_order_are_independent_of_color(self):
        a, b = self.snapshot(), self.snapshot()
        b["src"][0] = 7
        report = rig.compare(a, b)
        self.assertFalse(report["passed"])
        self.assertEqual(report["differentSparseEntries"]["color"], 0)
        self.assertEqual(report["differentSparseEntries"]["src"], 1)

    def test_small_joint_error_does_not_hide_pixel_error(self):
        a, b = self.snapshot(), self.snapshot()
        b["joints"]["handN"][0] = 1e-15
        b["ink"][0] = 1
        self.assertFalse(rig.compare(a, b)["passed"])

    def test_bad_lengths_and_duplicate_cells_reject(self):
        a, b = self.snapshot(), self.snapshot()
        b["ys"] = []
        with self.assertRaises(ValueError):
            rig.compare(a, b)
        b = self.snapshot()
        b["xs"] = [0, 0]
        with self.assertRaises(ValueError):
            rig.compare(a, b)


class PixelFxTrackComparisonTests(unittest.TestCase):
    def pair(self):
        pose = dict(rig.POSE_DEFAULTS)
        pose.update({name: False for name in rig.POSE_FLAGS})
        pose.update({name: "none" for name in rig.POSE_WEAPONS})
        pose["ghost"] = 1.0
        a = {"schema": "eve.pixelfx.track-probe/v1", "frames": [{"time": 0, "pose": {"ghost": 1},
             "joints": {"head": [0, -3, 0]}, "shortfall": 0}]}
        b = {"schema": "eve.pixelfx.track-probe/v1", "frames": [{"time": 0, "pose": pose,
             "joints": {"head": [0, -3, 0]}, "shortfall": 0}]}
        return a, b

    def test_numeric_ghost_keeps_its_type(self):
        a, b = self.pair()
        self.assertTrue(rig.compare(a, b)["passed"])
        b["frames"][0]["pose"]["ghost"] = True
        with self.assertRaises(ValueError):
            rig.compare(a, b)

    def test_unexpected_optional_field_is_visible(self):
        a, b = self.pair()
        b["frames"][0]["pose"]["turn"] = 1.0
        report = rig.compare(a, b)
        self.assertFalse(report["passed"])
        self.assertEqual(report["presenceDifferences"], 1)

    def test_part_mapping_is_checked_when_full_body_matches(self):
        a = PixelFxRigComparisonTests().snapshot()
        b = PixelFxRigComparisonTests().snapshot()
        part = {"name": "head", "part": 5, "xs": [0, 1], "ys": [0, 0],
                "color": [1, 2], "cx": 0.5, "cy": 0.0, "z": 1.0}
        a["parts"] = [part]
        b["parts"] = [{**part, "color": [2, 1]}]
        report = rig.compare(a, b)
        self.assertFalse(report["passed"])
        self.assertEqual(report["differentSpatialCells"], 0)
        self.assertFalse(report["parts"][0]["passed"])


class PixelFxFrameComparisonTests(unittest.TestCase):
    def test_identical_planes_are_exact(self):
        report = compare.compare_bytes(bytes([10, 20, 30, 255] * 4), bytes([10, 20, 30, 255] * 4), 2)
        self.assertTrue(report["exact"])
        self.assertIsNone(report["differenceBounds"])

    def test_swapped_pixels_do_not_cancel(self):
        reference = bytes([255, 0, 0, 255, 0, 0, 255, 255])
        actual = bytes([0, 0, 255, 255, 255, 0, 0, 255])
        report = compare.compare_bytes(reference, actual, 2)
        self.assertFalse(report["exact"])
        self.assertEqual(report["differentPixels"], 2)
        self.assertEqual(report["differentColorPixels"], 2)
        self.assertEqual(report["differentAlphaPixels"], 0)
        self.assertEqual(report["differenceBounds"], [0, 0, 1, 0])

    def test_alpha_differences_are_reported_separately(self):
        report = compare.compare_bytes(bytes([1, 2, 3, 0]), bytes([1, 2, 3, 128]), 1)
        self.assertEqual(report["differentColorPixels"], 0)
        self.assertEqual(report["differentAlphaPixels"], 1)
        self.assertEqual(report["maximumChannelError"], 128)

    def test_rgb_lsb_tolerance_is_explicit_and_keeps_exact_failure(self):
        result = compare.compare_bytes(bytes([10, 20, 30, 255]), bytes([11, 20, 30, 255]), 1)
        self.assertFalse(result["exact"])
        self.assertFalse(compare.accepts_plane(result, "color"))
        self.assertTrue(compare.accepts_plane(result, "color", 1))
        self.assertFalse(compare.accepts_plane(result, "light", 1))
        self.assertEqual(result["differenceBounds"], [0, 0, 0, 0])

    def test_tolerance_cannot_hide_alpha_coverage_or_larger_rgb_error(self):
        for reference, actual in [(bytes([10, 20, 30, 254]), bytes([10, 20, 30, 255])),
                                  (bytes(4), bytes([1, 0, 0, 0])),
                                  (bytes([10, 20, 30, 255]), bytes([12, 20, 30, 255]))]:
            with self.subTest(reference=reference, actual=actual):
                result = compare.compare_bytes(reference, actual, 1)
                self.assertFalse(compare.accepts_plane(result, "color", 1))

    def test_invalid_tolerance_contract_rejects(self):
        result = compare.compare_bytes(bytes(4), bytes(4), 1)
        for plane, tolerance in [("depth", 1), ("color", 2), ("color", -1)]:
            with self.assertRaises(ValueError):
                compare.accepts_plane(result, plane, tolerance)

    def test_partial_rows_and_unequal_lengths_are_rejected(self):
        for reference, actual, width in [(bytes(4), bytes(8), 1), (bytes(3), bytes(3), 1),
                                         (bytes(4), bytes(4), 2), (bytes(4), bytes(4), 0)]:
            with self.subTest(width=width, size=len(reference)):
                with self.assertRaises(ValueError):
                    compare.compare_bytes(reference, actual, width)


class PixelFxActorVisibilityTests(unittest.TestCase):
    def pair(self):
        reference = {"id": "scifi/warp-out", "frames": [{"time": 0, "actorVisible": True}, {"time": 0.425, "actorVisible": False}]}
        native = {"schema": "eve.pixelfx-actor-visibility/v1", "id": reference["id"], "frames": [dict(row) for row in reference["frames"]]}
        return reference, native

    def test_exact_and_unknown_metadata(self):
        a, b = self.pair()
        b["future"] = "ignored"
        self.assertTrue(compare.compare_actor_visibility(a, b)["exact"])

    def test_wrong_visibility_is_independent_of_plane_tolerance(self):
        a, b = self.pair()
        b["frames"][1]["actorVisible"] = True
        report = compare.compare_actor_visibility(a, b)
        self.assertFalse(report["exact"])
        self.assertEqual(report["differences"], [{"time": 0.425, "reference": False, "actual": True}])

    def test_missing_partial_and_malformed_metadata_reject(self):
        for change in [lambda b: b.update(schema="wrong"), lambda b: b.update(id="wrong"),
                       lambda b: b["frames"].pop(), lambda b: b["frames"][0].update(time=1),
                       lambda b: b["frames"][0].update(actorVisible=1)]:
            a, b = self.pair()
            change(b)
            with self.assertRaises(ValueError):
                compare.compare_actor_visibility(a, b)
        a, _ = self.pair()
        with self.assertRaises(ValueError):
            compare.compare_actor_visibility(a, None)
        del a["frames"][0]["actorVisible"]
        with self.assertRaises(ValueError):
            compare.compare_actor_visibility(a, None)

    def test_legacy_frame_schema_has_no_body_policy_claim(self):
        report = compare.compare_actor_visibility({"frames": [{"time": 0}]}, None)
        self.assertFalse(report["available"])
        self.assertTrue(report["exact"])


if __name__ == "__main__":
    unittest.main()
