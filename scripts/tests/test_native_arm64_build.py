"""Exercise native architecture rejection and dependency cache isolation."""

from pathlib import Path
import subprocess
import tempfile
import unittest

from scripts import utf8_stdio

ROOT = Path(__file__).resolve().parents[2]


class NativeArm64BuildTests(unittest.TestCase):
    def configure(self, source):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "CMakeLists.txt").write_text(source, encoding="utf-8")
            return subprocess.run(
                ["cmake", "-G", "Ninja", "-S", str(root), "-B", str(root / "build")],
                capture_output=True,
                encoding="utf-8",
                errors="replace",
                check=False,
            )

    def test_rejects_non_native_architectures(self):
        for host, target, cross in (
            ("x86_64", "x86_64", "OFF"),
            ("x86_64", "aarch64", "ON"),
            ("aarch64", "x86_64", "OFF"),
        ):
            with self.subTest(host=host, target=target, cross=cross):
                result = self.configure(
                    f"""
cmake_minimum_required(VERSION 3.21)
project(NativeGuard NONE)
set(CMAKE_HOST_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_HOST_SYSTEM_PROCESSOR {host})
set(CMAKE_SYSTEM_PROCESSOR {target})
set(CMAKE_CROSSCOMPILING {cross})
set(EVENGINE_NATIVE_ARM64 ON CACHE BOOL "")
include("{(ROOT / 'cmake/native_arm64.cmake').as_posix()}")
"""
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("requires a native Windows/Linux ARM64", result.stderr)

    def test_dependency_trees_are_separate(self):
        for platform in ("win32", "linux", "macosx"):
            for build_type, suffix in (("Release", ""), ("Debug", "-debug")):
                for native in ("ON", "OFF"):
                    architecture = "x86_64" if platform == "macosx" else "arm64"
                    option = "EVENGINE_NATIVE_MACOS_X86_64" if platform == "macosx" else "EVENGINE_NATIVE_ARM64"
                    expected = platform + (f"-{architecture}" if native == "ON" else "") + suffix
                    with self.subTest(expected=expected):
                        result = self.configure(
                            f"""
cmake_minimum_required(VERSION 3.21)
project(DependencyTree NONE)
include("{(ROOT / 'cmake/third_party_build.cmake').as_posix()}")
function(ensure_third_party_checkout)
    file(MAKE_DIRECTORY "${{CMAKE_SOURCE_DIR}}/third-party/cmake")
    file(WRITE "${{CMAKE_SOURCE_DIR}}/third-party/cmake/apply_patches.cmake"
        "# No vendored sources in this directory-isolation fixture.\n")
endfunction()
function(check_third_party_project name repo)
    if(NOT TP_BUILD_PATH STREQUAL "{expected}")
        message(FATAL_ERROR "Wrong dependency tree: ${{TP_BUILD_PATH}}")
    endif()
    add_custom_target(third-party)
endfunction()
set(BUILD_PLATFORM {platform})
set(CMAKE_BUILD_TYPE {build_type})
set({option} {native})
if(BUILD_PLATFORM STREQUAL "macosx" AND {native})
    set(CMAKE_OSX_ARCHITECTURES x86_64)
endif()
set(EVENGINE_THIRD_PARTY_PIN 0123456789abcdef)
file(MAKE_DIRECTORY "${{CMAKE_SOURCE_DIR}}/cmake")
configure_file("{(ROOT / 'cmake/third_party_release.cmake').as_posix()}"
    "${{CMAKE_SOURCE_DIR}}/cmake/third_party_release.cmake" COPYONLY)
set(EVENGINE_THIRD_PARTY_MODE SOURCE)
load_third_party()
"""
                        )
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    unittest.main()
