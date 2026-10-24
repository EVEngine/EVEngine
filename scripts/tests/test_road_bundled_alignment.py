"""Author data and real-frame comparison must reject incomplete or misleading mappings."""

import copy
import json
import sys
import unittest
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from road_bundled_alignment import canonical_network, shader_ambient, sha
from road_bundled_report import referenced_bounds, validate_capture, validate_derivation


class BundledAuthorTests(unittest.TestCase):
    def setUp(self):
        shape = [{"x": 3.0, "y": 0.0}, {"x": -3.0, "y": 0.0}]
        self.road = {
            "id": "root[0]/road[1]",
            "name": "duplicate-name",
            "closed": False,
            "resolution": 2.0,
            "shape": shape,
            "shapeUV": [0.0, 1.0],
            "shapeMaterials": [0, 0],
            "markers": [
                {
                    "position": {"x": x, "y": 0.0, "z": 10.0},
                    "strength": 0.5,
                    "control": 0,
                    "width": 0.0,
                    "roll": 0.0,
                    "pivot": 0.5,
                    "shape": shape,
                }
                for x in (0.0, 40.0)
            ],
        }
        self.template = {"edges": [{"pointProfiles": [{}], "style": {}}], "nodes": [{}, {}]}

    def test_reflects_author_coordinates_and_preserves_input(self):
        old = copy.deepcopy(self.road)
        network = canonical_network(self.road, self.template)
        self.assertEqual(network["edges"][0]["points"], [[0.0, 0.0, -10.0], [40.0, 0.0, -10.0]])
        self.assertEqual(self.road, old)
        self.assertEqual(network["nodes"][1]["z"], -10.0)

    def test_schema_numbers_remain_floats_after_json_round_trip(self):
        edge = json.loads(json.dumps(canonical_network(self.road, self.template)))["edges"][0]
        for key in ("laneWidth", "uvMeters", "samplingAngleDegrees"):
            self.assertIsInstance(edge["style"][key], float)
        self.assertIsInstance(edge["pointProfiles"][0]["widthScale"], float)

    def test_bad_strength_is_not_silently_clamped(self):
        self.road["markers"][0]["strength"] = 0
        with self.assertRaises(ValueError):
            canonical_network(self.road, self.template)

    def test_unmapped_width_or_marker_profile_is_rejected(self):
        for key, value in (("width", 8), ("shape", [])):
            candidate = copy.deepcopy(self.road)
            candidate["markers"][0][key] = value
            with self.assertRaises(ValueError):
                canonical_network(candidate, self.template)

    def test_missing_material_mapping_is_rejected(self):
        self.road["shapeMaterials"] = [0]
        with self.assertRaises(ValueError):
            canonical_network(self.road, self.template)

    def test_duplicate_profile_points_keep_outgoing_material(self):
        shape = [{"x": 3.0, "y": 0.0}, {"x": 3.0, "y": 0.0}, {"x": -3.0, "y": 0.0}]
        self.road.update(shape=shape, shapeMaterials=[1, 0, 0], shapeUV=[0.0, 0.0, 1.0])
        for marker in self.road["markers"]:
            marker["shape"] = shape
        section = canonical_network(self.road, self.template)["edges"][0]["style"]["crossSection"]
        self.assertEqual(len(section), 2)
        self.assertEqual(section[0]["material"], 0)

    def test_bounds_ignore_unused_submesh_vertices(self):
        bounds = referenced_bounds(
            [{"positions": [0, 0, 0, 1, 0, 0, 0, 0, 1, 999, 999, 999], "indices": [0, 1, 2]}]
        )
        self.assertEqual(bounds, [[0, 0, 0], [1, 0, 1]])

    def test_bounds_reject_missing_geometry(self):
        with self.assertRaises(ValueError):
            referenced_bounds([])

    def test_gamma_sh_is_encoded_before_standard_lighting(self):
        actual = shader_ambient(
            {
                "colorSpace": "Gamma",
                "ambientMode": "Flat",
                "ambientIrradiance": dict.fromkeys("rgb", 0.033104766570885055),
            }
        )
        for value in actual:
            self.assertAlmostEqual(value, 0.2, places=6)

    def test_nonflat_ambient_does_not_silently_use_upward_sh_only(self):
        with self.assertRaises(ValueError):
            shader_ambient({"colorSpace": "Gamma", "ambientMode": "Skybox"})


