#!/usr/bin/env python3
"""Compare generated upstream and native PixelFX inventories; never treat missing effects as done."""

from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import sys

import utf8_stdio


PINNED_REVISION = "a84159688af3ca37a4be05a0d422b94451b2e24e"


def assess(reference: dict, native: dict) -> dict:
    """Return owning coverage evidence from two generated catalogue documents."""
    upstream = reference["effects"]
    actual = native["effects"]
    upstream_counts = Counter(item["id"] for item in upstream)
    native_counts = Counter(item["id"] for item in actual)
    expected_ids = set(upstream_counts)
    actual_ids = set(native_counts)
    source = {item["id"]: item for item in upstream}
    mismatches = []
    for item in actual:
        original = source.get(item["id"])
        if original is None:
            continue
        for field in ("size", "origin", "duration", "loop"):
            if item.get(field) != original.get(field):
                mismatches.append({"id": item["id"], "field": field, "expected": original.get(field), "actual": item.get(field)})
    duplicates = sorted(key for key, count in native_counts.items() if count != 1)
    upstream_duplicates = sorted(key for key, count in upstream_counts.items() if count != 1)
    missing = sorted(expected_ids - actual_ids)
    extra = sorted(actual_ids - expected_ids)
    complete = (
        reference.get("revision") == PINNED_REVISION
        and len(upstream) == 244
        and len(actual) == 244
        and not duplicates
        and not upstream_duplicates
        and not missing
        and not extra
        and not mismatches
    )
    return {
        "schema": "eve.pixelfx-coverage/v1",
        "referenceRevision": reference.get("revision"),
        "referenceValid": reference.get("revision") == PINNED_REVISION,
        "expected": len(expected_ids),
        "implemented": len(actual_ids & expected_ids),
        "missing": missing,
        "extra": extra,
        "duplicateIds": duplicates,
        "referenceDuplicateIds": upstream_duplicates,
        "metadataMismatches": mismatches,
        "complete": complete,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", required=True, type=Path)
    parser.add_argument("--native", required=True, type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    report = assess(
        json.loads(args.reference.read_text(encoding="utf-8")),
        json.loads(args.native.read_text(encoding="utf-8")),
    )
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"PixelFX coverage: {report['implemented']}/{report['expected']}; complete={report['complete']}")
    for field in ("extra", "duplicateIds", "referenceDuplicateIds", "metadataMismatches"):
        if report[field]:
            print(f"{field}: {report[field]}")
    return 0 if report["complete"] else 1


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    sys.exit(main())
