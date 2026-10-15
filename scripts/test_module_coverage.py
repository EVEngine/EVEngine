#!/usr/bin/env python3
"""Audit all declared modules against canonical test ownership and references.

This is an evidence inventory, not line/branch coverage. A host test referencing
a satellite is recorded separately from that satellite owning local rule tests.
"""

from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import re
import sys

import check_module_manifest
import test_levels
import utf8_stdio

ROOT = Path(__file__).resolve().parents[1]


def audit(data: dict, inventory: dict, declarations: list) -> tuple[dict, list[str]]:
    modules = {m.name: {"directory": m.dir, "layer": m.layer, "unit_sources": [],
                        "unit_cases": 0, "integration_tasks": {}, "references": [],
                        "api_symbol_evidence": [], "implementation_kind": "implementation"}
               for m in declarations}
    by_directory = sorted(declarations, key=lambda m: len(m.dir), reverse=True)
    errors = []
    tasks: dict[str, dict] = {}
    symbols = {}
    for declaration in declarations:
        base = ROOT / "src" / ("engine" if getattr(declaration, "core", False) else "modules") / declaration.dir
        cpp = list(base.glob("*.cpp"))
        implementation = "\n".join(p.read_text(encoding="utf-8") for p in cpp)
        headers = "\n".join(p.read_text(encoding="utf-8") for p in [*base.glob("*.h"), *base.glob("*.hpp")])
        declared = set(re.findall(r'\b(?:class|struct)\s+(?:EVENGINE_API_\w+\s+)?([A-Z]\w*)\s*(?:final\s*)?(?::[^;{]*)?\{', headers))
        declared.update(re.findall(r'^(?!return\b|auto\b)[A-Za-z_][^\n;{}=]*?\b([A-Z]\w*)::\w+\s*\(', implementation, re.MULTILINE))
        if declaration.name == "procgen_graphics_editing":
            declared.update({"createHeightmapMesh", "updateHeightmapMesh"})
        symbols[declaration.name] = declared - {"Result", "Value", "Status", "Impl", "Data", "Target", "Event"}
        if cpp and re.fullmatch(r'\s*(?:#include[^\n]*\n|namespace[^{}]*\{|\}|//[^\n]*\n|constexpr\s+int\s+\w+\s*=\s*0\s*;|\s)*', implementation):
            modules[declaration.name]["implementation_kind"] = "type-facade-and-link-anchor"
    for source, policy in data["sources"].items():
        module = policy.get("module")
        if module not in modules:
            errors.append(f"{source}: missing or undeclared owner module {module}")
            continue
        levels = {test_levels.CLASSIFICATIONS[r][0] for r in policy["cases"]}
        if source.startswith("unit/"):
            if not source.startswith(f"unit/{module}/") or levels != {"unit"}:
                errors.append(f"{source}: unit path/owner/level mismatch")
            modules[module]["unit_sources"].append(source)
            modules[module]["unit_cases"] += sum(len(names) for names in policy["cases"].values())
        elif source.startswith("integration/"):
            task = policy.get("task")
            if not task or not source.startswith(f"integration/{task}/") or levels != {"integration"}:
                errors.append(f"{source}: integration task/path/level mismatch")
            else:
                tasks.setdefault(task, {"sources": [], "modules": set(), "cases": 0})
                tasks[task]["sources"].append(source)
                tasks[task]["cases"] += sum(len(names) for names in policy["cases"].values())
                tasks[task]["modules"].add(module)
                modules[module]["integration_tasks"].setdefault(task, []).append(source)
        elif "/" not in source and policy["role"] == "tests":
            errors.append(f"{source}: case-owning desktop source outside unit/integration layout")
        text = inventory[source]["text"]
        tokens = set(re.findall(r'\b[A-Za-z_]\w*\b', text))
        for name, names in symbols.items():
            hits = sorted(names & tokens)
            if hits:
                modules[name]["api_symbol_evidence"].append({"source": source, "symbols": hits,
                                                             "levels": sorted(levels)})
                if policy.get("task") in tasks:
                    tasks[policy["task"]]["modules"].add(name)
        for include in re.findall(r'#include\s*[<"]([^">\n]+)', text):
            for declaration in by_directory:
                if include.startswith(declaration.dir + "/"):
                    reference = {"source": source, "include": include,
                                 "levels": sorted(levels), "role": policy["role"]}
                    if reference not in modules[declaration.name]["references"]:
                        modules[declaration.name]["references"].append(reference)
                    if policy.get("task") in tasks:
                        tasks[policy["task"]]["modules"].add(declaration.name)
                    break
    for module in modules.values():
        module["status"] = ("local-rules-recorded" if module["unit_cases"] else
                            "task-tests-recorded" if module["integration_tasks"] else
                            "referenced-by-other-tests" if module["references"] else
                            "api-symbol-in-other-tests" if module["api_symbol_evidence"] else
                            "type-facade-no-independent-rules" if module["implementation_kind"] == "type-facade-and-link-anchor" else
                            "no-direct-test-evidence")
        module["unit_sources"].sort()
    for task in tasks.values():
        task["modules"] = sorted(task["modules"])
        task["sources"].sort()
    counts = Counter(m["status"] for m in modules.values())
    report = {"schema": "evengine.test-module-audit", "version": 1,
              "module_count": len(modules), "status_counts": dict(sorted(counts.items())),
              "modules": dict(sorted(modules.items())), "tasks": dict(sorted(tasks.items()))}
    return report, errors


