import json
import tempfile
import unittest
from pathlib import Path

from PIL import Image

from scripts.compare_render_backends import (
    ArtifactContractError,
    compare_scene,
    validate_artifact_contract,
)


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

    def test_contract_rejects_missing_candidate_manifest(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.write_scene(root / "vulkan", (10, 20, 30, 255))
            (root / "webgpu").mkdir()
            (root / "webgpu" / "other.json").write_text(
                json.dumps({"scene": "other", "backend": "webgpu"}),
                encoding="utf-8",
            )
            Image.new("RGBA", (4, 4), (10, 20, 30, 255)).save(
                root / "webgpu" / "other.png"
            )
            with self.assertRaisesRegex(
                ArtifactContractError, "candidate missing manifests: flat.json"
            ):
                validate_artifact_contract(root / "vulkan", root / "webgpu")

    def test_contract_rejects_missing_image(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.write_scene(root / "vulkan", (10, 20, 30, 255))
            self.write_scene(root / "webgpu", (10, 20, 30, 255))
            (root / "webgpu" / "flat.png").unlink()
            with self.assertRaisesRegex(
                ArtifactContractError,
                "candidate manifest flat.json is missing image flat.png",
            ):
                validate_artifact_contract(root / "vulkan", root / "webgpu")


if __name__ == "__main__":
    unittest.main()
