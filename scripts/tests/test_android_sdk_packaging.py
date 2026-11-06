"""Android SDK archives must avoid validation copies and strip Release libraries."""

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[2]


class AndroidSdkPackagingTests(unittest.TestCase):
    def test_archive_excludes_staged_libraries_but_keeps_canonical_runtime(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            sdk = work / "dist/eve-sdk/android"
            for name in (
                "lib/libmain.so",
                "platform/apk/app/src/main/jniLibs/arm64-v8a/libmain.so",
                "platform/apk/app/build/test.apk",
                "platform/apk/.gradle/cache",
                "platform/apk/app/src/main/AndroidManifest.xml",
            ):
                path = sdk / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"fixture")
            subprocess.run(
                [sys.executable, str(ROOT / ".github/scripts/zip-sdk.py"),
                 "android", "v1", "--dist-root", str(work / "dist"),
                 "--out-dir", str(work / "out")],
                check=True, capture_output=True,
            )
            with zipfile.ZipFile(work / "out/eve-sdk-android-v1.zip") as archive:
                self.assertEqual(set(archive.namelist()), {
                    "eve-sdk/android/lib/libmain.so",
                    "eve-sdk/android/platform/apk/app/src/main/AndroidManifest.xml",
                })

    def test_strip_is_release_only_and_failures_are_fatal(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            library = work / "sdk/lib/libmain.so"
            library.parent.mkdir(parents=True)
            library.write_bytes(b"original")
            stub = work / "strip.cmake"
            stub.write_text('file(WRITE "${CMAKE_ARGV4}" "stripped")\n', encoding="utf-8")
            tool = f"{Path(shutil.which('cmake')).as_posix()};-P;{stub.as_posix()}"
            template = (ROOT / "cmake/android_sdk_strip.cmake.in").read_text(encoding="utf-8")
            script = work / "install.cmake"
            script.write_text(template.replace("@CMAKE_STRIP@", tool), encoding="utf-8")
            def install(config):
                return subprocess.run(
                    ["cmake", f"-DCMAKE_INSTALL_PREFIX={library.parents[1].as_posix()}",
                     f"-DCMAKE_INSTALL_CONFIG_NAME={config}", "-P", str(script)],
                    capture_output=True, text=True, encoding="utf-8",
                )
            self.assertEqual(install("Debug").returncode, 0)
            self.assertEqual(library.read_bytes(), b"original")
            result = install("Release")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(library.read_bytes(), b"stripped")
            stub.write_text('message(FATAL_ERROR "strip failure")\n', encoding="utf-8")
            result = install("Release")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Android SDK strip failed", result.stderr)
