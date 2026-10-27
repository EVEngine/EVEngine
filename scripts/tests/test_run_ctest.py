"""Private desktop launch contract; the real Windows probe opens no windows."""

from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import run_ctest


class DesktopLauncherTests(unittest.TestCase):
    def setUp(self):
        self.api = Mock()
        self.api.create.return_value = 123
        self.calls = []
        self.temporary = tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2])
        self.addCleanup(self.temporary.cleanup)
        self.temp_patch = patch.object(tempfile, "tempdir", self.temporary.name)
        self.temp_patch.start()
        self.addCleanup(self.temp_patch.stop)

    def spawn(self, api, command, desktop):
        self.assertIs(api, self.api)
        self.calls.append((command, desktop))
        if "--desktop-name-file" in command:
            Path(command[-1]).write_text(desktop, encoding="utf-8")
            return 0
        return 17

    def test_probe_precedes_command_and_exit_code_is_preserved(self):
        command = ["ctest", "--test-dir", "a space", "-R", "a.*"]
        self.assertEqual(run_ctest.run(command, api=self.api, spawn=self.spawn), 17)
        self.assertEqual(self.calls[1][0], command)
        self.assertEqual(self.calls[0][1], self.calls[1][1])
        self.api.close.assert_called_once_with(123)

    def test_create_failure_never_runs_or_closes_unowned_handle(self):
        self.api.create.side_effect = OSError("denied")
        with self.assertRaises(OSError):
            run_ctest.run(["ctest"], api=self.api, spawn=self.spawn)
        self.assertEqual(self.calls, [])
        self.api.close.assert_not_called()

    def test_wrong_desktop_or_probe_failure_never_launches_ctest(self):
        for code, name in ((0, "Default"), (1, "")):
            with self.subTest(code=code):

                def bad_probe(api, command, desktop):
                    Path(command[-1]).write_text(name, encoding="utf-8")
                    return code

                with self.assertRaisesRegex(RuntimeError, "refusing"):
                    run_ctest.run(["ctest"], api=self.api, spawn=bad_probe)
        self.assertEqual(self.calls, [])
        self.assertEqual(self.api.close.call_count, 2)

    def test_probe_exception_and_command_exception_close_desktop(self):
        for fail_at in (1, 2):
            with self.subTest(fail_at=fail_at):
                self.calls.clear()

                def failing(api, command, desktop):
                    if len(self.calls) + 1 == fail_at:
                        raise OSError("spawn failed")
                    return self.spawn(api, command, desktop)

                with self.assertRaises(OSError):
                    run_ctest.run(["ctest"], api=self.api, spawn=failing)
        self.assertEqual(self.api.close.call_count, 2)

    def test_cleanup_failure_is_fatal(self):
        self.api.close.side_effect = OSError("close failed")
        with self.assertRaisesRegex(OSError, "close failed"):
            run_ctest.run(["ctest"], api=self.api, spawn=self.spawn)

    def test_visible_opt_out_preserves_runner_command_and_status(self):
        runner = Mock(return_value=subprocess.CompletedProcess([], 9))
        self.assertEqual(
            run_ctest.run(
                ["ctest", "-N"], windows="visible", api=self.api, runner=runner, spawn=self.spawn
            ),
            9,
        )
        runner.assert_called_once_with(["ctest", "-N"])
        self.api.create.assert_not_called()

    @unittest.skipUnless(sys.platform == "win32", "Win32 desktop integration probe")
    def test_real_child_desktop_arguments_environment_and_exit_code(self):
        # Use the writable checkout when a sandbox applies stricter ACLs to TEMP.
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]) as directory:
            output = Path(directory) / "child.json"
            old_temp = tempfile.tempdir
            old_value = os.environ.get("EVE_DESKTOP_TEST")
            tempfile.tempdir = directory
            os.environ["EVE_DESKTOP_TEST"] = "inherited"
            try:
                code = run_ctest.run(
                    [
                        sys.executable,
                        "-c",
                        "import json,os,sys;from pathlib import Path;"
                        "Path(sys.argv[1]).write_text(json.dumps([os.environ['EVE_DESKTOP_TEST'],"
                        "sys.argv[2],os.getcwd()]),encoding='utf-8');sys.exit(23)",
                        str(output),
                        "argument with spaces",
                    ]
                )
                self.assertEqual(code, 23)
                import json

                self.assertEqual(
                    json.loads(output.read_text(encoding="utf-8")),
                    ["inherited", "argument with spaces", os.getcwd()],
                )
            finally:
                tempfile.tempdir = old_temp
                if old_value is None:
                    os.environ.pop("EVE_DESKTOP_TEST", None)
                else:
                    os.environ["EVE_DESKTOP_TEST"] = old_value

    @unittest.skipUnless(sys.platform == "win32", "Win32 process-tree lifecycle probe")
    def test_real_job_close_terminates_leftover_descendant(self):
        import ctypes
        from ctypes import wintypes

        output = Path(self.temporary.name) / "descendant.txt"
        program = (
            "import subprocess,sys;from pathlib import Path;"
            "p=subprocess.Popen([sys.executable,'-c','import time;time.sleep(30)'],"
            "creationflags=0x08000000);Path(sys.argv[1]).write_text(str(p.pid),encoding='utf-8')"
        )
        self.assertEqual(run_ctest.run([sys.executable, "-c", program, str(output)]), 0)
        pid = int(output.read_text(encoding="utf-8"))
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        kernel.OpenProcess.restype = wintypes.HANDLE
        kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        kernel.WaitForSingleObject.restype = wintypes.DWORD
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        handle = kernel.OpenProcess(0x100000, False, pid)
        if handle:
            try:
                self.assertEqual(kernel.WaitForSingleObject(handle, 2000), 0)
            finally:
                kernel.CloseHandle(handle)
        else:
            self.assertEqual(ctypes.get_last_error(), 87)  # Process no longer exists.


