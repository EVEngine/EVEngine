"""Run CTest on a private, never activated Windows desktop.

SDL windows remain SHOWN on that desktop; neither window visibility nor the user's
active desktop is changed. Failure to isolate or close the desktop is fatal.
"""

from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import os
import tempfile
from pathlib import Path
import subprocess
import sys
import uuid

import utf8_stdio


class WindowsDesktopAPI:
    def __init__(self):
        self.user = ctypes.WinDLL("user32", use_last_error=True)
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self.kernel.GetCurrentThreadId.restype = wintypes.DWORD
        self.user.GetThreadDesktop.argtypes = [wintypes.DWORD]
        self.user.GetThreadDesktop.restype = wintypes.HANDLE
        self.user.CreateDesktopW.argtypes = [
            wintypes.LPCWSTR,
            wintypes.LPCWSTR,
            ctypes.c_void_p,
            wintypes.DWORD,
            wintypes.DWORD,
            ctypes.c_void_p,
        ]
        self.user.CreateDesktopW.restype = wintypes.HANDLE
        self.user.CloseDesktop.argtypes = [wintypes.HANDLE]
        self.user.CloseDesktop.restype = wintypes.BOOL
        self.user.GetUserObjectInformationW.argtypes = [
            wintypes.HANDLE,
            ctypes.c_int,
            ctypes.c_void_p,
            wintypes.DWORD,
            ctypes.POINTER(wintypes.DWORD),
        ]
        self.user.GetUserObjectInformationW.restype = wintypes.BOOL

    @staticmethod
    def checked(value, operation):
        if not value:
            raise OSError(ctypes.get_last_error(), operation + " failed")
        return value

    def current(self):
        return self.checked(
            self.user.GetThreadDesktop(self.kernel.GetCurrentThreadId()), "GetThreadDesktop"
        )

    def create(self, name):
        # All desktop-specific rights, without changing the active desktop.
        return self.checked(
            self.user.CreateDesktopW(name, None, None, 0, 0x01FF, None), "CreateDesktopW"
        )

    def close(self, desktop):
        self.checked(self.user.CloseDesktop(desktop), "CloseDesktop")

    def name(self):
        value = ctypes.create_unicode_buffer(32768)
        needed = wintypes.DWORD()
        self.checked(
            self.user.GetUserObjectInformationW(
                self.current(), 2, value, ctypes.sizeof(value), ctypes.byref(needed)
            ),
            "GetUserObjectInformationW",
        )
        return value.value


class StartupInfo(ctypes.Structure):
    _fields_ = [
        ("cb", wintypes.DWORD),
        ("lpReserved", wintypes.LPWSTR),
        ("lpDesktop", wintypes.LPWSTR),
        ("lpTitle", wintypes.LPWSTR),
        ("dwX", wintypes.DWORD),
        ("dwY", wintypes.DWORD),
        ("dwXSize", wintypes.DWORD),
        ("dwYSize", wintypes.DWORD),
        ("dwXCountChars", wintypes.DWORD),
        ("dwYCountChars", wintypes.DWORD),
        ("dwFillAttribute", wintypes.DWORD),
        ("dwFlags", wintypes.DWORD),
        ("wShowWindow", wintypes.WORD),
        ("cbReserved2", wintypes.WORD),
        ("lpReserved2", ctypes.c_void_p),
        ("hStdInput", wintypes.HANDLE),
        ("hStdOutput", wintypes.HANDLE),
        ("hStdError", wintypes.HANDLE),
    ]


class ProcessInfo(ctypes.Structure):
    _fields_ = [
        ("hProcess", wintypes.HANDLE),
        ("hThread", wintypes.HANDLE),
        ("dwProcessId", wintypes.DWORD),
        ("dwThreadId", wintypes.DWORD),
    ]


