"""Reference metrics and revision evidence must detect real failure cases."""
import json
import tempfile
import unittest
from pathlib import Path

from PIL import Image

from scene_qc_agent.reference import ReferenceRefiner, checked, image_metrics


class TestReference(unittest.TestCase):
    def test_mask_metrics_detect_shift_and_empty_image(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            a, b = Image.new("RGB", (8, 8)), Image.new("RGB", (8, 8))
            for x in range(2, 5):
                for y in range(2, 5):
                    a.putpixel((x, y), (255, 0, 0))
                    b.putpixel((x + 1, y), (255, 0, 0))
            a.save(root / "a.png")
            b.save(root / "b.png")
            mask = {"rgb": [255, 0, 0], "tolerance": 0}
            metrics = image_metrics(root / "a.png", root / "b.png", mask)
            self.assertAlmostEqual(metrics["silhouetteIou"], 0.5)
            self.assertAlmostEqual(metrics["centroidError"], 0.125)
            self.assertEqual(image_metrics(root / "a.png", root / "a.png", mask)["score"], 1)
            Image.new("RGB", (8, 8)).save(root / "empty.png")
            with self.assertRaises(ValueError):
                image_metrics(root / "empty.png", root / "empty.png", mask)

    def test_tool_failures_are_not_scores(self):
        for value in ({"ok": False}, {"error": "capture unavailable"}, "[]"):
            with self.assertRaises(RuntimeError):
                checked(value)

    def test_capture_refuses_concurrent_scene_revision(self):
        class Client:
            calls = 0

            def call_tool(self, name, arguments):
                self.calls += 1
                return json.dumps({"ok": True, "revision": self.calls, "frame": 1, "renderedRevision": 1})

        with tempfile.TemporaryDirectory() as directory:
            refiner = ReferenceRefiner(Client(), Path("reference.png"), {}, Path(directory))
            with self.assertRaisesRegex(RuntimeError, "Concurrent"):
                refiner.capture("candidate")

    def test_capture_waits_for_native_readback_frame(self):
        class Client:
            frame = 0
            captures = 0

            def call_tool(self, name, arguments):
                if name == "eve_scene_info":
                    self.frame += 1
                    return {"ok": True, "revision": 3, "frame": self.frame, "renderedRevision": 3}
                self.captures += 1
                if self.captures == 1:
                    return {"ok": False, "retryable": True, "reason": "no-presented-frame"}
                Image.new("RGB", (8, 8)).save(arguments["path"])
                return {"ok": True}

        with tempfile.TemporaryDirectory() as directory:
            client = Client()
            refiner = ReferenceRefiner(client, Path("reference.png"), {}, Path(directory))
            path, evidence = refiner.capture("candidate")
            self.assertTrue(path.is_file())
            self.assertEqual(client.captures, 2)
            self.assertEqual(evidence["revision"], 3)


if __name__ == "__main__":
    unittest.main()
