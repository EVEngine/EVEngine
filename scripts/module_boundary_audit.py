#!/usr/bin/env python3
"""Report-only audit of module boundaries against the six-face model.

This is the evidence collector behind ``docs/dev/模块边界审查清单.md``. It
performs no gating and never fails a build: it prints, per declared module, the
signals the review checklist asks about, plus the capability edge matrix (who
declares, who provides, who consumes each named capability).

It deliberately overlaps *nothing* with the existing gates:

* ``scripts/module_depgraph.py`` owns the ``#include`` graph and layering.
* ``scripts/check_architecture_contracts.py`` owns the enforced contract
  catalogue and the changed-line API shape lint.

Usage::

    python3 scripts/module_boundary_audit.py                 # summary table
    python3 scripts/module_boundary_audit.py --capabilities  # capability matrix
    python3 scripts/module_boundary_audit.py --findings      # checklist verdicts
    python3 scripts/module_boundary_audit.py --json out.json # machine readable
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import os
import re
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
MANIFEST_DIR = REPO / "cmake" / "module_manifest"
CONTRACT_CATALOGUE = REPO / "scripts" / "architecture_contracts.json"
SRC = REPO / "src"

DECLARE_OPEN = "eve_declare_module("
SOURCE_SUFFIXES = (".h", ".hpp", ".cpp", ".cc")

VALUE_KEYS = {
    "NAME",
    "DIR",
    "LAYER",
    "LIB",
    "SCRIPT",
    "SLOT",
    "DEPS",
    "OPTIONAL_DEPS",
    "THIRDPARTY",
    "GROUP",
}
FLAG_KEYS = {"REQUIRED", "CORE"}

# Files that *define* the capability primitives rather than use them.
PRIMITIVE_FILES = {SRC / "engine" / "common" / "Capability.h"}
# Files that define the convenience lookups, so their own hits are not uses.
CONVENIENCE_DEF_FILES = {
    SRC / "engine" / "common" / "Module.h",
    SRC / "engine" / "common" / "Module.cpp",
}

# --------------------------------------------------------------------------
# Signals.
# --------------------------------------------------------------------------

CAP_DECL = re.compile(r'capabilityName\s*=\s*"([^"]+)"')
CLASS_DECL = re.compile(r"\b(?:class|struct)\s+")
CAP_NAME = r"([A-Za-z_]\w*(?:::\w+)*)"
CAP_PROVIDE = re.compile(rf"cap::(provide|addListener|revoke|removeListener)\s*<\s*{CAP_NAME}")
CAP_CONSUME = re.compile(
    rf"cap::(query|forEach|forEachUntil|listenerCount|listenerAt)\s*<\s*{CAP_NAME}"
)
CAP_RAW = re.compile(r"cap::detail::(provideRaw|queryRaw)\s*\(\s*\"([^\"]+)\"")
CONVENIENCE = re.compile(
    r"\b(getModInst|requireModInst|ModuleManager::getInstance|ModuleManager::get\s*<)"
)
# NOTE (2026-09-15, second review round): the signals below carry known defects.
# See docs/dev/2026-09-15-模块边界审查台账.md §A.  Read that section before
# quoting any number this tool prints.  Fixed here: A1 (HOT_FILE_HINT) and A4
# (ECS recall).  Still open: A7 (OBSERVER conflates eve::Observer with
# eve::rx::Observer -> 123/220 inflation), A8 (SCRIPT_BIND counts
# `expose(ssq::Table` while the enforced surface is `addFunc`, ~8901 sites),
# A9 (only cap::query<X> counts as consumption, missing interfaces taken as
# parameter types or base classes), A2 (LINK still matches read-only `LinkOps`
# uses), A3/A5/A6 (entry coverage is unenforced; see the ledger).
SCRIPT_BIND = re.compile(r"\b(SSQ_REG|SSQ_TABLE|expose\s*\(\s*ssq::Table)")
OBSERVER = re.compile(r"\b(Observer\s*<|Subscription\b|subscribe\s*\()")
# A2: bare `LinkOps` is a *type* that is also read (e.g. scene::linkOps(id));
# only registration/kind tokens imply this module owns a link kind.
LINK = re.compile(r"\b(LinkKind|registerLinkKind|linkKindFor)\b")
# A4: the repo declares systems as <Domain>SystemContract (ClimbingSystemContract,
# NpcAiSystemContract), which the old \bSystemContract\b could not match.
ECS = re.compile(
    r"\b(\w*SystemContract|registerSystem|regSystem|\w*SystemContracts)\b"
)
BANNED_HOT = re.compile(
    r"(\bgetModInst\s*\(|\brequireModInst\s*\(|ModuleManager::getInstance|"
    r"cap::query\s*<|cap::forEach\s*<|cap::forEachUntil\s*<)"
)
HOMEMADE_REGISTRY = re.compile(
    r"\b(registerProvider|addProvider|removeProvider|unregisterProvider|"
    r"ProviderRegistry|ProviderHandle|ProviderLease)\b"
)
COST_TAG = re.compile(r"@cost\b")
OWNING_RETURN = re.compile(r"\bstd::(vector|string|map|unordered_map|set)\s*[<\s]")
CALLABLE = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
CONTROL_KEYWORDS = {
    "if", "for", "while", "switch", "return", "catch", "sizeof", "assert",
    "static_assert", "decltype", "alignof", "throw", "static_cast",
    "reinterpret_cast", "const_cast", "dynamic_cast", "operator", "new",
    "delete", "else", "defined",
}
# Files whose name suggests they run inside the per-frame loop.
#
# A1 fix: this used re.I, so the bare substring "system" matched "Filesystem"
# (matched a constructor) and every *System.cpp including the engine's System
# module, which is not an ECS system.  Match the CamelCase token
# case-sensitively, plus all-lowercase whole words.
# Also added EventSink: the platform-event sinks run once per native event --
# the hottest path in the engine -- and matched nothing at all before.
# Residual known FP: a file named exactly System.cpp still matches.
HOT_FILE_HINT = re.compile(
    r"System|Pump|Update|Tick|Step|Frame|Present|Sync|Dispatch|Flush|Advance|EventSink"
    r"|(?<![A-Za-z])(?:pump|update|tick|step|frame|present|sync|dispatch|flush|advance)(?![A-Za-z])"
)
HOT_LOOKUP = re.compile(
    r"(\bgetModInst\s*\(|\brequireModInst\s*\(|ModuleManager::getInstance|"
    r"cap::(?:query|forEach|forEachUntil)\s*<)"
)

INTERNAL_DIR_HINTS = ("detail", "internal", "impl", "private")


# --------------------------------------------------------------------------
# Source handling
# --------------------------------------------------------------------------


def strip_comments(text: str) -> str:
    """Blank out comment bodies while preserving offsets and line numbers."""
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            for k in range(i, j):
                out[k] = " "
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if out[k] != "\n":
                    out[k] = " "
            i = j
        elif text[i] == '"':
            i += 1
            while i < n and text[i] != '"':
                i += 2 if text[i] == "\\" else 1
            i += 1
        else:
            i += 1
    return "".join(out)


def attributed_class(text: str, pos: int) -> str | None:
    """Name of the class/struct owning the capabilityName at `pos`."""
    for m in reversed(list(CLASS_DECL.finditer(text, 0, pos))):
        tail = text[m.end() : m.end() + 160]
        stop = re.search(r"[{:;]", tail)
        if stop:
            tail = tail[: stop.start()]
        names = re.findall(r"[A-Za-z_]\w*", tail)
        for name in reversed(names):
            if not name.isupper() and len(name) > 1:
                return name
    return None


def line_of(text: str, pos: int) -> int:
    return text.count("\n", 0, pos) + 1


@dataclass
class SourceFile:
    path: Path
    rel: str
    raw: str
    code: str


@dataclass
class Module:
    name: str
    layer: str = ""
    src_dir: str = ""
    required: bool = False
    deps: list[str] = field(default_factory=list)
    optional_deps: list[str] = field(default_factory=list)
    groups: list[str] = field(default_factory=list)
    scripts: list[str] = field(default_factory=list)
    root: Path | None = None
    scan_dir: str = ""
    base: Path | None = None

    declares: dict[str, list[str]] = field(default_factory=lambda: defaultdict(list))
    capability_keys: dict[str, str] = field(default_factory=dict)
    provides: dict[str, list[str]] = field(default_factory=lambda: defaultdict(list))
    consumes: dict[str, list[str]] = field(default_factory=lambda: defaultdict(list))
    unresolved_caps: list[str] = field(default_factory=list)
    convenience: list[str] = field(default_factory=list)
    script_binds: list[str] = field(default_factory=list)
    observers: list[str] = field(default_factory=list)
    ecs: list[str] = field(default_factory=list)
    links: list[str] = field(default_factory=list)
    homemade_registry: list[str] = field(default_factory=list)
    hot_candidates: list[str] = field(default_factory=list)
    expensive_without_cost: list[str] = field(default_factory=list)
    cost_tags: int = 0
    loc: int = 0
    files: int = 0
    source_paths: list[str] = field(default_factory=list)
    max_cpp: tuple[int, str] = (0, "")

    def rel_of(self, path: Path) -> str:
        return path.relative_to(REPO).as_posix()


# --------------------------------------------------------------------------
# Manifest parsing
# --------------------------------------------------------------------------


def _declare_bodies(text: str) -> list[str]:
    bodies = []
    pos = 0
    while True:
        start = text.find(DECLARE_OPEN, pos)
        if start < 0:
            return bodies
        depth = 1
        i = start + len(DECLARE_OPEN)
        while i < len(text) and depth:
            if text[i] == "(":
                depth += 1
            elif text[i] == ")":
                depth -= 1
            i += 1
        bodies.append(text[start + len(DECLARE_OPEN) : i - 1])
        pos = i


def parse_manifests() -> list[Module]:
    modules: list[Module] = []
    by_name: dict[str, Module] = {}
    for path in sorted(MANIFEST_DIR.glob("*.cmake")):
        text = path.read_text(encoding="utf-8", errors="replace")
        for body in _declare_bodies(text):
            mod: Module | None = None
            key: str | None = None
            for token in body.split():
                if token in FLAG_KEYS:
                    key = token
                    if token == "REQUIRED" and mod is not None:
                        mod.required = True
                    continue
                if token in VALUE_KEYS:
                    key = token
                    continue
                if key == "NAME":
                    if token in by_name:
                        raise SystemExit(f"duplicate module declaration: {token}")
                    mod = Module(name=token)
                    by_name[token] = mod
                    modules.append(mod)
                    key = None
                elif mod is None:
                    continue
                elif key == "LAYER":
                    mod.layer = token
                elif key == "DIR":
                    mod.src_dir = token
                elif key == "DEPS":
                    mod.deps.append(token)
                elif key == "OPTIONAL_DEPS":
                    mod.optional_deps.append(token)
                elif key == "GROUP":
                    mod.groups.append(token)
                elif key == "SCRIPT":
                    mod.scripts.append(token)
            if mod is None:
                raise SystemExit(f"unparsable eve_declare_module in {path}: {body[:80]!r}")
    return modules


def resolve_root(mod: Module) -> Path | None:
    candidates: list[Path] = []
    if mod.src_dir:
        candidates.append(SRC / "modules" / mod.src_dir)
    if mod.name in ("common", "cmdline", "devtools"):
        candidates.append(SRC / "engine" / mod.name)
    candidates.append(SRC / "modules" / mod.name)
    for cand in candidates:
        if cand.is_dir():
            return cand
    return None


# --- Ports of cmake/modules.cmake so the audit sees the build's real dirs. ---

PACKAGE_ROOT_OVERRIDES = {
    "biome": "procgen/biome",
    "domain_gizmo": "editor/gizmo",
    "localization": "i18n",
    "material": "graphics/material",
    "lighting": "graphics/lighting",
    "input": "editor/input",
    "queue": "editor/queue",
    "level": "map/level",
    "sceneloader": "scene/loader",
}

SATELLITE_FACETS = ("streaming", "graphics", "physics", "procgen", "import", "replay", "scene", "thread")


def eve_package_root(stem: str) -> str:
    return PACKAGE_ROOT_OVERRIDES.get(stem, stem)


def eve_default_module_dir(name: str) -> str:
    """Mirror of eve_default_module_dir() in cmake/modules.cmake."""
    for suffix, facet in (("_graphics_editing", "graphics_editing"), ("_editing", "editing"), ("_editor", "editor")):
        if name.endswith(suffix):
            stem = name[: -len(suffix)]
            return f"{eve_package_root(stem)}/{facet}"
    if name == "buildingfx":
        return "building/fx"
    for facet in SATELLITE_FACETS:
        suffix = f"_{facet}"
        if name.endswith(suffix):
            stem = name[: -len(suffix)]
            return f"{eve_package_root(stem)}/{facet}"
    return eve_package_root(name)


def compute_scan_dirs(mods: list[Module]) -> None:
    """Fill mod.scan_dir for every module, exactly as the build resolves it."""
    for mod in mods:
        mod.scan_dir = mod.src_dir or eve_default_module_dir(mod.name)
        if mod.name in ("common", "cmdline", "devtools"):
            mod.base = SRC / "engine"
        else:
            mod.base = SRC / "modules"


def nested_excludes(mods: list[Module]) -> dict[str, set[str]]:
    """Immediate child dirs of a module that belong to another module."""
    out: dict[str, set[str]] = {}
    for mod in mods:
        prefix = mod.scan_dir + "/"
        children = set()
        for other in mods:
            if other is mod:
                continue
            if other.base != mod.base or not other.scan_dir.startswith(prefix):
                continue
            children.add(other.scan_dir[len(prefix) :].split("/")[0])
        out[mod.name] = children
    return out


def resolve_scan_root(mod: Module) -> Path | None:
    cand = mod.base / mod.scan_dir
    return cand if cand.is_dir() else None


def load_sources(mod: Module, excludes: set[str]) -> list[SourceFile]:
    assert mod.root is not None
    out: list[SourceFile] = []
    for dirpath, dirnames, filenames in os.walk(mod.root):
        dirnames[:] = [
            d for d in dirnames if d not in {".git", "build"} and d not in excludes
        ]
        for name in sorted(filenames):
            if not name.endswith(SOURCE_SUFFIXES):
                continue
            path = Path(dirpath) / name
            try:
                raw = path.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            out.append(SourceFile(path, mod.rel_of(path), raw, strip_comments(raw)))
    return sorted(out, key=lambda s: s.rel)


def is_public_header(mod: Module, path: Path) -> bool:
    if path.suffix not in (".h", ".hpp") or mod.root is None:
        return False
    parts = set(path.relative_to(mod.root).parts[:-1])
    if parts & set(INTERNAL_DIR_HINTS):
        return False
    stem = path.stem.lower()
    return not any(hint in stem for hint in INTERNAL_DIR_HINTS)


def context_has_cost(raw: str, pos: int, window: int = 1500) -> bool:
    segment = raw[max(0, pos - window) : pos]
    idx = segment.rfind("/**")
    if idx < 0:
        return False
    return bool(COST_TAG.search(segment[idx:]))


# --------------------------------------------------------------------------
# Scanning
# --------------------------------------------------------------------------


def collect_declarations(mod: Module, sources: list[SourceFile]) -> None:
    for sf in sources:
        for m in CAP_DECL.finditer(sf.code):
            owner = attributed_class(sf.code, m.start())
            if not owner:
                continue
            mod.declares[owner].append(sf.rel)
            mod.capability_keys[owner] = m.group(1)


def cost_candidates(mod: Module, sf: SourceFile) -> list[str]:
    """Public APIs that return an owning container **by value** with no @cost.

    This is the whole-tree proxy for "expensive API with a cheap-looking
    shape". Reference/pointer returns are excluded: they borrow, so they cost
    nothing at the boundary. Constructors are excluded as well.

    Note: a *name*-based trigger (Readback/Load/Save/Build/...) is deliberately
    NOT used here. Measured over the whole tree it fires on thousands of
    ordinary setters and resolvers; it is only viable in the changed-line gate
    (see the review ledger). Owning-by-value returns are discriminative.
    """
    out: list[str] = []
    code = sf.code
    for m in CALLABLE.finditer(code):
        ident = m.group(1)
        if ident in CONTROL_KEYWORDS:
            continue
        start = m.start()
        j = start - 1
        limit = max(0, start - 400)
        while j > limit and code[j] not in ";{}":
            j -= 1
        span = code[j + 1 : start]
        if "(" in span:
            continue
        stripped = span.rstrip()
        if not stripped or stripped[-1] in "&*":
            continue  # borrowed return
        if not OWNING_RETURN.search(span):
            continue
        if ident == attributed_class(code, start):
            continue
        if context_has_cost(sf.raw, start):
            continue
        out.append(f"{sf.rel}:{line_of(code, start)} {ident}")
    return out


def template_params(code: str) -> set[str]:
    """Names introduced as template parameters anywhere in the source."""
    out: set[str] = set()
    for m in re.finditer(r"\btemplate\s*<", code):
        depth, i = 1, m.end()
        while i < len(code) and depth:
            if code[i] == "<":
                depth += 1
            elif code[i] == ">":
                depth -= 1
            i += 1
        clause = code[m.end() : i - 1]
        out.update(re.findall(r"\b(?:class|typename)\s+([A-Za-z_]\w*)", clause))
    return out


def scan_module(mod: Module, sources: list[SourceFile], known: dict[str, str]) -> None:
    for sf in sources:
        code, raw = sf.code, sf.raw
        mod.files += 1
        mod.source_paths.append(sf.rel)
        mod.loc += code.count("\n") + 1
        if sf.path.suffix in (".cpp", ".cc"):
            lines = code.count("\n") + 1
            if lines > mod.max_cpp[0]:
                mod.max_cpp = (lines, sf.rel)

        skip_primitives = sf.path in PRIMITIVE_FILES

        if not skip_primitives:
            tparams = template_params(code)
            for rx, bucket in ((CAP_PROVIDE, mod.provides), (CAP_CONSUME, mod.consumes)):
                for m in rx.finditer(code):
                    name = m.group(2).split("::")[-1]
                    if name in tparams:
                        continue  # a template parameter, not a named capability
                    if name in known:
                        bucket[name].append(sf.rel)
                    else:
                        mod.unresolved_caps.append(
                            f"{sf.rel}:{line_of(code, m.start())} {m.group(1)}<{m.group(2)}>"
                        )
            for m in CAP_RAW.finditer(code):
                bucket = mod.provides if m.group(1) == "provideRaw" else mod.consumes
                bucket[m.group(2)].append(sf.rel)

        for rx, bucket in (
            (SCRIPT_BIND, mod.script_binds),
            (OBSERVER, mod.observers),
            (ECS, mod.ecs),
            (LINK, mod.links),
        ):
            for m in rx.finditer(code):
                bucket.append(f"{sf.rel}:{line_of(code, m.start())} {m.group(1)}")

        if sf.path not in CONVENIENCE_DEF_FILES:
            for m in CONVENIENCE.finditer(code):
                mod.convenience.append(f"{sf.rel}:{line_of(code, m.start())} {m.group(1)}")

        # A provider registry owned by *another* namespace (`editing::ProviderHandle`)
        # is a use of the canonical mechanism, and `.registerProvider(...)` is a
        # call on an object; neither is a newly invented registry.
        for m in HOMEMADE_REGISTRY.finditer(code):
            before = code[max(0, m.start() - 3) : m.start()]
            if before.endswith("::") or before.endswith(".") or before.endswith("->"):
                continue
            mod.homemade_registry.append(f"{sf.rel}:{line_of(code, m.start())} {m.group(1)}")

        mod.cost_tags += len(COST_TAG.findall(raw))

        # Hot-path candidates: name looks per-frame AND it does a lookup.
        if HOT_FILE_HINT.search(sf.path.stem) and not is_editor_satellite(mod):
            for m in HOT_LOOKUP.finditer(code):
                mod.hot_candidates.append(
                    f"{sf.rel}:{line_of(code, m.start())} {m.group(1).strip()}"
                )

        if is_public_header(mod, sf.path):
            mod.expensive_without_cost.extend(cost_candidates(mod, sf))


def is_editor_satellite(mod: Module) -> bool:
    return mod.name.endswith("_editing") or mod.name.endswith("_editor")


def is_runtime_module(mod: Module) -> bool:
    if is_editor_satellite(mod):
        return False
    return not any(
        tok in mod.name for tok in ("cmdline", "devtools", "editor", "import", "cook", "target", "demo")
    )


# --------------------------------------------------------------------------
# Analysis
# --------------------------------------------------------------------------


def capability_matrix(mods: list[Module]) -> dict:
    decl: dict[str, list[str]] = defaultdict(list)
    keys: dict[str, str] = {}
    prov: dict[str, list[str]] = defaultdict(list)
    cons: dict[str, list[str]] = defaultdict(list)
    for mod in mods:
        for cap in mod.declares:
            decl[cap].append(mod.name)
            keys.setdefault(cap, mod.capability_keys.get(cap, ""))
        for cap in mod.provides:
            prov[cap].append(mod.name)
        for cap in mod.consumes:
            cons[cap].append(mod.name)
    names = sorted(set(decl) | set(prov) | set(cons))
    return {
        cap: {
            "key": keys.get(cap, ""),
            "declared_by": decl.get(cap, []),
            "provided_by": prov.get(cap, []),
            "consumed_by": cons.get(cap, []),
        }
        for cap in names
    }


def load_catalogue() -> list[dict]:
    """The enforced contract catalogue, used for coverage — never to gate."""
    data = json.loads(CONTRACT_CATALOGUE.read_text(encoding="utf-8"))
    return data.get("entries", [])


def catalogue_scope_match(mod: Module, entry: dict) -> bool:
    """True when one of the module's own source files falls in the entry scope.

    A blanket scope such as ``src/**`` is a policy statement covering the whole
    tree; it is deliberately not accepted as per-module evidence.
    """
    scope = entry.get("scope", "")
    if not scope or scope in ("src/**", "src/**/*"):
        return False
    return any(fnmatch.fnmatch(path, scope) for path in mod.source_paths)


