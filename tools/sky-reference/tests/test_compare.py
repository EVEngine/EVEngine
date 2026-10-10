import importlib.util
import tempfile
import unittest
from pathlib import Path

import numpy as np
from PIL import Image


SPEC = importlib.util.spec_from_file_location("sky_compare", Path(__file__).parents[1] / "compare.py")
compare = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(compare)


class CompareTests(unittest.TestCase):
    def test_identical_sdr_images(self):
        rng = np.random.default_rng(7)
        pixels = rng.integers(0, 256, size=(32, 32, 3), dtype=np.uint8)
        result = compare.metrics(pixels, pixels)
        self.assertEqual(result["withinToleranceFraction"], 1)
        self.assertAlmostEqual(result["ssim"], 1)
        self.assertEqual(result["mae8bit"], 0)

    def test_pixel_pass_uses_maximum_channel_and_inclusive_tolerance(self):
        a = np.full((20, 20, 3), 100, dtype=np.uint8)
        b = a.copy()
        b[0, 0, 0] += 3
        b[0, 1, 2] += 4
        result = compare.metrics(a, b, 3)
        self.assertEqual(result["withinToleranceFraction"], 399 / 400)
        self.assertEqual(result["exactPixelFraction"], 398 / 400)

    def test_region_failure_cannot_be_hidden_by_full_frame(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            a = np.full((256, 256, 3), 100, dtype=np.uint8)
            b = a.copy()
            b[:11, :11] = 0
            Image.fromarray(a).save(root / "reference.png")
            Image.fromarray(b).save(root / "candidate.png")
            result = compare.compare_frames(root / "reference.png", root / "candidate.png", root / "out",
                                             3, .99, .9, {"sun": [0, 0, 11, 11]})
            self.assertEqual(result["regions"]["full"]["status"], "pass")
            self.assertEqual(result["regions"]["sun"]["status"], "fail")
            self.assertEqual(result["status"], "fail")

    def test_shape_mismatch_is_rejected_not_resized(self):
        with self.assertRaises(ValueError):
            compare.metrics(np.zeros((12, 12, 3), dtype=np.uint8), np.zeros((13, 12, 3), dtype=np.uint8))

    def test_high_precision_input_is_not_silently_quantized(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            Image.fromarray(np.full((12, 12), 1024, dtype=np.uint16)).save(root / "high.png")
            with self.assertRaisesRegex(ValueError, "implicit quantization"):
                compare.compare_frames(root / "high.png", root / "high.png", root / "out", 3, .99, .99, {})
            self.assertFalse((root / "out").exists())

    def test_mismatched_color_profiles_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            pixels = Image.fromarray(np.zeros((12, 12, 3), dtype=np.uint8))
            pixels.save(root / "a.png", icc_profile=b"test-profile-a")
            pixels.save(root / "b.png", icc_profile=b"test-profile-b")
            with self.assertRaisesRegex(ValueError, "color profiles differ"):
                compare.compare_frames(root / "a.png", root / "b.png", root / "out", 3, .99, .99, {})


if __name__ == "__main__":
    unittest.main()
