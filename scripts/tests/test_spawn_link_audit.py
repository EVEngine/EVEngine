"""A trim claim must reject contaminated, incomplete or stale actual build evidence."""

import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import spawn_link_audit


class SpawnLinkAuditTests(unittest.TestCase):
    def fixture(self, directory):
        (directory / "profile/spawn/CMakeFiles/eve_spawn_check.dir").mkdir(parents=True)
        cache = """EVENGINE_PROFILE:STRING=spawn-core-only
EVENGINE_MODULE_LINKAGE:STRING=OBJECT
BUILD_PLATFORM:STRING=macosx
CMAKE_GENERATOR:INTERNAL=Unix Makefiles
CMAKE_NM:FILEPATH=/toolchain/nm
EVE_ENABLED_MODULES:INTERNAL=common;spawn
EVE_MODULE_common_LIB:INTERNAL=EVCommon
EVE_MODULE_spawn_LIB:INTERNAL=EVSpawn
EVE_MODULE_map_LIB:INTERNAL=EVMap
"""
        (directory / "CMakeCache.txt").write_text(cache, encoding="utf-8")
        link = directory / "profile/spawn/CMakeFiles/eve_spawn_check.dir/link.txt"
        link.write_text(
            "c++ ../../src/engine/CMakeFiles/EVCommon.dir/common/Value.cpp.o "
            "../../src/modules/CMakeFiles/EVSpawn.dir/spawn/SpawnWorld.cpp.o -o eve_spawn_check",
            encoding="utf-8",
        )
        (directory / "profile/spawn/eve_spawn_check").write_bytes(b"fixture-binary")
        return link

    def run_audit(self, directory, defined=None, dependency=None):
        if defined is None:
            defined = "000100 T eve::spawn::SpawnWorld::advance(eve::SimulationStep const&)\n"
        if dependency is None:
            dependency = "/tmp/Metal-workspace/eve_spawn_check:\n\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0)\n"
        with mock.patch.object(spawn_link_audit.shutil, "which", return_value="/toolchain/otool"):
            with mock.patch.object(
                spawn_link_audit.subprocess,
                "check_output",
                side_effect=[defined, "U malloc\n", dependency],
            ) as commands:
                result = spawn_link_audit.audit(directory, "spawn-core-only", ["common", "spawn"])
        self.assertEqual(commands.call_count, 3)
        return result

    def test_map_runtime_requires_owning_domain_and_rejects_authoring_symbols(self):
        defined = "\n".join(
            [
                "eve::spawn::SpawnWorld::advance()",
                "eve::map_spawn::PreparedMapSpawn::prepare()",
                "eve::rpg_spawn::RPGSpawnFactory::prepare()",
                "eve::map_rpg_spawn::RPGMapRuntime::advance()",
                "eve::graphics::Graphics::present()",
                "eve::ui::UI::render()",
            ]
        )
        spawn_link_audit.verify_symbols("spawn-map-rpg-runtime", defined, "", "libvulkan.dylib")
        with self.assertRaisesRegex(RuntimeError, "Forbidden"):
            spawn_link_audit.verify_symbols(
                "spawn-map-rpg-runtime",
                defined + "\neve::spawn_editor::SpawnForm::create()",
                "",
                "",
            )
        with self.assertRaisesRegex(RuntimeError, "Missing real implementation"):
            spawn_link_audit.verify_symbols("spawn-map-rts-runtime", defined, "", "")

    def test_ninja_query_selects_link_command_and_uses_root_response_file(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            cache = {
                "CMAKE_GENERATOR": "Ninja",
                "CMAKE_MAKE_PROGRAM": "/toolchain/ninja",
                "EVE_MODULE_spawn_LIB": "EVSpawn",
            }
            response = directory / "objects.rsp"
            response.write_text(
                "src/modules/CMakeFiles/EVSpawn.dir/spawn/SpawnWorld.cpp.o", encoding="utf-8"
            )
            command = "c++ @objects.rsp -o profile/spawn/eve_spawn_check"
            with mock.patch.object(
                spawn_link_audit.subprocess,
                "check_output",
                return_value="c++ -c source.cpp -o source.o\n" + command + "\n",
            ):
                selected = spawn_link_audit.read_link_command(directory, cache)
            self.assertEqual(selected, command)
            self.assertEqual(spawn_link_audit.linked_modules(selected, directory, cache), ["spawn"])
            response.unlink()
            with self.assertRaisesRegex(RuntimeError, "Missing actual linker response"):
                spawn_link_audit.linked_modules(selected, directory, cache)

    def test_actual_object_inputs_and_defined_implementation_are_required(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.fixture(directory)
            report = self.run_audit(directory)
            self.assertTrue(report["ok"])
            self.assertEqual(report["linked_modules"], ["common", "spawn"])
            self.assertEqual(report["dynamic_dependencies"], ["libSystem.B.dylib"])
            self.assertEqual(len(report["binary_sha256"]), 64)
            self.assertTrue((directory / "profile/spawn/boundary/defined-symbols.txt").is_file())

    def test_link_contamination_fails_before_reading_symbols(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            link = self.fixture(directory)
            with link.open("a", encoding="utf-8") as stream:
                stream.write(" ../../src/modules/CMakeFiles/EVMap.dir/map/Map.cpp.o")
            with mock.patch.object(spawn_link_audit.subprocess, "check_output") as commands:
                with self.assertRaisesRegex(RuntimeError, "OBJECT link inputs differ"):
                    spawn_link_audit.audit(directory, "spawn-core-only", ["common", "spawn"])
                commands.assert_not_called()

    def test_an_extra_module_archive_cannot_hide_outside_object_inputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            link = self.fixture(directory)
            with link.open("a", encoding="utf-8") as stream:
                stream.write(" ../../src/modules/libEVMap.a")
            with self.assertRaisesRegex(RuntimeError, "OBJECT link inputs differ"):
                spawn_link_audit.audit(directory, "spawn-core-only", ["common", "spawn"])

    def test_symbol_contamination_removes_stale_success(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.fixture(directory)
            self.run_audit(directory)
            with self.assertRaisesRegex(RuntimeError, "Forbidden.*symbol"):
                self.run_audit(
                    directory, defined="eve::spawn::SpawnWorld::advance()\neve::ui::UI::render()"
                )
            self.assertFalse((directory / "profile/spawn/boundary/report.json").exists())

    def test_empty_stub_or_graphics_dynamic_library_is_not_trim_evidence(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.fixture(directory)
            with self.assertRaisesRegex(RuntimeError, "Missing real implementation"):
                self.run_audit(directory, defined="")
            with self.assertRaisesRegex(RuntimeError, "renderer/window/audio"):
                self.run_audit(
                    directory,
                    dependency="binary:\n\t/opt/libSDL2.dylib (compatibility version 2.0.0)\n",
                )

    def test_profile_mismatch_missing_binary_and_shared_are_not_soft_skips(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.fixture(directory)
            with self.assertRaisesRegex(RuntimeError, "canonical profile/manifest closure"):
                spawn_link_audit.audit(directory, "spawn-core-only", ["common", "spawn", "map"])
            (directory / "profile/spawn/eve_spawn_check").unlink()
            with self.assertRaisesRegex(RuntimeError, "executable missing"):
                spawn_link_audit.audit(directory, "spawn-core-only", ["common", "spawn"])
            cache = directory / "CMakeCache.txt"
            cache.write_text(
                cache.read_text(encoding="utf-8").replace("=OBJECT", "=SHARED"), encoding="utf-8"
            )
            with self.assertRaisesRegex(RuntimeError, "SHARED remains unverified"):
                spawn_link_audit.audit(directory, "spawn-core-only", ["common", "spawn"])
