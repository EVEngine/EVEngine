"""Run the two RT startup contracts under validation and GDB before the full suite."""

import json
import os
from pathlib import Path
import subprocess
import sys


def main():
    listing = subprocess.run(
        ["ctest", "--test-dir", sys.argv[1], "--show-only=json-v1"],
        check=True,
        capture_output=True,
        encoding="utf-8",
    )
    selected = {
        "graphics.raytracing.probeAndUnsupportedOps",
        "graphics.raytracing.reflectionHitVisibility",
    }
    tests = [test for test in json.loads(listing.stdout)["tests"] if test["name"] in selected]
    if len(tests) != len(selected):
        raise RuntimeError("ray tracing probe tests were not discovered")
    for test in tests:
        properties = {prop["name"]: prop["value"] for prop in test.get("properties", [])}
        environment = os.environ.copy()
        environment["EVENGINE_VULKAN_VALIDATION"] = "1"
        for entry in properties.get("ENVIRONMENT", []):
            key, value = entry.split("=", 1)
            environment[key] = value
        print(f"Vulkan validation/backtrace probe: {test['name']}", flush=True)
        subprocess.run(
            [
                "gdb",
                "--batch",
                "--return-child-result",
                "-ex",
                "run",
                "-ex",
                "thread apply all bt",
                "--args",
                *test["command"],
            ],
            cwd=Path(properties["WORKING_DIRECTORY"]),
            env=environment,
            check=True,
            timeout=180,
        )


if __name__ == "__main__":
    main()
