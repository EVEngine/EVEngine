#!/usr/bin/env python3
"""Validate the repository's bounded quality-debt allowlist.

The check covers four intentionally narrow inventories:

* ``binding_gap``: non-comment entries in ``check_bindings_gaps.txt``;
* ``todo``: explicit TODO/FIXME/HACK markers in repository source;
* ``soft_skip``: explicit ``soft-skip``/``soft skip`` markers;
* ``fallback``: explicit ``FALLBACK`` markers or fallback wording in comments.

Every inventory entry must match exactly one metadata scope with an owner,
issue, reason, expiry, and an explicit ``max_net_growth`` budget.  The baseline
stores the count for each metadata entry.  A normal check rejects unmatched
findings and any count increase above its budget; deleting old debt is allowed.
This keeps the gate bounded without requiring a one-shot cleanup of legacy
debt.

Usage::

    python3 scripts/check_quality_metadata.py
    python3 scripts/check_quality_metadata.py --json
    python3 scripts/check_quality_metadata.py --write-baseline

``--write-baseline`` is an explicit maintenance operation.  It is not used by
CI, because changing a baseline must remain visible in code review.
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import re
import sys
from collections import Counter
from dataclasses import asdict, dataclass
from datetime import date
from pathlib import Path
from typing import Any, Iterable, Mapping

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_METADATA = ROOT / "scripts" / "quality_debt_allowlist.json"
DEFAULT_BASELINE = ROOT / "scripts" / "quality_debt_baseline.json"
DEFAULT_BINDING_GAPS = ROOT / "scripts" / "check_bindings_gaps.txt"

SCHEMA_VERSION = 1
KINDS = frozenset({"binding_gap", "todo", "soft_skip", "fallback"})
SOURCE_EXTENSIONS = frozenset(
    {
        ".c",
        ".cc",
        ".cpp",
        ".cxx",
        ".h",
        ".hh",
        ".hpp",
        ".hxx",
        ".cmake",
        ".frag",
        ".glsl",
        ".nut",
        ".ps1",
        ".py",
        ".sh",
        ".toml",
        ".vert",
        ".yml",
        ".yaml",
    }
)
EXCLUDED_DIRS = frozenset(
    {
        ".git",
        ".codex",
        ".agents",
        ".opencode",
        "build",
        "docs",
        "external",
        "third-party",
        "fixtures_quality_metadata",
    }
)
EXCLUDED_FILES = frozenset(
    {
        "scripts/check_quality_metadata.py",
        "scripts/profile_matrix.py",
        "scripts/tests/test_quality_metadata.py",
    }
)
CONTROLLED_ROOTS = (
    "CMakeLists.txt",
    "Makefile",
    "cmake",
    "scripts",
    "src",
    "test",
    ".github",
)

TODO_RE = re.compile(r"\b(?:TODO|FIXME|HACK)\b")
SOFT_SKIP_RE = re.compile(r"\bsoft[- ]skip\b", re.IGNORECASE)
FALLBACK_RE = re.compile(r"\bfallback\b", re.IGNORECASE)


@dataclass(frozen=True)
class Finding:
    """One inventory finding, identified by kind, path, and source line."""

    kind: str
    path: str
    line: int
    text: str


def _relative(root: Path, path: Path) -> str:
    return path.relative_to(root).as_posix()


def _source_file(path: Path, relative: str) -> bool:
    if path.name in {"Makefile", "CMakeLists.txt"}:
        return True
    return path.suffix.lower() in SOURCE_EXTENSIONS and relative not in EXCLUDED_FILES


def iter_source_files(root: Path) -> Iterable[tuple[Path, str]]:
    """Yield source/config files in the controlled scan area."""

    candidates: list[Path] = []
    for name in CONTROLLED_ROOTS:
        candidate = root / name
        if candidate.is_file():
            candidates.append(candidate)
        elif candidate.is_dir():
            candidates.extend(candidate.rglob("*"))
    for path in sorted(candidates):
        if not path.is_file():
            continue
        relative = _relative(root, path)
        if any(part in EXCLUDED_DIRS for part in Path(relative).parts):
            continue
        if _source_file(path, relative):
            yield path, relative


def scan_source_findings(root: Path) -> list[Finding]:
    findings: list[Finding] = []
    for path, relative in iter_source_files(root):
        text = path.read_text(encoding="utf-8", errors="replace")
        for line_number, line in enumerate(text.splitlines(), start=1):
            if TODO_RE.search(line):
                findings.append(Finding("todo", relative, line_number, line.strip()))
            if SOFT_SKIP_RE.search(line):
                findings.append(Finding("soft_skip", relative, line_number, line.strip()))

            # A lowercase ``fallback`` in an ordinary identifier or parameter
            # is not a debt finding.  Uppercase FALLBACK is an explicit marker;
            # otherwise require the term to occur in a comment line.
            stripped = line.lstrip()
            in_comment = stripped.startswith(("//", "#", "/*", "*"))
            if "FALLBACK" in line or (in_comment and FALLBACK_RE.search(line)):
                findings.append(Finding("fallback", relative, line_number, line.strip()))
    return findings


def scan_binding_gaps(root: Path, binding_path: Path) -> list[Finding]:
    if not binding_path.is_absolute():
        binding_path = root / binding_path
    if not binding_path.is_file():
        return []
    relative = _relative(root, binding_path)
    findings: list[Finding] = []
    for line_number, line in enumerate(
        binding_path.read_text(encoding="utf-8", errors="replace").splitlines(), start=1
    ):
        stripped = line.strip()
        if stripped and not stripped.startswith("#"):
            findings.append(Finding("binding_gap", relative, line_number, stripped))
    return findings


def discover_findings(root: Path, binding_path: Path) -> list[Finding]:
    return scan_source_findings(root) + scan_binding_gaps(root, binding_path)


def _nonempty_string(value: Any) -> bool:
    return isinstance(value, str) and bool(value.strip())


def validate_metadata(metadata: Any, today: date | None = None) -> list[str]:
    today = today or date.today()
    errors: list[str] = []
    if not isinstance(metadata, Mapping):
        return ["metadata root must be an object"]
    if metadata.get("schema_version") != SCHEMA_VERSION:
        errors.append(f"metadata schema_version must be {SCHEMA_VERSION}")
    entries = metadata.get("entries")
    if not isinstance(entries, list):
        return errors + ["metadata entries must be an array"]

    seen: set[str] = set()
    required = {
        "id",
        "kind",
        "scope",
        "owner",
        "issue",
        "reason",
        "expiry",
        "max_net_growth",
    }
    for index, entry in enumerate(entries):
        prefix = f"metadata entries[{index}]"
        if not isinstance(entry, Mapping):
            errors.append(f"{prefix} must be an object")
            continue
        missing = sorted(required - set(entry))
        if missing:
            errors.append(f"{prefix} missing: {', '.join(missing)}")
            continue
        for field in ("id", "scope", "owner", "issue", "reason"):
            if not _nonempty_string(entry.get(field)):
                errors.append(f"{prefix}.{field} must be a non-empty string")
        entry_id = entry.get("id")
        if isinstance(entry_id, str) and entry_id in seen:
            errors.append(f"{prefix}.id duplicates '{entry_id}'")
        if isinstance(entry_id, str):
            seen.add(entry_id)
        if entry.get("kind") not in KINDS:
            errors.append(f"{prefix}.kind must be one of {', '.join(sorted(KINDS))}")
        growth = entry.get("max_net_growth")
        if isinstance(growth, bool) or not isinstance(growth, int) or growth < 0:
            errors.append(f"{prefix}.max_net_growth must be a non-negative integer")
        expiry = entry.get("expiry")
        if not isinstance(expiry, str):
            errors.append(f"{prefix}.expiry must be ISO date YYYY-MM-DD")
        else:
            try:
                expiry_date = date.fromisoformat(expiry)
            except ValueError:
                errors.append(f"{prefix}.expiry is not an ISO date: {expiry}")
            else:
                if expiry_date < today:
                    errors.append(f"{prefix}.expiry has passed: {expiry}")
    return errors


def validate_baseline(baseline: Any) -> list[str]:
    if not isinstance(baseline, Mapping):
        return ["baseline root must be an object"]
    errors: list[str] = []
    if baseline.get("schema_version") != SCHEMA_VERSION:
        errors.append(f"baseline schema_version must be {SCHEMA_VERSION}")
    counts = baseline.get("counts")
    if not isinstance(counts, Mapping):
        errors.append("baseline counts must be an object")
    else:
        for key, value in counts.items():
            if not isinstance(key, str) or isinstance(value, bool) or not isinstance(value, int) or value < 0:
                errors.append(f"baseline count {key!r} must be a non-negative integer")
    return errors


def _load_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        raise ValueError(f"file not found: {path}")
    except json.JSONDecodeError as error:
        raise ValueError(f"invalid JSON in {path}: {error}")


def _entries(metadata: Mapping[str, Any]) -> list[Mapping[str, Any]]:
    return [entry for entry in metadata.get("entries", []) if isinstance(entry, Mapping)]


def _matching_entries(finding: Finding, entries: list[Mapping[str, Any]]) -> list[Mapping[str, Any]]:
    return [
        entry
        for entry in entries
        if entry.get("kind") == finding.kind
        and isinstance(entry.get("scope"), str)
        and fnmatch.fnmatchcase(finding.path, entry["scope"])
    ]


def evaluate(
    metadata: Mapping[str, Any],
    baseline: Mapping[str, Any],
    findings: list[Finding],
    today: date | None = None,
) -> tuple[list[str], dict[str, int]]:
    """Return gate errors and the current count per metadata entry id."""

    errors = validate_metadata(metadata, today=today)
    errors.extend(validate_baseline(baseline))
    entries = _entries(metadata)
    counts: Counter[str] = Counter()
    entry_ids = {entry.get("id") for entry in entries}
    baseline_counts = baseline.get("counts", {})
    if not isinstance(baseline_counts, Mapping):
        baseline_counts = {}

    for finding in findings:
        matches = _matching_entries(finding, entries)
        if not matches:
            errors.append(
                f"unallowlisted {finding.kind} at {finding.path}:{finding.line}: {finding.text}"
            )
            continue
        if len(matches) > 1:
            ids = ", ".join(str(entry.get("id")) for entry in matches)
            errors.append(
                f"ambiguous metadata for {finding.kind} at {finding.path}:{finding.line}: {ids}"
            )
            continue
        entry_id = matches[0].get("id")
        if isinstance(entry_id, str):
            counts[entry_id] += 1

    if isinstance(baseline_counts, Mapping):
        unknown_baseline = sorted(set(baseline_counts) - entry_ids)
        for entry_id in unknown_baseline:
            errors.append(f"baseline has no metadata entry for '{entry_id}'")

    for entry in entries:
        entry_id = entry.get("id")
        if not isinstance(entry_id, str):
            continue
        if entry_id not in baseline_counts:
            errors.append(f"baseline has no count for metadata entry '{entry_id}'")
            continue
        current = counts.get(entry_id, 0)
        previous = baseline_counts[entry_id]
        if not isinstance(previous, int) or isinstance(previous, bool):
            continue
        delta = current - previous
        budget = entry.get("max_net_growth", 0)
        if isinstance(budget, int) and not isinstance(budget, bool) and delta > budget:
            errors.append(
                f"{entry_id}: net growth {delta} exceeds budget {budget} "
                f"(baseline={previous}, current={current})"
            )
    return errors, dict(counts)


def write_baseline(path: Path, counts: Mapping[str, int]) -> None:
    payload = {
        "schema_version": SCHEMA_VERSION,
        "generated_at": date.today().isoformat(),
        "counts": dict(sorted(counts.items())),
    }
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def _report(
    errors: list[str], findings: list[Finding], counts: Mapping[str, int], as_json: bool
) -> int:
    if as_json:
        print(
            json.dumps(
                {
                    "findings": [asdict(finding) for finding in findings],
                    "counts": dict(sorted(counts.items())),
                    "errors": errors,
                },
                ensure_ascii=False,
                indent=2,
            )
        )
    else:
        for error in errors:
            print(f"FAIL {error}", file=sys.stderr)
        print(
            "quality metadata: "
            + ", ".join(f"{kind}={counts.get(kind, 0)}" for kind in sorted(counts))
            + f"; findings={len(findings)}"
        )
    return 1 if errors else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--metadata", type=Path, default=DEFAULT_METADATA)
    parser.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE)
    parser.add_argument("--binding-gaps", type=Path, default=DEFAULT_BINDING_GAPS)
    parser.add_argument("--json", action="store_true", help="emit machine-readable output")
    parser.add_argument(
        "--write-baseline",
        action="store_true",
        help="write current matched counts to --baseline (explicit maintenance operation)",
    )
    args = parser.parse_args(argv)

    try:
        metadata = _load_json(args.metadata)
    except ValueError as error:
        print(f"FAIL {error}", file=sys.stderr)
        return 1
    if not isinstance(metadata, Mapping):
        print("FAIL metadata root must be an object", file=sys.stderr)
        return 1

    binding_path = args.binding_gaps
    if not binding_path.is_absolute():
        binding_path = args.root / binding_path
    findings = discover_findings(args.root, binding_path)
    metadata_errors: list[str] = []
    if not binding_path.is_file():
        metadata_errors.append(f"binding gap file not found: {binding_path}")

    if args.write_baseline:
        metadata_errors.extend(validate_metadata(metadata))
        entries = _entries(metadata)
        counts: Counter[str] = Counter()
        for finding in findings:
            matches = _matching_entries(finding, entries)
            if len(matches) != 1 or not isinstance(matches[0].get("id"), str):
                metadata_errors.append(
                    f"cannot baseline unallowlisted/ambiguous finding: "
                    f"{finding.kind} at {finding.path}:{finding.line}"
                )
            else:
                counts[matches[0]["id"]] += 1
        if metadata_errors:
            return _report(metadata_errors, findings, counts, args.json)
        write_baseline(args.baseline, counts)
        print(f"wrote quality debt baseline: {args.baseline}")
        return 0

    try:
        baseline = _load_json(args.baseline)
    except ValueError as error:
        print(f"FAIL {error}", file=sys.stderr)
        return 1
    if not isinstance(baseline, Mapping):
        print("FAIL baseline root must be an object", file=sys.stderr)
        return 1
    errors, counts = evaluate(metadata, baseline, findings)
    errors = metadata_errors + errors
    return _report(errors, findings, counts, args.json)


if __name__ == "__main__":
    sys.exit(main())
