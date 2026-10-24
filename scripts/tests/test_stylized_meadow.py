"""Deployment contracts for the code-generated default grass style."""

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class StylizedMeadowTests(unittest.TestCase):
    def test_example_distributes_the_canonical_style_without_drift(self):
        canonical = ROOT / "assets/styles/stylized-grass"
        packaged = ROOT / "examples/stylized-meadow/styles/stylized-grass"
        files = [p.relative_to(canonical) for p in canonical.rglob("*") if p.is_file()]
        self.assertTrue(files)
        self.assertEqual(
            set(files), {p.relative_to(packaged) for p in packaged.rglob("*") if p.is_file()}
        )
        for relative in files:
            with self.subTest(file=str(relative)):
                self.assertEqual(
                    (canonical / relative).read_bytes(), (packaged / relative).read_bytes()
                )

    def test_deployment_has_code_only_and_no_offline_grass_resources(self):
        for folder in [ROOT / "assets/styles/stylized-grass", ROOT / "examples/stylized-meadow"]:
            for path in folder.rglob("*"):
                if path.is_file() and "__pycache__" not in path.parts:
                    with self.subTest(file=str(path)):
                        self.assertIn(path.suffix, {".nut", ".md"})
