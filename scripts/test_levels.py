#!/usr/bin/env python3
"""Validate the complete C++ test-level catalogue and emit CTest metadata.

The catalogue is authoritative. Source inspection verifies coverage and rejects
obvious rendering/script setup in unit cases; it never silently classifies a
new source. Runtime discovery remains authoritative for active/preprocessed cases.
"""

from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import re
import sys
import tempfile

import utf8_stdio

ROOT = Path(__file__).resolve().parents[1]
CATALOGUE = ROOT / "test" / "test_levels.json"
CLASSIFICATIONS = {
    "local-logic": ("unit", ()),
    "local-format": ("unit", ()),
    "test-framework": ("unit", ()),
    "script-binding": ("integration", ()),
    "script-render": ("integration", ("graphics", "window")),
    "module-composition": ("integration", ()),
    "render-offscreen": ("integration", ("graphics",)),
    "render-window": ("integration", ("graphics", "window")),
    "platform-window": ("integration", ("window",)),
    "platform-audio": ("integration", ("audio",)),
    "platform-file": ("integration", ("filesystem",)),
    "platform-network": ("integration", ("network",)),
    "platform-process": ("integration", ("process",)),
    "concurrency": ("integration", ("threads",)),
    "asset-pipeline": ("integration", ()),
    "asset-files": ("integration", ("filesystem",)),
    "physics-backend": ("integration", ()),
    "ui-render": ("integration", ()),
    "runtime-lifecycle": ("integration", ()),
    "performance": ("integration", ()),
    "fuzz-property": ("unit", ()),
}
# Same-length lexical mask: comments and literals cannot invent test declarations
# or upset the brace matching used to separate cases from adjacent helpers.
LEXEME = re.compile(
    r'//[^\n]*|/\*.*?\*/|R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=delimiter)"'
    r'|"(?:\\.|[^"\\])*"|(?<![A-Za-z0-9_])(?:u8|[uUL])?\'(?:\\.|[^\'\\\r\n])*\'', re.DOTALL
)
CASE = re.compile(r'\b(?:FUZZ_)?TEST_CASE(?:_FIXTURE)?\s*\(')


def mask_cpp(text: str) -> str:
    """Keep source positions/newlines while hiding comments and literal contents."""
    return LEXEME.sub(lambda m: re.sub(r"[^\n]", " ", m[0]), text)


def generated_prefixes_in(text: str) -> set[str]:
    """Inventory unresolved macro names without treating them as real cases."""
    masked = mask_cpp(text)
    prefixes = set()
    for match in CASE.finditer(masked):
        end, depth = match.end(), 1
        while end < len(masked) and depth:
            depth += (masked[end] == "(") - (masked[end] == ")")
            end += 1
        declaration = text[match.end():end - 1]
        expression = declaration.split(",", 1)[1] if "_FIXTURE" in match[0] else declaration
        expression = expression.split(",", 1)[0]
        if re.search(r"[#A-Za-z_]", mask_cpp(expression)):
            strings = re.findall(r'"([^"\n]*)"', expression)
            # An empty prefix fails the catalogue gate rather than granting a
            # universal source default to arbitrary computed case names.
            prefixes.add(strings[0] if strings else "")
    return prefixes


def cases_in(text: str) -> dict[str, dict[str, object]]:
    """Find literal test declarations, including inactive platform branches.

    Macro-generated names remain the source's explicitly declared policy; the
    compiled runner supplies their expanded identities during CTest discovery.
    """
    masked = mask_cpp(text)
    result = {}
    matches = list(CASE.finditer(masked))
    for index, match in enumerate(matches):
        end = match.end()
        parens = 1
        while end < len(masked) and parens:
            parens += (masked[end] == "(") - (masked[end] == ")")
            end += 1
        if parens:
            raise ValueError("unterminated test declaration")
        declaration = text[match.end():end - 1]
        name_expression = declaration.split(",", 1)[1] if "_FIXTURE" in match[0] else declaration
        name_expression = name_expression.split(",", 1)[0]
        strings = re.findall(r'"([^"\n]*)"', name_expression)
        if not strings:
            continue
        name = "".join(strings)
        # Stringification/concatenation in generator macros is not a literal
        # case identity and must not appear as a fictitious expanded case.
        if re.search(r"[#A-Za-z_]", mask_cpp(name_expression)):
            continue
        start = masked.find("{", end)
        if start < 0:
            raise ValueError(f"missing test body: {name}")
        depth = 1
        stop = start + 1
        limit = matches[index + 1].start() if index + 1 < len(matches) else len(masked)
        while stop < limit and depth:
            depth += (masked[stop] == "{") - (masked[stop] == "}")
            stop += 1
        # Alternative #if branches can contain different brace layouts before
        # preprocessing. Bound that union by the next declaration; discovery
        # still validates the actual compiled case identities.
        fixture = declaration.split(",", 1)[0].strip() if "_FIXTURE" in match[0] else ""
        if name in result:
            # Conditional implementations of the same case are legal. Keep all
            # bodies so neither backend can evade the source safety check.
            result[name]["body"] += "\n" + text[start:stop]
        else:
            result[name] = {"line": text.count("\n", 0, match.start()) + 1,
                            "body": text[start:stop], "fixture": fixture}
    return result


