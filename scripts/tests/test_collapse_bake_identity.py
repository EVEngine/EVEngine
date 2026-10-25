"""Verify producer cache invalidation with the actual CMake metadata generator."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
GENERATOR = ROOT / "src/modules/physics/backend/bake/BakeBuildIdentity.cmake"


class CollapseBakeIdentityTests(unittest.TestCase):
    def run_cmake(self, *args):
        result = subprocess.run(
            ["cmake", *map(str, args)], capture_output=True, text=True, encoding="utf-8"
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def read_identity(self, root):
        return (root / "build/metadata/CollapseBakeBuildIdentity.h").read_text(encoding="utf-8")

    def test_relocation_stability_and_automatic_source_invalidation(self):
        with tempfile.TemporaryDirectory(prefix="eve-bake-identity-") as directory:
            root = Path(directory) / "first"
            (root / "src/modules/physics").mkdir(parents=True)
            (root / "cmake").mkdir()
            (root / "cmake/third_party_pin.cmake").write_text("# pinned fixture\n", encoding="utf-8")
            source = root / "src/modules/physics/Solver.cpp"
            source.write_text("// initial solver\n", encoding="utf-8")
            (root / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 3.22)\n'
                'project(BakeIdentityFixture LANGUAGES NONE)\n'
                'set(EVENGINE_THIRD_PARTY_PIN "fixture-pin")\n'
                'set(EVENGINE_TP_DESC "fixture-dependency")\n'
                'option(EVENGINE_BUILD_JOLT "fixture option" ON)\n'
                f'include("{GENERATOR.as_posix()}")\n'
                'eve_configure_collapse_bake_identity("${CMAKE_BINARY_DIR}/metadata")\n',
                encoding="utf-8",
            )
            other = Path(directory) / "relocated"
            shutil.copytree(root, other)
            self.run_cmake("-S", root, "-B", root / "build")
            initial = self.read_identity(root)
            self.run_cmake("-S", root, "-B", root / "build")
            self.assertEqual(initial, self.read_identity(root))
            self.run_cmake("-S", other, "-B", other / "build")
            self.assertEqual(initial, self.read_identity(other))
            source.write_text("// uncommitted algorithm change\n", encoding="utf-8")
            # An ordinary build, without manually requesting configure, must refresh the identity.
            self.run_cmake("--build", root / "build")
            changed = self.read_identity(root)
            self.assertNotEqual(initial, changed)
            self.run_cmake("-S", root, "-B", root / "build", "-DEVENGINE_BUILD_JOLT=OFF")
            self.assertNotEqual(changed, self.read_identity(root))
            configured = self.read_identity(root)
            (root / "src/modules/physics/NewAlgorithm.h").write_text("// new source\n", encoding="utf-8")
            self.run_cmake("--build", root / "build")
            self.assertNotEqual(configured, self.read_identity(root))
