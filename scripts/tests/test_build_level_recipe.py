import importlib.util
import sys
import unittest
from pathlib import Path


MODULE_PATH = (
    Path(__file__).parents[2]
    / "tools"
    / "unreal-uasset-converter"
    / "build_level_recipe.py"
)
SPEC = importlib.util.spec_from_file_location("build_level_recipe", MODULE_PATH)
recipe = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = recipe
SPEC.loader.exec_module(recipe)


class BuildLevelRecipeTests(unittest.TestCase):
    def test_collects_components_with_unreal_to_evengine_transform(self):
        report = {
            "actors": [{"components": [{
                "mesh": "/Game/Kit/SM_Roof",
                "transform": {
                    "locationCentimeters": [300.0, -400.0, 800.0],
                    "rotationDegrees": [0.0, 0.0, 90.0],
                    "rotationQuaternion": [0.0, 0.0, 0.7071067811865475,
                                           0.7071067811865476],
                    "scale": [1.0, 2.0, 3.0],
                },
                "instances": [],
            }]}]
        }
        result = recipe.collect_placements(report, "/Game/Kit/", (100.0, 0.0, 200.0))
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0]["file"], "Game__Kit__SM_Roof.glb")
        self.assertEqual((result[0]["x"], result[0]["y"], result[0]["z"]), (2.0, 6.0, -4.0))
        self.assertAlmostEqual(result[0]["yaw"], -90.0)
        self.assertAlmostEqual(result[0]["pitch"], 0.0)
        self.assertAlmostEqual(result[0]["roll"], 0.0)
        self.assertEqual(result[0]["scale"], [1.0, 3.0, 2.0])

    def test_converts_each_unreal_rotation_axis_to_evengine_basis(self):
        root_half = 2.0 ** -0.5
        yaw = recipe.unreal_quaternion_to_ev_euler_degrees([0, 0, root_half, root_half])
        pitch = recipe.unreal_quaternion_to_ev_euler_degrees([root_half, 0, 0, root_half])
        roll = recipe.unreal_quaternion_to_ev_euler_degrees([0, root_half, 0, root_half])
        self.assertAlmostEqual(yaw[0], -90.0)
        self.assertAlmostEqual(pitch[1], -90.0)
        self.assertAlmostEqual(roll[2], -90.0)

    def test_filters_prefix_and_output_is_deterministic(self):
        def component(mesh, x):
            return {
                "mesh": mesh,
                "transform": {
                    "locationCentimeters": [x, 0.0, 0.0],
                    "rotationDegrees": [0.0, 0.0, 0.0],
                    "scale": [1.0, 1.0, 1.0],
                },
                "instances": [],
            }
        report = {"actors": [{"components": [component("/Game/Kit/B", 1), component("/Game/Foliage/Moss", 2), component("/Game/Kit/A", 3)]}]}
        result = recipe.collect_placements(report, "/Game/Kit/", (0.0, 0.0, 0.0))
        self.assertEqual([item["asset"] for item in result], ["/Game/Kit/A", "/Game/Kit/B"])

    def test_collects_instances_and_applies_radius_and_exclusions(self):
        def transform(x):
            return {
                "locationCentimeters": [x, 0.0, 0.0],
                "rotationDegrees": [0.0, 0.0, 0.0],
                "scale": [1.0, 1.0, 1.0],
            }
        report = {"actors": [{"components": [
            {"mesh": "/Game/Kit/Crate", "transform": transform(0),
             "instances": [transform(50), transform(500)]},
            {"mesh": "/Game/Kit/Foliage/Moss", "transform": transform(20), "instances": []},
        ]}]}
        result = recipe.collect_placements(
            report, "/Game/Kit/", (0.0, 0.0, 0.0),
            exclude_fragments=("Foliage",), radius_cm=100.0,
        )
        self.assertEqual([item["x"] for item in result], [0.5])

    def test_fragment_sampling_is_deterministic_and_asset_specific(self):
        def transform(x):
            return {
                "locationCentimeters": [x, 0.0, 0.0],
                "rotationDegrees": [0.0, 0.0, 0.0],
                "scale": [1.0, 1.0, 1.0],
            }

        report = {"actors": [{"components": [
            {"mesh": "/Game/Foliage/Grass", "transform": transform(0),
             "instances": [transform(x) for x in range(1, 101)]},
            {"mesh": "/Game/Foliage/Tree", "transform": transform(200),
             "instances": []},
        ]}]}
        first = recipe.collect_placements(
            report, "/Game/Foliage/", (0.0, 0.0, 0.0),
            sample_rules=(("Grass", 8),),
        )
        second = recipe.collect_placements(
            report, "/Game/Foliage/", (0.0, 0.0, 0.0),
            sample_rules=(("Grass", 8),),
        )
        self.assertEqual(first, second)
        self.assertIn("/Game/Foliage/Tree", [item["asset"] for item in first])
        grass_count = sum(item["asset"].endswith("Grass") for item in first)
        self.assertGreater(grass_count, 4)
        self.assertLess(grass_count, 30)

    def test_minimum_radius_builds_an_annulus(self):
        def transform(x):
            return {
                "locationCentimeters": [x, 0.0, 0.0],
                "rotationDegrees": [0.0, 0.0, 0.0],
                "scale": [1.0, 1.0, 1.0],
            }

        report = {"actors": [{"components": [
            {"mesh": "/Game/Rocks/Cliff", "transform": transform(0),
             "instances": [transform(20), transform(60), transform(120)]},
        ]}]}
        result = recipe.collect_placements(
            report, "/Game/Rocks/", (0.0, 0.0, 0.0),
            min_radius_cm=50.0, radius_cm=100.0,
        )
        self.assertEqual([item["x"] for item in result], [0.6])


if __name__ == "__main__":
    unittest.main()