def source_inventory(test_dir: Path) -> dict[str, dict[str, object]]:
    """Inventory every C++ source, not only the desktop root glob."""
    inventory = {}
    for path in sorted(test_dir.rglob("*.cpp")):
        text = path.read_text(encoding="utf-8")
        inventory[path.relative_to(test_dir).as_posix()] = {
            "text": text, "cases": cases_in(text), "generated": generated_prefixes_in(text),
        }
    return inventory


def classified_cases(policy: dict) -> dict[str, str]:
    """Expand the compact reason-grouped case catalogue."""
    return {name: category for category, names in policy["cases"].items() for name in names}


UNIT_SETUP = re.compile(
    r'\b(?:openGfxWindow|openHeadlessGfx|headlessGraphics|GfxFixture|VoxelRenderFixture|'
    r'SDL_Init\w*|SDL_CreateWindow|vkCreate\w*|ScriptTest|UnitSciptTest|JobSystem|ThreadPool)\b'
    r'|\b(?:Graphics|Window)::create\b|\bModuleManager::(?:expose|initialize)\b'
    r'|\bssq::VM\b|\bImGui::(?:CreateContext|NewFrame|Render)\b'
    r'|\b(?:std::filesystem|std::(?:ifstream|ofstream|fstream)|TempDir|sleep_for|sleep_until)\b'
    r'|\bstd::(?:thread|jthread|async)\b'
    r'|\b(?:steady_clock|high_resolution_clock|system_clock)::now\s*\('
    r'|\b(?:QueryPerformanceCounter|std::clock)\s*\('
    r'|(?:\.|->)(?:initHeadless|runSource|compileSource|exposeScriptApi|new\w*FromFile)\s*\('
)


def local_helper_text(path: Path, text: str, test_dir: Path, seen: set[Path] | None = None) -> str:
    """Read only test-owned quoted headers; engine implementation is not inferred."""
    seen = set() if seen is None else seen
    result = text
    for include in re.findall(r'#include\s+"([^"\n]+)"', text):
        for candidate in (path.parent / include, test_dir / include):
            candidate = candidate.resolve()
            if candidate.is_file() and candidate.is_relative_to(test_dir.resolve()) and candidate.suffix in {".h", ".hpp"}:
                if candidate not in seen:
                    seen.add(candidate)
                    result += "\n" + local_helper_text(candidate, candidate.read_text(encoding="utf-8"), test_dir, seen)
                break
    return result


def helper_bodies(text: str) -> dict[str, str]:
    """Conservatively union overloaded test-helper definitions by identifier."""
    masked = mask_cpp(text)
    helpers: dict[str, str] = {}
    for match in re.finditer(r"\b([A-Za-z_]\w*)\s*\(", masked):
        name = match[1]
        if name in {"if", "while", "for", "switch", "catch", "TEST_CASE", "TEST_CASE_FIXTURE", "FUZZ_TEST_CASE"}:
            continue
        pos, depth = match.end(), 1
        while pos < len(masked) and depth:
            depth += (masked[pos] == "(") - (masked[pos] == ")")
            pos += 1
        suffix = re.match(r"\s*(?:const\s*)?(?:override\s*)?(?:final\s*)?(?:noexcept\s*)?(?::[^;{}]*)?\{", masked[pos:pos + 220])
        if not suffix:
            continue
        start, depth = pos + suffix.end() - 1, 1
        stop = start + 1
        while stop < len(masked) and depth:
            depth += (masked[stop] == "{") - (masked[stop] == "}")
            stop += 1
        helpers[name] = helpers.get(name, "") + "\n" + text[match.start():stop]
    for match in re.finditer(r"\bUnitSciptTest\s*\(\s*(\w+)\s*,", masked):
        helpers[match[1]] = "ScriptTest"
    return helpers


