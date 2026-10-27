#!/usr/bin/env python3
"""Run the full, source-label or build-derived affected CTest selection in CI."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

import utf8_stdio


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", required=True)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--label-regex", default="")
    args = parser.parse_args()
    mode = os.environ.get("CI_TEST_MODE", "full")
    if mode == "none":
        print("No native unit tests required for this change.")
        return 0
    if mode == "affected":
        paths = json.loads(os.environ["CI_NATIVE_PATHS"])
        if not isinstance(paths, list) or not paths or not all(isinstance(p, str) and p for p in paths):
            parser.error("affected mode requires a nonempty JSON array of changed native paths")
        command = [sys.executable, "-X", "utf8", str(Path(__file__).with_name("test_impact.py")),
                   "run", "--build-dir", "build/" + args.platform, "--jobs", str(args.jobs)]
        for path in paths:
            command.extend(["--changed", path])
        return subprocess.run(command, check=False).returncode
    if mode not in {"full", "selected"}:
        parser.error(f"unsupported CI_TEST_MODE: {mode}")
    command = [os.environ.get("MAKE", "make"), "--no-print-directory", "test/" + args.platform,
               "CTEST_JOBS=" + str(args.jobs), "LABEL_FILTER=" + args.label_regex]
    return subprocess.run(command, check=False).returncode


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    sys.exit(main())
