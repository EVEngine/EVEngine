"""Verify the exporter contract without requiring a TexturePacker license."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import export_sprite_atlas  # noqa: E402


class ExportSpriteAtlasTests(unittest.TestCase):
    def test_preset_and_failure_propagation(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source sprites"
            source.mkdir()
            output = Path(directory) / "output" / "hero.v2"
            arguments = ["export_sprite_atlas.py", str(source), str(output),
                         "--texturepacker", "custom packer"]
            with patch.object(sys, "argv", arguments), patch.object(
                export_sprite_atlas.subprocess, "run",
                return_value=subprocess.CompletedProcess([], 7),
            ) as run:
                self.assertEqual(export_sprite_atlas.main(), 7)
            run.assert_called_once_with([
                "custom packer", "--format", "json", "--disable-rotation",
                "--trim-mode", "Trim", "--extrude", "1",
                "--sheet", str(Path(str(output) + ".png").resolve()),
                "--data", str(Path(str(output) + ".json").resolve()),
                str(source.resolve()),
            ], check=False)
            self.assertTrue(output.parent.is_dir())