def markdown(report: dict) -> str:
    lines = ["# Module test evidence", "", "Generated by `scripts/test_module_coverage.py`; do not edit.", "",
             "The module manifest owns declarations; test_levels.json owns case level, module",
             "and task ownership. References are header/API-name evidence, not measured behavior/branch",
             "coverage. Missing local-rule coverage is reported explicitly; no empty assertion",
             "or placeholder test is counted as coverage.", "",
             f"Declared modules: **{report['module_count']}**. Task groups: **{len(report['tasks'])}**.", ""]
    for status, count in report["status_counts"].items():
        lines.append(f"- {status}: {count}")
    lines += ["", "## Gaps requiring behavioral review", ""]
    for name, module in report["modules"].items():
        if not module["unit_cases"]:
            lines.append(f"- `{name}` (`{module['directory']}`): {module['status']}; "
                         f"{len(module['references'])} direct header references; {len(module['api_symbol_evidence'])} API-name references. "
                         "A composition/adapter reference does not prove local rules are tested.")
    lines += ["", "## All modules", ""]
    for name, module in report["modules"].items():
        lines += [f"### {name}", "", f"Source directory: `src/{'engine' if name in {'common', 'os', 'cmdline'} else 'modules'}/{module['directory']}`. "
                  f"Status: **{module['status']}**. Local cases: **{module['unit_cases']}**.", ""]
        if module["implementation_kind"] == "type-facade-and-link-anchor":
            lines.append("This source package forwards canonical types and owns a link anchor; it has no independent runtime rules.")
        for source in module["unit_sources"]:
            lines.append(f"- Unit: [{source}]({source})")
        for task, sources in sorted(module["integration_tasks"].items()):
            lines.append(f"- Integration task `{task}`: " + ", ".join(f"[{Path(s).name}]({s})" for s in sources))
        if module["references"]:
            referenced = sorted({r['source'] for r in module['references']})
            lines.append("- Direct header evidence: " + ", ".join(f"[{Path(s).name}]({s})" for s in referenced))
        if module["api_symbol_evidence"] and not module["references"]:
            referenced = sorted({r['source'] for r in module['api_symbol_evidence']})
            lines.append("- API-name evidence (includes script strings/shared declarations; review assertions): " +
                         ", ".join(f"[{Path(s).name}]({s})" for s in referenced))
        lines.append("")
    lines += ["## Integration tasks", ""]
    for name, task in report["tasks"].items():
        lines += [f"### {name}", "", f"Cases: {task['cases']}. Referenced/owning modules: " + ", ".join(f"`{m}`" for m in task["modules"]) + ".", ""]
        lines.extend(f"- [{Path(source).name}]({source})" for source in task["sources"])
        lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true", help="Refresh the generated repository audit")
    args = parser.parse_args()
    data = json.loads(test_levels.CATALOGUE.read_text(encoding="utf-8"))
    inventory = test_levels.source_inventory(ROOT / "test")
    report, errors = audit(data, inventory, check_module_manifest.parse_manifest())
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    path = ROOT / "test/MODULE_TEST_COVERAGE.md"
    expected = markdown(report)
    if args.write:
        path.write_text(expected, encoding="utf-8")
    elif not path.exists() or path.read_text(encoding="utf-8") != expected:
        print("module test evidence is stale; run scripts/test_module_coverage.py --write", file=sys.stderr)
        return 1
    print(f"Module evidence: {report['module_count']} modules, {len(report['tasks'])} tasks; {report['status_counts']}")
    return 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    sys.exit(main())