def catalogue_rules_for(mod: Module, catalogue: list[dict]) -> set[str]:
    """Rules whose entry is scoped to this module (blanket `src/**` excluded)."""
    out: set[str] = set()
    for entry in catalogue:
        scope = entry.get("scope", "")
        if scope in ("src/**", "src/**/*"):
            continue
        if catalogue_scope_match(mod, entry):
            out.add(entry.get("rule", ""))
    return out


def findings(mod: Module, matrix: dict, catalogue: list[dict]) -> list[tuple[str, str]]:
    out: list[tuple[str, str]] = []

    if mod.root is None:
        out.append(("B0", "manifest 声明了模块，但找不到对应源码目录"))
        return out

    rules = catalogue_rules_for(mod, catalogue)

    # E1/E2: capabilities this module touches that nobody declares, or that it
    # declares but never provides.
    orphan_decl = [c for c in sorted(mod.declares) if not matrix[c]["provided_by"]]
    if orphan_decl:
        out.append(
            (
                "E2-ORPHAN",
                f"声明但仓内无人提供（in-tree 恒缺席）: {', '.join(orphan_decl)}",
            )
        )

    dup_decl = [c for c in sorted(mod.declares) if len(matrix[c]["declared_by"]) > 1]
    if dup_decl:
        out.append(
            (
                "E1-DUP",
                "同一能力接口在多个模块重复声明: "
                + "; ".join(f"{c} ({', '.join(matrix[c]['declared_by'])})" for c in dup_decl),
            )
        )

    unconsumed = [
        c
        for c in sorted(mod.provides)
        if not matrix[c]["consumed_by"] and not mod.consumes.get(c)
    ]
    if unconsumed:
        out.append(("E1-DEAD", f"提供但无人消费: {', '.join(unconsumed)}"))

    if mod.unresolved_caps:
        out.append(
            (
                "E1-UNRESOLVED",
                f"{len(mod.unresolved_caps)} 处 cap:: 使用无法归属到具名接口（模板参数或字符串键）: "
                + "; ".join(mod.unresolved_caps[:3]),
            )
        )

    if is_runtime_module(mod) and mod.convenience:
        out.append(
            (
                "M3",
                f"runtime 模块内出现 {len(mod.convenience)} 处便捷/单例查找原语: "
                + "; ".join(mod.convenience[:4]),
            )
        )

    if mod.hot_candidates:
        out.append(
            (
                "M1-HOT",
                f"{len(mod.hot_candidates)} 处「疑似每帧文件 + 键查找」需 triage（R-MECH-2 需 hot_path 声明）: "
                + "; ".join(mod.hot_candidates[:3]),
            )
        )

    if mod.links and "link" not in rules:
        out.append(
            (
                "E3-LINK",
                f"注册/使用 Link 种类（{len(mod.links)} 处）但 catalogue 无本模块的 link 条目: "
                + "; ".join(mod.links[:3]),
            )
        )

    if mod.ecs and "ecs-system" not in rules:
        out.append(
            (
                "E3-ECS",
                f"定义 ECS 系统契约（{len(mod.ecs)} 处）但 catalogue 无本模块的 ecs-system 条目: "
                + "; ".join(mod.ecs[:3]),
            )
        )

    if mod.optional_deps and "optional-capability" not in rules:
        out.append(
            (
                "E2-OPT",
                f"声明 OPTIONAL_DEPS（{', '.join(mod.optional_deps)}）但 catalogue 无本模块级的 "
                "optional-capability 证据（present/absent 探针）",
            )
        )

    if mod.homemade_registry:
        out.append(
            (
                "M2",
                f"{len(mod.homemade_registry)} 处自造 provider 注册表迹象: "
                + "; ".join(mod.homemade_registry[:4]),
            )
        )

    if mod.max_cpp[0] > 1000:
        out.append(("S1", f"{mod.max_cpp[1]} 达 {mod.max_cpp[0]} 行（超过 1000 行拆分阈值）"))

    return out


