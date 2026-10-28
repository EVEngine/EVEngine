"""Run HD2D pixel/behavior tests and reject Vulkan validation errors as failures."""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path)
    args = parser.parse_args()
    env = dict(os.environ, EVENGINE_VULKAN_VALIDATION="1")
    result = subprocess.run(
        ["ctest", "--test-dir", str(args.build_dir), "--output-on-failure", "-R", r"^hd2d\."],
        env=env,
        check=False,
    )
    if result.returncode:
        return result.returncode
    log = (args.build_dir / "Testing/Temporary/LastTest.log").read_text(
        encoding="utf-8", errors="replace"
    )
    if "Vulkan validation layers enabled" not in log:
        print("FAIL: no evidence that Vulkan validation was enabled")
        return 1
    errors = log.count("[ERROR: Validation]")
    print(f"HD2D Vulkan validation errors: {errors}")
    return int(errors != 0)


if __name__ == "__main__":
    raise SystemExit(main())
