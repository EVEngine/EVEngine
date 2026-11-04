#!/usr/bin/env python3
"""Export a single-page, non-rotated JSON Hash atlas using TexturePacker."""

import argparse
from pathlib import Path
import subprocess
import sys

import utf8_stdio


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="directory containing source sprites")
    parser.add_argument("output", type=Path, help="output base path, without extension")
    parser.add_argument("--texturepacker", default="TexturePacker", help="TexturePacker executable")
    args = parser.parse_args()
    if not args.source.is_dir():
        parser.error(f"source directory does not exist: {args.source}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # No passthrough options: callers cannot accidentally override the import contract.
    command = [
        args.texturepacker,
        "--format", "json",
        "--disable-rotation",
        "--trim-mode", "Trim",
        "--extrude", "1",
        "--sheet", str(Path(str(args.output) + ".png").resolve()),
        "--data", str(Path(str(args.output) + ".json").resolve()),
        str(args.source.resolve()),
    ]
    try:
        return subprocess.run(command, check=False).returncode
    except FileNotFoundError:
        print("TexturePacker executable not found; install TexturePacker or pass --texturepacker PATH.",
              file=sys.stderr)
        return 1


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
