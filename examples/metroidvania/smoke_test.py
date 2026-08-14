"""Launch Momentum Ruins and fail if the engine reports a frame/runtime error."""

from __future__ import annotations

import argparse
import subprocess
import sys
import time
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("engine", type=Path, help="Path to the eve executable")
    parser.add_argument("--seconds", type=float, default=6.0)
    args = parser.parse_args()

    game_dir = Path(__file__).resolve().parent
    process = subprocess.Popen(
        [str(args.engine.resolve()), "run", str(game_dir)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    time.sleep(args.seconds)
    exited_early = process.poll() is not None
    if not exited_early:
        process.terminate()
        try:
            process.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            process.kill()
    stdout, stderr = process.communicate()
    combined = stdout + "\n" + stderr
    bad_markers = ("frame error", "Run failed:", "Runtime error")
    failures = [marker for marker in bad_markers if marker.lower() in combined.lower()]

    if failures or (exited_early and process.returncode != 0):
        print(combined, file=sys.stderr)
        print("Smoke test failed: " + ", ".join(failures), file=sys.stderr)
        return 1
    print(f"Smoke test passed ({args.seconds:.1f}s, no frame/runtime errors).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