# --------------------------------------------------------------------------
# Reporting
# --------------------------------------------------------------------------


def print_summary(mods: list[Module], matrix: dict) -> None:
    print(f"# 模块边界审计（{len(mods)} 个已声明模块）\n")
    print(
        "| 模块 | L | 文件 | LOC | 声明 | 提供 | 消费 | 未归属 | 便捷 | 脚本 | 观察 | ECS | Link | 自造 | @cost |"
    )
    print("| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |")
    for mod in sorted(mods, key=lambda m: (m.layer, m.name)):
        print(
            f"| {mod.name} | {mod.layer or '-'} | {mod.files} | {mod.loc} | "
            f"{len(mod.declares)} | {sum(len(v) for v in mod.provides.values())} | "
            f"{sum(len(v) for v in mod.consumes.values())} | {len(mod.unresolved_caps)} | "
            f"{len(mod.convenience)} | {len(mod.script_binds)} | {len(mod.observers)} | "
            f"{len(mod.ecs)} | {len(mod.links)} | {len(mod.homemade_registry)} | {mod.cost_tags} |"
        )
    print(
        f"\n合计：LOC {sum(m.loc for m in mods)}，@cost {sum(m.cost_tags for m in mods)}，"
        f"具名能力 {len(matrix)}。"
    )


