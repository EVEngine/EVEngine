#!/usr/bin/env python3
"""Build a source-to-CTest reverse index and select affected tests.

This is dependency impact, not measured behavioral coverage. CMake's File API
owns target/source/link facts; CTest owns compiled registrations. Compiler include
records augment a conservative literal-include scan. Unknown changes broaden the
selection instead of silently dropping tests.
"""

from __future__ import annotations

import argparse
from collections import defaultdict
from functools import lru_cache
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

import utf8_stdio

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = "evengine.test-impact"
INCLUDE = re.compile(r'^\s*#\s*include\s*([<"])([^">\n]+)[">]', re.MULTILINE)
COMPILED = {".c", ".cc", ".cpp", ".cxx", ".m", ".mm"}


def invoke(args: list[str], cwd: Path | None = None) -> str:
    result = subprocess.run(args, cwd=cwd, text=True, encoding="utf-8",
                            errors="replace", capture_output=True, check=False)
    if result.returncode:
        raise ValueError(f"command failed ({result.returncode}): {' '.join(args)}\n{result.stderr}")
    return result.stdout


@lru_cache(maxsize=None)
def absolute(path: str, base: Path) -> Path:
    value = Path(path.replace("\\", "/"))
    return (value if value.is_absolute() else base / value).resolve()


@lru_cache(maxsize=None)
def relative(path: Path, root: Path) -> str | None:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return None


def write_json(path: Path, value: dict) -> None:
    rendered = json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_text(encoding="utf-8") == rendered:
        return
    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=path.parent,
                                     prefix=path.name + ".", delete=False) as stream:
        stream.write(rendered)
        temporary = Path(stream.name)
    try:
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def codemodel(build: Path, config: str) -> tuple[dict, dict[str, dict], dict]:
    reply = build / ".cmake/api/v1/reply"
    indices = sorted(reply.glob("index-*.json"), key=lambda p: p.stat().st_mtime_ns)
    if not indices:
        raise ValueError("no CMake File API reply; reconfigure with TestImpact.cmake enabled")
    index = json.loads(indices[-1].read_text(encoding="utf-8"))
    objects = [o for o in index.get("objects", []) if o["kind"] == "codemodel" and o["version"]["major"] == 2]
    if len(objects) != 1:
        raise ValueError("expected one codemodel-v2 reply")
    model = json.loads((reply / objects[0]["jsonFile"]).read_text(encoding="utf-8"))
    configs = model["configurations"]
    selected = [c for c in configs if c["name"] == config] if config else configs
    if len(selected) != 1:
        raise ValueError(f"select one build configuration with --config; available: {[c['name'] for c in configs]}")
    targets = {}
    for target in selected[0]["targets"]:
        targets[target["id"]] = json.loads((reply / target["jsonFile"]).read_text(encoding="utf-8"))
    return model, targets, {"name": selected[0]["name"], "generator": index["cmake"]["generator"]["name"]}


def closure(target: str, targets: dict[str, dict]) -> set[str]:
    seen, pending = set(), [target]
    while pending:
        current = pending.pop()
        if current in seen:
            continue
        if current not in targets:
            raise ValueError(f"missing codemodel target dependency: {current}")
        seen.add(current)
        pending.extend(d["id"] for d in targets[current].get("dependencies", []))
    return seen


def compiler_dependencies(build: Path, generator: str) -> dict[Path, set[Path]]:
    """Use Ninja's compiler records when available, including MSVC /showIncludes.

    Sources are matched through compdb output objects rather than guessed from
    object basenames (different modules may both own Window.cpp).
    """
    if "Ninja" not in generator:
        return {}
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    match = re.search(r"^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$", cache, re.MULTILINE)
    ninja = match[1].strip() if match else "ninja"
    database = json.loads(invoke([ninja, "-C", str(build), "-t", "compdb"]))
    objects = {absolute(item["output"], build): absolute(item["file"], Path(item["directory"]))
               for item in database if Path(item["file"]).suffix.lower() in COMPILED}
    records: dict[Path, set[Path]] = defaultdict(set)
    source = None
    for line in invoke([ninja, "-C", str(build), "-t", "deps"]).splitlines():
        header = re.match(r"^(.+): #deps \d+", line)
        if header:
            source = objects.get(absolute(header[1], build))
        elif source is not None and line.startswith("    "):
            records[source].add(absolute(line.strip(), build))
    return records


@lru_cache(maxsize=None)
def direct_includes(source: Path, dirs: tuple[Path, ...], root: Path) -> tuple[Path, ...]:
    local = relative(source, root)
    if not source.is_file() or local is None or local.startswith(("external/", "third-party/", "build/")):
        return ()
    text = source.read_text(encoding="utf-8", errors="replace")
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.DOTALL)
    dependencies = []
    for delimiter, name in INCLUDE.findall(text):
        search = ((source.parent,) if delimiter == '"' else ()) + dirs
        for directory in search:
            candidate = absolute(name, directory)
            if candidate.is_file():
                dependencies.append(candidate)
                break
    return tuple(dependencies)


