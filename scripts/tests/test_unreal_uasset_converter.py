import importlib.util
import hashlib
import json
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
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

PARITY_PATH = MODULE_PATH.with_name("audit_static_mesh_parity.py")
PARITY_SPEC = importlib.util.spec_from_file_location("audit_static_mesh_parity", PARITY_PATH)
parity = importlib.util.module_from_spec(PARITY_SPEC)
assert PARITY_SPEC.loader is not None
PARITY_SPEC.loader.exec_module(parity)

MATERIAL_PARITY_PATH = MODULE_PATH.with_name("audit_material_parity.py")
MATERIAL_PARITY_SPEC = importlib.util.spec_from_file_location(
    "audit_material_parity", MATERIAL_PARITY_PATH
)
material_parity = importlib.util.module_from_spec(MATERIAL_PARITY_SPEC)
assert MATERIAL_PARITY_SPEC.loader is not None
MATERIAL_PARITY_SPEC.loader.exec_module(material_parity)

MATERIAL_REPAIR_PATH = MODULE_PATH.with_name("repair_gltf_material_parity.py")
MATERIAL_REPAIR_SPEC = importlib.util.spec_from_file_location(
    "repair_gltf_material_parity", MATERIAL_REPAIR_PATH
)
material_repair = importlib.util.module_from_spec(MATERIAL_REPAIR_SPEC)
assert MATERIAL_REPAIR_SPEC.loader is not None
MATERIAL_REPAIR_SPEC.loader.exec_module(material_repair)


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


def write_static_glb(path: Path) -> None:
    document = json.dumps(
        {"asset": {"version": "2.0"}, "meshes": [{}]}, separators=(",", ":")
    ).encode("utf-8")
    document += b" " * ((4 - len(document) % 4) % 4)
    chunk = struct.pack("<I4s", len(document), b"JSON") + document
    path.write_bytes(b"glTF" + struct.pack("<II", 2, 12 + len(chunk)) + chunk)


def write_material_glb(path: Path, material_name: str) -> None:
    document = json.dumps(
        {
            "asset": {"version": "2.0"},
            "materials": [{"name": material_name}],
            "meshes": [{"primitives": [{"material": 0}]}],
        },
        separators=(",", ":"),
    ).encode("utf-8")
    document += b" " * ((4 - len(document) % 4) % 4)
    chunk = struct.pack("<I4s", len(document), b"JSON") + document
    path.write_bytes(b"glTF" + struct.pack("<II", 2, 12 + len(chunk)) + chunk)


