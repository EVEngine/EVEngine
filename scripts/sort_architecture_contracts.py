#!/usr/bin/env python3
"""Sort architecture-contract shards and refresh the composed snapshot.

Module-owned shards under ``scripts/architecture_contracts/*.json`` are the
edit surface.  ``scripts/architecture_contracts.json`` is rewritten as their
merge so existing tooling that still opens the composed path stays aligned.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from check_architecture_contracts import (
    COMPOSED_METADATA,
    DEFAULT_METADATA_DIR,
    catalogue_sort_key,
    compose_catalogue,
)
import utf8_stdio


def _sort_shard(path: Path, *, check_only: bool) -> tuple[bool, int]:
    metadata = json.loads(path.read_text(encoding="utf-8"))
    entries = metadata.get("entries")
    if not isinstance(entries, list) or not all(isinstance(entry, dict) for entry in entries):
        raise SystemExit(f"{path}: metadata entries must be an array of objects")
    ordered = sorted(entries, key=catalogue_sort_key)
    changed = entries != ordered
    if check_only:
        return changed, len(ordered)
    metadata["entries"] = ordered
    path.write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return changed, len(ordered)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "path",
        nargs="?",
        type=Path,
        default=DEFAULT_METADATA_DIR,
        help="shard directory or a single shard/composed JSON file",
    )
    parser.add_argument("--check", action="store_true", help="only verify canonical ordering")
    args = parser.parse_args()

    target = args.path
    if target.is_dir() or target.resolve() == DEFAULT_METADATA_DIR.resolve():
        directory = target if target.is_dir() else DEFAULT_METADATA_DIR
        dirty = False
        total = 0
        for shard in sorted(directory.glob("*.json")):
            changed, count = _sort_shard(shard, check_only=args.check)
            dirty = dirty or changed
            total += count
        composed = compose_catalogue(directory)
        if args.check:
            if dirty:
                print(f"{directory}: architecture contract shards are not in canonical order")
                return 1
            if COMPOSED_METADATA.is_file():
                on_disk = json.loads(COMPOSED_METADATA.read_text(encoding="utf-8"))
                if on_disk != composed:
                    print(
                        f"{COMPOSED_METADATA}: composed snapshot is out of sync with {directory}/"
                    )
                    return 1
            print(
                f"{directory}: {total} architecture contracts across "
                f"{len(list(directory.glob('*.json')))} shards are canonically ordered"
            )
            return 0
        COMPOSED_METADATA.write_text(
            json.dumps(composed, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        print(
            f"{directory}: sorted shards; wrote composed snapshot "
            f"({len(composed['entries'])} contracts)"
        )
        return 0

    # Single-file mode (legacy): sort one JSON document in place.
    metadata = json.loads(target.read_text(encoding="utf-8"))
    entries = metadata.get("entries")
    if not isinstance(entries, list) or not all(isinstance(entry, dict) for entry in entries):
        parser.error("metadata entries must be an array of objects")
    ordered = sorted(entries, key=catalogue_sort_key)
    if args.check:
        if entries != ordered:
            print(f"{target}: architecture contracts are not in canonical rule/id order")
            return 1
        print(f"{target}: architecture contracts are canonically ordered")
        return 0
    metadata["entries"] = ordered
    target.write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"{target}: sorted {len(ordered)} architecture contracts")
    return 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