class CreateProcessContractTests(unittest.TestCase):
    def make_api(self):
        api = Mock()

        def checked(value, operation):
            if not value:
                raise OSError(operation + " failed")
            return value

        api.checked.side_effect = checked
        kernel = api.kernel
        kernel.CreateJobObjectW.return_value = 41
        kernel.SetInformationJobObject.return_value = True
        kernel.AssignProcessToJobObject.return_value = True
        kernel.ResumeThread.return_value = 1
        kernel.TerminateProcess.return_value = True
        kernel.GetCurrentProcess.return_value = 11
        kernel.GetStdHandle.return_value = 12
        self.next_handle = 20

        def duplicate(*args):
            self.next_handle += 1
            args[3]._obj.value = self.next_handle
            return True

        kernel.DuplicateHandle.side_effect = duplicate

        def create(*args):
            self.assertEqual(args[8]._obj.lpDesktop, "private")
            self.assertTrue(args[5] & 4)  # Suspended until the job owns the process tree.
            self.assertEqual(args[8]._obj.hStdOutput, 22)
            self.assertIsNone(args[6])  # Inherit environment unchanged.
            self.assertIsNone(args[7])  # Inherit working directory unchanged.
            args[9]._obj.hThread = 31
            args[9]._obj.hProcess = 32
            return True

        kernel.CreateProcessW.side_effect = create
        kernel.WaitForSingleObject.return_value = 0

        def exit_code(process, result):
            result._obj.value = 23
            return True

        kernel.GetExitCodeProcess.side_effect = exit_code
        kernel.CloseHandle.return_value = True
        return api

    def test_native_launch_preserves_desktop_environment_streams_and_exit(self):
        api = self.make_api()
        self.assertEqual(run_ctest.spawn_on_desktop(api, ["ctest", "a space"], "private"), 23)
        self.assertEqual(api.kernel.CloseHandle.call_count, 6)
        command = api.kernel.CreateProcessW.call_args.args[1].value
        self.assertEqual(command, 'ctest "a space"')

    def test_each_native_failure_closes_every_owned_handle(self):
        for operation, expected in (
            ("CreateJobObjectW", 0),
            ("SetInformationJobObject", 1),
            ("DuplicateHandle", 1),
            ("CreateProcessW", 4),
            ("AssignProcessToJobObject", 6),
            ("ResumeThread", 6),
            ("WaitForSingleObject", 6),
            ("GetExitCodeProcess", 6),
        ):
            with self.subTest(operation=operation):
                api = self.make_api()
                method = getattr(api.kernel, operation)
                method.side_effect = None
                method.return_value = (
                    0xFFFFFFFF if operation in ("WaitForSingleObject", "ResumeThread") else False
                )
                with patch.object(run_ctest.ctypes, "get_last_error", return_value=5, create=True):
                    with self.assertRaises(OSError):
                        run_ctest.spawn_on_desktop(api, ["ctest"], "private")
                self.assertEqual(api.kernel.CloseHandle.call_count, expected)

    def test_invalid_standard_handle_is_never_duplicated_as_a_pseudo_process_handle(self):
        import ctypes

        for handle in (None, 0, ctypes.c_void_p(-1).value):
            with self.subTest(handle=handle):
                api = self.make_api()
                api.kernel.GetStdHandle.return_value = handle
                with self.assertRaisesRegex(OSError, "invalid"):
                    run_ctest.spawn_on_desktop(api, ["ctest"], "private")
                api.kernel.DuplicateHandle.assert_not_called()
                api.kernel.CreateProcessW.assert_not_called()
                api.kernel.CloseHandle.assert_called_once_with(41)

    def test_wait_polls_so_python_can_handle_interrupts(self):
        api = self.make_api()
        api.kernel.WaitForSingleObject.side_effect = [258, 258, 0]
        self.assertEqual(run_ctest.spawn_on_desktop(api, ["ctest"], "private"), 23)
        self.assertEqual(api.kernel.WaitForSingleObject.call_count, 3)
        for call in api.kernel.WaitForSingleObject.call_args_list:
            self.assertEqual(call.args, (32, 100))

    def test_interrupt_closes_job_and_every_process_handle(self):
        api = self.make_api()
        api.kernel.WaitForSingleObject.side_effect = KeyboardInterrupt
        with self.assertRaises(KeyboardInterrupt):
            run_ctest.spawn_on_desktop(api, ["ctest"], "private")
        self.assertEqual(api.kernel.CloseHandle.call_count, 6)
        self.assertEqual(api.kernel.CloseHandle.call_args.args, (41,))

    def test_failed_handle_close_is_explicit_and_other_handles_still_close(self):
        api = self.make_api()
        api.kernel.CloseHandle.side_effect = [False, True, True, True, True, True]
        with patch.object(run_ctest.ctypes, "get_last_error", return_value=5, create=True):
            with self.assertRaisesRegex(RuntimeError, "cleanup failed"):
                run_ctest.spawn_on_desktop(api, ["ctest"], "private")
        self.assertEqual(api.kernel.CloseHandle.call_count, 6)
