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
import utf8_stdio

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_METADATA_DIR = ROOT / "scripts" / "architecture_contracts"
DEFAULT_METADATA = DEFAULT_METADATA_DIR
COMPOSED_METADATA = ROOT / "scripts" / "architecture_contracts.json"

RULES = (
    "api-shape",
    "link",
    "state-owner",
    "ecs-system",
    "time-rng",
    "api-lifetime",
    "module-interface",
    "persistence",
    "optional-capability",
    "backend-contract",
    "module-interface",
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
    "module-interface": {
        "provides", "requires", "emits", "observes", "binds", "protocol",
        "thread_affinity", "hot_path", "trim", "cost_notes",
    },
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
    "module-interface": {
        "provides", "requires", "emits", "observes", "binds", "protocol",
        "thread_affinity", "trim", "cost_notes", "hot_path",
    },
}

# Broad scopes used only by policy envelopes; concrete modules must be narrower.
_POLICY_SCOPES = frozenset({"src/**", "src/*", "src/**/*"})

CAP_CALL = re.compile(
    r"\b(?:eve::)?cap::(provide|addListener|removeListener|query|forEach|forEachUntil)"
    r"\s*<\s*((?:[A-Za-z_]\w*::)*[A-Za-z_]\w*)\s*>"
)
PROVIDER_REF_BIND = re.compile(
    r"\b(?:eve::)?cap::ProviderRef\s*<\s*((?:[A-Za-z_]\w*::)*[A-Za-z_]\w*)\s*>::\s*bind\s*\("
)
HOT_PATH_PRIMITIVE = re.compile(
    r"\b(?:getModInst|requireModInst)\s*\(|ModuleManager::getInstance|"
    r"\b(?:eve::)?cap::(?:query|forEach|forEachUntil)\s*<"
)
RUNTIME_LOOKUP = re.compile(r"\b(?:getModInst|requireModInst)\s*\(|ModuleManager::getInstance")
EXPENSIVE_API_NAME = re.compile(
    r"\b(?:Readback|ReadAll|Load|Save|Build|Compile|Bake|Upload|Capture|Materialize|Collect)\w*\s*\("
)
OWNED_CONTAINER_RETURN = re.compile(
    r"^\s*(?:(?:\[\[(?:nodiscard|deprecated)(?:\([^]]*\))?\]\]\s*)*"
    r"(?:(?:static|virtual|inline|constexpr|explicit)\s+)*)"
    r"(?:std::)?(?:vector|string|map|unordered_map|set|unordered_set)\s*<"
)


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


