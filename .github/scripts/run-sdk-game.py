#!/usr/bin/env python3
"""Require a packaged game to stay alive and emit its script startup marker."""

import argparse
from pathlib import Path
import subprocess
import tempfile
import time


def check_game(
    command: list[str], directory: Path, marker: str, seconds: float, timeout: float
) -> None:
    if seconds <= 0 or timeout < seconds:
        raise ValueError("timeout must be at least the positive minimum run duration")
    with tempfile.TemporaryDirectory() as temporary, (Path(temporary) / "game.log").open("wb") as log:
        process = subprocess.Popen(command, cwd=directory, stdout=log, stderr=subprocess.STDOUT)
        start = time.monotonic()
        try:
            while True:
                output = (Path(temporary) / "game.log").read_text(encoding="utf-8", errors="replace")
                elapsed = time.monotonic() - start
                if process.poll() is not None:
                    raise RuntimeError(f"game exited early ({process.returncode}):\n{output}")
                if elapsed >= seconds and marker in output:
                    print(output)
                    print("OK: packaged game stayed alive and executed its startup script")
                    break
                if elapsed >= timeout:
                    raise RuntimeError(f"startup marker missing:\n{output}")
                time.sleep(0.2)
        finally:
            process.kill()
            process.wait()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("runtime", type=Path)
    parser.add_argument("--marker", default="SDK_TEST_GAME_OK")
    parser.add_argument("--seconds", type=float, default=5)
    parser.add_argument("--timeout", type=float, default=30)
    args = parser.parse_args()
    runtime = args.runtime.resolve()
    check_game([str(runtime)], runtime.parent, args.marker, args.seconds, args.timeout)


if __name__ == "__main__":
    main()
