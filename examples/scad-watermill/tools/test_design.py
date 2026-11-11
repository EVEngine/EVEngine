"""Geometry contracts for the authored model, independent of a graphics driver."""

import hashlib
import math
from pathlib import Path
import tempfile
import unittest

import design


class WatermillDesignTests(unittest.TestCase):
    def test_enclosed_walls_have_no_unframed_daylight_gaps(self):
        solids = []
        for material, triangles in design.part_groups("building", True):
            if material not in ("plaster", "glass", "timber") or len(triangles) != 12:
                continue
            points = [v for tri in triangles for v in tri]
            solids.append(
                [(min(v[i] for v in points), max(v[i] for v in points)) for i in range(3)]
            )

        def covered(point):
            return any(
                all(lo - 0.001 <= value <= hi + 0.001 for value, (lo, hi) in zip(point, bounds))
                for bounds in solids
            )

        for z in range(740, 5390, 100):
            for x in range(-2750, 2800, 100):
                self.assertTrue(covered((x, -2200, z)), ("rear", x, z))
            for y in range(-2150, 2400, 100):
                for x in (-2800, 2800):
                    self.assertTrue(covered((x, y, z)), ("side", x, y, z))

    def test_rig_pivots_and_transmission_are_preserved(self):
        self.assertEqual([j["name"] for j in design.JOINTS], list(design.PART_NAMES))
        self.assertEqual(design.JOINTS[3]["pivot"], [0, -2500, 1800])
        self.assertEqual(design.JOINTS[4]["pivot"], [0, -900, 900])
        self.assertEqual(design.JOINTS[5]["pivot"], [1450, -200, 2860])
        self.assertEqual(design.JOINTS[6]["parent"], "main")
        self.assertEqual(design.DRIVE_TO_MAIN, 80 / 24)
        self.assertEqual(design.MAIN_TO_RUNNER, 96 / 24)
        self.assertEqual(design.MAIN_TO_HOIST, 2.2)

    def test_wheel_stays_on_axis_and_above_pit_through_full_turn(self):
        wheel = [v for tri in design.part_triangles("drive") for v in tri if v[0] < -3400]
        self.assertAlmostEqual(min(v[1] for v in wheel), -max(v[1] for v in wheel), places=6)
        for angle in range(0, 360, 5):
            posed = [design.rotate_x(v, angle) for v in wheel]
            self.assertGreater(min(v[2] for v in posed) + 1800, 180)
            self.assertLess(max(abs(v[1]) for v in posed), 1620)

    def test_roof_pitch_and_transmission_clearances(self):
        roof = dict(design.part_groups("building"))["roof"]
        points = [v for tri in roof for v in tri]
        self.assertGreater(max(v[2] for v in points), 7900)
        self.assertLess(min(v[2] for v in points), 5600)

        # Measure the actual tooth solids, including their installed heights.
        def teeth(part, local_height):
            centers = []
            for material, triangles in design.part_groups(part, True):
                if material != "wood" or len(triangles) != 12:
                    continue
                points = [v for tri in triangles for v in tri]
                center = [
                    (min(v[i] for v in points) + max(v[i] for v in points)) / 2 for i in range(3)
                ]
                if abs(center[2] - local_height) < 0.001:
                    centers.append(center)
            return centers

        spur, pinion = teeth("main", 1260), teeth("runner", -700)
        self.assertEqual(len(spur), 96)
        self.assertEqual(len(pinion), 24)

        def radius(centers):
            return sum(math.hypot(v[0], v[1]) for v in centers) / len(centers)

        pivots = {j["name"]: j["pivot"] for j in design.JOINTS}
        distance = math.dist(pivots["main"][:2], pivots["runner"][:2])
        self.assertAlmostEqual(distance, radius(spur) + radius(pinion), delta=1)
        self.assertAlmostEqual(pivots["main"][2] + spur[0][2], pivots["runner"][2] + pinion[0][2])

    def test_exports_share_geometry_and_regenerate_deterministically(self):
        scad = design.emit_scad()
        solids = sum(len(design.part_groups(n, True)) for n in design.PART_NAMES)
        self.assertEqual(scad.count("polyhedron("), solids)
        self.assertIn("rotate(a=drive_angle, v=[1, 0, 0])", scad)
        scratch = design.ROOT.parents[1] / "build" / "watermill-tests"
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as tmp:
            for name in design.PART_NAMES:
                groups = design.part_groups(name)
                tris = design.part_triangles(name)
                self.assertEqual(len(tris), sum(len(t) for _, t in design.part_groups(name, True)))
                path = Path(tmp) / f"{name}.obj"
                design.write_obj(path, tris, groups)
                digest = hashlib.sha256(path.read_bytes()).hexdigest()
                design.write_obj(path, tris, groups)
                self.assertEqual(digest, hashlib.sha256(path.read_bytes()).hexdigest())
                self.assertEqual(path.read_text(encoding="utf-8").count("\nf "), len(tris))


if __name__ == "__main__":
    unittest.main()
