"""Validate the complete SDK archive set before uploading a release."""

from __future__ import annotations

import argparse
from pathlib import Path
import zipfile

import utf8_stdio

SDK_TARGETS = ("win32", "win32-arm64", "linux", "linux-arm64", "android", "ios", "macosx-arm64", "macosx-x86_64")


def verify(directory: Path, tag: str) -> None:
    expected = {f"eve-sdk-{target}-{tag}.zip" for target in SDK_TARGETS}
    actual = {path.name for path in directory.glob("eve-sdk-*.zip")}
    if actual != expected:
        raise ValueError(f"SDK archive set mismatch: missing={sorted(expected - actual)}, extra={sorted(actual - expected)}")
    for target in SDK_TARGETS:
        with zipfile.ZipFile(directory / f"eve-sdk-{target}-{tag}.zip") as archive:
            root = f"eve-sdk/{target}/share/eve"
            version = archive.read(f"{root}/VERSION").decode("utf-8").strip()
            platform = archive.read(f"{root}/TARGET_PLATFORM").decode("utf-8").strip()
            expected_platform = target.removesuffix("-arm64").removesuffix("-x86_64")
            if version != tag.removeprefix("v") or platform != expected_platform:
                raise ValueError(f"SDK version/platform mismatch in {target}: {version}/{platform}")
            if expected_platform == "macosx":
                architecture = archive.read(f"{root}/TARGET_ARCHITECTURE").decode("utf-8").strip()
                if architecture != target.removeprefix("macosx-"):
                    raise ValueError(f"SDK architecture mismatch in {target}: {architecture}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("tag")
    args = parser.parse_args()
    try:
        verify(args.directory, args.tag)
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        parser.exit(1, f"ERROR: {error}\n")
    print(f"OK: all {len(SDK_TARGETS)} SDK archives match {args.tag}")


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    main()
