"""Release package validation and transactional installation tests."""

import json
import hashlib
import io
import sys
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import third_party_package as tp  # noqa: E402


class ThirdPartyPackageTests(unittest.TestCase):
    def test_missing_release_is_download_unavailable(self):
        error = tp.urllib.error.HTTPError("https://github.com", 404, "Not Found", {}, None)
        self.addCleanup(error.close)
        with patch.object(tp.urllib.request, "urlopen", side_effect=error):
            with self.assertRaises(tp.DownloadUnavailable):
                tp.fetch("EVEngine/third-party", "v1.0.0", "win32", "Debug",
                         self.root / "install", "x86_64")
        self.assertFalse((self.root / "install").exists())

    def test_timeout_is_download_unavailable(self):
        with patch.object(tp.urllib.request, "urlopen", side_effect=TimeoutError("timed out")):
            with self.assertRaises(tp.DownloadUnavailable):
                tp.fetch("EVEngine/third-party", "v1.0.0", "win32", "Debug",
                         self.root / "install", "x86_64")
        self.assertFalse((self.root / "install").exists())

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.prefix = self.root / "prefix"
        for folder in ("include", "lib", "bin"):
            (self.prefix / folder).mkdir(parents=True)
        (self.prefix / "include/api.h").write_text("header", encoding="utf-8")
        (self.prefix / "lib/api.lib").write_bytes(b"library")
        (self.prefix / "bin/api.dll").write_bytes(b"runtime")
        (self.prefix / "third-party-version.txt").write_text(
            f"aggregate={tp.pin()}\nplatform=win32\nbuild_type=Debug\n", encoding="utf-8")

    def archive(self):
        archive = self.root / tp.package_name("win32", "Debug", "v1.0.0", "x86_64")
        manifest = {"schema": "evengine.third-party-package", "version": 1,
                    "platform": "win32", "configuration": "Debug", "aggregate": tp.pin(),
                    "release": "v1.0.0", "architecture": "x86_64"}
        with zipfile.ZipFile(archive, "w") as bundle:
            for path in self.prefix.rglob("*"):
                if path.is_file():
                    bundle.write(path, path.relative_to(self.prefix).as_posix())
            bundle.writestr("eve-third-party-package.json", json.dumps(manifest))
        return archive

    def test_fetch_checks_release_checksum_and_installs(self):
        archive = self.archive()
        content = archive.read_bytes()
        sums = f"{hashlib.sha256(content).hexdigest()}  {archive.name}\n".encode("utf-8")
        destination = self.root / "install"
        with patch.object(tp.urllib.request, "urlopen", side_effect=[io.BytesIO(sums), io.BytesIO(content)]):
            tp.fetch("EVEngine/EVEngine", "v1.0.0", "win32", "Debug", destination, "x86_64")
        self.assertTrue((destination / "lib/api.lib").is_file())

    def test_fetch_corrupt_checksum_does_not_install(self):
        archive = self.archive()
        destination = self.root / "install"
        sums = f"{'0' * 64}  {archive.name}\n".encode("utf-8")
        with patch.object(tp.urllib.request, "urlopen", side_effect=[io.BytesIO(sums), io.BytesIO(archive.read_bytes())]):
            with self.assertRaises(ValueError):
                tp.fetch("EVEngine/EVEngine", "v1.0.0", "win32", "Debug", destination, "x86_64")
        self.assertFalse(destination.exists())

    def test_reconfigure_reuses_validated_install_without_network(self):
        destination = self.root / "install"
        tp.unpack(self.archive(), destination, "win32", "Debug", "v1.0.0", "x86_64")
        with patch.object(tp.urllib.request, "urlopen") as request:
            tp.fetch("EVEngine/third-party", "v1.0.0", "win32", "Debug", destination, "x86_64")
        request.assert_not_called()

    def test_wrong_architecture_does_not_publish(self):
        destination = self.root / "install"
        with self.assertRaises(ValueError):
            tp.unpack(self.archive(), destination, "win32", "Debug", "v1.0.0", "arm64")
        self.assertFalse(destination.exists())

    def test_linux_debug_fetch_reuses_release_libraries(self):
        name = tp.package_name("linux", "Release", "v1.0.0", "x86_64")
        content = b"archive"
        sums = f"{hashlib.sha256(content).hexdigest()}  {name}\n".encode("utf-8")
        destination = self.root / "linux-debug"
        with patch.object(tp.urllib.request, "urlopen", side_effect=[io.BytesIO(sums), io.BytesIO(content)]) as request:
            with patch.object(tp, "unpack") as unpack:
                tp.fetch("EVEngine/EVEngine", "v1.0.0", "linux", "Debug", destination, "x86_64")
                self.assertEqual(unpack.call_args.args[1:], (destination, "linux", "Release", "v1.0.0", "x86_64"))
            self.assertTrue(request.call_args_list[1].args[0].endswith(name))

    def test_roundtrip_preserves_headers_libraries_and_runtime(self):
        destination = self.root / "install"
        tp.unpack(self.archive(), destination, "win32", "Debug", "v1.0.0", "x86_64")
        self.assertEqual((destination / "bin/api.dll").read_bytes(), b"runtime")
        self.assertEqual((destination / "lib/api.lib").read_bytes(), b"library")
        self.assertEqual((destination / "include/api.h").read_text(encoding="utf-8"), "header")

    def test_configuration_mismatch_does_not_publish(self):
        destination = self.root / "install"
        with self.assertRaises(ValueError):
            tp.unpack(self.archive(), destination, "win32", "Release", "v1.0.0", "x86_64")
        self.assertFalse(destination.exists())

    def test_changed_source_pin_does_not_publish(self):
        archive = self.archive()
        destination = self.root / "install"
        with patch.object(tp, "pin", return_value="changed"), self.assertRaises(ValueError):
            tp.unpack(archive, destination, "win32", "Debug", "v1.0.0", "x86_64")
        self.assertFalse(destination.exists())

    def test_existing_install_is_preserved(self):
        destination = self.root / "install"
        destination.mkdir()
        with self.assertRaises(ValueError):
            tp.unpack(self.archive(), destination, "win32", "Debug", "v1.0.0", "x86_64")
        self.assertEqual(list(destination.iterdir()), [])

    def test_archive_traversal_is_rejected(self):
        archive = self.archive()
        with zipfile.ZipFile(archive, "a") as bundle:
            bundle.writestr("../escape", "bad")
        with self.assertRaises(ValueError):
            tp.unpack(archive, self.root / "install", "win32", "Debug", "v1.0.0", "x86_64")
        self.assertFalse((self.root / "escape").exists())

    def test_future_schema_is_rejected(self):
        archive = self.archive()
        with zipfile.ZipFile(archive) as bundle:
            entries = {entry: bundle.read(entry) for entry in bundle.namelist()}
        manifest = json.loads(entries["eve-third-party-package.json"])
        manifest["version"] = 2
        entries["eve-third-party-package.json"] = json.dumps(manifest).encode("utf-8")
        with zipfile.ZipFile(archive, "w") as bundle:
            for name, data in entries.items():
                bundle.writestr(name, data)
        with self.assertRaises(ValueError):
            tp.unpack(archive, self.root / "install", "win32", "Debug", "v1.0.0", "x86_64")


