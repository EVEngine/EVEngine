#!/usr/bin/env python3
"""Validate ownership and budget metadata for the binding-gap allowlist.

This is deliberately a separate gate from check_bindings.py. It reads the
existing plain-text gap list and its JSON sidecar; it never rewrites either
file and never invokes check_bindings.py --write-gaps.
"""

from __future__ import annotations

import datetime as dt
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GAPS = ROOT / "scripts" / "check_bindings_gaps.txt"
METADATA = ROOT / "scripts" / "check_bindings_gaps.metadata.json"
ENTRY = re.compile(r"^[A-Za-z0-9_.-]+:[A-Za-z0-9_.-]+$")
REQUIRED = ("owner", "issue", "reason", "expiry")


def main() -> int:
    try:
        metadata = json.loads(METADATA.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"FAIL binding gap metadata cannot be read: {error}")
        return 1

    entries = [
        line.strip()
        for line in GAPS.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]
    failures: list[str] = []
    if any(not ENTRY.fullmatch(entry) for entry in entries):
        failures.append("gap entries must use module:binding format")
    if len(entries) != len(set(entries)):
        failures.append("gap entries must be unique")

    default = metadata.get("default", {})
    for key in REQUIRED:
        if not isinstance(default.get(key), str) or not default[key].strip():
            failures.append(f"default metadata is missing {key}")
    expiry = default.get("expiry", "")
    try:
        if dt.date.fromisoformat(expiry) <= dt.date.today():
            failures.append("default metadata expiry must be in the future")
    except ValueError:
        failures.append("default metadata expiry must be YYYY-MM-DD")

    baseline = metadata.get("baselineCount")
    if not isinstance(baseline, int) or baseline < 0:
        failures.append("baselineCount must be a non-negative integer")
    elif len(entries) > baseline:
        failures.append(f"binding-gap allowlist grew from baseline {baseline} to {len(entries)}")

    policy = metadata.get("policy", {})
    if policy.get("allowNetGrowth") is not False:
        failures.append("policy.allowNetGrowth must remain false")
    if policy.get("metadataFileIsReadOnly") is not True:
        failures.append("policy.metadataFileIsReadOnly must remain true")

    if failures:
        for failure in failures:
            print(f"FAIL {failure}")
        return 1
    print(f"binding gap metadata OK: {len(entries)} entries, baseline {baseline}, no net growth")
    return 0


if __name__ == "__main__":
    sys.exit(main())
