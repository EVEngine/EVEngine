#!/usr/bin/env python3
"""Reconcile a historical integration inventory with current C++ declarations.

The Markdown report keeps the frozen historical identities. Current destinations
are always rediscovered from source, never maintained in a migration catalogue.
"""

from __future__ import annotations

import argparse
from collections import defaultdict
import json
from pathlib import Path
import re
import subprocess

import test_levels
import utf8_stdio

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT / "docs/dev/testing/integration-audit.md"
HISTORICAL_CASE = re.compile(r"^- `([^`]+)` \(original line (\d+); policy `([^`]+)`\)")
SUBCASE = re.compile(r'\bSUB_CASE\s*\(')


class Baseline(dict):
    """Historical literal identities plus independently counted generator evidence."""

    def __init__(self):
        super().__init__()
        self.generated = {}
        self.generated_prefixes = {}


def subcase_names(text: str) -> list[str]:
    """Read real literal SUB_CASE calls while ignoring comments and strings."""
    masked = test_levels.mask_cpp(text)
    names = []
    for match in SUBCASE.finditer(masked):
        literal = re.match(r'\s*"([^"\n]+)"', text[match.end():])
        if literal:
            names.append(literal[1])
    return names


def generated_calls(text: str) -> dict[str, tuple]:
    """Expand the historical one-argument TestScript registration generators."""
    result = {}
    masked = test_levels.mask_cpp(text)
    prefix = None
    for match in re.finditer(r'^#define TestScript\(func\)|^TestScript\((\w+)\)', masked, re.MULTILINE):
        if match[1] is None:
            declaration = text[match.end():].split("#undef TestScript", 1)[0]
            literal = re.search(r'TEST_CASE(?:_FIXTURE)?\([^\n]*?"([^"\n]+)"\s*#func', declaration)
            if not literal:
                raise ValueError("unrecognized historical TestScript registration")
            prefix = literal[1]
        else:
            if prefix is None:
                raise ValueError("TestScript invocation without known generator")
            result[prefix + match[1]] = (text.count("\n", 0, match.start()) + 1, "TestScript", "")
    return result


def read_baseline(path: Path) -> dict[str, dict]:
    """Read an initial JSON capture or the report's frozen historical appendix."""
    result = Baseline()
    if path.suffix == ".json":
        sources = json.loads(path.read_text(encoding="utf-8"))["sources"]
        for source, data in sources.items():
            classified = test_levels.classified_cases(data["policy"], data["cases"])
            result[source] = {name: (case["line"], classified[name], "") for name, case in data["cases"].items()}
            if data["policy"].get("generated"):
                result.generated_prefixes[source] = data["policy"]["generated"]
        return result
    current = None
    target = result
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("### Original `") or line.startswith("### Generated original `"):
            current = line.split("`", 2)[1]
            target = result.generated if line.startswith("### Generated") else result
            target[current] = {}
        match = HISTORICAL_CASE.match(line)
        if match and current is not None:
            name, number, category = match.groups()
            retired = line.split(" — RETIRED: ", 1)[1] if " — RETIRED: " in line else ""
            target[current][name] = (int(number), category, retired)
    if not result:
        raise ValueError("baseline has no historical source inventory")
    return result


def current_locations() -> tuple[dict, dict]:
    """Find registered cases and subcases using the catalogue's C++ lexer."""
    direct, subcases = defaultdict(list), defaultdict(list)
    for source, data in test_levels.source_inventory(ROOT / "test").items():
        for name, case in data["cases"].items():
            direct[name].append((source, name, int(case["line"])))
            for child in subcase_names(case["body"]):
                location = (source, name, int(case["line"]))
                subcases[child].append(location)
                # Binding families use short child labels; the containing case
                # supplies their namespace (GraphicTest.bindings -> .basic).
                subcases[name.rsplit(".", 1)[0] + "." + child].append(location)
    return direct, subcases


def disposition(source: str, parent: str, name: str) -> str:
    if source.startswith("unit/"):
        reason = "Moved out of integration: local algorithm/format or injected-provider contract; coverage retained in unit."
    elif source.startswith("benchmark/"):
        reason = "Moved out of default integration: fixed workload timing benchmark; coverage retained for explicit benchmark runs."
    elif source.startswith("integration/"):
        reason = "Retained in integration; current placement is not independent proof of the module/platform boundary."
    else:
        reason = "Retained outside the integration tree; review the owning runner."
    if parent != name:
        reason += f" Named subcase of `{parent}`; no independent CTest process."
    return reason