def reachable_helper_text(body: str, fixture: str, helpers: dict[str, str]) -> str:
    """Follow test-local helper names to a fixed point, including fixture setup."""
    result = body + "\n" + fixture
    seen: set[str] = set()
    while True:
        names = (set(re.findall(r"\b[A-Za-z_]\w*\b", mask_cpp(result))) & helpers.keys()) - seen
        if not names:
            return result
        seen.update(names)
        result += "\n" + "\n".join(helpers[name] for name in sorted(names))


def catalogue_errors(data: dict, inventory: dict) -> list[str]:
    errors = []
    if data.get("schema") != "evengine.test-levels" or data.get("version") != 1:
        errors.append("unsupported test-level catalogue schema/version")
    if set(data) != {"schema", "version", "sources"}:
        errors.append("unknown catalogue fields")
    sources = data.get("sources", {})
    for source in sorted(set(inventory) - set(sources)):
        errors.append(f"{source}: missing explicit test classification")
    for source in sorted(set(sources) - set(inventory)):
        errors.append(f"{source}: stale source classification")
    for source in sorted(set(sources) & set(inventory)):
        policy = sources[source]
        if not {"role", "default", "cases"} <= set(policy) or set(policy) - {"role", "default", "cases", "generated", "module", "task", "origins"}:
            errors.append(f"{source}: unknown/missing source fields")
        if policy.get("role") not in {"tests", "runner", "helper", "probe", "compile-contract", "fuzz"}:
            errors.append(f"{source}: invalid source role")
        if policy.get("default") not in CLASSIFICATIONS:
            errors.append(f"{source}: invalid default classification")
        origins = policy.get("origins", [])
        if not isinstance(origins, list) or any(not isinstance(o, str) or not re.fullmatch(r"[A-Za-z0-9_]+\.cpp", o) for o in origins):
            errors.append(f"{source}: invalid relocation origins")
        elif len(origins) != len(set(origins)):
            errors.append(f"{source}: duplicate relocation origins")
        generated = policy.get("generated", [])
        if not isinstance(generated, list) or any(not isinstance(p, str) or not p for p in generated):
            errors.append(f"{source}: invalid generated-name prefixes")
        elif set(generated) != inventory[source].get("generated", set()):
            errors.append(f"{source}: missing or stale macro-generated name policy")
        cases = inventory[source]["cases"]
        if cases and policy.get("role") not in {"tests", "fuzz"}:
            errors.append(f"{source}: case-owning source must have tests or fuzz role")
        groups = policy.get("cases", {})
        for category, names in groups.items():
            if category not in CLASSIFICATIONS or not isinstance(names, list) or not all(isinstance(n, str) for n in names):
                errors.append(f"{source}: invalid classification group {category}")
        if errors and any(error.startswith(source + ": invalid classification group") for error in errors):
            continue
        declared = classified_cases(policy)
        if sum(len(names) for names in groups.values()) != len(declared):
            errors.append(f"{source}: case appears in more than one classification group")
        for name in sorted(set(cases) - set(declared)):
            errors.append(f"{source}:{name}: missing explicit case classification")
        for name in sorted(set(declared) - set(cases)):
            errors.append(f"{source}:{name}: stale case classification")
        test_dir = ROOT / "test"
        helper_text = local_helper_text(test_dir / source, inventory[source]["text"], test_dir)
        helpers = helper_bodies(helper_text)
        if generated and policy.get("default") in CLASSIFICATIONS and CLASSIFICATIONS[policy["default"]][0] == "unit":
            setup = UNIT_SETUP.search(mask_cpp(helper_text))
            if setup:
                errors.append(f"{source}: unit generator contains external setup {setup[0]}")
        for name in sorted(set(declared) & set(cases)):
            category = declared[name]
            if category in CLASSIFICATIONS and CLASSIFICATIONS[category][0] == "unit":
                case = cases[name]
                reachable = reachable_helper_text(case["body"], case["fixture"], helpers)
                setup = UNIT_SETUP.search(mask_cpp(reachable))
                if setup:
                    errors.append(f"{source}:{name}: unit case contains external setup {setup[0]}")
                if "Script" in case["fixture"]:
                    errors.append(f"{source}:{name}: unit case uses script fixture")
    return errors


def cmake_quote(text: str) -> str:
    return '"' + text.replace("\\", "/").replace('"', '\\"').replace("$", "\\$") + '"'


