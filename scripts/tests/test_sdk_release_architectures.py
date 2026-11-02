"""Release completeness, CPU mismatch rejection and SDK build routing."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import verify_sdk_release as release  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]


class SdkReleaseArchitectureTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def write_archive(self, target, *, architecture=None, version="1.2.3"):
        path = self.root / f"eve-sdk-{target}-v1.2.3.zip"
        with zipfile.ZipFile(path, "w") as archive:
            root = f"eve-sdk/{target}/share/eve"
            archive.writestr(f"{root}/VERSION", version)
            archive.writestr(f"{root}/TARGET_PLATFORM", target.removesuffix("-arm64").removesuffix("-x86_64"))
            if target.startswith("macosx-"):
                archive.writestr(f"{root}/TARGET_ARCHITECTURE", architecture or target.removeprefix("macosx-"))
            if target == "webgpu":
                for filename in ("eve.html", "eve.js", "eve.wasm", "eve.data"):
                    archive.writestr(f"eve-sdk/webgpu/bin/{filename}", "fixture")
        return path

    def complete_release(self):
        for target in release.SDK_TARGETS:
            self.write_archive(target)

    def test_accepts_complete_release_with_two_macos_architectures(self):
        self.complete_release()
        release.verify(self.root, "v1.2.3")

    def test_rejects_missing_intel_even_if_an_extra_archive_keeps_the_count(self):
        self.complete_release()
        (self.root / "eve-sdk-macosx-x86_64-v1.2.3.zip").unlink()
        self.write_archive("macosx")
        with self.assertRaisesRegex(ValueError, "archive set mismatch"):
            release.verify(self.root, "v1.2.3")

    def test_requires_windows_and_linux_arm64_releases(self):
        self.complete_release()
        for target in ("win32-arm64", "linux-arm64"):
            with self.subTest(target=target):
                archive = self.root / f"eve-sdk-{target}-v1.2.3.zip"
                archive.unlink()
                with self.assertRaisesRegex(ValueError, target):
                    release.verify(self.root, "v1.2.3")
                self.write_archive(target)

    def test_rejects_wrong_architecture_and_version(self):
        self.complete_release()
        self.write_archive("macosx-x86_64", architecture="arm64")
        with self.assertRaisesRegex(ValueError, "architecture mismatch"):
            release.verify(self.root, "v1.2.3")
        self.write_archive("macosx-x86_64", version="0.0.1")
        with self.assertRaisesRegex(ValueError, "version/platform mismatch"):
            release.verify(self.root, "v1.2.3")

    def test_requires_webgpu_and_its_preloaded_game_data(self):
        self.complete_release()
        webgpu = self.root / "eve-sdk-webgpu-v1.2.3.zip"
        webgpu.unlink()
        with self.assertRaisesRegex(ValueError, "webgpu"):
            release.verify(self.root, "v1.2.3")
        self.write_archive("webgpu")
        with zipfile.ZipFile(webgpu) as archive:
            entries = {name: archive.read(name) for name in archive.namelist() if not name.endswith("eve.data")}
        with zipfile.ZipFile(webgpu, "w") as archive:
            for name, value in entries.items():
                archive.writestr(name, value)
        with self.assertRaisesRegex(KeyError, "eve.data"):
            release.verify(self.root, "v1.2.3")

    def test_sdk_targets_install_the_matching_build(self):
        for architecture in ("arm64", "x86_64"):
            for suffix in ("", "-debug"):
                target = f"macosx-{architecture}{suffix}"
                with self.subTest(target=target):
                    result = subprocess.run(["make", "-n", f"sdk/{target}"], cwd=ROOT,
                                            capture_output=True, encoding="utf-8", check=False)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertIn(f"-DCMAKE_OSX_ARCHITECTURES={architecture}", result.stdout)
                    self.assertIn("-DCMAKE_OSX_DEPLOYMENT_TARGET=13.3", result.stdout)
                    self.assertIn(f"cmake --install build/{target} --prefix dist/eve-sdk/{target}", result.stdout)

    def test_source_install_uses_resolved_architecture_prefix(self):
        # Exercise the real install rules without compiling the engine or fetching dependencies.
        source = self.root / "source"
        build = self.root / "build"
        for directory in (
            "src/engine/common", "external/zeroerr/include/zeroerr", "examples/basic",
            "assets/styles", "tools/eve-mcp", ".cursor/skills/evescript", "cmake/sdk",
        ):
            (source / directory).mkdir(parents=True, exist_ok=True)
        for filename in ("LICENSE", "LICENSE-COMMERCIAL", "LICENSE-OPENSOURCE",
                         ".cursor/skills/evescript/SKILL.md", "cmake/sdk/llms.txt"):
            (source / filename).write_text("fixture", encoding="utf-8")
        for filename in ("EVEngineConfig.cmake.in", "EVEnginePlugin.cmake"):
            shutil.copyfile(ROOT / "cmake" / filename, source / "cmake" / filename)
        (source / "cmake/timidity_share.cmake").write_text(
            "function(eve_install_timidity_share)\nendfunction()\n", encoding="utf-8")
        # Vulkan bundling is tested separately with the SDK architecture checker.
        (source / "cmake/macosx_bundle_vulkan.cmake").write_text("# fixture\n", encoding="utf-8")
        for filename in ("config.h", "eve-api.json", "eve-api.d.ts"):
            path = build / "src/engine/common" / filename
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("fixture", encoding="utf-8")
        (source / "third-party/simplesquirrel/include/simplesquirrel").mkdir(parents=True)
        (source / "main.cpp").write_text("int main() { return 0; }\n", encoding="utf-8")
        (source / "CMakeLists.txt").write_text(f"""
