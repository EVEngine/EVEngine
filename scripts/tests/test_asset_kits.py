"""Tests for AI asset-kit catalog + embed freshness."""

from __future__ import annotations

import json
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "scripts"
sys.path.insert(0, str(SCRIPTS))

import asset_kits  # noqa: E402
import embed_asset_kits  # noqa: E402

CATALOG = ROOT / "cmake" / "sdk" / "asset-kits" / "catalog.json"
SCHEMA = ROOT / "cmake" / "sdk" / "asset-kits" / "schema.json"
EMBED = ROOT / "src" / "engine" / "devtools" / "AssetKitsEmbedded.inc"


class AssetKitsTest(unittest.TestCase):
    def test_catalog_validates(self):
        catalog = asset_kits.load_catalog(CATALOG)
        findings = asset_kits.validate_catalog(catalog, SCHEMA)
        self.assertEqual([], findings, findings)

    def test_required_kits_present(self):
        index = asset_kits.kit_index(asset_kits.load_catalog(CATALOG))
        for kit_id in ("card.playing", "card.collectible", "visual-novel.dialogue"):
            self.assertIn(kit_id, index)

    def test_card_playing_face_size(self):
        kit = asset_kits.kit_index(asset_kits.load_catalog(CATALOG))["card.playing"]
        face = next(slot for slot in kit["slots"] if slot["id"] == "face")
        self.assertEqual(face["pixelSize"], {"width": 262, "height": 372})
        self.assertEqual(face["displaySize"], {"width": 110, "height": 150})

    def test_visual_novel_portrait_size(self):
        kit = asset_kits.kit_index(asset_kits.load_catalog(CATALOG))["visual-novel.dialogue"]
        portrait = next(slot for slot in kit["slots"] if slot["id"] == "portrait")
        self.assertEqual(portrait["pixelSize"], {"width": 1024, "height": 1536})
        self.assertEqual(portrait["displaySize"], {"width": 256, "height": 384})
        self.assertEqual(portrait["alpha"], "required")
        background = next(slot for slot in kit["slots"] if slot["id"] == "background")
        self.assertEqual(background["pixelSize"], {"width": 1920, "height": 1080})
        self.assertEqual(background["aspectRatio"], "16:9")

    def test_cli_list_and_get(self):
        list_proc = subprocess.run(
            [sys.executable, str(SCRIPTS / "asset_kits.py"), "--json", "list"],
            cwd=str(ROOT),
            capture_output=True,
            text=True,
            check=False,
            encoding="utf-8",
        )
        self.assertEqual(list_proc.returncode, 0, list_proc.stderr)
        listed = json.loads(list_proc.stdout)
        self.assertTrue(listed["ok"])
        self.assertGreaterEqual(listed["count"], 3)

        get_proc = subprocess.run(
            [sys.executable, str(SCRIPTS / "asset_kits.py"), "--json", "get", "card.collectible"],
            cwd=str(ROOT),
            capture_output=True,
            text=True,
            check=False,
            encoding="utf-8",
        )
        self.assertEqual(get_proc.returncode, 0, get_proc.stderr)
        got = json.loads(get_proc.stdout)
        self.assertTrue(got["ok"])
        self.assertEqual(got["kit"]["id"], "card.collectible")

    def test_embed_matches_catalog(self):
        self.assertTrue(EMBED.is_file(), "AssetKitsEmbedded.inc missing; run embed_asset_kits.py")
        rendered = embed_asset_kits.render_inc(CATALOG.read_text(encoding="utf-8"))
        self.assertEqual(
            EMBED.read_text(encoding="utf-8"),
            rendered,
            "AssetKitsEmbedded.inc is stale; run: python3 scripts/embed_asset_kits.py",
        )
        check = subprocess.run(
            [sys.executable, str(SCRIPTS / "embed_asset_kits.py"), "--check"],
            cwd=str(ROOT),
            capture_output=True,
            text=True,
            check=False,
            encoding="utf-8",
        )
        self.assertEqual(check.returncode, 0, check.stderr)


if __name__ == "__main__":
    unittest.main()