def print_capabilities(mods: list[Module], matrix: dict) -> None:
    print(f"# 能力面矩阵（{len(matrix)} 个具名能力）\n")
    print("| 能力接口 | 注册键 | 声明模块 | 提供方 | 消费方 |")
    print("| --- | --- | --- | --- | --- |")
    for cap, info in sorted(matrix.items()):
        providers = ", ".join(info["provided_by"]) or "**无提供方**"
        consumers = ", ".join(info["consumed_by"]) or "**无消费方**"
        declared = ", ".join(info["declared_by"]) or "**未声明**"
        key = info["key"]
        key_cell = "（同类型名）" if key == cap else (f"`{key}`" if key else "**未找到**")
        print(f"| `{cap}` | {key_cell} | {declared} | {providers} | {consumers} |")


def print_findings(mods: list[Module], matrix: dict, catalogue: list[dict]) -> None:
    by_id: dict[str, int] = defaultdict(int)
    modules_hit = 0
    for mod in mods:
        items = findings(mod, matrix, catalogue)
        if not items:
            continue
        modules_hit += 1
        for cid, _ in items:
            by_id[cid] += 1
    print(f"# 清单发现（{modules_hit}/{len(mods)} 个模块）\n")
    print("| 检查项 | 命中模块数 |")
    print("| --- | --- |")
    for cid, n in sorted(by_id.items(), key=lambda kv: -kv[1]):
        print(f"| {cid} | {n} |")
    print()
    for mod in sorted(mods, key=lambda m: m.name):
        items = findings(mod, matrix, catalogue)
        if not items:
            continue
        print(f"\n## {mod.name}  (L{mod.layer or '?'}, {mod.root.relative_to(REPO).as_posix() if mod.root else '?'})")
        for cid, detail in items:
            print(f"- [{cid}] {detail}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--capabilities", action="store_true", help="print the capability matrix")
    ap.add_argument("--findings", action="store_true", help="print checklist verdicts")
    ap.add_argument("--json", metavar="PATH", help="write machine-readable output here")
    args = ap.parse_args()

    mods = parse_manifests()
    compute_scan_dirs(mods)
    excludes = nested_excludes(mods)
    for mod in mods:
        mod.root = resolve_scan_root(mod)

    sources: dict[str, list[SourceFile]] = {}
    for mod in mods:
        if mod.root is not None:
            sources[mod.name] = load_sources(mod, excludes[mod.name])
            collect_declarations(mod, sources[mod.name])

    known = {cap: mod.capability_keys[cap] for mod in mods for cap in mod.declares}

    for mod in mods:
        if mod.root is not None:
            scan_module(mod, sources[mod.name], known)

    matrix = capability_matrix(mods)
    catalogue = load_catalogue()

    if args.json:
        payload = {
            "modules": [
                {
                    "name": m.name,
                    "layer": m.layer,
                    "root": m.root.relative_to(REPO).as_posix() if m.root else None,
                    "files": m.files,
                    "loc": m.loc,
                    "max_cpp": {"lines": m.max_cpp[0], "file": m.max_cpp[1]},
                    "declares": sorted(m.declares),
                    "capability_keys": m.capability_keys,
                    "provides": sorted(m.provides),
                    "consumes": sorted(m.consumes),
                    "unresolved_caps": m.unresolved_caps,
                    "convenience": m.convenience,
                    "script_binds": m.script_binds,
                    "observers": m.observers,
                    "ecs": m.ecs,
                    "links": m.links,
                    "homemade_registry": m.homemade_registry,
                    "hot_candidates": m.hot_candidates,
                    "owning_return_census": m.expensive_without_cost,
                    "catalogue_rules": sorted(catalogue_rules_for(m, catalogue)),
                    "cost_tags": m.cost_tags,
                    "findings": [
                        {"id": cid, "detail": d} for cid, d in findings(m, matrix, catalogue)
                    ],
                }
                for m in sorted(mods, key=lambda x: x.name)
            ],
            "capabilities": matrix,
        }
        Path(args.json).write_text(
            json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8"
        )

    if args.capabilities:
        print_capabilities(mods, matrix)
    if args.findings:
        print_findings(mods, matrix, catalogue)
    if not (args.capabilities or args.findings or args.json):
        print_summary(mods, matrix)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
