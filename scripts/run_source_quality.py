#!/usr/bin/env python3
"""Run independent source gates and unittest modules with bounded parallelism.

Each command owns a subprocess and a buffered log, so failures retain their full
output without interleaving GitHub log groups. No persistent scan cache is used.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

import utf8_stdio

ROOT = Path(__file__).resolve().parents[1]


def run_commands(commands: list[tuple[str, list[str]]], jobs: int) -> int:
    """Run every command; return failure if any child fails."""
    def run(item: tuple[str, list[str]]) -> tuple[str, int, float, str]:
        name, command = item
        start = time.monotonic()
        with tempfile.TemporaryFile(mode="w+", encoding="utf-8") as log:
            result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                                    encoding="utf-8", env={**os.environ, "PYTHONUTF8": "1"}, check=False)
            log.seek(0)
            return name, result.returncode, time.monotonic() - start, log.read()

    failed = False
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = [pool.submit(run, item) for item in commands]
        for future in as_completed(futures):
            name, code, elapsed, output = future.result()
            title = f"{name} ({elapsed:.1f}s, exit {code})"
            print(f"::group::{title}" if os.environ.get("GITHUB_ACTIONS") else f"==> {title}", flush=True)
            print(output, end="" if output.endswith("\n") else "\n", flush=True)
            if os.environ.get("GITHUB_ACTIONS"):
                print("::endgroup::", flush=True)
            failed |= code != 0
    return int(failed)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", default="origin/dev")
    parser.add_argument("--jobs", type=int, default=int(os.environ.get("SOURCE_QUALITY_JOBS", min(4, os.cpu_count() or 1))))
    parser.add_argument("--tests-only", action="store_true")
    parser.add_argument("--test-module", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    if args.test_module:
        sys.path.insert(0, str(ROOT))
        sys.path.insert(0, str(ROOT / "scripts/tests"))
        suite = unittest.defaultTestLoader.loadTestsFromName(args.test_module)
        return int(not unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful())
    python = sys.executable
    test_commands = [(path.stem, [python, "-X", "utf8", str(Path(__file__).resolve()),
                                 "--test-module", path.stem])
                     for path in sorted((ROOT / "scripts/tests").glob("test_*.py"))]
    if args.tests_only:
        commands = test_commands
    else:
        # Make owns the inventory and command flags. One query avoids keeping
        # a second hand-maintained list in this scheduler.
        make = os.environ.get("MAKE", "make")
        targets = subprocess.check_output([make, "--no-print-directory", "-s", "check/source-targets"],
                                          cwd=ROOT, encoding="utf-8").split()
        commands = [(target, [make, "--no-print-directory", target, f"CI_BASE={args.base}"])
                    for target in targets]
        commands.extend(test_commands)
    return run_commands(commands, args.jobs)


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    sys.exit(main())