def includes_for(source: Path, dirs: tuple[Path, ...], cache: dict, root: Path) -> set[Path]:
    """Overapproximate all conditional literal includes, preserving include order.

    Compiler records also cover macro includes and forced/PCH includes for built
    objects. Nonliteral or external changes never qualify for an empty selection.
    """
    key = (source, dirs)
    if key in cache:
        return cache[key]
    seen, pending = set(), [source]
    while pending:
        current = pending.pop()
        if current in seen:
            continue
        seen.add(current)
        pending.extend(direct_includes(current, dirs, root))
    cache[key] = seen
    return seen


def build_index(build: Path, root: Path, config: str = "", required: list[str] | None = None) -> dict:
    absolute.cache_clear()
    relative.cache_clear()
    direct_includes.cache_clear()
    model, targets, configuration = codemodel(build, config)
    source_base = Path(model["paths"]["source"])
    build_base = Path(model["paths"]["build"])
    target_list = build / "test-impact" / f"targets-{configuration['name']}.txt"
    if not required and target_list.exists():
        required = target_list.read_text(encoding="utf-8").splitlines()
    ctest_args = ["ctest", "--test-dir", str(build), "--show-only=json-v1"]
    if configuration["name"]:
        ctest_args += ["-C", configuration["name"]]
    registrations = json.loads(invoke(ctest_args))["tests"]
    artifact_targets = defaultdict(set)
    for identifier, target in targets.items():
        if target["type"] == "EXECUTABLE":
            for artifact in target.get("artifacts", []):
                artifact_targets[absolute(artifact["path"], build_base)].add(identifier)
    tests, by_target = {}, defaultdict(set)
    for test in registrations:
        properties = {p["name"]: p["value"] for p in test.get("properties", [])}
        if properties.get("DISABLED"):
            continue
        labels = properties.get("LABELS", [])
        if "bundle" in labels or "benchmark" in labels:
            continue
        command = test.get("command", [])
        matches = artifact_targets.get(absolute(command[0], build)) if command else None
        name = test["name"]
        if name in tests:
            raise ValueError(f"duplicate CTest name: {name}")
        # Python/shell/profile commands that aren't a modeled executable remain
        # conservatively affected by every change, never silently ignored.
        tests[name] = {"labels": labels, "targets": sorted(matches or []),
                       "sources": [label[7:] for label in labels if label.startswith("source:")]}
        for identifier in matches or []:
            by_target[identifier].add(name)
    observed = {targets[t]["name"] for t in by_target}
    missing = sorted(set(required or []) - observed)
    if missing:
        raise ValueError(f"undiscovered test targets: {', '.join(missing)}; build them before selecting tests")
    compiler = compiler_dependencies(build, configuration["generator"])
    ordered_names = sorted(tests)
    bits = {name: 1 << index for index, name in enumerate(ordered_names)}
    forward: dict[str, int] = defaultdict(int)
    evidence: dict[str, set[str]] = defaultdict(set)
    cache = {}
    for consumer, names in by_target.items():
        consumer_mask = sum(bits[name] for name in names)
        for identifier in closure(consumer, targets):
            target = targets[identifier]
            for entry in target.get("sources", []):
                if "compileGroupIndex" not in entry:
                    continue
                source = absolute(entry["path"], source_base)
                source_key = relative(source, root)
                if source_key is None or source_key.startswith(("external/", "third-party/")):
                    continue
                group = target.get("compileGroups", [])[entry["compileGroupIndex"]]
                dirs = tuple(absolute(i["path"], source_base) for i in group.get("includes", []))
                dependencies = includes_for(source, dirs, cache, root) | compiler.get(source, set())
                selected_mask = consumer_mask
                if source_key.startswith("test/") and identifier == consumer:
                    specific = {n for n in names if source.name in tests[n]["sources"]}
                    if specific:
                        selected_mask = sum(bits[name] for name in specific)
                for dependency in dependencies:
                    key = relative(dependency, root)
                    if key is None or key.startswith(("external/", "third-party/", "build/")):
                        continue
                    # Own test TUs can be restricted by their actual case-source
                    # label. Engine/link/shared-runner dependencies affect the
                    # complete executable. Missing labels use the safe union.
                    forward[key] |= selected_mask
                    evidence[key].add("compiler/include" if dependency != source else
                                      "test-source" if key.startswith("test/") else "target/link")
    # Shared DLL closure makes thousands of paths share the same consumer set.
    # Intern those sets instead of serializing millions of repeated case names.
    masks, test_sets, reverse = {}, {}, {}
    for source, mask in sorted(forward.items()):
        if mask not in masks:
            identifier = str(len(masks))
            masks[mask] = identifier
            test_sets[identifier] = [name for position, name in enumerate(ordered_names) if mask & (1 << position)]
        reverse[source] = {"test_set": masks[mask], "evidence": sorted(evidence[source])}
    result = {"schema": SCHEMA, "version": 2, "root": str(root.resolve()),
              "build": str(build.resolve()), "configuration": configuration,
              "required_targets": sorted(required or []), "tests": tests,
              "test_sets": test_sets, "reverse": reverse}
    write_json(build / "test-impact/index.json", result)
    return result


