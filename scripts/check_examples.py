#!/usr/bin/env python3
"""Enforce the examples/ layout contract.

Every directory under ``examples/`` is one of two reviewed kinds:

* a **runnable example** -- it owns ``main.nut`` plus a ``config.nut`` that
  assigns the engine ``config`` table, and it is executed by
  ``scripts/smoke_examples.sh`` in CI; or
* a **non-runnable sample** -- a C++/plugin/asset package that is explicitly
  listed in :data:`NON_RUNNABLE_EXAMPLES` with the reason it cannot be started
  by ``eve run``.

Both kinds must own a ``README.md``, and every example must appear in the
``examples/README.md`` overview so the directory stays discoverable.

The runtime half of the contract lives in ``scripts/smoke_examples.sh``: every
directory that this gate classifies as runnable is launched for at least
``MIN_RUN_SECONDS`` and must still be alive, with no error markers, when the
harness kills it.  A new example therefore cannot reach ``dev`` without either
surviving that smoke run or being added to the reviewed exemption list here.

Usage::

    python3 scripts/check_examples.py
    python3 scripts/check_examples.py --json
    python3 scripts/check_examples.py --list-runnable
    python3 scripts/check_examples.py --print-missing-rows
    python3 scripts/check_examples.py --changed-base origin/dev
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Mapping
import utf8_stdio

REPO = Path(__file__).resolve().parent.parent
EXAMPLES_DIR = REPO / "examples"
INDEX_FILE = EXAMPLES_DIR / "README.md"

# Directories that intentionally cannot be started as an ``eve run`` project.
# Every entry needs a reason; a stale entry (missing directory) and an entry
# whose directory grew a main.nut both fail this gate.
NON_RUNNABLE_EXAMPLES: Mapping[str, str] = {
    "agent": "C++ 强化学习示例：main.cpp + train.nut，随 CMake 构建，不是 eve 可运行项目。",
    "ai-editor": "AI 生成编辑器的演示资料（editor_demo.py + editors/*.editor.json），通过 eve mcp 无头主机运行。",
    "economy": "单文件 economy 模块脚本演示，用 eve run -r examples/economy/economy_demo.nut 运行，没有项目级 config/main。",
    "live2d-backend-plugin": "Live2D 后端替换插件的 C++ 骨架（CMakeLists.txt），随宿主工程编译，不是 eve 可运行项目。",
    "native-plugin": "SDK 原生插件示例（CMakeLists.txt + hello_plugin.cpp），由 CMake 构建后再被脚本加载。",
    "shader_effect_package": "打包好的 shader effect 资产包（effect.vert/frag + parameters.json），供其它示例引用。",
    "surface-fluid-dynamic": "C++ 侧流体示例（main.cpp），随构建系统编译，不是 eve 可运行项目。",
}

# ``config = { ... }`` / ``config <- { ... }`` or a direct field assignment.
CONFIG_ASSIGN_RE = re.compile(r"^\s*config\s*(?:<-|=)(?!=)", re.MULTILINE)
CONFIG_FIELD_RE = re.compile(r"^\s*config\.[A-Za-z_]", re.MULTILINE)
INDEX_LINK_RE = re.compile(r"\]\(([^)]+?)/README\.md\)")
HEADING_RE = re.compile(r"^#\s+(.+?)\s*$", re.MULTILINE)


@dataclass(frozen=True)
class Example:
    """One directory directly under ``examples/``."""

    name: str
    path: Path
    runnable: bool

    @property
    def main(self) -> Path:
        return self.path / "main.nut"

    @property
    def config(self) -> Path:
        return self.path / "config.nut"

    @property
    def readme(self) -> Path:
        return self.path / "README.md"

    @property
    def relative(self) -> str:
        return f"examples/{self.name}"


def discover(root: Path = REPO) -> list[Example]:
    """Return every example directory, sorted by name."""
    examples_dir = root / "examples"
    if not examples_dir.is_dir():
        return []
    return [
        Example(name=child.name, path=child, runnable=(child / "main.nut").is_file())
        for child in sorted(examples_dir.iterdir())
        if child.is_dir()
    ]


def linked_examples(index_text: str) -> list[str]:
    """Return the example names linked from the overview README, in file order."""
    names: list[str] = []
    for raw in INDEX_LINK_RE.findall(index_text):
        name = raw.strip().removeprefix("./").rstrip("/")
        if "/" in name or not name or name.startswith(".."):
            continue
        names.append(name)
    return names


def assigns_config_table(config_text: str) -> bool:
    """True when ``config.nut`` actually overrides the engine ``config`` table."""
    return bool(CONFIG_ASSIGN_RE.search(config_text) or CONFIG_FIELD_RE.search(config_text))


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def index_path(root: Path = REPO) -> Path:
    return root / "examples" / "README.md"


def index_exists(root: Path = REPO) -> bool:
    return index_path(root).is_file()


def read_index_text(root: Path = REPO) -> str:
    path = index_path(root)
    return read_text(path) if path.is_file() else ""


def readme_title(path: Path) -> str:
    """First ``# heading`` of an example README, or the directory name."""
    if not path.is_file():
        return path.parent.name
    match = HEADING_RE.search(read_text(path))
    if not match:
        return path.parent.name
    return match.group(1).lstrip("# ").strip()


