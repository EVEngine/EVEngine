"""Exercise Intel macOS host rejection and independent build entry points."""

from pathlib import Path
import subprocess
import tempfile
import unittest

from scripts import utf8_stdio

ROOT = Path(__file__).resolve().parents[2]


class NativeMacosX8664BuildTests(unittest.TestCase):
    def test_rejects_wrong_host_target_and_universal_builds(self):
        for host_os, target_os, host, target, architectures, cross, platform in (
            ("Linux", "Linux", "x86_64", "x86_64", "x86_64", "OFF", "macosx"),
            ("Darwin", "Darwin", "arm64", "x86_64", "x86_64", "OFF", "macosx"),
            ("Darwin", "Darwin", "x86_64", "arm64", "arm64", "OFF", "macosx"),
            ("Darwin", "Darwin", "x86_64", "x86_64", "arm64;x86_64", "OFF", "macosx"),
            ("Darwin", "Darwin", "x86_64", "x86_64", "x86_64", "ON", "macosx"),
            ("Darwin", "Linux", "x86_64", "x86_64", "x86_64", "OFF", "macosx"),
            ("Darwin", "Darwin", "x86_64", "x86_64", "x86_64", "OFF", "linux"),
        ):
            with self.subTest(host_os=host_os, target_os=target_os, host=host,
                              target=target, architectures=architectures, cross=cross, platform=platform):
                with tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    (root / "CMakeLists.txt").write_text(
                        f"""
cmake_minimum_required(VERSION 3.21)
project(NativeGuard NONE)
set(CMAKE_HOST_SYSTEM_NAME {host_os})
set(CMAKE_SYSTEM_NAME {target_os})
set(CMAKE_HOST_SYSTEM_PROCESSOR {host})
set(CMAKE_SYSTEM_PROCESSOR {target})
set(CMAKE_OSX_ARCHITECTURES "{architectures}")
set(CMAKE_CROSSCOMPILING {cross})
set(EVENGINE_BUILD_PLATFORM {platform})
set(EVENGINE_NATIVE_MACOS_X86_64 ON CACHE BOOL "")
include("{(ROOT / 'cmake/native_macos_x86_64.cmake').as_posix()}")
""", encoding="utf-8")
                    result = subprocess.run(
                        ["cmake", "-G", "Ninja", "-S", str(root), "-B", str(root / "build")],
                        capture_output=True, encoding="utf-8", errors="replace", check=False,
                    )
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("requires a native Intel macOS host and target", result.stderr)

    def test_make_builds_matching_dependencies_before_engine(self):
        for suffix, configuration in (("", "Release"), ("-debug", "Debug")):
            with self.subTest(configuration=configuration):
                result = subprocess.run(
                    ["make", "-n", f"build/macosx-x86_64{suffix}", "JOBS=4"],
                    cwd=ROOT, capture_output=True, encoding="utf-8", errors="replace", check=False,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                commands = [line for line in result.stdout.splitlines() if line.startswith("cmake ")]
                self.assertEqual(len(commands), 3, result.stdout)
                self.assertIn(f"-DCMAKE_BUILD_TYPE={configuration}", commands[0])
                self.assertIn("-DCMAKE_OSX_ARCHITECTURES=x86_64", commands[0])
                self.assertIn("-DCMAKE_OSX_DEPLOYMENT_TARGET=13.3", commands[0])
                self.assertIn("-DEVENGINE_NATIVE_MACOS_X86_64=ON", commands[0])
                self.assertIn(f"-B build/macosx-x86_64{suffix}", commands[0])
                self.assertEqual(commands[1], f"cmake --build build/macosx-x86_64{suffix} --target deps -j 4")
                self.assertEqual(commands[2], f"cmake --build build/macosx-x86_64{suffix} -j 4")


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    unittest.main()