class CMakeReleaseSelectionTests(unittest.TestCase):
    def test_installed_jolt_is_visible_to_parent_and_sibling_with_cross_root(self):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            package = root / "prefix/lib/cmake/Jolt"
            package.mkdir(parents=True)
            (package / "JoltConfig.cmake").write_text(
                "add_library(Jolt::Jolt INTERFACE IMPORTED)\n", encoding="utf-8")
            modules = (tp.ROOT / "src/modules/CMakeLists.txt").read_text(encoding="utf-8")
            # Exercise the production import policy with an Android-style package root.
            import_policy = modules.split("    find_package(Jolt", 1)[1].split("  else()", 1)[0]
            (root / "modules").mkdir()
            (root / "modules/CMakeLists.txt").write_text(
                "find_package(Jolt" + import_policy, encoding="utf-8")
            assertion = 'if(NOT TARGET Jolt::Jolt)\nmessage(FATAL_ERROR "Jolt scope lost")\nendif()\n'
            (root / "tests").mkdir()
            (root / "tests/CMakeLists.txt").write_text(assertion, encoding="utf-8")
            (root / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.21)\nproject(JoltScope NONE)\n"
                f'set(EVENGINE_THIRD_PARTY_BINARY_DIR "{(root / "prefix").as_posix()}")\n'
                f'set(CMAKE_FIND_ROOT_PATH "{(root / "sysroot").as_posix()}")\n'
                "set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)\nadd_subdirectory(modules)\n"
                + assertion + "add_subdirectory(tests)\n", encoding="utf-8")
            result = subprocess.run(["cmake", "-G", "Ninja", "-S", str(root), "-B", str(root / "build")],
                                    capture_output=True, text=True, encoding="utf-8")
            self.assertEqual(result.returncode, 0, result.stderr)

    def configure(self, mode, target="win32", download_result=0, cached_prefix=False):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            (root / "scripts").mkdir()
            (root / "scripts/third_party_package.py").write_text(
                "import pathlib, sys\n"
                "pathlib.Path('invoked.txt').write_text(' '.join(sys.argv), encoding='utf-8')\n"
                f"sys.exit({download_result})\n", encoding="utf-8")
            resolver = (tp.ROOT / "cmake/third_party_release.cmake").as_posix()
            (root / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.21)\nproject(ReleaseSelection NONE)\n"
                f"set(EVENGINE_THIRD_PARTY_MODE {mode})\nset(BUILD_PLATFORM {target})\n"
                "set(CMAKE_SYSTEM_PROCESSOR AMD64)\nset(CMAKE_BUILD_TYPE Debug)\n"
                "set(TP_BUILD_PATH win32-debug)\nset(EVENGINE_THIRD_PARTY_RELEASE v1.0.0)\n"
                + ('set(EVENGINE_THIRD_PARTY_BINARY_DIR "previous-prefix" CACHE PATH "")\n' if cached_prefix else "")
                + ('set(EVENGINE_TP_AUTOMATIC_PREFIX "previous-prefix" CACHE INTERNAL "")\n' if cached_prefix and mode == "AUTO" else "")
                + f'include("{resolver}")\neve_resolve_third_party_release()\n'
                + 'message(STATUS "selected-prefix=${EVENGINE_THIRD_PARTY_BINARY_DIR}")\n', encoding="utf-8")
            result = subprocess.run(["cmake", "-G", "Ninja", "-S", str(root), "-B", str(root / "build")],
                                    cwd=root, capture_output=True, text=True, encoding="utf-8")
            invoked = (root / "invoked.txt").read_text(encoding="utf-8") if (root / "invoked.txt").exists() else ""
            return result, invoked

    def test_supported_auto_target_downloads_exact_configuration(self):
        result, invoked = self.configure("AUTO")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--configuration Debug", invoked)
        self.assertIn("--tag v1.0.0", invoked)
        self.assertIn("--architecture x86_64", invoked)

    def test_source_mode_does_not_download(self):
        result, invoked = self.configure("SOURCE")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(invoked, "")

    def test_source_mode_clears_previous_prebuilt_selection(self):
        result, invoked = self.configure("SOURCE", cached_prefix=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("selected-prefix=\n", result.stdout)
        self.assertNotIn("selected-prefix=previous-prefix", result.stdout)
        self.assertEqual(invoked, "")

    def test_unpublished_target_auto_selects_source(self):
        result, invoked = self.configure("AUTO", "webgpu")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(invoked, "")

    def test_unpublished_target_prebuilt_is_an_error(self):
        result, invoked = self.configure("PREBUILT", "webgpu")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("No published third-party package", result.stderr)
        self.assertEqual(invoked, "")

    def test_unavailable_download_auto_selects_source(self):
        result, invoked = self.configure("AUTO", download_result=2)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("selected-prefix=\n", result.stdout)
        self.assertIn("building pinned dependencies from source", result.stdout)
        self.assertTrue(invoked)

    def test_unavailable_download_clears_cached_automatic_prefix(self):
        result, invoked = self.configure("AUTO", download_result=2, cached_prefix=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("selected-prefix=\n", result.stdout)
        self.assertNotIn("selected-prefix=previous-prefix", result.stdout)
        self.assertTrue(invoked)

    def test_unavailable_download_prebuilt_is_fatal(self):
        result, invoked = self.configure("PREBUILT", download_result=2)
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(invoked)

    def test_invalid_package_is_not_a_source_fallback(self):
        result, invoked = self.configure("AUTO", download_result=7)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Failed to obtain the pinned third-party release", result.stderr)
        self.assertTrue(invoked)


if __name__ == "__main__":
    unittest.main()
