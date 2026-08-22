#!/usr/bin/env python3
"""Zip dist/eve-sdk/<platform> into artifacts/eve-sdk-<platform>-<tag>.zip"""
from __future__ import annotations

import argparse
import os
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
        # os.walk tolerates unreadable directories (e.g. Windows permission
        # quirks inside vendored license trees) instead of raising like rglob;
        # individual unreadable files/symlinks are skipped too.
        for dirpath, dirnames, filenames in os.walk(root, onerror=lambda _e: None):
            dirnames[:] = [
                d for d in dirnames
                # Never ship build outputs: the android SDK template is compiled
                # by test-sdk.sh before packaging, leaving app/build outputs
                # (e.g. a CI test APK) that consumers must not inherit.
                if d not in ("build", ".gradle")
                if os.access(os.path.join(dirpath, d), os.R_OK)
            ]
            for name in filenames:
                if name == ".DS_Store":
                    continue
                path = pathlib.Path(dirpath) / name
                try:
                    zf.write(path, path.relative_to(args.dist_root).as_posix())
                except OSError:
                    print(f"WARN: skipping unreadable entry {path}")
    print(f"wrote {out} ({out.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
