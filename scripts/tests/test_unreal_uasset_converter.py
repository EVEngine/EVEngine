import importlib.util
import json
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = (
    Path(__file__).parents[2]
    / "tools"
    / "unreal-uasset-converter"
    / "unreal_uasset_converter.py"
)
SPEC = importlib.util.spec_from_file_location("unreal_uasset_converter", MODULE_PATH)
converter = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = converter
SPEC.loader.exec_module(converter)


def write_glb(path: Path) -> None:
    document = json.dumps(
        {
            "asset": {"version": "2.0"},
            "meshes": [{}],
            "skins": [{}],
            "animations": [{"name": "Vault"}],
        },
        separators=(",", ":"),
    ).encode("utf-8")
    document += b" " * ((4 - len(document) % 4) % 4)
    chunk = struct.pack("<I4s", len(document), b"JSON") + document
    data = b"glTF" + struct.pack("<II", 2, 12 + len(chunk)) + chunk
    path.write_bytes(data)


class UnrealUassetConverterTests(unittest.TestCase):
    def make_config(self, root: Path, **overrides):
        project = root / "Owned.uproject"
        project.write_text("{}", encoding="utf-8")
        editor = root / "UnrealEditor-Cmd.exe"
        editor.write_bytes(b"fixture")
        values = dict(
            project=project,
            assets=("/Game/Animations/Vault",),
            output=root / "published",
            output_format="glb",
            unreal_editor=editor,
            rights_confirmed=True,
            timeout_seconds=30,
        )
        values.update(overrides)
        return converter.ConversionConfig(**values)

    def test_uasset_under_project_content_maps_to_game_reference(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project = root / "Owned.uproject"
            project.write_text("{}", encoding="utf-8")
            source = root / "Content" / "Animations" / "Vault.uasset"
            source.parent.mkdir(parents=True)
            source.write_bytes(b"not parsed by the converter")
            self.assertEqual(
                converter.uasset_to_reference(project, source), "/Game/Animations/Vault"
            )

    def test_plugin_mount_asset_reference_is_supported(self):
        self.assertEqual(
            converter.normalize_asset_reference("/OwnedPlugin/Animations/Vault.Vault"),
            "/OwnedPlugin/Animations/Vault.Vault",
        )

    def test_rights_confirmation_is_required_before_process_launch(self):
        with tempfile.TemporaryDirectory() as temporary:
            config = self.make_config(Path(temporary), rights_confirmed=False)
            called = False

            def runner(*args, **kwargs):
                nonlocal called
                called = True

            with self.assertRaisesRegex(converter.ConversionError, "rights-confirmed"):
                converter.convert(config, runner)
            self.assertFalse(called)
            self.assertFalse(config.output.exists())

    def test_success_publishes_validated_artifact_and_manifest_atomically(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            config = self.make_config(root)

            def runner(command, **kwargs):
                request_path = Path(kwargs["env"]["EVENGINE_UE_EXPORT_REQUEST"])
                request = json.loads(request_path.read_text(encoding="utf-8"))
                output = Path(request["outputDirectory"])
                artifact = output / request["assets"][0]["outputFile"]
                write_glb(artifact)
                result = {
                    "schema": converter.RESULT_SCHEMA,
                    "status": "success",
                    "engineVersion": "5.8.0",
                    "artifacts": [
                        {
                            "sourceAsset": "/Game/Animations/Vault",
                            "outputFile": "Vault.glb",
                            "assetClass": "AnimSequence",
                        }
                    ],
                    "diagnostics": [],
                }
                Path(request["resultFile"]).write_text(json.dumps(result), encoding="utf-8")
                # Unreal commandlets can return non-zero for unrelated logged errors;
                # a complete success result plus validated artifacts is authoritative.
                return subprocess.CompletedProcess(command, 1, "", "")

            manifest_path = converter.convert(config, runner)
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            self.assertEqual(manifest["schema"], converter.MANIFEST_SCHEMA)
            self.assertEqual(manifest["source"]["engineVersion"], "5.8.0")
            self.assertEqual(manifest["artifacts"][0]["path"], "Vault.glb")
            self.assertEqual(len(manifest["artifacts"][0]["sha256"]), 64)
            self.assertTrue((config.output / "Vault.glb").is_file())

    def test_failed_batch_never_publishes_partial_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            config = self.make_config(root)

            def runner(command, **kwargs):
                request = json.loads(
                    Path(kwargs["env"]["EVENGINE_UE_EXPORT_REQUEST"]).read_text(encoding="utf-8")
                )
                write_glb(Path(request["outputDirectory"]) / "Vault.glb")
                result = {
                    "schema": converter.RESULT_SCHEMA,
                    "status": "failed",
                    "engineVersion": "5.8.0",
                    "artifacts": [],
                    "diagnostics": ["injected export failure"],
                }
                Path(request["resultFile"]).write_text(json.dumps(result), encoding="utf-8")
                return subprocess.CompletedProcess(command, 0, "", "")

            with self.assertRaisesRegex(converter.ConversionError, "injected export failure"):
                converter.convert(config, runner)
            self.assertFalse(config.output.exists())

    def test_unknown_result_fields_are_rejected(self):
        request = {
            "assets": [{"sourceAsset": "/Game/Animations/Vault", "outputFile": "Vault.glb"}]
        }
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            result = {
                "schema": converter.RESULT_SCHEMA,
                "status": "success",
                "engineVersion": "5.8.0",
                "artifacts": [],
                "diagnostics": [],
                "future": True,
            }
            with self.assertRaisesRegex(converter.ConversionError, "unknown fields"):
                converter._validate_result(result, request, output)

    def test_colliding_asset_names_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            config = self.make_config(
                root,
                assets=("/Game/A/Vault", "/Game/B/Vault"),
            )
            with self.assertRaisesRegex(converter.ConversionError, "colliding"):
                converter.build_request(config, root / "result.json", root / "payload")

    def test_gltf_sidecars_are_validated_and_hashed(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            (output / "Vault.bin").write_bytes(b"animation")
            (output / "Vault.gltf").write_text(
                json.dumps(
                    {
                        "asset": {"version": "2.0"},
                        "meshes": [{}],
                        "skins": [{}],
                        "animations": [{"name": "Vault"}],
                        "buffers": [{"uri": "Vault.bin", "byteLength": 9}],
                    }
                ),
                encoding="utf-8",
            )
            request = {
                "assets": [
                    {"sourceAsset": "/Game/Animations/Vault", "outputFile": "Vault.gltf"}
                ]
            }
            result = {
                "schema": converter.RESULT_SCHEMA,
                "status": "success",
                "engineVersion": "5.8.0",
                "artifacts": [
                    {
                        "sourceAsset": "/Game/Animations/Vault",
                        "outputFile": "Vault.gltf",
                        "assetClass": "AnimSequence",
                    }
                ],
                "diagnostics": [],
            }
            artifacts = converter._validate_result(result, request, output)
            self.assertEqual(artifacts[0]["dependencies"][0]["path"], "Vault.bin")
            self.assertEqual(len(artifacts[0]["dependencies"][0]["sha256"]), 64)
            self.assertEqual(artifacts[0]["content"]["animationCount"], 1)


if __name__ == "__main__":
    unittest.main()
