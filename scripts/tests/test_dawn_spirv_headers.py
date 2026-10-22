"""Exercise native Dawn dependency configuration without network or a GPU."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
REVISION = "0d25db97cb9b8f725e4c95e4553001710e7fc39d"


@unittest.skipUnless(shutil.which("cmake"), "CMake is required")
class DawnSpirvHeadersTests(unittest.TestCase):
    def configure(self, root, *, revision=REVISION, missing_header=False):
        dawn = root / "dawn"
        headers = root / "headers"
        dawn.mkdir()
        headers.mkdir()
        (dawn / "DEPS").write_text(
            "deps = {'third_party/spirv-headers/src': {\n"
            f"'url': '{{chromium_git}}/external/github.com/KhronosGroup/SPIRV-Headers@{revision}'\n"
            "}}\n", encoding="utf-8",
        )
        (headers / "CMakeLists.txt").write_text(
            "add_library(SPIRV-Headers INTERFACE)\n", encoding="utf-8",
        )
        header = headers / "include/spirv/unified1/spirv.h"
        header.parent.mkdir(parents=True)
        if not missing_header:
            header.write_text("// fixture\n", encoding="utf-8")
        (dawn / "CMakeLists.txt").write_text(
            '''if(NOT DAWN_SPIRV_HEADERS_DIR STREQUAL SPIRV-Headers_SOURCE_DIR)
    message(FATAL_ERROR "Dawn and SPIRV-Tools header paths diverged")
endif()
if(NOT EXISTS "${DAWN_SPIRV_HEADERS_DIR}/include/spirv/unified1/spirv.h")
    message(FATAL_ERROR "Dawn configured before SPIRV-Headers was prepared")
endif()
add_subdirectory("${DAWN_SPIRV_HEADERS_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/headers")
add_library(native_dawn INTERFACE)
add_library(dawn::webgpu_dawn ALIAS native_dawn)
''', encoding="utf-8",
        )
        (root / "CMakeLists.txt").write_text(
            f'''cmake_minimum_required(VERSION 3.24)
project(DawnDependencyFixture NONE)
set(EVENGINE_BUILD_HOST ON)
set(BUILD_PLATFORM webgpu)
set(FETCHCONTENT_SOURCE_DIR_DAWN "{dawn.as_posix()}")
set(FETCHCONTENT_SOURCE_DIR_EVE_SPIRV_HEADERS "{headers.as_posix()}")
add_library(eve_engine_includes INTERFACE)
include("{(ROOT / 'cmake/webgpu_backend.cmake').as_posix()}")
load_webgpu_backend()
if(NOT TARGET SPIRV-Headers OR NOT EVENGINE_WEBGPU_LIB STREQUAL "dawn::webgpu_dawn")
    message(FATAL_ERROR "Native Dawn dependency targets were not configured")
endif()
''', encoding="utf-8",
        )
        return subprocess.run(
            ["cmake", "-S", str(root), "-B", str(root / "build")],
            capture_output=True, text=True, encoding="utf-8", check=False,
        )

    def test_prepare_headers_before_dawn_and_spirv_tools(self):
        with tempfile.TemporaryDirectory() as temporary:
            result = self.configure(Path(temporary))
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_incomplete_archive_fails_before_dawn_configuration(self):
        with tempfile.TemporaryDirectory() as temporary:
            result = self.configure(Path(temporary), missing_header=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Incomplete pinned SPIRV-Headers archive", result.stderr)

    def test_unpinned_revision_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            result = self.configure(Path(temporary), revision="main")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("full 40-character commit SHA", result.stderr)


class DawnCiDependencyCheckTests(unittest.TestCase):
    def check_dependencies(self, root):
        workflow = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
        start = workflow.index("          dawn_tp=build/webgpu-macos/")
        end = workflow.index("          for attempt in", start)
        script = workflow[start:end] + "\ndawn_deps_ok\n"
        return subprocess.run(["bash", "-c", script], cwd=root, check=False,
                              capture_output=True, text=True, encoding="utf-8")

    def test_accepts_prepared_headers_outside_dawn_checkout(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for relative in ["dawn-src/third_party/abseil-cpp", "dawn-src/third_party/glfw3/src",
                             "eve_spirv_headers-src"]:
                directory = root / "build/webgpu-macos/_deps" / relative
                directory.mkdir(parents=True)
                (directory / "CMakeLists.txt").write_text("# fixture\n", encoding="utf-8")
            result = self.check_dependencies(root)
            self.assertEqual(result.returncode, 0, result.stderr)
            (root / "build/webgpu-macos/_deps/eve_spirv_headers-src/CMakeLists.txt").unlink()
            self.assertNotEqual(self.check_dependencies(root).returncode, 0)
