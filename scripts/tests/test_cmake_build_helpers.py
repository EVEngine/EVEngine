"""Configure and build the real module helpers without engine dependencies."""

import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

from scripts import utf8_stdio

ROOT = Path(__file__).resolve().parents[2]


class BuildHelperTests(unittest.TestCase):
    def run_cmake(self, *args):
        command = ["cmake", *map(str, args)]
        if os.name == "nt":
            command = [str(ROOT / "cmake" / "with-msvc.cmd"), *command]
        return subprocess.run(command, capture_output=True, encoding="utf-8", errors="replace")

    def test_arm64_monolithic_test_linker_is_scoped_to_linux_gcc(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source"
            build = Path(directory) / "build"
            source.mkdir()
            (source / "main.cpp").write_text("int main() { return 0; }\n", encoding="utf-8")
            cases = (
                ("linux_aarch64_gcc", "Linux", "aarch64", "GNU", True),
                ("linux_arm64_gcc", "Linux", "arm64", "GNU", True),
                ("linux_x64_gcc", "Linux", "x86_64", "GNU", False),
                ("linux_arm64_clang", "Linux", "aarch64", "Clang", False),
                ("mac_arm64_gcc", "Darwin", "arm64", "GNU", False),
                ("win_arm64_msvc", "Windows", "ARM64", "MSVC", False),
            )
            checks = []
            for name, system, processor, compiler, expected in cases:
                checks.append(f"""
set(CMAKE_SYSTEM_NAME {system})
set(CMAKE_SYSTEM_PROCESSOR {processor})
set(CMAKE_CXX_COMPILER_ID {compiler})
add_executable({name} main.cpp)
eve_configure_monolithic_test_linker({name})
get_target_property(options {name} LINK_OPTIONS)
if({"NOT " if expected else ""}"-fuse-ld=lld" IN_LIST options)
    message(FATAL_ERROR "Unexpected linker policy for {name}: ${{options}}")
endif()
get_target_property(compiler_options {name} COMPILE_OPTIONS)
if(compiler_options)
    message(FATAL_ERROR "Linker policy changed compiler options")
endif()
""")
            (source / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.21)\n"
                "project(TestLinkerPolicy LANGUAGES CXX)\n"
                f'include("{(ROOT / "cmake/compiler_options.cmake").as_posix()}")\n'
                + "".join(checks),
                encoding="utf-8",
            )
            configured = self.run_cmake("-G", "Ninja", "-S", source, "-B", build)
            self.assertEqual(configured.returncode, 0, configured.stdout + configured.stderr)
        test_cmake = (ROOT / "test/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("eve_configure_monolithic_test_linker(unit_test)", test_cmake)
        workflow = (ROOT / ".github/workflows/release-ci.yml").read_text(encoding="utf-8")
        arm_lane = re.split(r"^  [a-z0-9_-]+:", workflow.split("  linux-arm64:\n", 1)[1],
                            maxsplit=1, flags=re.MULTILINE)[0]
        self.assertIn("build-essential cmake ninja-build lld", arm_lane)
        self.assertIn("CC: gcc", arm_lane)
        self.assertIn("CXX: g++", arm_lane)

    def test_module_targets_and_generated_headers(self):
        for linkage in ("OBJECT", "SHARED"):
            with self.subTest(linkage=linkage), tempfile.TemporaryDirectory() as directory:
                source = Path(directory) / "source with spaces"
                build = Path(directory) / "build with spaces"
                (source / "cmake").mkdir(parents=True)
                (source / "sample" / "excluded").mkdir(parents=True)
                for name in ("scan_source.cmake", "collect_module_sources.cmake"):
                    shutil.copyfile(ROOT / "cmake" / name, source / "cmake" / name)
                (source / "sample" / "keep.cpp").write_text(
                    "int sample() { return 7; }\n", encoding="utf-8"
                )
                (source / "sample" / "excluded" / "bad.cpp").write_text(
                    "#error excluded source\n", encoding="utf-8"
                )
                (source / "input.in").write_text("value=@VALUE@\n", encoding="utf-8")
                (source / "CMakeLists.txt").write_text(
                    f"""
cmake_minimum_required(VERSION 3.21)
project(HelperContract LANGUAGES CXX)
include("{(ROOT / "cmake/module_targets.cmake").as_posix()}")
include("{(ROOT / "cmake/compiler_options.cmake").as_posix()}")
include("{(ROOT / "cmake/engine_usage.cmake").as_posix()}")
add_library(eve_engine_includes INTERFACE)
set(EVENGINE_PROFILE_HOSTLESS ON)
set(EVENGINE_MODULE_LINKAGE {linkage})
set(EVE_MODULE_sample_LINK_GROUP EVFoundation)
set(EVENGINE_EXCLUDED_BACKEND_SUBDIRS excluded)
function(eve_module_scan_dir name out)
    set(${{out}} "${{name}}" PARENT_SCOPE)
endfunction()
function(eve_module_nested_scan_excludes name out)
    set(${{out}} "" PARENT_SCOPE)
endfunction()
set(module_src sentinel)
create_module(Sample sample)
if(NOT module_src STREQUAL "sentinel")
    message(FATAL_ERROR "Module scan leaked into caller")
endif()
get_target_property(pic Sample POSITION_INDEPENDENT_CODE)
get_target_property(definitions Sample COMPILE_DEFINITIONS)
if(EVENGINE_MODULE_LINKAGE STREQUAL "SHARED")
    if(NOT pic OR NOT "EVENGINE_EXPORTS_FOUNDATION" IN_LIST definitions)
        message(FATAL_ERROR "Shared module lost its export/PIC contract")
    endif()
endif()
set(VALUE 7)
eve_configure_file_if_changed(input.in "${{CMAKE_BINARY_DIR}}/generated/value.h")
eve_configure_strict_warnings(eve_engine_includes)
eve_configure_ipo()
if(EVENGINE_ENABLE_IPO AND NOT CMAKE_INTERPROCEDURAL_OPTIMIZATION)
    message(FATAL_ERROR "IPO setting did not reach target creation scope")
endif()
""",
                    encoding="utf-8",
                )
                configured = self.run_cmake(
                    "-G", "Ninja", "-S", source, "-B", build, "-DEVENGINE_ENABLE_STRICT_WARNINGS=ON"
                )
                self.assertEqual(configured.returncode, 0, configured.stdout + configured.stderr)
                compiled = self.run_cmake("--build", build, "--target", "Sample")
                self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
                header = build / "generated" / "value.h"
                stamp = header.stat().st_mtime_ns
                configured = self.run_cmake("-S", source, "-B", build, "-DEVENGINE_ENABLE_IPO=ON")
                self.assertEqual(configured.returncode, 0, configured.stdout + configured.stderr)
                self.assertEqual(header.stat().st_mtime_ns, stamp)
                self.assertEqual(header.read_text(encoding="utf-8"), "value=7\n")
                # A previous successful source list must not mask a later scan failure.
                (source / "cmake" / "scan_source.cmake").write_text(
                    'message(FATAL_ERROR "injected scan failure")\n', encoding="utf-8"
                )
                failed = self.run_cmake("-S", source, "-B", build)
                self.assertNotEqual(failed.returncode, 0)
                self.assertIn("Source scan failed for module", failed.stderr)


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    unittest.main()
