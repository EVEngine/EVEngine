"""Tests for build metadata and documentation split-file discovery."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import check_bindings  # noqa: E402
import check_module_manifest  # noqa: E402


class SplitMetadataSourceTests(unittest.TestCase):
    def test_manifest_fragments_follow_entrypoint_include_order(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fragments = root / "module_manifest"
            fragments.mkdir()
            (fragments / "later.cmake").write_text(
                "# L2 -- later\neve_declare_module(NAME later LAYER 2)\n",
                encoding="utf-8",
            )
            (fragments / "earlier.cmake").write_text(
                "# L1 -- earlier\neve_declare_module(NAME earlier LAYER 1)\n",
                encoding="utf-8",
            )
            entrypoint = root / "module_manifest.cmake"
            entrypoint.write_text(
                "include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/earlier.cmake)\n"
                "include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/later.cmake)\n",
                encoding="utf-8",
            )

            declarations = check_module_manifest.parse_manifest(entrypoint)

            self.assertEqual(["earlier", "later"], [item.name for item in declarations])

    def test_module_doc_includes_topic_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            overview = root / "graphics.md"
            topics = root / "graphics"
            topics.mkdir()
            overview.write_text("overviewBinding()\n", encoding="utf-8")
            (topics / "effects.md").write_text("topicBinding()\n", encoding="utf-8")

            text = check_bindings.doc_text(overview)

            self.assertIn("overviewBinding()", text)
            self.assertIn("topicBinding()", text)


if __name__ == "__main__":
    unittest.main()
