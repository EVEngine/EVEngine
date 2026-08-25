import json
import tempfile
import unittest
from pathlib import Path

from PIL import Image

from scripts.compare_render_backends import compare_scene


class CompareRenderBackendsTest(unittest.TestCase):
    def write_scene(self, root: Path, rgba: tuple[int, int, int, int]) -> Path:
        root.mkdir(parents=True)
        Image.new("RGBA", (4, 4), rgba).save(root / "flat.png")
        manifest = root / "flat.json"
        manifest.write_text(
            json.dumps({"scene": "flat", "backend": root.name, "profile": "flat2d"}),
            encoding="utf-8",
        )
        return manifest

    def test_identical_images_pass(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = self.write_scene(root / "vulkan", (10, 20, 30, 255))
            self.write_scene(root / "webgpu", (10, 20, 30, 255))
            self.assertEqual(
                compare_scene(root / "vulkan", root / "webgpu", manifest), []
            )

    def test_large_color_difference_fails(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = self.write_scene(root / "vulkan", (0, 0, 0, 255))
            self.write_scene(root / "webgpu", (255, 255, 255, 255))
            failures = compare_scene(root / "vulkan", root / "webgpu", manifest)
            self.assertTrue(any("mean linear RGB" in failure for failure in failures))


if __name__ == "__main__":
    unittest.main()