def conformance_findings(
    root: Path = REPO,
    exemptions: Mapping[str, str] | None = None,
) -> list[str]:
    """Return every examples/ layout violation as a human-readable line."""
    exemptions = NON_RUNNABLE_EXAMPLES if exemptions is None else exemptions
    examples = discover(root)
    if not examples:
        return [f"error: no example directories found under {root / 'examples'}"]

    findings: list[str] = []
    by_name = {example.name: example for example in examples}

    for example in examples:
        if not example.readme.is_file():
            findings.append(f"{example.relative}/README.md: missing example README")

        if example.runnable:
            if not example.config.is_file():
                findings.append(
                    f"{example.relative}/config.nut: runnable example has no config.nut"
                )
            elif not assigns_config_table(read_text(example.config)):
                findings.append(
                    f"{example.relative}/config.nut: does not assign the engine `config` "
                    "table, so window size/title are silently ignored"
                )
            if not read_text(example.main).strip():
                findings.append(f"{example.relative}/main.nut: empty")
        elif example.name not in exemptions:
            findings.append(
                f"{example.relative}/: has no main.nut and is not listed in "
                "NON_RUNNABLE_EXAMPLES in scripts/check_examples.py; either make it a "
                "runnable example (main.nut + config.nut) or exempt it with a reason"
            )

    for name in sorted(exemptions):
        reason = exemptions[name]
        example = by_name.get(name)
        if example is None:
            findings.append(
                f"scripts/check_examples.py: NON_RUNNABLE_EXAMPLES entry '{name}' has no "
                "matching examples/ directory"
            )
            continue
        if not reason.strip():
            findings.append(
                f"scripts/check_examples.py: NON_RUNNABLE_EXAMPLES entry '{name}' needs a reason"
            )
        if example.runnable:
            findings.append(
                f"scripts/check_examples.py: NON_RUNNABLE_EXAMPLES entry '{name}' is stale: "
                "the directory now owns main.nut and is smoke-testable"
            )

    linked = linked_examples(read_index_text(root))
    linked_set = set(linked)
    if not index_exists(root):
        findings.append("examples/README.md: missing example overview")
    for name in linked:
        if name not in by_name:
            findings.append(
                f"examples/README.md: links examples/{name}/README.md but that directory "
                "does not exist"
            )
    for example in examples:
        if example.name not in linked_set:
            findings.append(
                f"examples/README.md: {example.relative} is not listed in the overview table"
            )
    return findings


def missing_index_rows(root: Path = REPO) -> list[str]:
    """Markdown table rows for examples missing from the overview README."""
    linked = set(linked_examples(read_index_text(root)))
    return [
        f"| [{example.name}]({example.name}/README.md) | {readme_title(example.readme)} |"
        for example in discover(root)
        if example.name not in linked
    ]


def changed_example_dirs(base: str, root: Path = REPO) -> list[str]:
    """Example directory names added or modified between ``base`` and HEAD."""
    result = subprocess.run(
        ["git", "diff", "--name-only", f"{base}...HEAD", "--", "examples"],
        cwd=root,
        check=False,
        capture_output=True,
        text=True,
        # git writes UTF-8; the locale default (cp936 on a Chinese Windows)
        # would mangle or reject a non-ASCII path instead.
        encoding="utf-8",
        errors="replace",
    )
    if result.returncode != 0:
        return []
    names: set[str] = set()
    for line in result.stdout.splitlines():
        parts = Path(line.strip()).parts
        if len(parts) >= 2 and parts[0] == "examples":
            names.add(parts[1])
    return sorted(names)


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=REPO, help=argparse.SUPPRESS)
    parser.add_argument("--json", action="store_true", help="emit findings as JSON")
    parser.add_argument(
        "--list-runnable",
        action="store_true",
        help="print the runnable examples that scripts/smoke_examples.sh must launch",
    )
    parser.add_argument(
        "--print-missing-rows",
        action="store_true",
        help="print the overview rows needed for unregistered examples",
    )
    parser.add_argument(
        "--changed-base",
        help="git revision to diff against; reports which examples this change adds",
    )
    args = parser.parse_args(list(argv) if argv is not None else None)

    examples = discover(args.root)

    if args.list_runnable:
        for example in examples:
            if example.runnable:
                print(example.name)
        return 0

    if args.print_missing_rows:
        for row in missing_index_rows(args.root):
            print(row)
        return 0

    if args.changed_base:
        changed = changed_example_dirs(args.changed_base, args.root)
        known = {example.name for example in examples}
        runnable = {example.name for example in examples if example.runnable}
        print(f"changed example directories ({args.changed_base}...HEAD): {len(changed)}")
        for name in changed:
            if name not in known:
                continue
            if name in runnable:
                print(f"  smoke-covered  examples/{name}")
            else:
                print(f"  exempt         examples/{name} (NON_RUNNABLE_EXAMPLES)")
        new = [name for name in changed if name not in known]
        if new:
            print(f"  removed        {', '.join(new)}")

    findings = conformance_findings(args.root)
    if args.json:
        print(json.dumps({"ok": not findings, "findings": findings}, ensure_ascii=False))
        return 1 if findings else 0

    if findings:
        for finding in findings:
            print(f"error: {finding}", file=sys.stderr)
        print(
            f"error: {len(findings)} example contract violation(s); see the "
            "\"示例规范\" section of examples/README.md",
            file=sys.stderr,
        )
        return 1

    runnable = sum(1 for example in examples if example.runnable)
    exempt = sum(1 for example in examples if not example.runnable)
    print(
        f"examples layout OK: {len(examples)} examples "
        f"({runnable} runnable, {exempt} reviewed non-runnable)"
    )
    return 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    sys.exit(main())
