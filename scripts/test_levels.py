#!/usr/bin/env python3
"""Validate the complete C++ test-level catalogue and emit CTest metadata.

The catalogue owns file defaults and case overrides. Source inspection supplies
literal identities and rejects obvious rendering/script setup in unit cases;
new sources still require an explicit policy. Runtime discovery remains
authoritative for active/preprocessed cases.
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


def classified_cases(policy: dict, case_names=None) -> dict[str, str]:
    """Resolve source identities using the file default plus explicit overrides."""
    result = {name: policy["default"] for name in case_names} if case_names is not None else {}
    result.update({name: category for category, names in policy.get("cases", {}).items() for name in names})
    return result


def source_policy(source: str, policy: dict) -> dict:
    """Derive structural ownership from canonical unit/integration paths.

    Explicit legacy fields remain visible so the audit can reject mismatches.
    Integration module ownership is semantic and must still be declared.
    """
    result = dict(policy)
    parts = Path(source).parts
    if len(parts) >= 3:
        if parts[0] == "unit":
            result.setdefault("module", parts[1])
        elif parts[0] == "integration":
            result.setdefault("task", parts[1])
    return result


def expanded_catalogue(data: dict, inventory: dict) -> dict:
    """Build the full v1 consumer projection in memory; never persist identities.

    Version 1 remains readable for existing tools/fixtures and migration checks.
    Version 2 stores only overrides. Unknown versions are rejected by validation.
    """
    sources = {}
    for source, policy in data["sources"].items():
        policy = source_policy(source, policy)
        if data.get("version") != 2:
            sources[source] = policy
            continue
        groups = {}
        for name, category in classified_cases(policy, inventory[source]["cases"]).items():
            groups.setdefault(category, []).append(name)
        sources[source] = {**policy, "cases": groups}
    return {**data, "sources": sources, **({"version": 1} if data.get("version") == 2 else {})}


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


def generator_line_collisions(text: str) -> list[int]:
    """zeroerr registration identity includes the physical source line."""
    lines = mask_cpp(text).splitlines()
    macros = set()
    definitions = set()
    for index, line in enumerate(lines):
        match = re.match(r"\s*#define\s+(\w+)\(", line)
        if not match:
            continue
        body = line
        definitions.add(index)
        stop = index
        while lines[stop].rstrip().endswith("\\") and stop + 1 < len(lines):
            stop += 1
            definitions.add(stop)
            body += "\n" + lines[stop]
        if CASE.search(body):
            macros.add(match[1])
    return [index + 1 for index, line in enumerate(lines) if index not in definitions
            and sum(len(re.findall(r"\b" + re.escape(macro) + r"\s*\(", line)) for macro in macros) > 1]


def catalogue_errors(data: dict, inventory: dict) -> list[str]:
    errors = []
    if data.get("schema") != "evengine.test-levels" or data.get("version") not in {1, 2}:
        errors.append("unsupported test-level catalogue schema/version")
    if set(data) != {"schema", "version", "sources"}:
        errors.append("unknown catalogue fields")
    sources = data.get("sources", {})
    for source in sorted(set(inventory) - set(sources)):
        errors.append(f"{source}: missing explicit test classification")
    for source in sorted(set(sources) - set(inventory)):
        errors.append(f"{source}: stale source classification")
    for source in sorted(set(sources) & set(inventory)):
        policy = source_policy(source, sources[source])
        required = {"role", "default", "cases"} if data.get("version") == 1 else {"role", "default"}
        allowed = {"role", "default", "cases", "generated", "module", "task", "origins", "modules"}
        if not required <= set(policy) or set(policy) - allowed:
            errors.append(f"{source}: unknown/missing source fields")
        if policy.get("role") not in {"tests", "runner", "helper", "probe", "compile-contract", "fuzz"}:
            errors.append(f"{source}: invalid source role")
        if policy.get("default") not in CLASSIFICATIONS:
            errors.append(f"{source}: invalid default classification")
        owners = policy.get("modules", [policy.get("module")])
        if "modules" in policy and (not source.startswith("integration/") or not isinstance(owners, list)
                                    or not owners or any(not isinstance(owner, str) for owner in owners)
                                    or len(owners) != len(set(owners))):
            errors.append(f"{source}: invalid composition owners")
        origins = policy.get("origins", [])
        if not isinstance(origins, list) or any(not isinstance(o, str) or not re.fullmatch(r"[A-Za-z0-9_]+\.cpp", o) for o in origins):
            errors.append(f"{source}: invalid relocation origins")
        elif len(origins) != len(set(origins)):
            errors.append(f"{source}: duplicate relocation origins")
        for line in generator_line_collisions(inventory[source]["text"]):
            errors.append(f"{source}:{line}: generator invocations share a source line and can lose registrations")
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
        explicit = classified_cases(policy)
        declared = classified_cases(policy, cases if data.get("version") == 2 else None)
        if sum(len(names) for names in groups.values()) != len(explicit):
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
        source_text = inventory[source]["text"]
        line_offsets = [0]
        for line in source_text.splitlines(keepends=True):
            line_offsets.append(line_offsets[-1] + len(line))
        scopes = list(re.finditer(r"^namespace (test_\w+) \{\n", source_text, re.MULTILINE))
        scope_helpers = {}
        for name in sorted(set(declared) & set(cases)):
            category = declared[name]
            if category in CLASSIFICATIONS and CLASSIFICATIONS[category][0] == "unit":
                case = cases[name]
                scoped_helpers = helpers
                # Consolidated responsibilities live in separate C++ namespaces.
                # Resolve unqualified helper names within the case's responsibility,
                # rather than unioning unrelated fixtures with the same method name.
                case_offset = line_offsets[case["line"] - 1]
                for scope in scopes:
                    close = source_text.find("}  // namespace " + scope[1], scope.end())
                    if scope.start() <= case_offset < close:
                        if scope[1] not in scope_helpers:
                            section = source_text[scope.end():close]
                            # Keep the responsibility's own include directives for fixture headers.
                            section_start = source_text.rfind("#if !defined(EVE_TEST_FILTER_ORIGINS)", 0, scope.start())
                            includes = source_text[section_start:scope.start()]
                            for qualifier in re.findall(r"\b(test_\w+)::", mask_cpp(section)):
                                target = next((candidate for candidate in scopes if candidate[1] == qualifier), None)
                                if target:
                                    target_end = source_text.find("}  // namespace " + qualifier, target.end())
                                    section += "\n" + source_text[target.end():target_end]
                            scoped_helpers = helper_bodies(local_helper_text(test_dir / source, includes + section, test_dir))
                            scope_helpers[scope[1]] = scoped_helpers
                        scoped_helpers = scope_helpers[scope[1]]
                        break
                reachable = reachable_helper_text(case["body"], case["fixture"], scoped_helpers)
                setup = UNIT_SETUP.search(mask_cpp(reachable))
                if setup:
                    errors.append(f"{source}:{name}: unit case contains external setup {setup[0]}")
                if "Script" in case["fixture"]:
                    errors.append(f"{source}:{name}: unit case uses script fixture")
    return errors


def cmake_quote(text: str) -> str:
    return '"' + text.replace("\\", "/").replace('"', '\\"').replace("$", "\\$") + '"'


def origin_guard_ranges(text: str) -> list[tuple[int, int, int, tuple[str, ...]]]:
    """Match legacy selection guards to their real, possibly nested closing directive."""
    directives = re.finditer(
        r"^[ \t]*#[ \t]*(if|ifdef|ifndef|endif)\b(?:[^\n]*\\\n)*[^\n]*(?:\n|$)",
        mask_cpp(text), re.MULTILINE,
    )
    stack = []
    ranges = []
    for directive in directives:
        kind = directive[1]
        if kind != "endif":
            expression = directive[0].replace("\\\n", " ")
            origins = tuple(re.findall(r"defined\s*\(\s*EVE_TEST_ORIGIN_(\w+)\s*\)", expression))
            selected = origins if kind == "if" and "EVE_TEST_FILTER_ORIGINS" in expression else ()
            stack.append((directive.start(), directive.end(), selected))
        elif stack:
            start, body, origins = stack.pop()
            if origins:
                ranges.append((start, body, directive.end(), origins))
    return sorted(ranges)


def source_text_in_ranges(text: str, ranges: list[tuple[int, int]]) -> str:
    """Keep selected C++ intervals and mask the rest without changing source positions."""
    result = list(re.sub(r"[^\n]", " ", text))
    for start, end in ranges:
        result[start:end] = text[start:end]
    return "".join(result)


def always_source_text(text: str) -> str:
    """Return code outside all origin-selection blocks, preserving other backend guards."""
    result = list(text)
    for start, _, end, _ in origin_guard_ranges(text):
        result[start:end] = re.sub(r"[^\n]", " ", text[start:end])
    return "".join(result)


def origin_source_sections(text: str, policy: dict) -> dict[str, str]:
    """Select relocated responsibilities while retaining physical source line numbers."""
    intervals: dict[str, list[tuple[int, int]]] = {}
    for _, start, end, origins in origin_guard_ranges(text):
        for origin in origins:
            intervals.setdefault(origin + ".cpp", []).append((start, end))
    result = {origin: source_text_in_ranges(text, ranges) for origin, ranges in intervals.items()}
    unconditional = always_source_text(text)
    for origin in policy.get("origins", []):
        result.setdefault(origin, unconditional)
    return result


def origin_case_policies(text: str, policy: dict) -> dict[str, dict[str, str]]:
    """Derive legacy selections from the C++ origin guards, never another list."""
    declared = classified_cases(policy)
    result = {}
    for origin, section in origin_source_sections(text, policy).items():
        result[origin] = {name: declared[name] for name in cases_in(section) if name in declared}
    return result


def emit_cmake(data: dict, output: Path) -> None:
    """Emit one policy for source defaults and each literal compiled identity."""
    if data.get("version") == 2:
        data = expanded_catalogue(data, source_inventory(CATALOGUE.parent))
    lines = ["# Generated from test/test_levels.json; do not edit."]
    seen = set()
    aliases: dict[str, list[str]] = {}
    origin_policies = {}
    root = CATALOGUE.parent
    for source, policy in data["sources"].items():
        basename = Path(source).name
        if basename in seen:
            raise ValueError(f"ambiguous zeroerr source basename: {basename}")
        seen.add(basename)
        path = root / source
        source_text = path.read_text(encoding="utf-8") if path.exists() else ""
        origin_policies[source] = origin_case_policies(source_text, policy)
        for origin in policy.get("origins", []):
            aliases.setdefault(origin, []).append(source)
        lines.append(f"set({cmake_quote('EVE_TEST_GENERATED_' + basename)} {cmake_quote(';'.join(policy.get('generated', [])))})")
        lines.append(f"set({cmake_quote('EVE_TEST_CASE_NAMES_' + basename)} {cmake_quote(';'.join(classified_cases(policy)))})")
        if origin_guard_ranges(source_text):
            always = ';'.join(cases_in(always_source_text(source_text)))
            lines.append(f"set({cmake_quote('EVE_TEST_ALWAYS_CASE_NAMES_' + basename)} {cmake_quote(always)})")
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
        origin_basename = Path(origin).name
        canonical_metadata = origin_basename in seen
        key = 'EVE_TEST_RELOCATED_' + Path(origin).name
        lines.append(f"set({cmake_quote(key)} {cmake_quote(';'.join(str(root / s) for s in destinations))})")
        desktop.append(str(root / origin))
        names = []
        prefixes = []
        for dest in destinations:
            policy = data['sources'][dest]
            prefixes.extend(policy.get('generated', []))
            for name, category in origin_policies[dest].get(origin, classified_cases(policy)).items():
                names.append(name)
                level, resources = CLASSIFICATIONS[category]
                labels = [level, 'reason:' + category] + ['requires:' + r for r in resources]
                if 'graphics' not in resources and 'window' not in resources:
                    labels.append('no-render')
                if not canonical_metadata:
                    lines.append(f"set({cmake_quote('EVE_TEST_LEVEL_' + origin_basename + '|' + name)} {cmake_quote(';'.join(labels))})")
        # Physical source metadata has precedence over a deleted origin with
        # the same basename. Keep that origin's distinct selection available
        # only through its explicit legacy-selector namespace.
        lines.append(f"set({cmake_quote('EVE_TEST_ORIGIN_CASE_NAMES_' + origin_basename)} {cmake_quote(';'.join(names))})")
        if not canonical_metadata:
            lines.append(f"set({cmake_quote('EVE_TEST_CASE_NAMES_' + origin_basename)} {cmake_quote(';'.join(names))})")
            lines.append(f"set({cmake_quote('EVE_TEST_GENERATED_' + origin_basename)} {cmake_quote(';'.join(prefixes))})")
        if prefixes and not canonical_metadata:
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
    data = expanded_catalogue(data, inventory)
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
