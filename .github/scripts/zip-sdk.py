#!/usr/bin/env python3
"""Zip dist/eve-sdk/<platform> into artifacts/eve-sdk-<platform>-<tag>.zip"""
from __future__ import annotations

import argparse
import pathlib
import zipfile


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("platform", help="SDK platform directory name (win32, linux, ...)")
    parser.add_argument("tag", help="Release tag used in the archive filename")
    parser.add_argument(
        "--dist-root",
        default="dist",
        type=pathlib.Path,
        help="Directory that contains eve-sdk/<platform>",
    )
    parser.add_argument(
        "--out-dir",
        default="artifacts",
        type=pathlib.Path,
        help="Directory for the resulting zip",
    )
    args = parser.parse_args()

    root = args.dist_root / "eve-sdk" / args.platform
    if not root.is_dir():
        raise SystemExit(f"SDK directory not found: {root}")

    args.out_dir.mkdir(parents=True, exist_ok=True)
    out = args.out_dir / f"eve-sdk-{args.platform}-{args.tag}.zip"
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zf:
        for path in root.rglob("*"):
            if not path.is_file():
                continue
            if path.name == ".DS_Store":
                continue
            zf.write(path, path.relative_to(args.dist_root).as_posix())
    print(f"wrote {out} ({out.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
