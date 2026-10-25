"""Profile contracts must inspect build fragments only when actually composed."""

from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import profile_matrix


class ProfileCompositionTests(unittest.TestCase):
    def test_only_included_owner_settings_supply_profile_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "physics").mkdir()
            owner = root / "physics/BuildSettings.cmake"
            owner.write_text("add_library(EVPhysicsCoreCheck OBJECT physics/World.cpp)\n", encoding="utf-8")
            hub = root / "CMakeLists.txt"
            hub.write_text("# no physics inclusion\n", encoding="utf-8")
            self.assertNotIn("EVPhysicsCoreCheck", profile_matrix.module_build_text(root))
            hub.write_text('include("${CMAKE_CURRENT_SOURCE_DIR}/physics/BuildSettings.cmake")\n', encoding="utf-8")
            self.assertIn("EVPhysicsCoreCheck", profile_matrix.module_build_text(root))
            owner.unlink()
            with self.assertRaises(FileNotFoundError):
                profile_matrix.module_build_text(root)