class JobLimits(ctypes.Structure):
    _fields_ = [
        ("perProcessTime", ctypes.c_longlong),
        ("perJobTime", ctypes.c_longlong),
        ("flags", wintypes.DWORD),
        ("minimumWorkingSet", ctypes.c_size_t),
        ("maximumWorkingSet", ctypes.c_size_t),
        ("activeProcessLimit", wintypes.DWORD),
        ("affinity", ctypes.c_size_t),
        ("priority", wintypes.DWORD),
        ("schedulingClass", wintypes.DWORD),
    ]


class JobIOCounters(ctypes.Structure):
    _fields_ = [
        (name, ctypes.c_ulonglong)
        for name in (
            "readOperations",
            "writeOperations",
            "otherOperations",
            "readBytes",
            "writeBytes",
            "otherBytes",
        )
    ]


class ExtendedJobLimits(ctypes.Structure):
    _fields_ = [
        ("basic", JobLimits),
        ("io", JobIOCounters),
        ("processMemory", ctypes.c_size_t),
        ("jobMemory", ctypes.c_size_t),
        ("peakProcessMemory", ctypes.c_size_t),
        ("peakJobMemory", ctypes.c_size_t),
    ]


def spawn_on_desktop(api, command, desktop):
    """Use the real Win32 lpDesktop field absent from Python's STARTUPINFO."""
    kernel = api.kernel
    kernel.GetCurrentProcess.restype = wintypes.HANDLE
    kernel.GetStdHandle.argtypes = [wintypes.DWORD]
    kernel.GetStdHandle.restype = wintypes.HANDLE
    kernel.DuplicateHandle.argtypes = [
        wintypes.HANDLE,
        wintypes.HANDLE,
        wintypes.HANDLE,
        ctypes.POINTER(wintypes.HANDLE),
        wintypes.DWORD,
        wintypes.BOOL,
        wintypes.DWORD,
    ]
    kernel.DuplicateHandle.restype = wintypes.BOOL
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.restype = wintypes.BOOL
    kernel.CreateProcessW.argtypes = [
        wintypes.LPCWSTR,
        wintypes.LPWSTR,
        ctypes.c_void_p,
        ctypes.c_void_p,
        wintypes.BOOL,
        wintypes.DWORD,
        ctypes.c_void_p,
        wintypes.LPCWSTR,
        ctypes.POINTER(StartupInfo),
        ctypes.POINTER(ProcessInfo),
    ]
    kernel.CreateProcessW.restype = wintypes.BOOL
    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel.WaitForSingleObject.restype = wintypes.DWORD
    kernel.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
    kernel.GetExitCodeProcess.restype = wintypes.BOOL
    kernel.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
    kernel.CreateJobObjectW.restype = wintypes.HANDLE
    kernel.SetInformationJobObject.argtypes = [
        wintypes.HANDLE,
        ctypes.c_int,
        ctypes.c_void_p,
        wintypes.DWORD,
    ]
    kernel.SetInformationJobObject.restype = wintypes.BOOL
    kernel.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
    kernel.AssignProcessToJobObject.restype = wintypes.BOOL
    kernel.ResumeThread.argtypes = [wintypes.HANDLE]
    kernel.ResumeThread.restype = wintypes.DWORD
    kernel.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
    kernel.TerminateProcess.restype = wintypes.BOOL
    info = StartupInfo()
    info.cb = ctypes.sizeof(info)
    info.lpDesktop = desktop
    info.dwFlags = 0x100  # STARTF_USESTDHANDLES; preserve redirected build/test logs.
    process = ProcessInfo()
    handles = []
    try:
        job = api.checked(kernel.CreateJobObjectW(None, None), "CreateJobObjectW")
        handles.append(job)
        limits = ExtendedJobLimits()
        limits.basic.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE, including descendants.
        api.checked(
            kernel.SetInformationJobObject(job, 9, ctypes.byref(limits), ctypes.sizeof(limits)),
            "SetInformationJobObject",
        )
        owner = kernel.GetCurrentProcess()
        for field, identifier in (("hStdInput", -10), ("hStdOutput", -11), ("hStdError", -12)):
            raw = kernel.GetStdHandle(identifier)
            if raw in (None, 0, ctypes.c_void_p(-1).value):
                raise OSError("GetStdHandle returned an invalid " + field)
            handle = wintypes.HANDLE()
            api.checked(
                kernel.DuplicateHandle(owner, raw, owner, ctypes.byref(handle), 0, True, 2),
                "DuplicateHandle",
            )
            handles.append(handle)
            setattr(info, field, handle.value)
        line = ctypes.create_unicode_buffer(
            subprocess.list2cmdline([os.fspath(arg) for arg in command])
        )
        # Null environment/current directory inherit both unchanged. No console is opened.
        api.checked(
            kernel.CreateProcessW(
                None,
                line,
                None,
                None,
                True,
                0x08000004,
                None,
                None,
                ctypes.byref(info),
                ctypes.byref(process),
            ),
            "CreateProcessW",
        )
        handles.extend((process.hThread, process.hProcess))
        try:
            api.checked(
                kernel.AssignProcessToJobObject(job, process.hProcess), "AssignProcessToJobObject"
            )
        except BaseException:
            # The suspended child cannot escape or leave descendants before assignment.
            api.checked(kernel.TerminateProcess(process.hProcess, 1), "TerminateProcess")
            raise
        if kernel.ResumeThread(process.hThread) == 0xFFFFFFFF:
            raise OSError(ctypes.get_last_error(), "ResumeThread failed")
        # Return to Python regularly so Ctrl+C can enter finally and close the job.
        while True:
            status = kernel.WaitForSingleObject(process.hProcess, 100)
            if status == 0:
                break
            if status != 258:  # WAIT_TIMEOUT
                raise OSError(ctypes.get_last_error(), "WaitForSingleObject failed")
        code = wintypes.DWORD()
        api.checked(
            kernel.GetExitCodeProcess(process.hProcess, ctypes.byref(code)), "GetExitCodeProcess"
        )
        return code.value
    finally:
        failures = []
        for handle in reversed(handles):
            if not kernel.CloseHandle(handle):
                failures.append(str(ctypes.get_last_error()))
        if failures:
            raise RuntimeError("process handle cleanup failed: " + "; ".join(failures))


