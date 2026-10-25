"""Native Android staging contracts, including paths containing spaces."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[2] / "cmake" / "sync_android_libs.cmake"


class AndroidLibraryStagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "repo with spaces"
        self.build = self.root / "build" / "android"
        self.libs = self.root / "build/third-party-binary/android/lib"
        self.ndk = self.root / "ndk with spaces"
        self.destination = self.root / "staged libs"
        self.engine = self.build / "src/engine/libmain.so"
        self.sdl = self.libs / "libSDL2.so"
        self.runtime = self.ndk / "toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so"
        for path, content in ((self.engine, b"engine"), (self.sdl, b"sdl"), (self.runtime, b"cpp")):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)

    def stage(self, abi="arm64-v8a"):
        return subprocess.run(
            ["cmake", f"-DREPO_ROOT={self.root}", f"-DBUILD_DIR={self.build}",
             f"-DJNI_LIBS={self.destination}", f"-DANDROID_NDK={self.ndk}",
             f"-DANDROID_ABI={abi}", "-P", str(SCRIPT)],
            capture_output=True, text=True, encoding="utf-8",
        )

    def test_engine_and_required_runtimes_are_staged(self):
        result = self.stage()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.destination / "libmain.so").read_bytes(), b"engine")
        self.assertEqual((self.destination / "libSDL2.so").read_bytes(), b"sdl")
        self.assertEqual((self.destination / "libc++_shared.so").read_bytes(), b"cpp")

    def test_prebuilt_package_uses_configured_install_prefix(self):
        package = self.root / "build/third-party-releases/v0.1.0/android-arm64-Release"
        library = package / "lib/libSDL2.so"
        library.parent.mkdir(parents=True)
        library.write_bytes(b"published sdl")
        (self.build / "CMakeCache.txt").write_text(
            f"EVENGINE_TP_INSTALL_PREFIX:INTERNAL={package.as_posix()}\n", encoding="utf-8")
        result = self.stage()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.destination / "libSDL2.so").read_bytes(), b"published sdl")

    def test_make_target_stages_libraries_from_a_path_with_spaces(self):
        helper = self.root / "cmake/sync_android_libs.cmake"
        helper.parent.mkdir()
        shutil.copyfile(SCRIPT, helper)
        for filename in ("native_arm64.mk", "native_macos.mk"):
            shutil.copyfile(SCRIPT.parent / filename, helper.parent / filename)
        shutil.copyfile(SCRIPT.parent.parent / "Makefile", self.root / "Makefile")
        # Stage the real recipe composition too: sync/android-libs now belongs
        # to the Android fragment rather than the root Makefile.
        for directory in ("platforms", "tasks"):
            shutil.copytree(SCRIPT.parent / directory, helper.parent / directory)
        result = subprocess.run(
            ["make", "sync/android-libs", f"BUILD_DIR={self.build}",
             f"JNI_LIBS={self.destination}", f"ANDROID_NDK={self.ndk}", "ANDROID_ABI=arm64-v8a"],
            cwd=self.root, capture_output=True, text=True, encoding="utf-8",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.destination / "libmain.so").read_bytes(), b"engine")

    def test_test_runner_takes_precedence_and_hidapi_is_optional(self):
        runner = self.build / "test/libmain.so"
        runner.parent.mkdir()
        runner.write_bytes(b"test runner")
        (self.libs / "libhidapi.so").write_bytes(b"hid")
        result = self.stage()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.destination / "libmain.so").read_bytes(), b"test runner")
        self.assertEqual((self.destination / "libhidapi.so").read_bytes(), b"hid")

    def test_missing_required_library_does_not_publish_partial_files(self):
        self.sdl.unlink()
        result = self.stage()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Required Android library missing", result.stderr)
        self.assertFalse(self.destination.exists())

    def test_missing_ndk_runtime_fails(self):
        self.runtime.unlink()
        result = self.stage()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("NDK libc++_shared.so missing", result.stderr)

    def test_unknown_abi_fails(self):
        result = self.stage("unknown")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Unsupported Android ABI", result.stderr)

    def test_supported_abis_use_their_target_runtime(self):
        triples = {"armeabi-v7a": "arm-linux-androideabi", "x86_64": "x86_64-linux-android", "x86": "i686-linux-android"}
        for abi, triple in triples.items():
            with self.subTest(abi=abi):
                runtime = self.runtime.parent.parent / triple / "libc++_shared.so"
                runtime.parent.mkdir(parents=True, exist_ok=True)
                runtime.write_bytes(abi.encode("ascii"))
                result = self.stage(abi)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual((self.destination / "libc++_shared.so").read_bytes(), abi.encode("ascii"))
