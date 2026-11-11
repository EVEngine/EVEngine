"""Real SDK acceptance with a controlled Windows publication-lock race."""

import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest


@unittest.skipUnless(os.name == "nt" and os.environ.get("EVENGINE_PACKAGE_TEST_SDK"), "requires Windows SDK fixture")
class WindowsPackagePublishTests(unittest.TestCase):
    def test_borrow_released_before_owner_query_publishes_verified_candidate(self):
        self.exercise_publication(release_borrow=True)

    def test_persistent_borrow_fails_and_restores_previous_package(self):
        self.exercise_publication(release_borrow=False)

    def exercise_publication(self, *, release_borrow):
        sdk = Path(os.environ["EVENGINE_PACKAGE_TEST_SDK"]).resolve()
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                      ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
        kernel.CreateFileW.restype = wintypes.HANDLE
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        with tempfile.TemporaryDirectory(prefix="eve-publish-race-") as temporary:
            root = Path(temporary)
            game = root / "game"
            game.mkdir()
            (game / "config.nut").write_text(
                'config = { width=320 height=240 title="publish-race" debug=false hotReload=false };',
                encoding="utf-8",
            )
            (game / "main.nut").write_text(
                'eve_init = function() {}; eve_update = function(dt) {};', encoding="utf-8"
            )
            destination = root / "published"
            destination.mkdir()
            (destination / "previous.txt").write_text("previous package", encoding="utf-8")
            candidate = root / "published.package-lock/candidate/eve.exe"
            process = subprocess.Popen(
                [str(sdk / "bin/eve.exe"), "package", str(game), "--sdk", str(sdk), "-o", str(destination)],
                stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, encoding="utf-8", errors="replace",
            )
            handle = None
            try:
                deadline = time.monotonic() + 15
                while time.monotonic() < deadline and process.poll() is None:
                    # Read-sharing permits acceptance to run, but omitting
                    # FILE_SHARE_DELETE prevents the publication rename.
                    acquired = kernel.CreateFileW(str(candidate), 0x80000000, 3, None, 3, 0, None)
                    if acquired != wintypes.HANDLE(-1).value:
                        handle = acquired
                        break
                    time.sleep(0.005)
                self.assertIsNotNone(handle, "did not acquire candidate borrow before acceptance finished")
                log = []
                for line in process.stderr:
                    log.append(line)
                    if release_borrow and "Package publish failed:" in line and handle is not None:
                        kernel.CloseHandle(handle)
                        handle = None
                output = "".join(log)
                result = process.wait(timeout=90)
                self.assertIn("Package publish failed: code=5", output)
                if release_borrow:
                    self.assertEqual(result, 0, output)
                    self.assertIn("No package lockers reported; retrying verified candidate", output)
                    self.assertIn("acceptance=passed", (destination / "build-identity.manifest").read_text(encoding="utf-8"))
                    self.assertIn("[package-check] passed: 30 frames", (destination / "acceptance.log").read_text(encoding="utf-8"))
                    self.assertFalse((destination / "previous.txt").exists())
                else:
                    self.assertNotEqual(result, 0, output)
                    self.assertEqual((destination / "previous.txt").read_text(encoding="utf-8"), "previous package")
                    self.assertFalse((destination / "build-identity.manifest").exists())
            finally:
                if handle is not None:
                    kernel.CloseHandle(handle)
                if process.poll() is None:
                    process.kill()
                    process.wait()
                process.stderr.close()