def run(command, *, windows="hidden", api=None, runner=subprocess.run, spawn=spawn_on_desktop):
    if windows == "visible" or (sys.platform != "win32" and api is None):
        return runner(command).returncode
    api = api or WindowsDesktopAPI()
    name = "EVEngineTests_" + uuid.uuid4().hex
    desktop = api.create(name)
    try:
        # Verify explicit child desktop placement before allowing CTest to run any test.
        with tempfile.TemporaryDirectory(prefix="eve-desktop-probe-") as directory:
            evidence = Path(directory) / "desktop.txt"
            result = spawn(
                api,
                [
                    sys.executable,
                    "-c",
                    "import subprocess,sys;raise SystemExit(subprocess.call(sys.argv[1:]))",
                    sys.executable,
                    str(Path(__file__).resolve()),
                    "--desktop-name-file",
                    str(evidence),
                ],
                name,
            )
            actual = evidence.read_text(encoding="utf-8") if evidence.exists() else ""
            if result != 0 or actual != name:
                raise RuntimeError(
                    "child did not use private desktop; refusing to run visible tests: "
                    + repr(actual)
                    + " (exit "
                    + str(result)
                    + ")"
                )
        return spawn(api, command, name)
    finally:
        api.close(desktop)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--windows", choices=("hidden", "visible"), default="hidden")
    parser.add_argument("--desktop-name", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--desktop-name-file", help=argparse.SUPPRESS)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    if args.desktop_name_file:
        Path(args.desktop_name_file).write_text(WindowsDesktopAPI().name(), encoding="utf-8")
        return 0
    if args.desktop_name:
        print(WindowsDesktopAPI().name())
        return 0
    command = args.command
    if command[:1] == ["--"]:
        command = command[1:]
    if not command:
        parser.error("a command is required after --")
    try:
        return run(command, windows=args.windows)
    except (OSError, RuntimeError) as error:
        print("CTest desktop isolation failed: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