class DerivationTests(unittest.TestCase):
    def setUp(self):
        original_point = {"x": 1, "y": 0, "z": 2}
        derived_point = {"x": 1, "y": 5, "z": 2}
        self.original = {
            "sceneSha256": "source",
            "roads": [{"name": "0045", "markers": [{"position": original_point}]}],
        }
        self.scene = {
            "scene": "original.unity",
            "sceneSha256": "source",
            "roads": [{"markers": [{"position": derived_point}]}],
        }
        self.record = {
            "schema": "eve.tests.bundled-derivation",
            "version": 1,
            "generatorSha256": "generator",
            "sourceScene": "original.unity",
            "sourceRoad": "0045",
            "originalMarkers": [original_point],
            "markers": [derived_point],
        }

    def test_exact_original_and_derived_inputs_are_required(self):
        validate_derivation(self.record, self.scene, "generator", self.original)
        for key in ("originalMarkers", "markers"):
            changed = copy.deepcopy(self.record)
            changed[key][0]["x"] += 1
            with self.assertRaises(ValueError):
                validate_derivation(changed, self.scene, "generator", self.original)

    def test_changed_generator_or_source_is_rejected(self):
        with self.assertRaises(ValueError):
            validate_derivation(self.record, self.scene, "other-generator", self.original)
        self.scene["sceneSha256"] = "other-source"
        with self.assertRaises(ValueError):
            validate_derivation(self.record, self.scene, "generator", self.original)

    def test_unknown_schema_version_is_rejected(self):
        self.record["version"] = 2
        with self.assertRaises(ValueError):
            validate_derivation(self.record, self.scene, "generator", self.original)


class CaptureInputTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in ("scene.glb", "main.nut", "config.nut", "frame.png"):
            (self.root / name).write_bytes(b"original")
        self.record = {
            "schema": "eve.tests.bundled-capture-inputs",
            "version": 1,
            "frameSha256": sha(self.root / "frame.png"),
            "inputs": {n: sha(self.root / n) for n in ("scene.glb", "main.nut", "config.nut")},
        }
        self.save()

    def save(self):
        (self.root / "capture-inputs.json").write_text(json.dumps(self.record), encoding="utf-8")

    def test_unchanged_captured_inputs_are_accepted(self):
        validate_capture(self.root)

    def test_publishing_rejects_unrecorded_or_rebuilt_runtime(self):
        runtime = {"eve.exe": "original-runtime"}
        with self.assertRaises(ValueError):
            validate_capture(self.root, runtime)
        self.record["runtime"] = runtime
        self.save()
        validate_capture(self.root, runtime)
        with self.assertRaises(ValueError):
            validate_capture(self.root, {"eve.exe": "rebuilt-runtime"})

    def test_modified_geometry_is_rejected(self):
        (self.root / "scene.glb").write_bytes(b"changed")
        with self.assertRaises(ValueError):
            validate_capture(self.root)

    def test_stale_or_replaced_frame_is_rejected(self):
        (self.root / "frame.png").write_bytes(b"changed")
        with self.assertRaises(ValueError):
            validate_capture(self.root)

    def test_missing_or_outside_scene_input_is_rejected(self):
        self.record["inputs"].pop("scene.glb")
        self.save()
        with self.assertRaises(ValueError):
            validate_capture(self.root)
        self.record["inputs"]["../scene.glb"] = "invalid"
        self.save()
        with self.assertRaises(ValueError):
            validate_capture(self.root)
