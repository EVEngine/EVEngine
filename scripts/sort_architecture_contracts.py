#!/usr/bin/env python3
"""Sort the architecture contract catalogue into its canonical review order."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from check_architecture_contracts import DEFAULT_METADATA, catalogue_sort_key


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", nargs="?", type=Path, default=DEFAULT_METADATA)
    parser.add_argument("--check", action="store_true", help="only verify canonical ordering")
    args = parser.parse_args()

    metadata = json.loads(args.path.read_text(encoding="utf-8"))
    entries = metadata.get("entries")
    if not isinstance(entries, list) or not all(isinstance(entry, dict) for entry in entries):
        parser.error("metadata entries must be an array of objects")

    ordered = sorted(entries, key=catalogue_sort_key)
    if args.check:
        if entries != ordered:
            print(f"{args.path}: architecture contracts are not in canonical rule/id order")
            return 1
        print(f"{args.path}: architecture contracts are canonically ordered")
        return 0

    metadata["entries"] = ordered
    args.path.write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"{args.path}: sorted {len(ordered)} architecture contracts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
