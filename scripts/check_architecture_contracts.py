#!/usr/bin/env python3
"""Check the executable architecture-contract policy.

This gate has two deliberately different jobs:

* validate the reviewed contract catalogue (ownership, links, ECS systems,
  time, persistence, capabilities, backends and debt); and
* lint only *added* C/C++ lines for the high-signal API mistakes which are
  cheap to detect without a C++ parser (ambiguous operation ``bool``,
  ``lastError`` channels and undocumented raw-pointer APIs).

The changed-line mode is important.  It makes the rule a no-net-growth gate
for a dirty worktree or a pull request without pretending that a regular
expression can prove every property of old C++ code.  Existing debt belongs
in the existing reviewed baseline/allowlist; it is never silently moved into
this gate's baseline.

Usage::

    python3 scripts/check_architecture_contracts.py
    python3 scripts/check_architecture_contracts.py --base origin/dev
    python3 scripts/check_architecture_contracts.py --all
    python3 scripts/check_architecture_contracts.py --json

``--all`` validates the catalogue and reports the same API smells over every
source file.  CI uses the default changed-only mode so an old violation cannot
hide a newly introduced one.
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from datetime import date, datetime
from pathlib import Path
from typing import Any, Iterable, Mapping

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_METADATA = ROOT / "scripts" / "architecture_contracts.json"

RULES = (
    "api-shape",
    "link",
    "state-owner",
    "ecs-system",
    "time-rng",
    "api-lifetime",
    "persistence",
    "optional-capability",
    "backend-contract",
    "debt-metadata",
)

COMMON_REQUIRED = {
    "owner",
    "issue",
    "reason",
    "expiry",
    "evidence",
    "tests",
}
RULE_REQUIRED = {
    "api-shape": {"result_policy", "nodiscard_policy", "pointer_policy"},
    "link": {"symbols", "create", "ownership", "destroy_order", "restore", "stale"},
    "state-owner": {"state", "authoritative_owner", "projections"},
    "ecs-system": {
        "entity_scope",
        "view",
        "read_set",
        "write_set",
        "structural_changes",
        "events",
        "services",
        "phase",
        "systems",
    },
    "time-rng": {"time_source", "rng_stream", "determinism", "tolerance"},
    "api-lifetime": {
        "thread_affinity",
        "reentrancy",
        "ownership_contract",
        "lifetime",
        "lock_callback_policy",
    },
    "persistence": {
        "schema",
        "version",
        "migration",
        "unknown_fields",
        "restore_atomicity",
    },
    "optional-capability": {
        "present_test",
        "absent_test",
        "fallback_observable",
        "fallback_policy",
    },
    "backend-contract": {
        "contract",
        "providers",
        "shared_contract_tests",
        "failure_injection",
    },
    "debt-metadata": {
        "removal_condition",
        "max_net_growth",
    },
}


@dataclass(frozen=True)
class SourceLine:
    path: str
    line: int
    text: str


@dataclass(frozen=True)
class Finding:
    rule: str
    code: str
    path: str
    line: int
    message: str

    def as_dict(self) -> dict[str, Any]:
        return {
            "rule": self.rule,
            "code": self.code,
            "path": self.path,
            "line": self.line,
            "message": self.message,
        }


def load_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read {path}: {error}") from error


def relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def path_matches(path: str, scope: str) -> bool:
    # fnmatch's ** behaviour differs slightly between Python versions.  Try
    # both the spelling supplied by the metadata and a slash-normalized one.
    candidate = path.replace("\\", "/")
    pattern = scope.replace("\\", "/")
    return fnmatch.fnmatch(candidate, pattern) or fnmatch.fnmatch(
        candidate, pattern.replace("**/", "*/")
    )


def nonempty_string(value: Any) -> bool:
    return isinstance(value, str) and bool(value.strip())


def nonempty_list(value: Any) -> bool:
    return isinstance(value, list) and bool(value) and all(nonempty_string(item) for item in value)


def validate_catalogue(metadata: Any, today: date | None = None) -> list[str]:
    """Validate the contract catalogue without inspecting source code."""

    today = today or date.today()
    errors: list[str] = []
    if not isinstance(metadata, Mapping):
        return ["metadata root must be an object"]
    if metadata.get("schema_version") != 1:
        errors.append("metadata schema_version must be 1")
    entries = metadata.get("entries")
    if not isinstance(entries, list):
        return errors + ["metadata entries must be an array"]

    seen: set[str] = set()
    covered: set[str] = set()
    for index, entry in enumerate(entries):
        prefix = f"entries[{index}]"
        if not isinstance(entry, Mapping):
            errors.append(f"{prefix} must be an object")
            continue
        entry_id = entry.get("id")
        rule = entry.get("rule")
        if not nonempty_string(entry_id):
            errors.append(f"{prefix}.id must be a non-empty string")
        elif entry_id in seen:
            errors.append(f"duplicate contract id: {entry_id}")
        else:
            seen.add(entry_id)
        if rule not in RULES:
            errors.append(f"{prefix}.rule must be one of {', '.join(RULES)}")
        else:
            covered.add(rule)
        for field in ("scope", "owner", "issue", "reason"):
            if not nonempty_string(entry.get(field)):
                errors.append(f"{prefix}.{field} must be a non-empty string")
        scope = entry.get("scope")
        if nonempty_string(scope):
            scoped_files = [
                candidate
                for candidate in ROOT.rglob("*")
                if candidate.is_file() and path_matches(relative(candidate), scope)
            ]
            if not scoped_files:
                errors.append(f"{prefix}.scope matches no repository file: {scope}")
            if rule in {"link", "ecs-system"} and scope in {"src/**", "src/*", "src/**/*"}:
                errors.append(f"{prefix}.{rule} scope must be path-specific, not {scope}")
            names_field = "symbols" if rule == "link" else "systems" if rule == "ecs-system" else None
            if names_field is not None:
                names = entry.get(names_field)
                if not nonempty_list(names):
                    errors.append(f"{prefix}.{names_field} must be a non-empty string array")
                else:
                    source_text = "\n".join(
                        candidate.read_text(encoding="utf-8", errors="replace")
                        for candidate in scoped_files
                    )
                    for name in names:
                        if re.search(r"\b" + re.escape(name) + r"\b", source_text) is None:
                            errors.append(f"{prefix}.{names_field} symbol not found in scope: {name}")
        for field in ("evidence", "tests"):
            if not nonempty_list(entry.get(field)):
                errors.append(f"{prefix}.{field} must be a non-empty string array")
            else:
                for item in entry[field]:
                    evidence_path = ROOT / item
                    if not evidence_path.is_file():
                        errors.append(f"{prefix}.{field} references missing file {item}")
        expiry = entry.get("expiry")
        try:
            expiry_date = datetime.strptime(expiry, "%Y-%m-%d").date()
            if expiry_date < today:
                errors.append(f"{prefix}.expiry is past: {expiry}")
        except (TypeError, ValueError):
            errors.append(f"{prefix}.expiry must be YYYY-MM-DD")
        if rule in RULES:
            for field in COMMON_REQUIRED | RULE_REQUIRED[rule]:
                if field not in entry:
                    errors.append(f"{prefix} ({rule}) is missing {field}")
            if rule == "debt-metadata":
                growth = entry.get("max_net_growth")
                if not isinstance(growth, int) or growth < 0:
                    errors.append(f"{prefix}.max_net_growth must be a non-negative integer")

    missing = sorted(set(RULES) - covered)
    if missing:
        errors.append("catalogue has no entry for rule(s): " + ", ".join(missing))
    return errors


def _git(args: list[str]) -> str:
    try:
        result = subprocess.run(
            ["git", *args],
            cwd=ROOT,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError):
        return ""
    return result.stdout


def _changed_lines(base: str | None) -> list[SourceLine]:
    """Return added C/C++ lines from a git diff and untracked source files."""

    diff_args = ["diff", "--no-color", "--unified=0"]
    if base:
        diff_args.append(base)
    diff_args.extend(["--", "src/**/*.h", "src/**/*.hpp", "src/**/*.cpp"])
    diff = _git(diff_args)
    lines: list[SourceLine] = []
    current: str | None = None
    new_line = 0
    for raw in diff.splitlines():
        if raw.startswith("+++ b/"):
            current = raw[6:]
            continue
        if raw.startswith("@@"):
            match = re.search(r"\+(\d+)(?:,(\d+))?", raw)
            if match:
                new_line = int(match.group(1))
            continue
        if current is None or not raw.startswith("+") or raw.startswith("+++"):
            if current is not None and raw.startswith("-"):
                continue
            continue
        lines.append(SourceLine(current, new_line, raw[1:]))
        new_line += 1

    untracked = _git(["ls-files", "--others", "--exclude-standard", "--", "src"])
    known = {(item.path, item.line) for item in lines}
    for name in untracked.splitlines():
        path = ROOT / name
        if path.suffix not in {".h", ".hpp", ".cpp"} or not path.is_file():
            continue
        for number, text in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            if (name, number) not in known:
                lines.append(SourceLine(name, number, text))
    return lines


def _all_lines() -> list[SourceLine]:
    result: list[SourceLine] = []
    for path in sorted((ROOT / "src").rglob("*")):
        if path.suffix not in {".h", ".hpp", ".cpp"} or not path.is_file():
            continue
        for number, text in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            result.append(SourceLine(relative(path), number, text))
    return result


BOOL_RETURN = re.compile(
    r"^\s*(?:(?:\[\[(?:nodiscard|deprecated)(?:\([^]]*\))?\]\]\s*)*"
    r"(?:(?:static|virtual|inline|constexpr|explicit)\s+)*)bool\s+"
    r"([A-Za-z_]\w*)\s*\("
)
POINTER_API = re.compile(
    r"^\s*(?:(?:\[\[nodiscard(?:\([^]]*\))?\]\]\s*)?"
    r"(?:static|virtual|inline|constexpr|explicit|const)\s+)*"
    r"(?:[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*(?:\s*<[^;{}()]+>)?\s*)?\*\s*"
    r"(?:[A-Za-z_]\w*)?\s*(?:\(|;|=)"
)
# A bool query is a legitimate API shape when its name follows the project's
# query vocabulary.  Keep this list deliberately lexical: this checker does
# not have a C++ type system, so it must not infer intent from a return value's
# callers.  The first-token forms cover names such as ``activeExecuted`` and
# ``usingGpu``; the last-token forms cover names such as
# ``transactionStateEquals``.  Exact names cover state predicates whose
# natural spelling has no ``is`` prefix (``ok``, ``active``, ...).
PREDICATE_FIRST_TOKENS = frozenset(
    {
        "is",
        "get",
        "has",
        "can",
        "should",
        "supports",
        "contains",
        "empty",
        "valid",
        "matches",
        "equals",
        "ok",
        "active",
        "paused",
        "passed",
        "owns",
        "using",
        "used",
        "critical",
        "disposed",
        "changed",
        "ready",
        "available",
        "enabled",
        "visible",
        "playing",
        "finished",
        "stale",
        "bound",
        "dirty",
        "as",
        "exact",
        "initialized",
    }
)
PREDICATE_LAST_TOKENS = frozenset(
    {
        "matches",
        "equals",
        "changed",
        "active",
        "paused",
        "passed",
        "disposed",
        "critical",
        "owned",
        "ready",
        "available",
        "valid",
        "visible",
        "enabled",
        "stale",
        "dirty",
    }
)


def _identifier_tokens(name: str) -> list[str]:
    """Split a C++ identifier into lower-case semantic name tokens."""

    words = re.sub(r"([a-z0-9])([A-Z])", r"\1 \2", name).replace("_", " ")
    return [word.lower() for word in words.split() if word]


def _is_predicate_name(name: str) -> bool:
    tokens = _identifier_tokens(name)
    if not tokens:
        return False
    return tokens[0] in PREDICATE_FIRST_TOKENS or tokens[-1] in PREDICATE_LAST_TOKENS


def _is_compatibility_declaration(text: str, context: str) -> bool:
    """Recognize an explicitly documented one-way compatibility facade.

    Looking for these words anywhere in a class made the old checker suppress
    unrelated declarations after a single legacy method.  Restrict the
    exemption to the declaration's own attribute or nearest Doxygen block.
    """

    if re.search(r"\[\[deprecated(?:\([^]]*\))?\]\]", text, re.IGNORECASE):
        return True
    blocks = re.findall(r"/\*\*.*?\*/", context, flags=re.DOTALL)
    if not blocks:
        return False
    return bool(re.search(r"\b(?:deprecated|compatibility|legacy)\b", blocks[-1], re.IGNORECASE))


def _context(lines: list[SourceLine], index: int) -> str:
    """Read the real preceding source, not only the diff hunk.

    A Doxygen contract is often unchanged while its declaration is edited. A
    changed-line lint must therefore inspect the complete file context or it
    would report a false violation merely because the documentation was not
    touched in the same patch.
    """

    item = lines[index]
    source = ROOT / item.path
    if source.is_file():
        content = source.read_text(encoding="utf-8", errors="replace").splitlines()
        start = max(0, item.line - 25)
        return "\n".join(content[start : item.line])
    start = max(0, index - 24)
    return "\n".join(candidate.text for candidate in lines[start : index + 1] if candidate.path == item.path)


def _is_non_public_declaration(item: SourceLine) -> bool:
    """Return whether a header declaration is under ``private``/``protected``.

    API-shape lint is intentionally about public contracts.  A private helper
    in a header may use a scalar control result internally without creating a
    public compatibility surface.  This small access-label check is the
    narrow parser-free approximation; it only treats an explicit access label
    immediately preceding the declaration as authoritative.
    """

    source = ROOT / item.path
    if not source.is_file() or item.line <= 0:
        return False
    prefix = source.read_text(encoding="utf-8", errors="replace").splitlines()[: item.line - 1]
    # Remove comments while retaining line breaks.  The small brace scanner
    # below tracks class scopes instead of taking the last access label in the
    # whole file (which would misclassify a later public class after an earlier
    # private section).
    text = "\n".join(prefix)
    text = re.sub(r"/\*.*?\*/", lambda match: "\n" * match.group(0).count("\n"), text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", "", text)

    scopes: list[tuple[bool, str]] = []
    pending_class: str | None = None
    for line in text.splitlines():
        class_match = re.search(r"\b(class|struct)\s+[A-Za-z_]\w*[^;{]*", line)
        if class_match:
            pending_class = "public" if class_match.group(1) == "struct" else "private"
        access_match = re.match(r"\s*(public|protected|private)\s*:", line)
        if access_match and scopes and scopes[-1][0]:
            scopes[-1] = (True, access_match.group(1))
        for character in line:
            if character == "{":
                if pending_class is not None:
                    scopes.append((True, pending_class))
                    pending_class = None
                else:
                    scopes.append((False, ""))
            elif character == "}" and scopes:
                scopes.pop()
    return bool(scopes and scopes[-1][0] and scopes[-1][1] in {"private", "protected"})


def lint_api_shapes(lines: list[SourceLine]) -> list[Finding]:
    findings: list[Finding] = []
    for index, item in enumerate(lines):
        text = item.text
        context = _context(lines, index).lower()
        # Reads/assignments in an implementation are often the compatibility
        # facade's internal plumbing.  The dangerous API shape is declaring a
        # second error channel in a public header; keep this check high-signal.
        if re.search(
            r"\b(?:bool|Status|int)\s+[A-Za-z_]\w*\s*\([^;{}]*"
            r"(?:lastError|last_error|\b(?:err|error)\b)\s*\*",
            text,
            re.IGNORECASE,
        ):
            findings.append(
                Finding(
                    "api-shape",
                    "bool-error-pointer-channel",
                    item.path,
                    item.line,
                    "operation combines a scalar result with an error pointer; return structured Result/Diagnostic",
                )
            )
        declares_last_error = item.path.endswith((".h", ".hpp")) and re.search(
            r"\b(?:lastError|last_error)(?:_|\b)\s*(?:[;=,)])", text
        )
        if declares_last_error:
            findings.append(
                Finding(
                    "api-shape",
                    "last-error-channel",
                    item.path,
                    item.line,
                    "new code mentions lastError; return structured Result/Diagnostic instead",
                )
            )
        # Definitions inside an anonymous namespace are implementation
        # predicates/helpers, not public API.  Public declarations live in
        # headers and are the safe, parser-free surface to lint here.
        match = BOOL_RETURN.match(text) if item.path.endswith((".h", ".hpp")) else None
        if match:
            name = match.group(1)
            if _is_non_public_declaration(item):
                continue
            lower_name = name.lower()
            predicate = _is_predicate_name(name) or lower_name in {
                "operator bool",
                "operator==",
                "operator!=",
            }
            compatibility = _is_compatibility_declaration(text, context)
            if not predicate and not compatibility:
                findings.append(
                    Finding(
                        "api-shape",
                        "ambiguous-operation-bool",
                        item.path,
                        item.line,
                        f"operation {name} returns bool; use a Result or named status enum",
                    )
                )
        if item.path.endswith((".h", ".hpp")) and "*" in text and "(" in text:
            if POINTER_API.search(text) and not re.search(
                r"@(?:ownership|lifetime|borrowed|owned|outlives|thread)",
                context,
                re.IGNORECASE,
            ):
                findings.append(
                    Finding(
                        "api-lifetime",
                        "undocumented-raw-pointer-api",
                        item.path,
                        item.line,
                        "public raw-pointer API needs Doxygen ownership and lifetime contract",
                    )
                )
    return findings


def contract_matches(path: str, rule: str, entries: Iterable[Mapping[str, Any]]) -> list[Mapping[str, Any]]:
    return [entry for entry in entries if entry.get("rule") == rule and path_matches(path, entry.get("scope", ""))]


def lint_contract_coverage(lines: list[SourceLine], metadata: Mapping[str, Any]) -> list[Finding]:
    """Require a catalogue entry for newly introduced contract surfaces.

    This is intentionally a high-signal coverage check.  The broad policy
    entries in the catalogue cover established conventions; concrete Link and
    ECS declarations must additionally be represented by a path-scoped entry.
    """

    entries = [entry for entry in metadata.get("entries", []) if isinstance(entry, Mapping)]
    findings: list[Finding] = []
    seen: set[tuple[str, str]] = set()
    for item in lines:
        text = item.text
        triggers: list[tuple[str, str]] = []
        if re.search(r"\b(?:struct|class)\s+[A-Za-z_]\w*Link\b|\busing\s+\w*Link\b", text):
            triggers.append(("link", "new Link declaration"))
        if re.search(r"\b(?:class|struct)\s+[A-Za-z_]\w*System\b", text):
            triggers.append(("ecs-system", "new System declaration"))
        if re.search(r"\b(?:SimulationStep|Rng|RNG|seedFor)\b", text):
            triggers.append(("time-rng", "injected time/RNG surface"))
        if re.search(r"\b(?:restore|Snapshot|schema_version|migrate)\b", text, re.IGNORECASE):
            triggers.append(("persistence", "persistent or restore surface"))
        if re.search(r"\b(?:Capability|Provider|Backend)\b", text):
            triggers.append(("backend-contract", "provider/backend surface"))
        for rule, description in triggers:
            key = (item.path, rule)
            if key in seen:
                continue
            seen.add(key)
            matches = contract_matches(item.path, rule, entries)
            if not matches:
                findings.append(
                    Finding(rule, "missing-contract-entry", item.path, item.line, f"{description} has no catalogue entry")
                )
    return findings


def render(findings: list[Finding], catalogue_errors: list[str], json_output: bool) -> int:
    if json_output:
        print(
            json.dumps(
                {"catalogue_errors": catalogue_errors, "findings": [finding.as_dict() for finding in findings]},
                ensure_ascii=False,
                indent=2,
            )
        )
    else:
        for error in catalogue_errors:
            print(f"FAIL catalogue: {error}")
        for finding in findings:
            print(f"FAIL {finding.rule} {finding.path}:{finding.line}: {finding.message}")
        if not catalogue_errors and not findings:
            print("architecture contracts OK: catalogue valid; no new high-signal violations")
    return 1 if catalogue_errors or findings else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--metadata", type=Path, default=DEFAULT_METADATA)
    parser.add_argument("--base", help="git base for changed-only mode (default: CI base or HEAD)")
    parser.add_argument("--all", action="store_true", help="lint all src C/C++ lines instead of only additions")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    try:
        metadata = load_json(args.metadata)
    except ValueError as error:
        return render([], [str(error)], args.json)
    catalogue_errors = validate_catalogue(metadata)
    if not isinstance(metadata, Mapping):
        return render([], catalogue_errors, args.json)
    if args.all:
        lines = _all_lines()
    else:
        base = args.base
        if base is None:
            base = os.environ.get("EVENGINE_ARCHITECTURE_BASE")
        lines = _changed_lines(base or "HEAD")
    findings = lint_api_shapes(lines) + lint_contract_coverage(lines, metadata)
    return render(findings, catalogue_errors, args.json)


if __name__ == "__main__":
    sys.exit(main())
