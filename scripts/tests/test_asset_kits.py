"""Tests for AI asset-kit catalog, API envelope, overlays, and embed freshness."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "scripts"
sys.path.insert(0, str(SCRIPTS))

import asset_kits  # noqa: E402
import embed_asset_kits  # noqa: E402

CATALOG = ROOT / "cmake" / "sdk" / "asset-kits" / "catalog.json"
SCHEMA = ROOT / "cmake" / "sdk" / "asset-kits" / "schema.json"
PROTOCOL = ROOT / "cmake" / "sdk" / "asset-kits" / "protocol.json"
EMBED = ROOT / "src" / "engine" / "devtools" / "AssetKitsEmbedded.inc"


class AssetKitsTest(unittest.TestCase):
    def test_catalog_validates(self) -> None:
        catalog = asset_kits.load_catalog(CATALOG)
        findings = asset_kits.validate_catalog(catalog, SCHEMA)
        self.assertEqual(findings, [])

    def test_protocol_document_present(self) -> None:
        data = json.loads(PROTOCOL.read_text(encoding="utf-8"))
        self.assertEqual(data["api"], "eve.asset-kit.api")
        self.assertEqual(data["apiVersion"], 1)
        for op in ("list", "get", "resolve", "validate", "sources"):
            self.assertIn(op, data["operations"])

    def test_required_kits_present(self) -> None:
        index = asset_kits.kit_index(asset_kits.load_catalog(CATALOG))
        for kid in ("card.playing", "card.collectible", "visual-novel.dialogue"):
            self.assertIn(kid, index)
            self.assertTrue(index[kid].get("tags"))
            self.assertTrue(index[kid].get("bindings"))

    def test_card_playing_face_size(self) -> None:
        kit = asset_kits.kit_index(asset_kits.load_catalog(CATALOG))["card.playing"]
        face = next(s for s in kit["slots"] if s["id"] == "face")
        self.assertEqual(face["pixelSize"], {"width": 262, "height": 372})
        self.assertEqual(face["displaySize"], {"width": 110, "height": 150})

    def test_visual_novel_portrait_size(self) -> None:
        kit = asset_kits.kit_index(asset_kits.load_catalog(CATALOG))["visual-novel.dialogue"]
        portrait = next(s for s in kit["slots"] if s["id"] == "portrait")
        self.assertEqual(portrait["pixelSize"], {"width": 1024, "height": 1536})
        self.assertEqual(portrait["alpha"], "required")

    def test_store_list_filters_and_envelope(self) -> None:
        store = asset_kits.CatalogStore.open(base=CATALOG, extras=[], include_env_extras=False)
        payload = store.list(genre="card", tag="ccg")
        self.assertTrue(payload["ok"])
        self.assertEqual(payload["api"], "eve.asset-kit.api")
        self.assertEqual(payload["apiVersion"], 1)
        ids = {k["id"] for k in payload["kits"]}
        self.assertEqual(ids, {"card.collectible"})

    def test_resolve_fills_path_and_reports_unresolved(self) -> None:
        store = asset_kits.CatalogStore.open(base=CATALOG, extras=[], include_env_extras=False)
        payload = store.resolve(
            "card.playing",
            "face",
            {"suit": "hearts", "rank": "A"},
        )
        self.assertTrue(payload["ok"])
        job = payload["job"]
        self.assertEqual(job["path"], "assets/playing-cards/hearts_A.png")
        self.assertEqual(job["pixelSize"], {"width": 262, "height": 372})
        self.assertIn("style", job["unresolved"])
        self.assertTrue(any(b.get("system") == "pipeline" for b in job["bindings"]))

    def test_overlay_replaces_kit_by_id(self) -> None:
        overlay = {
            "schema": "eve.asset-kit.catalog",
            "schemaVersion": 1,
            "kits": [
                {
                    "id": "card.playing",
                    "genre": "card",
                    "title": "Overlay playing deck",
                    "summary": "Company-specific playing-card sizes for internal pipeline.",
                    "tags": ["card", "acme"],
                    "modules": ["card"],
                    "window": {"width": 1280, "height": 720},
                    "slots": [
                        {
                            "id": "face",
                            "role": "full-card-face",
                            "required": True,
                            "pixelSize": {"width": 300, "height": 420},
                            "aspectRatio": "5:7",
                            "format": "png",
                            "colorType": "rgba",
                            "alpha": "optional",
                            "pathPattern": "assets/acme/{suit}_{rank}.png",
                            "constraints": ["Overlay face"],
                        }
                    ],
                }
            ],
        }
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "catalog.json"
            path.write_text(json.dumps(overlay), encoding="utf-8")
            store = asset_kits.CatalogStore.open(
                base=CATALOG, extras=[path], include_env_extras=False
            )
            listed = store.list(tag="acme")
            self.assertEqual(listed["count"], 1)
            got = store.get("card.playing")
            self.assertTrue(got["ok"])
            self.assertEqual(got["source"], "overlay-1")
            self.assertEqual(got["kit"]["slots"][0]["pixelSize"]["width"], 300)
            sources = {s["id"] for s in store.source_dicts()}
            self.assertEqual(sources, {"engine", "overlay-1"})

    def test_cli_list_and_get(self) -> None:
        listed = subprocess.run(
            [sys.executable, str(SCRIPTS / "asset_kits.py"), "--json", "list", "--genre", "visual-novel"],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
        payload = json.loads(listed.stdout)
        self.assertTrue(payload["ok"])
        self.assertEqual(payload["api"], "eve.asset-kit.api")
        self.assertEqual(payload["count"], 1)

        got = subprocess.run(
            [sys.executable, str(SCRIPTS / "asset_kits.py"), "--json", "get", "card.collectible"],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
        doc = json.loads(got.stdout)
        self.assertTrue(doc["ok"])
        self.assertEqual(doc["kit"]["id"], "card.collectible")

    def test_cli_resolve(self) -> None:
        resolved = subprocess.run(
            [
                sys.executable,
                str(SCRIPTS / "asset_kits.py"),
                "--json",
                "resolve",
                "visual-novel.dialogue",
                "portrait",
                "--vars",
                json.dumps(
                    {
                        "characterId": "hero",
                        "characterDescription": "traveler",
                        "facing": "left",
                    }
                ),
            ],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
        payload = json.loads(resolved.stdout)
        self.assertTrue(payload["ok"])
        self.assertEqual(payload["job"]["path"], "assets/hero.png")
        self.assertEqual(payload["job"]["pixelSize"]["width"], 1024)

    def test_embed_matches_catalog(self) -> None:
        self.assertTrue(EMBED.is_file(), "AssetKitsEmbedded.inc missing; run embed_asset_kits.py")
        rendered = embed_asset_kits.render_inc(CATALOG.read_text(encoding="utf-8"))
        self.assertEqual(
            EMBED.read_text(encoding="utf-8"),
            rendered,
            "AssetKitsEmbedded.inc is stale; run: python3 scripts/embed_asset_kits.py",
        )
        check = subprocess.run(
            [sys.executable, str(SCRIPTS / "embed_asset_kits.py"), "--check"],
            cwd=ROOT,
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
        self.assertEqual(check.returncode, 0, check.stderr)


if __name__ == "__main__":
    import utf8_stdio

    utf8_stdio.enable_utf8_stdio()
    unittest.main()
