"""Source-backed checks for declared module boundaries (no build side effects)."""

from __future__ import annotations

import fnmatch
import re
from pathlib import Path


CAPABILITY = re.compile(
    r"\bcap::(provide|addListener|query|forEach|forEachUntil)\s*<\s*([\w:]+)\s*>"
)
HOT_LOOKUP = re.compile(
    r"\b(?:getModInst|requireModInst)\s*\(|ModuleManager::getInstance|\bcap::(?:query|forEach|forEachUntil)\s*<"
)


def names(records, key):
    return (
        {item.get(key) for item in records if isinstance(item, dict)}
        if isinstance(records, list)
        else set()
    )


def capability_errors(text, entry):
    errors = []
    for operation, capability in CAPABILITY.findall(text):
        face = "provides" if operation in {"provide", "addListener"} else "requires"
        declared = names(entry.get(face), "capability")
        if capability not in declared:
            errors.append(f"{face} does not declare capability {capability}")
    return errors


def validate(entry, files: list[Path], root: Path, prefix: str) -> list[str]:
    errors = []
    for field in (
        "provides",
        "requires",
        "emits",
        "observes",
        "binds",
        "protocol",
        "cost_notes",
        "hot_path",
    ):
        if not isinstance(entry.get(field), list):
            errors.append(f"{prefix}.{field} must be an array")
    if errors:
        return errors
    for field, key in (("provides", "capability"), ("requires", "capability"),
                       ("emits", "event"), ("binds", "script_class"),
                       ("protocol", "method"), ("cost_notes", "api")):
        for record in entry[field]:
            if not isinstance(record, dict) or not isinstance(record.get(key), str) or not record[key]:
                errors.append(f"{prefix}.{field} needs records with a nonempty {key}")
    if any(not isinstance(value, str) for value in entry["observes"]):
        errors.append(f"{prefix}.observes must contain event names")
    if errors:
        return errors
    text = "\n".join(
        path.read_text(encoding="utf-8") for path in files if path.suffix in {".h", ".cpp", ".hpp"}
    )
    errors.extend(f"{prefix}: {error}" for error in capability_errors(text, entry))
    bound = names(entry.get("binds"), "script_class")
    classes = set(re.findall(r'addClass<[^>]+>\s*\(\s*"([\w]+)"', text))
    classes.update(re.findall(r"void\s+(\w+)::expose\s*\(ssq::(?:Table|Class)\s*&", text))
    classes.update(re.findall(r"\bSSQ_REG\s*\(\s*(\w+)", text))
    for cls in classes - bound:
        errors.append(f"{prefix}.binds does not declare script class {cls}")
    events = names(entry.get("emits"), "event") | set(entry.get("observes", []))
    for event in re.findall(r"\bObserver\s*<\s*([\w:]+)\s*>", text):
        if event not in events:
            errors.append(f"{prefix}: undeclared observer {event}")
    for dependency in entry.get("requires", []):
        if isinstance(dependency, dict) and dependency.get("required") is False:
            for key in ("present_test", "absent_test"):
                path = dependency.get(key)
                if not isinstance(path, str) or not (root / path).is_file():
                    errors.append(f"{prefix}: optional {dependency.get('capability')} needs {key}")
    for pattern in entry.get("hot_path", []):
        if not isinstance(pattern, str) or not any(
            fnmatch.fnmatch(path.relative_to(root).as_posix(), pattern) for path in files
        ):
            errors.append(f"{prefix}.hot_path matches no scoped file: {pattern}")
    trim = entry.get("trim", {})
    if not isinstance(trim, dict) or not isinstance(trim.get("absent_profile"), str):
        errors.append(f"{prefix}.trim.absent_profile must name a profile")
    else:
        import profile_matrix

        profile = trim["absent_profile"]
        if profile not in profile_matrix.PROFILES:
            errors.append(f"{prefix}.trim references unknown profile {profile}")
        else:
            selected, _ = profile_matrix.resolve(profile)
            module = entry.get("module")
            if not isinstance(module, str) or module not in {
                item.name for item in profile_matrix.contracts()
            }:
                errors.append(f"{prefix}.module must name a manifest module")
            elif module in selected:
                errors.append(f"{prefix}.trim profile {profile} still selects {module}")
    return errors


def lint_line(path: str, text: str, entries) -> list[str]:
    matches = [
        item
        for item in entries
        if item.get("rule") == "module-interface" and fnmatch.fnmatch(path, item.get("scope", ""))
    ]
    errors = []
    if CAPABILITY.search(text) and not matches:
        errors.append("new capability boundary has no module-interface entry")
    for entry in matches:
        errors.extend(capability_errors(text, entry))
        if any(
            fnmatch.fnmatch(path, pattern) for pattern in entry.get("hot_path", [])
        ) and HOT_LOOKUP.search(text):
            errors.append(
                "hot path adds a string-keyed or capability lookup; bind the service outside the hot path"
            )
    return errors