def render(baseline: dict, direct: dict, subcases: dict) -> tuple[str, int]:
    total = sum(len(cases) for cases in baseline.values())
    unresolved = 0
    lines = [
        "# Integration test coverage audit",
        "",
        f"Frozen starting inventory: {len(baseline)} source files, {total} literal registered test identities.",
        "",
        "This is a historical coverage ledger, not a second test-policy catalogue. Original line numbers and policy labels are frozen evidence; destinations are regenerated from current C++ source. Policy labels are static classifications, not a claim that every retained test was manually proven to require integration. Active platform cases and macro-expanded identities require compiled discovery.",
        "",
        "Regenerate with `python -X utf8 scripts/audit_integration_tests.py`. To create a report from an initial capture, pass `--baseline <capture.json> --output <report.md>`. Without a baseline argument, the existing report supplies only its historical names/lines/labels; all current paths are rescanned.",
        "",
        "Reviewed removals from the integration lane preserve behavior coverage: deterministic crowd/index/snapshot/order algorithms and fake scheduler contracts belong in unit; weapon-local carrier motion and injected collision probes belong in unit; profile codecs and fake asset-provider compatibility checks belong in unit. Real worker execution, bindings, physics backends, filesystem stores and cross-module adapters remain integration. The 1000-body timing workload belongs in the explicit benchmark lane.",
        "",
        "Explicitly retired registrations carry per-case reasons below, including deliberate scope reductions approved by the user. A retirement does not claim equivalent scenario coverage. Every other missing destination is an unresolved coverage gap, never an implicit approval to delete a test. Names consolidated into subcases retain their individual entries.",
        "",
        "## Complete original file and case ledger",
        "",
    ]
    sections = [(False, original, cases) for original, cases in baseline.items()]
    generated = getattr(baseline, "generated", {})
    sections += [(True, original, cases) for original, cases in generated.items()]
    generated_started = False
    for is_generated, original, cases in sections:
        if is_generated and not generated_started:
            generated_started = True
            lines.extend(["## Historical generated registrations", "", f"Additional generated identities: {sum(map(len, generated.values()))}; excluded from the {total} literal count. Expanded from the original TestScript macro calls, with commented-out calls excluded.", ""])
        heading = "Generated original" if is_generated else "Original"
        lines.extend([f"### {heading} `{original}`", "", f"Original {'generated' if is_generated else 'literal'} cases: {len(cases)}.", ""])
        for name, (number, category, retired) in cases.items():
            locations = direct.get(name, []) or subcases.get(name, [])
            locations = sorted(set(locations))
            prefix = f"- `{name}` (original line {number}; policy `{category}`)"
            if not locations and retired:
                lines.append(prefix + " — RETIRED: " + retired)
                continue
            if not locations:
                unresolved += 1
                lines.append(prefix + " — UNRESOLVED: no current literal case/subcase; investigate before removing coverage.")
                continue
            destinations = []
            for source, parent, line in locations:
                destinations.append(f"`test/{source}:{line}`. " + disposition(source, parent, name))
            lines.append(prefix + " → " + " / ".join(destinations))
        lines.append("")
    lines.extend(["## Reconciliation", "", f"Unresolved historical identities: {unresolved}.", ""])
    return "\n".join(lines), unresolved


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, help="initial JSON capture or prior generated report")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--retirements", type=Path, help="import explicit to:null retirement evidence once into Markdown")
    parser.add_argument("--generated-from-git", help="one-time expansion of baseline TestScript calls at this Git revision")
    args = parser.parse_args()
    baseline = read_baseline(args.baseline or args.output)
    if args.retirements:
        for move in json.loads(args.retirements.read_text(encoding="utf-8")):
            if move.get("to", "missing") is not None:
                continue
            reason = move["reason"]
            if not reason.strip():
                raise ValueError("retirement requires a concrete reason")
            for name in move["case_names"]:
                found = [(cases, name) for cases in baseline.values() if name in cases]
                if len(found) != 1:
                    raise ValueError(f"retired identity must resolve exactly once: {name}")
                cases, key = found[0]
                number, category, _ = cases[key]
                cases[key] = (number, category, reason)
    if args.generated_from_git:
        for source in baseline.generated_prefixes:
            text = subprocess.check_output(["git", "show", f"{args.generated_from_git}:test/{source}"], cwd=ROOT, encoding="utf-8")
            baseline.generated[source] = generated_calls(text)
    report, unresolved = render(baseline, *current_locations())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(report, encoding="utf-8")
    print(f"Wrote {args.output}: {len(baseline)} original files, {sum(map(len, baseline.values()))} identities, {unresolved} unresolved")
    return 1 if unresolved else 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