def emit_cmake(data: dict, output: Path) -> None:
    """Emit one policy for source defaults and each literal compiled identity."""
    lines = ["# Generated from test/test_levels.json; do not edit."]
    seen = set()
    aliases: dict[str, list[str]] = {}
    root = CATALOGUE.parent
    for source, policy in data["sources"].items():
        basename = Path(source).name
        if basename in seen:
            raise ValueError(f"ambiguous zeroerr source basename: {basename}")
        seen.add(basename)
        for origin in policy.get("origins", []):
            aliases.setdefault(origin, []).append(source)
        lines.append(f"set({cmake_quote('EVE_TEST_GENERATED_' + basename)} {cmake_quote(';'.join(policy.get('generated', [])))})")
        lines.append(f"set({cmake_quote('EVE_TEST_CASE_NAMES_' + basename)} {cmake_quote(';'.join(classified_cases(policy)))})")
        for name, category in [("", policy["default"]), *classified_cases(policy).items()]:
            key = f"{basename}|{name}"
            level, resources = CLASSIFICATIONS[category]
            labels = [level, "reason:" + category]
            labels.extend("requires:" + resource for resource in resources)
            if "graphics" not in resources and "window" not in resources:
                labels.append("no-render")
            lines.append(f"set({cmake_quote('EVE_TEST_LEVEL_' + key)} {cmake_quote(';'.join(labels))})")
    # Original names are migration metadata, not another source declaration.
    # Existing profile filters and focused projects resolve to the canonical
    # source(s), including both halves of formerly mixed files.
    desktop = []
    for source, policy in data["sources"].items():
        if "/" not in source or (source.startswith(("unit/", "integration/")) and not policy.get("origins")):
            desktop.append(str(root / source))
    for origin, destinations in sorted(aliases.items()):
        key = 'EVE_TEST_RELOCATED_' + Path(origin).name
        lines.append(f"set({cmake_quote(key)} {cmake_quote(';'.join(str(root / s) for s in destinations))})")
        desktop.append(str(root / origin))
        names = []
        prefixes = []
        for dest in destinations:
            policy = data['sources'][dest]
            prefixes.extend(policy.get('generated', []))
            for name, category in classified_cases(policy).items():
                names.append(name)
                level, resources = CLASSIFICATIONS[category]
                labels = [level, 'reason:' + category] + ['requires:' + r for r in resources]
                if 'graphics' not in resources and 'window' not in resources:
                    labels.append('no-render')
                lines.append(f"set({cmake_quote('EVE_TEST_LEVEL_' + Path(origin).name + '|' + name)} {cmake_quote(';'.join(labels))})")
        lines.append(f"set({cmake_quote('EVE_TEST_CASE_NAMES_' + Path(origin).name)} {cmake_quote(';'.join(names))})")
        lines.append(f"set({cmake_quote('EVE_TEST_GENERATED_' + Path(origin).name)} {cmake_quote(';'.join(prefixes))})")
        if prefixes:
            policy = data['sources'][destinations[0]]
            level, resources = CLASSIFICATIONS[policy['default']]
            labels = [level, 'reason:' + policy['default']] + ['requires:' + r for r in resources]
            if 'graphics' not in resources and 'window' not in resources:
                labels.append('no-render')
            lines.append(f"set({cmake_quote('EVE_TEST_LEVEL_' + Path(origin).name + '|')} {cmake_quote(';'.join(labels))})")
    lines.append(f"set(EVE_TEST_DESKTOP_ORIGINS {cmake_quote(';'.join(sorted(set(desktop))))})")
    rendered = "\n".join(lines) + "\n"
    if not output.exists() or output.read_text(encoding="utf-8") != rendered:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=output.parent,
                                         prefix=output.name + ".", suffix=".tmp", delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(rendered)
        try:
            temporary.replace(output)
        finally:
            temporary.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emit-cmake", type=Path)
    parser.add_argument("--summary", action="store_true")
    args = parser.parse_args()
    data = json.loads(CATALOGUE.read_text(encoding="utf-8"))
    inventory = source_inventory(ROOT / "test")
    errors = catalogue_errors(data, inventory)
    if errors:
        for error in errors:
            print("error: " + error, file=sys.stderr)
        return 1
    if args.emit_cmake:
        emit_cmake(data, args.emit_cmake)
    counts = Counter(CLASSIFICATIONS[category][0] for policy in data["sources"].values()
                     for category in classified_cases(policy).values())
    print(f"test levels OK: {len(inventory)} sources; literal identities {dict(counts)}")
    if args.summary:
        for source, policy in data["sources"].items():
            local = Counter(CLASSIFICATIONS[category][0] for category in classified_cases(policy).values())
            print(f"{source}: {policy['role']}; {dict(local)}")
    return 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    sys.exit(main())
