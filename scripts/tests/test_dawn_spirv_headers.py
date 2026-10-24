"""Exercise native Dawn dependency configuration without network or a GPU."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import re
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[2]
REVISION = "0d25db97cb9b8f725e4c95e4553001710e7fc39d"


@unittest.skipUnless(shutil.which("cmake"), "CMake is required")
class DawnSpirvHeadersTests(unittest.TestCase):
    def configure(self, root, *, revision=REVISION, missing_header=False,
                  missing_support=None, broken_jinja=False, support_revision=REVISION, unsupported_modules=False):
        dawn = root / "dawn"
        headers = root / "headers"
        dawn.mkdir()
        headers.mkdir()
        (dawn / "DEPS").write_text(
            "deps = {'third_party/spirv-headers/src': {\n"
            f"'url': '{{chromium_git}}/external/github.com/KhronosGroup/SPIRV-Headers@{revision}'\n"
            "}}\n", encoding="utf-8",
        )
        support = root / "support"
        required = {
            "abseil": ["CMakeLists.txt", "absl/strings/string_view.h"],
            "glfw": ["CMakeLists.txt", "include/GLFW/glfw3.h"],
            "jinja2": ["__init__.py", "environment.py", "loaders.py"],
            "markupsafe": ["__init__.py", "_native.py", "_compat.py"],
        }
        urls = {
            "abseil": "chromium/src/third_party/abseil-cpp",
            "glfw": "external/github.com/glfw/glfw",
            "jinja2": "chromium/src/third_party/jinja2",
            "markupsafe": "chromium/src/third_party/markupsafe",
        }
        deps = (dawn / "DEPS").read_text(encoding="utf-8")
        for name, files in required.items():
            for filename in files:
                path = support / name / filename
                path.parent.mkdir(parents=True, exist_ok=True)
                if (name, filename) == missing_support:
                    continue
                text = ""
                if filename == "CMakeLists.txt":
                    text = f"add_library(fixture_{name} INTERFACE)\n"
                elif name == "jinja2" and filename == "__init__.py" and not broken_jinja:
                    text = "class BaseLoader: pass\nclass Environment: pass\n"
                elif name == "markupsafe" and filename == "__init__.py":
                    text = "def escape(value): return value\n"
                path.write_text(text, encoding="utf-8")
            deps += f"# '{{chromium_git}}/{urls[name]}@{support_revision}'\n"
        (dawn / "DEPS").write_text(deps, encoding="utf-8")
        (headers / "CMakeLists.txt").write_text(
            "add_library(SPIRV-Headers INTERFACE)\n", encoding="utf-8",
        )
        header = headers / "include/spirv/unified1/spirv.h"
        header.parent.mkdir(parents=True)
        if not missing_header:
            header.write_text("// fixture\n", encoding="utf-8")
        module_fixture = ""
        if unsupported_modules:
            (dawn / "dawn.cppm").write_text("export module dawn;\n", encoding="utf-8")
            module_fixture = '''
# Model Dawn's successful -fmodules-ts probe on a compiler without CMake's
# module dependency scanner. Its cached result must be disabled by the host.
if(NOT DEFINED DAWN_SUPPORTS_CXX_MODULES)
    set(DAWN_SUPPORTS_CXX_MODULES ON CACHE BOOL "")
endif()
if(DAWN_SUPPORTS_CXX_MODULES)
    add_library(dawncpp_module OBJECT)
    target_sources(dawncpp_module PUBLIC FILE_SET modules TYPE CXX_MODULES FILES dawn.cppm)
    target_compile_features(dawncpp_module PUBLIC cxx_std_20)
endif()
'''
        (dawn / "CMakeLists.txt").write_text(
            '''if(NOT DAWN_SPIRV_HEADERS_DIR STREQUAL SPIRV-Headers_SOURCE_DIR)
    message(FATAL_ERROR "Dawn and SPIRV-Tools header paths diverged")
endif()
if(NOT EXISTS "${DAWN_SPIRV_HEADERS_DIR}/include/spirv/unified1/spirv.h")
    message(FATAL_ERROR "Dawn configured before SPIRV-Headers was prepared")
endif()
foreach(dependency IN ITEMS ABSEIL GLFW JINJA2 MARKUPSAFE)
    if(NOT DEFINED DAWN_${dependency}_DIR)
        message(FATAL_ERROR "Dawn configured before ${dependency} was prepared")
    endif()
endforeach()
add_subdirectory("${DAWN_ABSEIL_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/abseil")
add_subdirectory("${DAWN_GLFW_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/glfw")
add_subdirectory("${DAWN_SPIRV_HEADERS_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/headers")
add_library(native_dawn INTERFACE)
add_library(dawn::webgpu_dawn ALIAS native_dawn)
''' + module_fixture, encoding="utf-8",
        )
        overrides = "\n".join(
            f'set(FETCHCONTENT_SOURCE_DIR_EVE_DAWN_{name.upper()} "{(support / name).as_posix()}")'
            for name in required
        )
        (root / "CMakeLists.txt").write_text(
            f'''cmake_minimum_required(VERSION 3.24)
project(DawnDependencyFixture {"LANGUAGES CXX" if unsupported_modules else "NONE"})
{"set(CMAKE_CXX_SCANDEP_SOURCE)" if unsupported_modules else ""}
set(EVENGINE_BUILD_HOST ON)
set(BUILD_PLATFORM webgpu)
set(FETCHCONTENT_SOURCE_DIR_DAWN "{dawn.as_posix()}")
set(FETCHCONTENT_SOURCE_DIR_EVE_SPIRV_HEADERS "{headers.as_posix()}")
{overrides}
add_library(eve_engine_includes INTERFACE)
include("{(ROOT / 'cmake/webgpu_backend.cmake').as_posix()}")
load_webgpu_backend()
if(NOT TARGET SPIRV-Headers OR NOT EVENGINE_WEBGPU_LIB STREQUAL "dawn::webgpu_dawn")
    message(FATAL_ERROR "Native Dawn dependency targets were not configured")
endif()
''', encoding="utf-8",
        )
        return subprocess.run(
            ["cmake", *(["-G", "Ninja"] if unsupported_modules else []), "-S", str(root), "-B", str(root / "build")],
            capture_output=True, text=True, encoding="utf-8", check=False,
        )

    def test_prepare_headers_before_dawn_and_spirv_tools(self):
        with tempfile.TemporaryDirectory() as temporary:
            result = self.configure(Path(temporary))
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_header_integration_does_not_require_optional_cpp_module_scanner(self):
        with tempfile.TemporaryDirectory() as temporary:
            result = self.configure(Path(temporary), unsupported_modules=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            cache = (Path(temporary) / "build/CMakeCache.txt").read_text(encoding="utf-8")
            self.assertIn("DAWN_SUPPORTS_CXX_MODULES:BOOL=OFF", cache)

    def test_incomplete_archive_fails_before_dawn_configuration(self):
        with tempfile.TemporaryDirectory() as temporary:
            result = self.configure(Path(temporary), missing_header=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Incomplete pinned SPIRV-Headers archive", result.stderr)

    def test_unpinned_revision_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            result = self.configure(Path(temporary), revision="main")
            self.assertNotEqual(result.returncode, 0)
            self.assertRegex(result.stderr, r"full\s+40-character commit SHA")

    def test_incomplete_support_checkouts_fail_before_dawn_configuration(self):
        for dependency, filename in [
            ("abseil", "CMakeLists.txt"),
            ("glfw", "CMakeLists.txt"),
            ("jinja2", "__init__.py"),
            ("markupsafe", "_native.py"),
        ]:
            with self.subTest(dependency=dependency), tempfile.TemporaryDirectory() as temporary:
                result = self.configure(Path(temporary), missing_support=(dependency, filename))
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(f"Incomplete pinned Dawn {dependency} checkout", result.stderr)

    def test_python_namespace_or_empty_package_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            result = self.configure(Path(temporary), broken_jinja=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Pinned Dawn generator dependencies cannot be imported", result.stderr)

    def test_unpinned_support_revision_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            result = self.configure(Path(temporary), support_revision="main")
            self.assertNotEqual(result.returncode, 0)
            self.assertRegex(result.stderr, r"full\s+40-character commit SHA")

    def test_cached_incomplete_archive_is_rejected_on_reconfigure(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            result = self.configure(root)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            (root / "support/glfw/CMakeLists.txt").unlink()
            result = subprocess.run(
                ["cmake", "-S", str(root), "-B", str(root / "build")],
                capture_output=True, text=True, encoding="utf-8", check=False,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Incomplete pinned Dawn glfw checkout", result.stderr)


class DawnCiConfigureTests(unittest.TestCase):
    def check_dependencies(self, root, cmake_status):
        workflow = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
        step = workflow.split("      - name: Configure native Dawn smoke lane\n", 1)[1]
        step = step.split("      - name:", 1)[0]
        script = textwrap.dedent(step.split("        run: |\n", 1)[1])
        script = re.sub(r"\$\{\{.*?\}\}", "", script)
        binaries = root / "bin"
        binaries.mkdir(exist_ok=True)
        cmake = binaries / "cmake"
        cmake.write_text(f"#!/bin/sh\necho attempt >> attempts\nexit {cmake_status}\n", encoding="utf-8")
        cmake.chmod(0o755)
        sleep = binaries / "sleep"
        sleep.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
        sleep.chmod(0o755)
        bash = "bash"
        if os.name == "nt":
            git = shutil.which("git")
            if git is None:
                raise RuntimeError("Git for Windows is required to run the CI Bash contract")
            git_path = Path(git).resolve()
            candidates = [parent / "bin" / "bash.exe" for parent in git_path.parents]
            bash = str(next(path for path in candidates if path.is_file()))
        env = {**os.environ, "PATH": str(binaries) + os.pathsep + os.environ["PATH"]}
        return subprocess.run([bash, "-c", script], cwd=root, env=env, check=False,
                              capture_output=True, text=True, encoding="utf-8")

    def test_cmake_validates_prepared_dependencies_and_failed_configures_are_bounded(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            # Dependency layout is owned and checked by CMake, not CI shell guesses.
            result = self.check_dependencies(root, 0)
            self.assertEqual(result.returncode, 0, result.stderr)
            attempts = root / "attempts"
            self.assertEqual(len(attempts.read_text(encoding="utf-8").splitlines()), 1)
            attempts.unlink()
            result = self.check_dependencies(root, 1)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(len(attempts.read_text(encoding="utf-8").splitlines()), 3)