class UnrealUassetConverterTests(unittest.TestCase):
    def test_material_parity_repair_restores_ue_core_flags_and_manifest_hash(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            glb = root / "mesh.glb"
            write_material_glb(glb, "MI_Leaves_SM_Tree")
            audit = {"assets": [{
                "class": "StaticMesh", "package": "/Game/SM_Tree",
                "materialSlots": [{"material": "/Game/MI_Leaves.MI_Leaves"}],
            }]}
            material_audit = {"materials": [{
                "asset": "/Game/MI_Leaves.MI_Leaves",
                "blend_mode": "<BlendMode.BLEND_MASKED: 1>",
                "shading_model": "<MaterialShadingModel.MSM_FROM_MATERIAL_EXPRESSION: 14>",
                "two_sided": True,
                "opacity_mask_clip_value": 0.33329999446868896,
            }]}
            manifest = {"artifacts": [{
                "assetClass": "StaticMesh", "sourceAsset": "/Game/SM_Tree",
                "path": "mesh.glb", "bytes": glb.stat().st_size,
                "sha256": hashlib.sha256(glb.read_bytes()).hexdigest(),
            }]}
            before = material_parity.build_report(audit, material_audit, manifest, root)
            self.assertEqual(before["semanticMismatchCount"], 1)
            self.assertEqual(material_repair.repair(before, manifest, root), 1)
            after = material_parity.build_report(audit, material_audit, manifest, root)
            self.assertEqual(after["mappingFailureCount"], 0)
            self.assertEqual(after["semanticMismatchCount"], 0)
            self.assertEqual(manifest["artifacts"][0]["bytes"], glb.stat().st_size)
            self.assertEqual(
                manifest["artifacts"][0]["sha256"],
                hashlib.sha256(glb.read_bytes()).hexdigest(),
            )

    def test_static_mesh_parity_accepts_exporter_suffix_and_unused_ue_slots(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            glb = root / "mesh.glb"
            write_material_glb(glb, "MI_Wood_SM_Wall")
            digest = parity._sha256(glb)
            audit = {"assets": [{
                "class": "StaticMesh", "package": "/Game/SM_Wall", "lodCount": 1,
                "materialSlots": [
                    {"material": "/Game/Materials/MI_Wood.MI_Wood"},
                    {"material": "/Game/Materials/MI_Unused.MI_Unused"},
                ],
            }]}
            manifest = {"artifacts": [{
                "assetClass": "StaticMesh", "sourceAsset": "/Game/SM_Wall",
                "outputFile": "mesh.glb", "bytes": glb.stat().st_size, "sha256": digest,
                "content": {"meshCount": 1, "primitiveCount": 1,
                            "primitivesWithoutMaterial": 0, "referencedMaterialCount": 1,
                            "materialCount": 1, "materialTextureUsage": {"baseColor": 1}},
            }]}
            report = parity.build_report(audit, manifest, root)
            self.assertEqual(report["counts"]["invalid"], 0)
            self.assertEqual(report["counts"]["materialSlotMismatch"], 0)
            self.assertEqual(report["assets"][0]["glb"]["unusedAssignedMaterialCount"], 1)

    def test_publication_staging_inherits_destination_access_and_cleans_up(self):
        with tempfile.TemporaryDirectory() as temporary:
            parent = Path(temporary)
            original_mkdir = Path.mkdir
            modes = []
            def mkdir(path, mode=0o777, parents=False, exist_ok=False):
                modes.append(mode)
                return original_mkdir(path, mode, parents, exist_ok)
            with mock.patch.object(Path, "mkdir", mkdir):
                with converter.publication_staging(parent, "test") as staging:
                    root = Path(staging)
                    self.assertEqual(root.parent, parent.resolve())
                    self.assertTrue(root.is_dir())
                    (root / "payload").mkdir()
            self.assertFalse(root.exists())
            self.assertTrue(modes)
            self.assertNotIn(0o700, modes)

    def test_quantized_weights_become_float_without_changing_other_payload(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "skin.glb"
            document = {"asset": {"version": "2.0"},
                        "buffers": [{"byteLength": 12}],
                        "bufferViews": [{"buffer": 0, "byteOffset": 4, "byteLength": 8, "byteStride": 4}],
                        "accessors": [{"bufferView": 0, "componentType": 5121,
                                       "normalized": True, "count": 2, "type": "VEC4"}],
                        "meshes": [{"primitives": [{"attributes": {"WEIGHTS_0": 0}}]}]}
            raw = json.dumps(document).encode()
            raw += b" " * (-len(raw) % 4)
            binary = b"KEEP" + bytes([255, 0, 0, 0, 128, 127, 0, 0])
            chunks = struct.pack("<I4s", len(raw), b"JSON") + raw + struct.pack("<I4s", len(binary), b"BIN\0") + binary
            path.write_bytes(b"glTF" + struct.pack("<II", 2, 12 + len(chunks)) + chunks)
            self.assertEqual(converter.normalize_glb_weights(path), 1)
            output = path.read_bytes()
            length = struct.unpack_from("<I", output, 12)[0]
            result = json.loads(output[20:20 + length])
            accessor = result["accessors"][0]
            self.assertEqual(accessor["componentType"], 5126)
            self.assertNotIn("normalized", accessor)
            payload = output[28 + length:]
            self.assertEqual(payload[:12], binary)
            floats = struct.unpack_from("<8f", payload, result["bufferViews"][-1]["byteOffset"])
            self.assertEqual(floats[:4], (1., 0., 0., 0.))
            self.assertAlmostEqual(floats[4], 128 / 255)
            self.assertAlmostEqual(floats[5], 127 / 255)
            self.assertEqual(converter.normalize_glb_weights(path), 0)

    def test_export_command_enables_material_baking(self):
        with tempfile.TemporaryDirectory() as temporary:
            command = converter.build_command(self.make_config(Path(temporary)), MODULE_PATH)
            self.assertNotIn("-nullrhi", command)
            self.assertIn("-AllowCommandletRendering", command)

    def test_request_carries_bounded_texture_size(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            config = self.make_config(root, texture_size=512)
            request = converter.build_request(config, root / "result.json", root / "payload")
            self.assertEqual(request["textureSize"], 512)

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

    def test_static_mesh_uses_general_asset_manifest_without_skin(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            config = self.make_config(root, assets=("/Game/Architecture/Wall",))

            def runner(command, **kwargs):
                request = json.loads(
                    Path(kwargs["env"]["EVENGINE_UE_EXPORT_REQUEST"]).read_text(encoding="utf-8")
                )
                write_static_glb(Path(request["outputDirectory"]) / "Wall.glb")
                result = {
                    "schema": converter.RESULT_SCHEMA,
                    "status": "success",
                    "engineVersion": "5.8.0",
                    "artifacts": [{
                        "sourceAsset": "/Game/Architecture/Wall",
                        "outputFile": "Wall.glb",
                        "assetClass": "StaticMesh",
                    }],
                    "diagnostics": [],
                }
                Path(request["resultFile"]).write_text(json.dumps(result), encoding="utf-8")
                return subprocess.CompletedProcess(command, 0, "", "")

            manifest_path = converter.convert(config, runner)
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            self.assertEqual(manifest["schema"], converter.ASSET_MANIFEST_SCHEMA)
            self.assertEqual(manifest["artifacts"][0]["content"]["skinCount"], 0)
            self.assertEqual(manifest["artifacts"][0]["content"]["primitiveCount"], 0)
            self.assertEqual(manifest["artifacts"][0]["content"]["materialCount"], 0)

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

    def test_preserve_path_names_disambiguates_colliding_assets(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            config = self.make_config(
                root,
                assets=("/Game/A/Vault", "/Game/B/Vault"),
                preserve_path_names=True,
            )
            request = converter.build_request(config, root / "result.json", root / "payload")
            names = [entry["outputFile"] for entry in request["assets"]]
            self.assertEqual(names, ["Game__A__Vault.glb", "Game__B__Vault.glb"])

    def test_asset_list_supports_comments_and_utf8(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project = root / "Owned.uproject"
            project.write_text("{}", encoding="utf-8")
            editor = root / "UnrealEditor-Cmd.exe"
            editor.write_bytes(b"fixture")
            asset_list = root / "assets.txt"
            asset_list.write_text("# selected assets\n/Game/Town/Wall\n\n/Game/Town/Roof\n", encoding="utf-8")
            args = converter.build_parser().parse_args([
                "--project", str(project), "--asset-list", str(asset_list),
                "--output", str(root / "out"), "--unreal-editor", str(editor),
                "--rights-confirmed",
            ])
            config = converter._config_from_args(args)
            self.assertEqual(config.assets, ("/Game/Town/Wall", "/Game/Town/Roof"))

    def test_asset_audit_selects_every_static_mesh(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project = root / "Owned.uproject"
            project.write_text("{}", encoding="utf-8")
            editor = root / "UnrealEditor-Cmd.exe"
            editor.write_bytes(b"fixture")
            audit = root / "audit.json"
            audit.write_text(json.dumps({
                "schema": "eve.unreal-asset-audit/1",
                "assets": [
                    {"class": "StaticMesh", "package": "/Game/Town/Wall"},
                    {"class": "Texture2D", "package": "/Game/Town/Wall_D"},
                    {"class": "StaticMesh", "package": "/Game/Town/Roof"},
                ],
            }), encoding="utf-8")
            args = converter.build_parser().parse_args([
                "--project", str(project), "--asset-audit", str(audit),
                "--output", str(root / "out"), "--unreal-editor", str(editor),
                "--rights-confirmed",
            ])
            config = converter._config_from_args(args)
            self.assertEqual(config.assets, ("/Game/Town/Wall", "/Game/Town/Roof"))

    def test_exclude_asset_filters_audited_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project = root / "Owned.uproject"
            project.write_text("{}", encoding="utf-8")
            editor = root / "UnrealEditor-Cmd.exe"
            editor.write_bytes(b"fixture")
            audit = root / "audit.json"
            audit.write_text(json.dumps({
                "schema": "eve.unreal-asset-audit/1",
                "assets": [
                    {"class": "StaticMesh", "package": "/Game/Town/Wall"},
                    {"class": "StaticMesh", "package": "/Game/Town/Broken"},
                ],
            }), encoding="utf-8")
            args = converter.build_parser().parse_args([
                "--project", str(project), "--asset-audit", str(audit),
                "--exclude-asset", "/Game/Town/Broken",
                "--output", str(root / "out"), "--unreal-editor", str(editor),
                "--rights-confirmed",
            ])
            config = converter._config_from_args(args)
            self.assertEqual(config.assets, ("/Game/Town/Wall",))

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