def changed_files(root: Path, base: str) -> list[str]:
    # --no-renames includes both removed and added paths; deletions must invalidate
    # old dependencies and additions must never appear as an unrelated no-op.
    committed = invoke(["git", "diff", "--no-renames", "--name-only", "-z", base, "HEAD"], root)
    working = invoke(["git", "diff", "--no-renames", "--name-only", "-z", "HEAD"], root)
    untracked = invoke(["git", "ls-files", "--others", "--exclude-standard", "-z"], root)
    return sorted(set(filter(None, (committed + working + untracked).split("\0"))))


def select(index: dict, paths: list[str], level: str = "all", domain: str = "") -> dict:
    if index.get("schema") != SCHEMA or index.get("version") != 2:
        raise ValueError("unsupported test-impact schema/version")
    all_tests = set(index["tests"])
    if domain and not any(f"unit_test_{domain}" in t["labels"] for t in index["tests"].values()):
        raise ValueError(f"no registered test domain '{domain}' in this build configuration")
    selected, reasons = set(), {}
    for path in paths:
        key = relative(absolute(path, Path(index["root"])), Path(index["root"]))
        # Without compiler depfiles, a macro or forced include can be invisible
        # to the lexical scan. Do not narrow header changes on those generators.
        if key and Path(key).suffix.lower() in {".h", ".hpp", ".hh", ".hxx", ".inc", ".inl"} and "Ninja" not in index["configuration"]["generator"]:
            matches = all_tests
            reasons[key] = ["no compiler include database: select all registered tests"]
        elif key in index["reverse"]:
            matches = set(index["test_sets"][index["reverse"][key]["test_set"]])
            reasons[key] = index["reverse"][key]["evidence"]
        else:
            matches = all_tests
            reasons[key or path] = ["unknown dependency: select all registered tests"]
        selected.update(matches)
    if paths:
        selected.update(n for n, t in index["tests"].items() if not t["targets"])
    if level != "all":
        selected = {n for n in selected if level in index["tests"][n]["labels"]}
    if domain:
        selected = {n for n in selected if f"unit_test_{domain}" in index["tests"][n]["labels"]}
    return {"schema": "evengine.test-selection", "version": 1, "level": level,
            "changed": paths, "reasons": reasons, "tests": sorted(selected)}


def run_selection(build: Path, selection: dict, config: str, jobs: int = 1) -> int:
    if not selection["tests"]:
        print("No affected tests in the requested level.")
        return 0
    # CMake >=3.21 support: avoid Windows command-line limits without requiring
    # the newer --tests-from-file option. Every batch has an exact literal regex.
    batches, batch, length = [], [], 0
    for name in selection["tests"]:
        escaped = re.sub(r"([.\\+*?\[\]^$(){}|])", r"\\\1", name)
        if batch and length + len(escaped) > 6000:
            batches.append(batch)
            batch, length = [], 0
        batch.append(escaped)
        length += len(escaped) + 1
    if batch:
        batches.append(batch)
    status = 0
    for batch in batches:
        args = ["ctest", "--test-dir", str(build), "--output-on-failure", "--no-tests=error",
                "-R", "^(" + "|".join(batch) + ")$"]
        if config:
            args += ["-C", config]
        args += ["-j", str(jobs), "--repeat", "until-pass:2"]
        result = subprocess.run(args, check=False)
        status = status or result.returncode
    return status


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["index", "select", "run"])
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--config", default="")
    parser.add_argument("--target", action="append", default=[])
    parser.add_argument("--base", default="origin/dev")
    parser.add_argument("--changed", action="append")
    parser.add_argument("--level", choices=["all", "unit", "integration"], default="all")
    parser.add_argument("--domain", default="")
    parser.add_argument("--jobs", type=int, default=1)
    args = parser.parse_args()
    try:
        if args.jobs < 1:
            raise ValueError("--jobs must be positive")
        previous = args.build_dir / "test-impact/index.json"
        required = args.target
        if not required and previous.exists():
            required = json.loads(previous.read_text(encoding="utf-8")).get("required_targets", [])
        index = build_index(args.build_dir.resolve(), args.root.resolve(), args.config, required)
        if args.action == "index":
            print(f"Indexed {len(index['reverse'])} files -> {len(index['tests'])} tests: {previous}")
            return 0
        paths = args.changed if args.changed is not None else changed_files(args.root, args.base)
        selection = select(index, paths, args.level, args.domain)
        write_json(args.build_dir / "test-impact/selection.json", selection)
        (args.build_dir / "test-impact/affected-tests.txt").write_text(
            "".join(name + "\n" for name in selection["tests"]), encoding="utf-8")
        print(f"{len(paths)} changed files -> {len(selection['tests'])} affected tests")
        print(f"Reasons: {args.build_dir / 'test-impact/selection.json'}")
        if args.action == "run":
            return run_selection(args.build_dir, selection, index["configuration"]["name"], args.jobs)
        return 0
    except (OSError, ValueError, KeyError) as error:
        print(f"test impact error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    sys.exit(main())
