#!/usr/bin/env python3
"""Remove SDK-test-owned Windows work directories after bounded external borrows."""

import argparse
import errno
import os
from pathlib import Path
import runpy
import shutil
import sys
import time

import utf8_stdio

MAX_WAIT_SECONDS = 30
MARKER = ".eve-sdk-test-work"


def report_owners(directory: Path) -> list[int]:
    reporter = Path(__file__).resolve().parents[1] / ".github/scripts/report-windows-locks.py"
    return runpy.run_path(str(reporter))["report"](directory)


def cleanup(directory: Path) -> None:
    """Delete owned work; never terminate owners, change ACLs or ignore errors."""
    if directory.is_symlink() or not (directory / MARKER).is_file():
        raise ValueError("SDK cleanup requires a marked, non-symlink work directory")
    directory = directory.resolve()
    if directory == Path(directory.anchor):
        raise ValueError("SDK cleanup refuses a filesystem root")
    deadline = time.monotonic() + MAX_WAIT_SECONDS
    while True:
        try:
            shutil.rmtree(directory)
            if directory.exists():
                raise RuntimeError("SDK cleanup left the work directory present")
            print("SDK cleanup completed", flush=True)
            return
        except OSError as error:
            winerror = getattr(error, "winerror", None)
            if winerror not in (5, 32, 33) and error.errno not in (errno.EACCES, errno.EBUSY):
                raise
            owners = report_owners(directory)
            if os.getpid() in owners:
                raise RuntimeError("SDK cleanup found a publisher-owned handle") from error
            # Sharing/lock violations are explicit OS evidence. Access denied
            # alone needs a confirmed external borrower; never retry an ACL error.
            if not owners and winerror not in (32, 33):
                raise
            if time.monotonic() >= deadline:
                raise TimeoutError("SDK cleanup borrow did not end within 30 seconds") from error
            print(f"SDK cleanup waiting for external borrow (winerror={winerror})", flush=True)
            time.sleep(0.2)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    try:
        cleanup(args.directory)
    except (OSError, RuntimeError, ValueError) as error:
        print(f"SDK cleanup failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
