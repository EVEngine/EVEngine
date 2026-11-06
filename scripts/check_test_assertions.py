#!/usr/bin/env python3
"""Prove the actual zeroerr runner returns failure for failed assertions."""

import argparse
import subprocess
import utf8_stdio


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", required=True)
    args = parser.parse_args()
    listing = subprocess.run(
        [args.runner, "--list-test-cases", "--list-format=plain"],
        capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=30,
    )
    registered = {line.split("\t", 1)[0] for line in listing.stdout.splitlines()}
    for name in ("singleEvaluation", "expectedInvariant", "checkFails", "comparisonFails",
                 "requireFails", "assertFails", "helperFails"):
        if listing.returncode or "assertionProbe." + name not in registered:
            print("assertion contract probe not registered: " + name)
            return 1
        run = subprocess.run(
            [args.runner, "--testcase=^assertionProbe." + name + "$"],
            capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=30,
        )
        expected_pass = name in ("singleEvaluation", "expectedInvariant")
        output = run.stdout + run.stderr
        if (run.returncode == 0) != expected_pass or (not expected_pass and "Assertion Failed" not in output):
            print(f"assertion contract failed: {name}, exit={run.returncode}\n{output}")
            return 1
    print("assertion contract passed: single evaluation; CHECK/REQUIRE/ASSERT/helper failures exit nonzero")
    return 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
