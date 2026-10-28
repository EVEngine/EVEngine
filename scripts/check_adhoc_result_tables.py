#!/ usr / bin / env python3
"""Forbid hand-built `{ok, ...}` Squirrel Result tables outside a shrink-only allowlist.

Canonical script Results must come from `eve::script::projectResult` /
`projectStatusResult` in `SquirrelBinding.h`. New `.set("ok", ...)` sites in
`src/` fail unless the file is listed in `adhoc_result_tables_allowlist.json`.
Allowlisted occurrences may shrink but must not grow past `baselineCount`.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import re
import sys
from pathlib import Path

import utf8_stdio

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_ALLOWLIST = ROOT / "scripts" / "adhoc_result_tables_allowlist.json"
OK_SET = re.compile(r"""\.set\(\s*["']ok["']\s*,""")
REQUIRED = ("owner", "issue", "reason", "expiry")


def discover_hits(src_root: Path) -> list[tuple[str, int, str]]:
    hits: list[tuple[str, int, str]] = []
    for path in sorted(src_root.rglob("*.cpp")):
        rel = path.relative_to(ROOT).as_posix()
        try:
            text = path.read_text(encoding="utf-8")
        except OSError as error:
            raise SystemExit(f"FAIL cannot read {rel}: {error}") from error
        for line_no, line in enumerate(text.splitlines(), start=1):
            if OK_SET.search(line):
                hits.append((rel, line_no, line.strip()))
    return hits


def load_allowlist(path: Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise SystemExit(f"FAIL allowlist cannot be read: {error}") from error


def validate_allowlist(data: dict, today: dt.date) -> list[str]:
    failures: list[str] = []
    if data.get("schema_version") != 1:
        failures.append("schema_version must be 1")
    baseline = data.get("baselineCount")
    if not isinstance(baseline, int) or baseline < 0:
        failures.append("baselineCount must be a non-negative integer")
    policy = data.get("policy", {})
    if policy.get("allowNetGrowth") is not False:
        failures.append("policy.allowNetGrowth must remain false")
    if policy.get("metadataFileIsReadOnly") is not True:
        failures.append("policy.metadataFileIsReadOnly must remain true")
    default = data.get("default", {})
    for key in REQUIRED:
        if not isinstance(default.get(key), str) or not default[key].strip():
            failures.append(f"default metadata is missing {key}")
    expiry = default.get("expiry", "")
    try:
        if dt.date.fromisoformat(expiry) <= today:
            failures.append("default metadata expiry must be in the future")
    except ValueError:
        failures.append("default metadata expiry must be YYYY-MM-DD")
    files = data.get("files")
    if not isinstance(files, dict):
        failures.append("files must be an object keyed by relative path")
    else:
        for rel, meta in files.items():
            if not isinstance(rel, str) or not rel.startswith("src/"):
                failures.append(f"allowlisted path must be under src/: {rel!r}")
            if meta is None or not isinstance(meta, dict):
                failures.append(f"allowlist entry for {rel} must be an object")
                continue
            merged = {
    **default, **meta}
            for key in REQUIRED:
                if not isinstance(merged.get(key), str) or not merged[key].strip():
                    failures.append(f"{rel} is missing {key}")
            try:
                if dt.date.fromisoformat(merged.get("expiry", "")) <= today:
                    failures.append(f"{rel} expiry must be in the future")
            except ValueError:
                failures.append(f"{rel} expiry must be YYYY-MM-DD")
    exempt = data.get("exempt", [])
    if not isinstance(exempt, list) or any(not isinstance(item, str) for item in exempt):
        failures.append("exempt must be a list of relative paths")
    return failures


def evaluate(hits: list[tuple[str, int, str]], data: dict) -> list[str]:
    failures: list[str] = []
    exempt = set(data.get("exempt", []))
    allowed_files = set(data.get("files", {}))
    allowlisted_hits = [hit for hit in hits if hit[0] in allowed_files]
    for rel, line_no, text in hits:
        if rel in exempt:
            continue
        if rel in allowed_files:
            continue
        failures.append(f"unallowlisted ad-hoc Result table at {rel}:{line_no}: {text}")
    baseline = data["baselineCount"]
    count = len(allowlisted_hits)
    if count > baseline:
        failures.append(
            f"ad-hoc Result allowlist grew from baseline {baseline} to {count}"
        )
#Stale allowlist entries(file listed but no hits) are OK — they document intent
#to finish migration — but warn so the list can shrink.
    for rel in sorted(allowed_files):
        if not any(hit[0] == rel for hit in allowlisted_hits):
            failures.append(
                f"allowlisted file has no remaining .set(\"ok\") sites; remove it: {rel}"
            )
    return failures


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--allowlist",
        type=Path,
        default=DEFAULT_ALLOWLIST,
        help="Path to the shrink-only allowlist JSON",
    )
    parser.add_argument(
        "--src",
        type=Path,
        default=ROOT / "src",
        help="Source tree to scan (default: repo src/)",
    )
    args = parser.parse_args(argv)

    data = load_allowlist(args.allowlist)
    meta_failures = validate_allowlist(data, today=dt.date.today())
    if meta_failures:
        for failure in meta_failures:
            print(f"FAIL {failure}")
        return 1

    hits = discover_hits(args.src)
    failures = evaluate(hits, data)
    if failures:
        for failure in failures:
            print(f"FAIL {failure}")
        return 1

    allowlisted = sum(1 for rel, _, _ in hits if rel in data.get("files", {}))
    print(
        f"ad-hoc Result tables OK: {allowlisted} allowlisted sites "
        f"(baseline {data['baselineCount']}), "
        f"{len(data.get('exempt', []))} projector exempt files"
    )
    return 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    sys.exit(main())