def compose_catalogue(directory: Path) -> dict[str, Any]:
    """Merge per-module JSON shards into one catalogue.

    Shards live under ``scripts/architecture_contracts/*.json``.  Global
    (cross-module) rules use ``_global.json``.  Entry order is the canonical
    rule/id order so reviewers and CI see one composed view without editing a
    shared blob when a single module's contracts change.
    """

    if not directory.is_dir():
        raise ValueError(f"catalogue directory missing: {directory}")
    shard_paths = sorted(directory.glob("*.json"))
    if not shard_paths:
        raise ValueError(f"catalogue directory has no shards: {directory}")

    schema_version: Any = None
    entries: list[Any] = []
    seen_ids: set[str] = set()
    for shard in shard_paths:
        try:
            payload = json.loads(shard.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            raise ValueError(f"cannot read {shard}: {error}") from error
        if not isinstance(payload, Mapping):
            raise ValueError(f"{shard}: catalogue shard must be an object")
        version = payload.get("schema_version")
        if schema_version is None:
            schema_version = version
        elif version != schema_version:
            raise ValueError(
                f"{shard}: schema_version {version!r} disagrees with {schema_version!r}"
            )
        shard_entries = payload.get("entries")
        if not isinstance(shard_entries, list):
            raise ValueError(f"{shard}: entries must be an array")
        for entry in shard_entries:
            if isinstance(entry, Mapping):
                entry_id = entry.get("id")
                if isinstance(entry_id, str) and entry_id:
                    if entry_id in seen_ids:
                        raise ValueError(f"duplicate catalogue id {entry_id!r} in {shard}")
                    seen_ids.add(entry_id)
            entries.append(entry)

    ordered = sorted(
        [entry for entry in entries if isinstance(entry, Mapping)],
        key=catalogue_sort_key,
    )
    # Preserve any non-mapping leftovers at the end so validate_catalogue can reject them.
    leftovers = [entry for entry in entries if not isinstance(entry, Mapping)]
    return {"schema_version": schema_version, "entries": ordered + leftovers}


def load_json(path: Path) -> Any:
    try:
        if path.is_dir():
            return compose_catalogue(path)
        # Prefer module-owned shards when the composed snapshot path is requested
        # but the shard directory exists — shards are the edit surface.
        if path.resolve() == COMPOSED_METADATA.resolve() and DEFAULT_METADATA_DIR.is_dir():
            return compose_catalogue(DEFAULT_METADATA_DIR)
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read {path}: {error}") from error


def composed_snapshot_matches(directory: Path = DEFAULT_METADATA_DIR,
                              composed: Path = COMPOSED_METADATA) -> list[str]:
    """Return errors when the composed JSON drifts from the shard merge."""

    if not composed.is_file() or not directory.is_dir():
        return []
    try:
        from_shards = compose_catalogue(directory)
        from_file = json.loads(composed.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError, ValueError) as error:
        return [str(error)]
    if from_shards != from_file:
        return [
            f"{composed.relative_to(ROOT).as_posix()} is out of sync with "
            f"{directory.relative_to(ROOT).as_posix()}/; run "
            "scripts/sort_architecture_contracts.py"
        ]
    return []


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


def catalogue_sort_key(entry: Mapping[str, Any]) -> tuple[int, str]:
    """Return the canonical review order for one catalogue entry."""

    rule = entry.get("rule")
    rule_index = RULES.index(rule) if rule in RULES else len(RULES)
    entry_id = entry.get("id")
    return rule_index, entry_id if isinstance(entry_id, str) else ""


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

    sortable_entries = [entry for entry in entries if isinstance(entry, Mapping)]
    if len(sortable_entries) == len(entries) and sortable_entries != sorted(
        sortable_entries, key=catalogue_sort_key
    ):
        errors.append(
            "metadata entries must use canonical rule/id order; run "
            "scripts/sort_architecture_contracts.py"
        )

    seen: set[str] = set()
    covered: set[str] = set()
    repository_files: list[Path] | None = None
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
            # One filesystem snapshot per validation. Rewalking build/dependency
            # trees for every contract multiplies the cost without adding evidence.
            if repository_files is None:
                repository_files = [candidate for candidate in ROOT.rglob("*") if candidate.is_file()]
            scoped_files = [
                candidate
                for candidate in repository_files
                if path_matches(relative(candidate), scope)
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
            if rule == "module-interface":
                for field in ("provides", "requires", "emits", "observes", "binds", "protocol", "cost_notes", "hot_path"):
                    if not isinstance(entry.get(field), list):
                        errors.append(f"{prefix}.{field} must be an array (empty when not applicable)")
                if not isinstance(entry.get("trim"), Mapping) or not nonempty_string(
                    entry["trim"].get("absent_profile")
                ):
                    errors.append(f"{prefix}.trim.absent_profile must name a profile")
                if not nonempty_string(entry.get("thread_affinity")):
                    errors.append(f"{prefix}.thread_affinity must be a non-empty string")
                errors.extend(_validate_module_interface_fields(prefix, entry))

    missing = sorted(set(RULES) - covered)
    if missing:
        errors.append("catalogue has no entry for rule(s): " + ", ".join(missing))
    return errors


def _capability_names(field_value: Any) -> list[str]:
    """Normalize provides/requires entries to capability type names."""

    if not isinstance(field_value, list):
        return []
    names: list[str] = []
    for item in field_value:
        if isinstance(item, str) and item.strip():
            names.append(item.strip())
        elif isinstance(item, Mapping):
            capability = item.get("capability")
            if isinstance(capability, str) and capability.strip():
                names.append(capability.strip())
    return names


def _capability_matches(declared: str, mentioned: str) -> bool:
    left = declared.split("::")[-1]
    right = mentioned.split("::")[-1]
    return declared == mentioned or left == right


def _validate_module_interface_fields(prefix: str, entry: Mapping[str, Any]) -> list[str]:
    errors: list[str] = []
    for field in ("provides", "requires", "emits", "observes", "binds"):
        value = entry.get(field)
        if not isinstance(value, list):
            errors.append(f"{prefix}.{field} must be an array")
            continue
        if field in {"provides", "requires"}:
            for index, item in enumerate(value):
                if isinstance(item, str):
                    if not item.strip():
                        errors.append(f"{prefix}.{field}[{index}] must be non-empty")
                elif isinstance(item, Mapping):
                    if not nonempty_string(item.get("capability")):
                        errors.append(f"{prefix}.{field}[{index}].capability must be a non-empty string")
                else:
                    errors.append(f"{prefix}.{field}[{index}] must be a string or object")
        elif field == "binds":
            for index, item in enumerate(value):
                if isinstance(item, str):
                    if not item.strip():
                        errors.append(f"{prefix}.{field}[{index}] must be non-empty")
                elif isinstance(item, Mapping):
                    if not nonempty_string(item.get("script_class")):
                        errors.append(f"{prefix}.{field}[{index}].script_class must be a non-empty string")
                else:
                    errors.append(f"{prefix}.{field}[{index}] must be a string or object")
        else:
            if not all(isinstance(item, str) and item.strip() for item in value):
                errors.append(f"{prefix}.{field} must be an array of non-empty strings")
    trim = entry.get("trim")
    if not isinstance(trim, Mapping) or not nonempty_string(trim.get("absent_profile")):
        errors.append(f"{prefix}.trim.absent_profile must be a non-empty string")
    if not nonempty_string(entry.get("thread_affinity")):
        errors.append(f"{prefix}.thread_affinity must be a non-empty string")
    hot_path = entry.get("hot_path", [])
    if hot_path is None:
        hot_path = []
    if not isinstance(hot_path, list) or not all(isinstance(item, str) and item.strip() for item in hot_path):
        errors.append(f"{prefix}.hot_path must be an array of non-empty path globs when present")
    return errors


def _module_interface_entries(metadata: Mapping[str, Any]) -> list[Mapping[str, Any]]:
    return [
        entry
        for entry in metadata.get("entries", [])
        if isinstance(entry, Mapping) and entry.get("rule") == "module-interface"
    ]


def _specific_module_interfaces(metadata: Mapping[str, Any]) -> list[Mapping[str, Any]]:
    return [
        entry
        for entry in _module_interface_entries(metadata)
        if nonempty_string(entry.get("scope")) and entry.get("scope") not in _POLICY_SCOPES
    ]


def _is_runtime_module_path(path: str) -> bool:
    """True for module code that must not grow new getModInst/requireModInst uses."""

    normalized = path.replace("\\", "/")
    if not normalized.startswith("src/modules/"):
        return False
    parts = normalized.split("/")
    # Authoring / tool satellites remain free to use convenience lookups.
    if any(part in {"editing", "editor", "devtools", "graphics_editing"} for part in parts):
        return False
    return True


def _git(args: list[str]) -> str:
    try:
        result = subprocess.run(
            ["git", *args],
            cwd=ROOT,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            # git writes UTF-8; the locale default (cp936 on a Chinese Windows)
            # would mangle or reject a non-ASCII path instead.
            encoding="utf-8",
            errors="replace",
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
    r"[A-Za-z_]\w*\s*(?:\(|;|=)"
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


_BASE_SOURCE_CACHE: dict[tuple[str, str], str] = {}


def _base_source(base: str, path: str) -> str:
    """Return one file as it is in the base revision ('' when it is absent).

    Cached: the changed-line lint asks once per candidate line and one file
    usually owns several of them.
    """

    key = (base, path)
    if key not in _BASE_SOURCE_CACHE:
        completed = subprocess.run(
            ["git", "-C", str(ROOT), "show", f"{base}:{path}"],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
        _BASE_SOURCE_CACHE[key] = (
            completed.stdout.decode("utf-8", errors="replace") if completed.returncode == 0 else ""
        )
    return _BASE_SOURCE_CACHE[key]


def _declared_surface(text: str) -> str | None:
    """Return the class/struct name on this line when it names a Link or System."""

    match = re.search(
        r"\b(?:class|struct)\s+(?:EVENGINE_API\w*\s+)?([A-Za-z_]\w*(?:Link|System))\b", text
    )
    return match.group(1) if match else None


def lint_module_interface(lines: list[SourceLine], metadata: Mapping[str, Any]) -> list[Finding]:
    """Enforce G-2/G-3/G-4/G-5 on changed lines against module-interface entries."""

    findings: list[Finding] = []
    specific = _specific_module_interfaces(metadata)
    hot_globs: list[str] = []
    for entry in specific:
        for glob in entry.get("hot_path") or []:
            if isinstance(glob, str) and glob.strip():
                hot_globs.append(glob.strip())

    for index, item in enumerate(lines):
        text = item.text
        context = _context(lines, index)

        # G-2: capability surface → module-interface provides/requires.
        mentioned: list[tuple[str, str]] = []
        for match in CAP_CALL.finditer(text):
            kind = match.group(1)
            type_name = match.group(2)
            field = "provides" if kind in {"provide", "addListener", "removeListener"} else "requires"
            mentioned.append((field, type_name))
        for match in PROVIDER_REF_BIND.finditer(text):
            mentioned.append(("requires", match.group(1)))
        for field, type_name in mentioned:
            matches = [entry for entry in specific if path_matches(item.path, entry.get("scope", ""))]
            if not matches:
                findings.append(
                    Finding(
                        "module-interface",
                        "missing-module-interface",
                        item.path,
                        item.line,
                        f"{field} use of {type_name} has no path-scoped module-interface entry",
                    )
                )
                continue
            declared: list[str] = []
            for entry in matches:
                declared.extend(_capability_names(entry.get(field)))
            if not any(_capability_matches(name, type_name) for name in declared):
                findings.append(
                    Finding(
                        "module-interface",
                        "capability-not-declared",
                        item.path,
                        item.line,
                        f"{type_name} must appear in module-interface.{field} for this scope",
                    )
                )

        # G-4: no new convenience lookups in runtime module sources.
        if _is_runtime_module_path(item.path) and RUNTIME_LOOKUP.search(text):
            findings.append(
                Finding(
                    "module-interface",
                    "runtime-convenience-lookup",
                    item.path,
                    item.line,
                    "getModInst/requireModInst/ModuleManager::getInstance belong only to "
                    "non-runtime (editing/editor/devtools) code",
                )
            )

        # G-3: hot_path files may not grow forbidden primitives.
        if hot_globs and any(path_matches(item.path, glob) for glob in hot_globs):
            if HOT_PATH_PRIMITIVE.search(text):
                findings.append(
                    Finding(
                        "module-interface",
                        "hot-path-forbidden-primitive",
                        item.path,
                        item.line,
                        "hot_path files must not add getModInst/requireModInst/cap::query|forEach*",
                    )
                )

        # G-5: expensive or owning-container public APIs need @cost.
        if item.path.endswith((".h", ".hpp")) and not _is_non_public_declaration(item):
            expensive = bool(
                ("(" in text and OWNED_CONTAINER_RETURN.search(text)) or EXPENSIVE_API_NAME.search(text)
            )
            if expensive and not re.search(r"@cost\b", context, re.IGNORECASE):
                findings.append(
                    Finding(
                        "module-interface",
                        "missing-cost-annotation",
                        item.path,
                        item.line,
                        "public owned-container or expensive-named API needs a Doxygen @cost contract",
                    )
                )

    return findings


def lint_contract_coverage(
    lines: list[SourceLine], metadata: Mapping[str, Any], base: str | None = None
) -> list[Finding]:
    """Require a catalogue entry for newly introduced contract surfaces.

    This is intentionally a high-signal coverage check.  The broad policy
    entries in the catalogue cover established conventions; concrete Link and
    ECS declarations must additionally be represented by a path-scoped entry.

    ``base`` is the revision the changed lines are compared against.  It is used
    to tell a *new* declaration from an established one whose line only gained
    the per-link-group export macro: `class EVENGINE_API_WORLD TransformSystem`
    is not new surface, and demanding a catalogue entry for it would make every
    annotated declaration a finding.
    """

    entries = [entry for entry in metadata.get("entries", []) if isinstance(entry, Mapping)]
    findings: list[Finding] = []
    seen: set[tuple[str, str]] = set()
    for item in lines:
        text = item.text
        triggers: list[tuple[str, str]] = []
        # The per-link-group export macro (src/engine/common/Export.h) sits
        # between the class key and the class name, so the token after `class`
        # is not always the declared name: without this the Link/System rules
        # silently stop firing on every annotated declaration.
        macro = r"(?:EVENGINE_API\w*\s+)?"
        declared = _declared_surface(text)
        # Established means the baseline *declares* the same name. A bare
        # word-boundary search over the whole baseline file also matched a name
        # mentioned in a comment, a string or an unrelated member, which let a
        # genuinely new Link/System declaration skip catalogue coverage.
        established = False
        if declared and base:
            established = any(
                _declared_surface(base_line) == declared
                for base_line in _base_source(base, item.path).splitlines()
            )
        if not established and re.search(rf"\b(?:struct|class)\s+{macro}[A-Za-z_]\w*Link\b|\busing\s+\w*Link\b", text):
            triggers.append(("link", "new Link declaration"))
        if not established and re.search(rf"\b(?:class|struct)\s+{macro}[A-Za-z_]\w*System\b", text):
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
    metadata_path = args.metadata.resolve()
    if metadata_path in {DEFAULT_METADATA_DIR.resolve(), COMPOSED_METADATA.resolve()}:
        catalogue_errors.extend(composed_snapshot_matches())
    if not isinstance(metadata, Mapping):
        return render([], catalogue_errors, args.json)
    if args.all:
        lines = _all_lines()
        lint_base: str | None = None
    else:
        base = args.base
        if base is None:
            base = os.environ.get("EVENGINE_ARCHITECTURE_BASE")
        # One effective baseline for both halves of the changed-line lint: the
        # revision the lines are diffed against is also the revision that decides
        # whether a declaration is established. Leaving the latter None disabled
        # that suppression in the default (no --base) mode, so an ordinary
        # export-only edit looked like newly introduced contract surface.
        lint_base = base or "HEAD"
        lines = _changed_lines(lint_base)
    findings = (
        lint_api_shapes(lines)
        + lint_contract_coverage(lines, metadata, lint_base)
        + lint_module_interface(lines, metadata)
    )
    return render(findings, catalogue_errors, args.json)


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    sys.exit(main())
