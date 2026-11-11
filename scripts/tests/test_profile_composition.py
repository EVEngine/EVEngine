"""Profile contracts must inspect build fragments only when actually composed."""

from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import profile_matrix


class ProfileCompositionTests(unittest.TestCase):
    def test_unconditional_graphics_text_api_pulls_its_actual_font_implementation(self):
        modules, _ = profile_matrix.resolve("minimal")
        self.assertIn("font", modules)
        graphics = next(item for item in profile_matrix.contracts() if item.name == "graphics")
        self.assertIn("font", graphics.deps)
        self.assertNotIn("font", profile_matrix.resolve("spawn-core-only")[0])

    def test_map_runtime_profiles_keep_the_requested_domain_and_exclude_authoring(self):
        for profile, root in profile_matrix.MAP_RUNTIME_ROOTS.items():
            with self.subTest(profile=profile):
                modules, _ = profile_matrix.resolve(profile)
                self.assertIn(root, modules)
                self.assertIn("map_spawn", modules)
                self.assertIn("graphics", modules)
                self.assertIn("window", modules)
                self.assertFalse(set(modules) & {"editing", "editor", "spawn_editing", "spawn_editor", "map_rpg_spawn_editor"})
                self.assertFalse(any(profile_matrix.is_editor_module(name) for name in modules))
                opposite = "map_rts_spawn" if root == "map_rpg_spawn" else "map_rpg_spawn"
                self.assertNotIn(opposite, modules)

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
