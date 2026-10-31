#!/usr/bin/env python3
"""Report Restart Manager file owners after SDK cleanup fails; never stop processes."""

import argparse
import ctypes
from ctypes import wintypes
from pathlib import Path
import sys


def report(directory: Path) -> None:
    if sys.platform != "win32":
        return

    class UniqueProcess(ctypes.Structure):
        _fields_ = [("pid", wintypes.DWORD), ("started", wintypes.FILETIME)]

    class ProcessInfo(ctypes.Structure):
        _fields_ = [
            ("process", UniqueProcess),
            ("application", wintypes.WCHAR * 256),
            ("service", wintypes.WCHAR * 64),
            ("kind", ctypes.c_int),
            ("status", wintypes.ULONG),
            ("session", wintypes.DWORD),
            ("restartable", wintypes.BOOL),
        ]

    files = sorted(str(path.resolve()) for path in directory.rglob("*") if path.is_file())
    print(f"cleanup-locks: remaining_files={len(files)}", flush=True)
    if not files:
        return
    # System32-only lookup avoids loading a DLL from a package or working directory.
    manager = ctypes.WinDLL("Rstrtmgr.dll", winmode=0x00000800)
    start = manager.RmStartSession
    start.argtypes = [ctypes.POINTER(wintypes.DWORD), wintypes.DWORD, wintypes.LPWSTR]
    start.restype = wintypes.DWORD
    register = manager.RmRegisterResources
    register.argtypes = [
        wintypes.DWORD,
        wintypes.UINT,
        ctypes.POINTER(wintypes.LPCWSTR),
        wintypes.UINT,
        ctypes.POINTER(UniqueProcess),
        wintypes.UINT,
        ctypes.POINTER(wintypes.LPCWSTR),
    ]
    register.restype = wintypes.DWORD
    get_list = manager.RmGetList
    get_list.argtypes = [
        wintypes.DWORD,
        ctypes.POINTER(wintypes.UINT),
        ctypes.POINTER(wintypes.UINT),
        ctypes.POINTER(ProcessInfo),
        ctypes.POINTER(wintypes.DWORD),
    ]
    get_list.restype = wintypes.DWORD
    end = manager.RmEndSession
    end.argtypes = [wintypes.DWORD]
    end.restype = wintypes.DWORD
    session = wintypes.DWORD()
    key = ctypes.create_unicode_buffer(33)
    status = start(ctypes.byref(session), 0, key)
    if status:
        raise OSError(status, "RmStartSession")
    try:
        paths = (wintypes.LPCWSTR * len(files))(*files)
        status = register(session, len(files), paths, 0, None, 0, None)
        if status:
            raise OSError(status, "RmRegisterResources")
        needed, count, reasons = wintypes.UINT(), wintypes.UINT(), wintypes.DWORD()
        status = get_list(
            session, ctypes.byref(needed), ctypes.byref(count), None, ctypes.byref(reasons)
        )
        if status == 234:  # ERROR_MORE_DATA: size the native process list.
            count.value = needed.value
            owners = (ProcessInfo * count.value)()
            status = get_list(
                session, ctypes.byref(needed), ctypes.byref(count), owners, ctypes.byref(reasons)
            )
        else:
            owners = ()
        print(f"cleanup-locks: status={status} process_count={count.value}", flush=True)
        if status:
            raise OSError(status, "RmGetList")
        for owner in owners[: count.value]:
            print(
                f"cleanup-locks: pid={owner.process.pid} application={owner.application!r} "
                f"service={owner.service!r}",
                flush=True,
            )
    finally:
        end(session)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    try:
        report(args.directory.resolve())
    except (OSError, ValueError) as error:
        print(f"cleanup-locks: diagnostic failed: {error}", file=sys.stderr)


if __name__ == "__main__":
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stderr.reconfigure(encoding="utf-8")
    main()
