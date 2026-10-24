#!/usr/bin/env python3
"""Reject non-native PE/ELF runtimes and libraries before running an ARM64 SDK."""

import argparse
from pathlib import Path
import struct


def verify(path: Path) -> None:
    with path.open("rb") as stream:
        header = stream.read(64)
        if header[:2] == b"MZ":
            if len(header) < 64:
                raise ValueError(f"truncated PE header: {path}")
            stream.seek(struct.unpack_from("<I", header, 60)[0])
            pe = stream.read(6)
            if len(pe) != 6 or pe[:4] != b"PE\0\0" or struct.unpack_from("<H", pe, 4)[0] != 0xAA64:
                raise ValueError(f"expected ARM64 PE: {path}")
        elif header[:4] == b"\x7fELF":
            if (
                len(header) < 20
                or header[4:6] != b"\x02\x01"
                or struct.unpack_from("<H", header, 18)[0] != 183
            ):
                raise ValueError(f"expected little-endian AArch64 ELF64: {path}")
        else:
            raise ValueError(f"unrecognized binary: {path}")
    print(f"OK: ARM64 {path}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", type=Path)
    args = parser.parse_args()
    for path in args.paths:
        if path.is_dir():
            binaries = sorted(
                p
                for p in path.rglob("*")
                if p.is_file()
                and (
                    p.suffix.lower() in (".exe", ".dll", ".so")
                    or p.name == "eve"
                    or ".so." in p.name
                )
            )
            if not binaries:
                raise ValueError(f"no runtime binaries found: {path}")
            for binary in binaries:
                verify(binary)
        else:
            verify(path)


if __name__ == "__main__":
    main()
