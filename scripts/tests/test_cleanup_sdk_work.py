"""SDK cleanup must wait for real borrows and preserve all failure gates."""

from contextlib import contextmanager
import errno
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import cleanup_sdk_work as cleaner  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]


class CleanupSdkWorkTests(unittest.TestCase):
    def test_unmarked_directory_is_not_deleted(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaises(ValueError):
                cleaner.cleanup(root)
            self.assertTrue(root.exists())

    def test_acl_denial_and_self_owned_handles_are_not_retried(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / cleaner.MARKER).touch()
            denied = PermissionError(errno.EACCES, "denied")
            denied.winerror = 5
            with patch.object(cleaner.shutil, "rmtree", side_effect=denied), patch.object(
                cleaner, "report_owners", return_value=[]
            ), patch.object(cleaner.time, "sleep") as sleep:
                with self.assertRaises(PermissionError):
                    cleaner.cleanup(root)
                sleep.assert_not_called()
            with patch.object(cleaner.shutil, "rmtree", side_effect=denied), patch.object(
                cleaner, "report_owners", return_value=[os.getpid()]
            ), patch.object(cleaner.time, "sleep") as sleep:
                with self.assertRaisesRegex(RuntimeError, "publisher-owned"):
                    cleaner.cleanup(root)
                sleep.assert_not_called()

    @unittest.skipUnless(shutil.which("bash"), "requires bash")
    def test_exit_trap_preserves_test_failure_and_cleanup_failure(self):
        source = (ROOT / ".github/scripts/test-sdk.sh").read_text(encoding="utf-8")
        cleanup = source[source.index("cleanup() {"):source.index("trap cleanup EXIT")]
        bash = shutil.which("bash")
        if os.name == "nt":
            git_bash = Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "Git/bin/bash.exe"
            if git_bash.is_file():
                bash = str(git_bash)
        with tempfile.TemporaryDirectory() as temporary:
            script_path = Path(temporary) / "trap.sh"
            for test_result, cleanup_result, expected in ((0, 0, 0), (0, 1, 1), (7, 0, 7), (7, 1, 7)):
                script = (f"set -euo pipefail; PLAT=win32; WORK=/unused; SCRIPTS=/unused; "
                          f"python3() {{ return {cleanup_result}; }};\n" + cleanup +
                          f"\ntrap cleanup EXIT\nexit {test_result}\n")
                script_path.write_text(script, encoding="utf-8")
                result = subprocess.run([bash, str(script_path)], capture_output=True)
                self.assertEqual(result.returncode, expected, result.stderr.decode("utf-8", "replace"))

    @contextmanager
    def borrowed_file(self, root):
        target = root / "eve.exe"
        target.write_bytes(b"borrowed test fixture")
        source = r'''
import ctypes,sys
from ctypes import wintypes as w
k=ctypes.WinDLL('kernel32',use_last_error=True)
k.CreateFileW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,ctypes.c_void_p,w.DWORD,w.DWORD,w.HANDLE]
k.CreateFileW.restype=w.HANDLE
k.CloseHandle.argtypes=[w.HANDLE]
h=k.CreateFileW(sys.argv[1],0x80000000,3,None,3,0,None)
if h==w.HANDLE(-1).value: raise ctypes.WinError(ctypes.get_last_error())
print('borrow ready',flush=True)
sys.stdin.readline()
k.CloseHandle(h)
'''
        child = subprocess.Popen([sys.executable, "-u", "-c", source, str(target)],
                                 stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 text=True, encoding="utf-8")
        try:
            self.assertEqual(child.stdout.readline().strip(), "borrow ready")
            yield child, target
        finally:
            if child.poll() is None:
                child.stdin.write("release\n")
                child.stdin.flush()
            child.wait(timeout=10)
            child.stdin.close()
            child.stdout.close()
            child.stderr.close()

    @unittest.skipUnless(os.name == "nt", "requires Windows file-sharing semantics")
    def test_real_external_borrow_is_waited_out_and_directory_is_removed(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "work"
            root.mkdir()
            (root / cleaner.MARKER).touch()
            with self.borrowed_file(root) as (child, _target):
                def release():
                    child.stdin.write("release\n")
                    child.stdin.flush()
                timer = threading.Timer(1, release)
                timer.start()
                try:
                    cleaner.cleanup(root)
                    self.assertFalse(root.exists())
                finally:
                    timer.join()
                self.assertEqual(child.wait(timeout=10), 0)

    @unittest.skipUnless(os.name == "nt", "requires Windows file-sharing semantics")
    def test_persistent_external_borrow_fails_without_stopping_its_owner(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / cleaner.MARKER).touch()
            with self.borrowed_file(root) as (child, target), patch.object(cleaner, "MAX_WAIT_SECONDS", 0.1):
                with self.assertRaises(TimeoutError):
                    cleaner.cleanup(root)
                self.assertIsNone(child.poll())
                self.assertTrue(target.exists())