cmake_minimum_required(VERSION 3.21)
project(SdkInstallFixture CXX)
add_executable(eve main.cpp)
set(EVENGINE_NATIVE_TARGET eve)
set(BUILD_PLATFORM macosx)
set(CMAKE_OSX_ARCHITECTURES x86_64)
set(CMAKE_BUILD_TYPE Debug)
set(EVENGINE_MAJOR_VERSION 1)
set(EVENGINE_MINOR_VERSION 2)
set(EVENGINE_PATCH_VERSION 3)
set(EVE_ENABLED_MODULES "")
include("{(ROOT / 'cmake/install_sdk.cmake').as_posix()}")
""", encoding="utf-8")
        dependency = self.root / "resolved x86_64 dependency prefix"
        (dependency / "lib").mkdir(parents=True)
        (dependency / "include").mkdir()
        (dependency / "lib/libfixture.dylib").write_text("x86_64", encoding="utf-8")
        (dependency / "include/squirrel.h").write_text("x86_64 header", encoding="utf-8")
        installed = self.root / "sdk"
        for command in (
            ["cmake", "-G", "Ninja", "-S", str(source), "-B", str(build),
             f"-DEVENGINE_TP_INSTALL_PREFIX={dependency}"],
            ["cmake", "--build", str(build)],
            ["cmake", "--install", str(build), "--prefix", str(installed)],
        ):
            result = subprocess.run(command, capture_output=True, encoding="utf-8", check=False)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((installed / "lib/libfixture.dylib").read_text(encoding="utf-8"), "x86_64")
        self.assertEqual((installed / "include/squirrel.h").read_text(encoding="utf-8"), "x86_64 header")
        self.assertEqual((installed / "share/eve/TARGET_ARCHITECTURE").read_text(encoding="utf-8").strip(), "x86_64")

    @unittest.skipIf(os.name == "nt", "POSIX SDK checker runs on macOS")
    def test_runtime_checker_rejects_mislabeled_and_mixed_libraries(self):
        sdk = self.root / "sdk"
        for directory in ("bin", "lib", "share/eve"):
            (sdk / directory).mkdir(parents=True)
        for name, value in {
            "share/eve/TARGET_PLATFORM": "macosx",
            "share/eve/TARGET_ARCHITECTURE": "x86_64",
            "bin/eve": "x86_64",
            "lib/libvulkan.1.dylib": "arm64 x86_64",
            "lib/libMoltenVK.dylib": "x86_64",
            "lib/MoltenVK_icd.json": "{}",
        }.items():
            (sdk / name).write_text(value, encoding="utf-8")
        fake_bin = self.root / "tools"
        fake_bin.mkdir()
        lipo = fake_bin / "lipo"
        lipo.write_text('#!/bin/sh\ncat "$2"\nprintf "\\n"\n', encoding="utf-8")
        lipo.chmod(0o755)
        env = dict(os.environ, PATH=f"{fake_bin}{os.pathsep}{os.environ['PATH']}")

        def check():
            return subprocess.run(["bash", str(ROOT / ".github/scripts/check-macos-sdk.sh"), str(sdk), "x86_64"],
                                  env=env, capture_output=True, encoding="utf-8", check=False)

        result = check()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        (sdk / "lib/libMoltenVK.dylib").write_text("arm64", encoding="utf-8")
        result = check()
        self.assertIn("library does not contain x86_64", result.stderr)
        self.assertNotEqual(result.returncode, 0)
        (sdk / "share/eve/TARGET_ARCHITECTURE").write_text("arm64", encoding="utf-8")
        self.assertIn("architecture marker mismatch", check().stderr)


if __name__ == "__main__":
    unittest.main()
