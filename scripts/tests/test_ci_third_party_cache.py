"""Exercise cache isolation, stamp validation, and target toolchain selection."""

import os
import json
import re
import shutil
from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[2]
ACTION = ROOT / ".github/actions/restore-third-party/action.yml"
VERIFY = ROOT / ".github/scripts/verify-third-party-cache.sh"
TOOLCHAIN = ROOT / ".github/scripts/identify-third-party-toolchain.sh"


class ThirdPartyCacheTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def identity(self, **overrides):
        action = ACTION.read_text(encoding="utf-8")
        script = action.split("        python3 - <<'PY'\n", 1)[1].split("\n        PY", 1)[0]
        output = self.root / "outputs"
        output.write_text("", encoding="utf-8")
        env = {
            **os.environ,
            "CACHE_PLATFORM": "linux",
            "CACHE_CONFIGURATION": "Debug",
            "CACHE_TOOLCHAIN": "gcc-13",
            "CACHE_RECIPE": "recipe-1",
            "CACHE_RUNNER_OS": "Linux",
            "CACHE_RUNNER_ARCH": "X64",
            "CACHE_RUNNER_IMAGE": "ubuntu24",
            "GITHUB_OUTPUT": str(output),
            **overrides,
        }
        subprocess.run(["python3", "-c", textwrap.dedent(script)], env=env, check=True)
        return dict(line.split("=", 1) for line in output.read_text(encoding="utf-8").splitlines())

    def test_key_changes_for_each_abi_and_recipe_input(self):
        baseline = self.identity()
        self.assertEqual(baseline, self.identity())
        for field, value in (
            ("CACHE_PLATFORM", "android"),
            ("CACHE_TOOLCHAIN", "gcc-14"),
            ("CACHE_RECIPE", "recipe-2"),
            ("CACHE_RUNNER_OS", "Windows"),
            ("CACHE_RUNNER_ARCH", "ARM64"),
            ("CACHE_RUNNER_IMAGE", "ubuntu26"),
        ):
            with self.subTest(field=field):
                self.assertNotEqual(baseline["key"], self.identity(**{field: value})["key"])

    def test_non_windows_configurations_share_release_dependencies(self):
        for platform in ("linux", "macosx", "android", "ios"):
            with self.subTest(platform=platform):
                debug = self.identity(CACHE_PLATFORM=platform, CACHE_CONFIGURATION="Debug")
                release = self.identity(CACHE_PLATFORM=platform, CACHE_CONFIGURATION="Release")
                self.assertEqual(debug["key"], release["key"])
        debug = self.identity(CACHE_PLATFORM="win32", CACHE_CONFIGURATION="Debug")
        release = self.identity(CACHE_PLATFORM="win32", CACHE_CONFIGURATION="Release")
        self.assertNotEqual(debug["key"], release["key"])

    def test_install_prefix_and_dependency_configuration_match_cmake(self):
        for platform in ("win32", "linux", "macosx", "android", "ios"):
            for configuration in ("Debug", "Release", "RelWithDebInfo"):
                with self.subTest(platform=platform, configuration=configuration):
                    result = self.identity(CACHE_PLATFORM=platform, CACHE_CONFIGURATION=configuration)
                    suffix = "-debug" if configuration == "Debug" else ""
                    self.assertEqual(result["path"], f"build/third-party-binary/{platform}{suffix}")
                    expected_type = configuration if platform == "win32" else "Release"
                    self.assertEqual(result["build-type"], expected_type)

    def verify(self, fields):
        prefix = self.root / "install with spaces"
        (prefix / "include").mkdir(parents=True, exist_ok=True)
        (prefix / "lib").mkdir(exist_ok=True)
        (prefix / "third-party-version.txt").write_text(
            "\n".join(f"{key}={value}" for key, value in fields.items()) + "\n", encoding="utf-8"
        )
        return subprocess.run(
            ["bash", str(VERIFY), str(prefix), "android", "Release"],
            capture_output=True, text=True, encoding="utf-8",
        )

    def test_matching_stamp_is_accepted_and_incompatible_stamps_are_rejected(self):
        pin = (ROOT / "cmake/third_party_pin.cmake").read_text(encoding="utf-8").split('"')[1]
        fields = {"platform": "android", "build_type": "Release", "aggregate": pin, "dirty": "1"}
        result = self.verify(fields)
        self.assertEqual(result.returncode, 0, result.stderr)
        for field, value in (("platform", "ios"), ("build_type", "Debug"), ("aggregate", "0" * 40), ("dirty", "")):
            with self.subTest(field=field):
                result = self.verify({**fields, field: value})
                self.assertNotEqual(result.returncode, 0)

    def test_release_engine_can_restore_debug_lane_release_dependencies(self):
        scripts = self.root / ".github/scripts"
        scripts.mkdir(parents=True)
        shutil.copyfile(VERIFY, scripts / VERIFY.name)
        cmake = self.root / "cmake"
        cmake.mkdir()
        shutil.copyfile(ROOT / "cmake/third_party_pin.cmake", cmake / "third_party_pin.cmake")
        pin = (cmake / "third_party_pin.cmake").read_text(encoding="utf-8").split('"')[1]
        prefix = self.root / "build/third-party-binary/android-debug"
        (prefix / "include").mkdir(parents=True)
        (prefix / "lib").mkdir()
        stamp = prefix / "third-party-version.txt"
        stamp.write_text(f"platform=android\nbuild_type=Release\naggregate={pin}\ndirty=1\n", encoding="utf-8")
        action = ACTION.read_text(encoding="utf-8")
        script = textwrap.dedent(action.rsplit("      run: |\n", 1)[1])
        output = self.root / "restore-outputs"
        env = {
            **os.environ,
            "CACHE_PREFIX": "build/third-party-binary/android",
            "CACHE_PLATFORM": "android",
            "CACHE_BUILD_TYPE": "Release",
            "GITHUB_OUTPUT": str(output),
        }
        restored = subprocess.run(["bash", "-ec", script], cwd=self.root, env=env, capture_output=True, text=True)
        self.assertEqual(restored.returncode, 0, restored.stderr)
        self.assertIn("cmake-args=-DEVENGINE_THIRD_PARTY_BINARY_DIR=build/third-party-binary/android-debug", output.read_text(encoding="utf-8"))
        self.assertFalse((self.root / "build/third-party-binary/android").exists())
        stamp.write_text(stamp.read_text(encoding="utf-8").replace("build_type=Release", "build_type=Debug"), encoding="utf-8")
        output.unlink()
        invalid = subprocess.run(["bash", "-ec", script], cwd=self.root, env=env, capture_output=True, text=True)
        self.assertNotEqual(invalid.returncode, 0)
        self.assertFalse(output.exists())

    def test_verify_uses_resolved_release_prefix_and_package_manifest(self):
        prefix = self.root / "downloaded package with spaces"
        (prefix / "include").mkdir(parents=True)
        (prefix / "lib").mkdir()
        pin_text = (ROOT / "cmake/third_party_pin.cmake").read_text(encoding="utf-8")
        pin = re.search(r'set\(EVENGINE_THIRD_PARTY_PIN "([^"]+)"', pin_text)[1]
        tag = re.search(r'set\(EVENGINE_THIRD_PARTY_RELEASE "([^"]+)"', pin_text)[1]
        (prefix / "third-party-version.txt").write_text(f"aggregate={pin}\nplatform=android\nbuild_type=Release\n", encoding="utf-8")
        manifest = {"schema": "evengine.third-party-package", "version": 1, "platform": "android", "configuration": "Release", "aggregate": pin, "release": tag, "architecture": "arm64"}
        manifest_path = prefix / "eve-third-party-package.json"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        cache = self.root / "CMakeCache.txt"
        cache.write_text(f"EVENGINE_TP_INSTALL_PREFIX:INTERNAL={prefix}\n", encoding="utf-8")
        command = ["bash", str(VERIFY), str(self.root / "unused source prefix"), "android", "Release", str(cache)]
        valid = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(valid.returncode, 0, valid.stderr)
        manifest_path.write_text(json.dumps({**manifest, "architecture": "x86_64"}), encoding="utf-8")
        invalid = subprocess.run(command, capture_output=True, text=True)
        self.assertNotEqual(invalid.returncode, 0)
        self.assertIn("architecture", invalid.stderr)

    def test_android_fingerprint_tracks_actual_ndk_and_target_settings(self):
        ndk = self.root / "ndk with spaces"
        ndk.mkdir()
        properties = ndk / "source.properties"
        properties.write_text("Pkg.Revision = 27.3.13750724\n", encoding="utf-8")
        binaries = self.root / "bin"
        binaries.mkdir()
        cmake = binaries / "cmake"
        cmake.write_text("#!/bin/sh\nprintf 'cmake version 3.29.0\\n'\n", encoding="utf-8")
        cmake.chmod(0o755)
        env = {**os.environ, "ANDROID_NDK": str(ndk), "PATH": str(binaries) + os.pathsep + os.environ["PATH"]}

        def fingerprint(**overrides):
            return subprocess.check_output(
                ["bash", str(TOOLCHAIN), "android"], env={**env, **overrides}, text=True, encoding="utf-8"
            )

        baseline = fingerprint(ANDROID_ABI="arm64-v8a", ANDROID_PLATFORM="android-24", ANDROID_STL="c++_shared")
        self.assertNotEqual(baseline, fingerprint(ANDROID_ABI="x86_64"))
        self.assertNotEqual(baseline, fingerprint(ANDROID_PLATFORM="android-26"))
        self.assertNotEqual(baseline, fingerprint(ANDROID_STL="c++_static"))
        properties.write_text("Pkg.Revision = 28.0.0\n", encoding="utf-8")
        self.assertNotEqual(baseline, fingerprint())
