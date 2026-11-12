"""Validate checked-in texture assets against the renderer's packed-normal contract."""
from pathlib import Path
import tempfile
import unittest

from PIL import Image

from generate_materials import SIZE, generate, surface

ROOT = Path(__file__).parent / "materials"


class CabinetMaterialTests(unittest.TestCase):
    def test_channel_contract(self):
        for kind in ("wood", "lacquer", "brushed"):
            maps = {}
            for name in ("albedo", "normal", "roughness", "metallic", "ao", "orm", "packed"):
                with Image.open(ROOT / f"{kind}_{name}.png") as image:
                    self.assertEqual(image.size, (SIZE, SIZE))
                    self.assertEqual(image.mode, "RGBA")
                    raw = image.tobytes()
                    maps[name] = [tuple(raw[i:i + 4]) for i in range(0, len(raw), 4)]
            for n, r, m, a, orm, packed in zip(maps["normal"], maps["roughness"], maps["metallic"], maps["ao"], maps["orm"], maps["packed"]):
                self.assertEqual(orm, (a[0], r[0], m[0], 255))
                self.assertEqual(packed, (n[0], n[1], a[0], 255 - r[0]))
                self.assertEqual(m[0], 255 if kind == "brushed" else 0)
                self.assertTrue(32 <= r[0] <= 200)
                xyz = [c / 255 * 2 - 1 for c in n[:3]]
                self.assertAlmostEqual(sum(c * c for c in xyz), 1.0, delta=0.02)

    def test_seamless_fields(self):
        for kind in ("wood", "lacquer", "brushed"):
            for u, v in ((0.0, 0.0), (0.23, 0.74), (0.91, 0.37)):
                for dx, dy in ((1, 0), (0, 1)):
                    for a, b in zip(surface(kind, u, v), surface(kind, u + dx, v + dy)):
                        self.assertAlmostEqual(a, b, places=10)

    def test_assets_are_reproducible(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            for kind in ("wood", "lacquer", "brushed"):
                generate(kind, output)
            for path in ROOT.glob("*.png"):
                with Image.open(output / path.name) as generated, Image.open(path) as checked_in:
                    self.assertEqual(generated.tobytes(), checked_in.tobytes())


if __name__ == "__main__":
    unittest.main()
