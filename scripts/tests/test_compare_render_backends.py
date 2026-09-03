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
    def write_scene(
        self,
        root: Path,
        rgba: tuple[int, int, int, int],
        *,
        contract: dict | None = None,
    ) -> Path:
        root.mkdir(parents=True)
        Image.new("RGBA", (4, 4), rgba).save(root / "flat.png")
        manifest = root / "flat.json"
        manifest.write_text(
            json.dumps(
                {
                    "schema": "evengine.render-parity",
                    "version": 1,
                    "scene": "flat",
                    "backend": root.name,
                    "contract": contract
                    or {
                        "color_space": "srgb-linearized",
                        "alpha_coverage_delta_max": 0.005,
                        "mean_rgb_error_max": 2.0 / 255.0,
                        "p99_rgb_error_max": 8.0 / 255.0,
                    },
                }
            ),
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

    def test_scene_contract_controls_tolerance(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            permissive = {
                "color_space": "srgb-linearized",
                "alpha_coverage_delta_max": 0.005,
                "mean_rgb_error_max": 1.0,
                "p99_rgb_error_max": 1.0,
            }
            manifest = self.write_scene(
                root / "vulkan", (0, 0, 0, 255), contract=permissive
            )
            self.write_scene(
                root / "webgpu", (255, 255, 255, 255), contract=permissive
            )
            self.assertEqual(
                compare_scene(root / "vulkan", root / "webgpu", manifest), []
            )

    def test_contract_rejects_unversioned_manifest(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.write_scene(root / "vulkan", (10, 20, 30, 255))
            self.write_scene(root / "webgpu", (10, 20, 30, 255))
            manifest = root / "webgpu" / "flat.json"
            payload = json.loads(manifest.read_text(encoding="utf-8"))
            del payload["version"]
            manifest.write_text(json.dumps(payload), encoding="utf-8")
            with self.assertRaisesRegex(
                ArtifactContractError, "missing required field: version"
            ):
                validate_artifact_contract(root / "vulkan", root / "webgpu")

    def test_contract_rejects_backend_tolerance_disagreement(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.write_scene(root / "vulkan", (10, 20, 30, 255))
            candidate = self.write_scene(root / "webgpu", (10, 20, 30, 255))
            payload = json.loads(candidate.read_text(encoding="utf-8"))
            payload["contract"]["mean_rgb_error_max"] = 1.0
            candidate.write_text(json.dumps(payload), encoding="utf-8")
            with self.assertRaisesRegex(
                ArtifactContractError, "contract differs between backends"
            ):
                validate_artifact_contract(root / "vulkan", root / "webgpu")

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
