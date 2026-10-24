"""Reject missing visual evidence and preserve thin-component measurement limits."""

import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from road_component_parity import metrics


class ComponentComparisonTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def frame(self, name, bounds):
        path = self.root / name
        image = Image.new("RGB", (1280, 720), (77, 77, 77))
        if bounds:
            ImageDraw.Draw(image).rectangle(bounds, fill=(170, 120, 40))
        image.save(path)
        return path

    def test_blank_reference_rejected(self):
        with self.assertRaisesRegex(ValueError, "Empty"):
            metrics(
                self.frame("ev.png", (100, 100, 200, 200)),
                self.frame("unity.png", None),
                self.root / "diff.png",
            )

    def test_identical_thick_component_passes(self):
        path = self.frame("frame.png", (100, 100, 200, 200))
        result = metrics(path, path, self.root / "diff.png")
        self.assertTrue(result["withinThresholds"])
        self.assertEqual(result["interiorRgbMAE"], 0)

    def test_thin_component_is_not_false_color_pass(self):
        path = self.frame("frame.png", (100, 100, 200, 101))
        result = metrics(path, path, self.root / "diff.png")
        self.assertEqual(result["surfaceIoU"], 1)
        self.assertIsNone(result["interiorRgbMAE"])
        self.assertFalse(result["withinThresholds"])

    def test_layout_mismatch_fails(self):
        result = metrics(
            self.frame("ev.png", (100, 100, 200, 200)),
            self.frame("unity.png", (150, 100, 250, 200)),
            self.root / "diff.png",
        )
        self.assertFalse(result["withinThresholds"])
        self.assertLess(result["surfaceIoU"], 0.98)


if __name__ == "__main__":
    unittest.main()
