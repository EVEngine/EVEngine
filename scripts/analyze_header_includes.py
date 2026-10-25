#!/usr/bin/env python3
"""Reverse-include report for project headers.

Maps every project header to the ``.h`` / ``.cpp`` files that include it
directly, and ranks headers by how much moving their includes into a ``.cpp``
(or replacing them with forward declarations) would shrink the compile cascade.

Expansion counts **unique lines** in the transitive ``#include`` closure,
covering both project headers and resolvable system / third-party headers
(compiler default ``-isystem`` paths, ``compile_commands.json`` ``-I`` /
``-isystem``, vendored third-party include roots, and ``--include-path``).

Typical use:

    python3 scripts/analyze_header_includes.py
    python3 scripts/analyze_header_includes.py --sort expand
    python3 scripts/analyze_header_includes.py --header graphics/Graphics.h
    python3 scripts/analyze_header_includes.py \\
        --compile-commands build/linux-debug/compile_commands.json
    make stats/headers

``priority`` = ``header_users * outgoing_project``.
``expand_lines`` = unique lines in the full (project + system) closure.
``projL`` / ``sysL`` split that total; ``incL`` is the closure minus the
header itself.
"""

from __future__ import annotations

import argparse
import collections
import json
import os
import re
import shlex
import subprocess
from dataclasses import asdict, dataclass, field
from pathlib import Path

import utf8_stdio

SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}
HEADER_SUFFIXES = {".h", ".hh", ".hpp", ".hxx"}
CPP_SUFFIXES = {".c", ".cc", ".cpp", ".cxx"}
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*([<"])([^">]+)[">]', re.MULTILINE)
INCLUDE_NEXT_RE = re.compile(r'^\s*#\s*include_next\s*[<"]([^">]+)[">]', re.MULTILINE)

DEFAULT_INCLUDE_ROOTS = ("src/engine", "src/modules")
DEFAULT_SCAN_ROOTS = ("src", "test")
SORT_CHOICES = ("priority", "expand", "impact", "fanout")

# Vendored / prebuilt roots that mirror cmake/engine_usage.cmake. Missing paths
# are skipped; present ones resolve angle includes like <glm/...> / <SDL.h>.
THIRD_PARTY_INCLUDE_CANDIDATES = (
    "build/third-party-binary/linux-debug/include",
    "build/third-party-binary/linux/include",
    "build/third-party-binary/win32-debug/include",
    "build/third-party-binary/win32/include",
    "build/third-party-binary/macosx-debug/include",
    "build/third-party-binary/macosx/include",
    "third-party/medialoader/include",
    "third-party/medialoader/model-libs/assimp/include",
    "third-party/DungeonTemplateLibrary/include",
    "third-party/SDL2/include",
    "third-party/squirrel/include",
    "third-party/simplesquirrel/include",
    "third-party/poco/Foundation/include",
    "third-party/poco/JSON/include",
    "third-party/poco/Net/include",
    "third-party/poco/XML/include",
    "third-party/poco/Data/include",
    "third-party/poco/Data/SQLite/include",
    "third-party/imgui",
    "third-party/glm",
    "third-party/box2d/include",
    "third-party/box3d/include",
    "third-party/jolt",
    "external/ECS.hpp/src",
    "external/ik.hpp",
    "external/zeroerr/include",
    "external/VKBuilder/include",
    "external/VKBuilder/test/include",
)


@dataclass(frozen=True)
class HeaderSize:
    lines: int
    bytes: int


@dataclass
class FileIncludes:
    """Direct includes resolved from one source file."""

    path: str
    project: list[str] = field(default_factory=list)
    # Absolute resolved paths of system / third-party headers.
    system_paths: list[Path] = field(default_factory=list)
    # Angle/quoted names that could not be resolved against any include root.
    unresolved: list[str] = field(default_factory=list)
    # Original angle-include spellings (for display), parallel to resolution.
    system_names: list[str] = field(default_factory=list)


@dataclass(frozen=True)
class HeaderStat:
    path: str
    include_key: str
    header_users: int
    cpp_users: int
    outgoing_project: int
    outgoing_system: int
    unresolved: int
    lines: int
    bytes: int
    priority: int
    expand_headers: int
    expand_project_headers: int
    expand_system_headers: int
    expand_lines: int
    expand_project_lines: int
    expand_system_lines: int
    expand_bytes: int
    include_lines: int
    include_bytes: int
    impact: int

    @property
    def total_users(self) -> int:
        return self.header_users + self.cpp_users


@dataclass
class IncludeGraph:
    repo: Path
    headers_by_key: dict[str, Path]
    key_by_path: dict[Path, str]
    files: dict[str, FileIncludes]
    sizes: dict[Path, HeaderSize]
    system_roots: list[Path]
    project_roots: list[Path]
    dependents: dict[str, set[str]] = field(default_factory=lambda: collections.defaultdict(set))
    _scan_cache: dict[Path, FileIncludes] = field(default_factory=dict, repr=False)
    _closure_cache: dict[str, frozenset[Path]] = field(default_factory=dict, repr=False)

    def header_keys(self) -> list[str]:
        return sorted(self.headers_by_key)

    def display_path(self, path: Path) -> str:
        key = self.key_by_path.get(path)
        if key is not None:
            return key
        try:
            return path.resolve().relative_to(self.repo.resolve()).as_posix()
        except ValueError:
            pass
        for root in self.system_roots:
            try:
                return path.resolve().relative_to(root.resolve()).as_posix()
            except ValueError:
                continue
        return str(path)


def is_header(path: Path) -> bool:
    return path.suffix.lower() in HEADER_SUFFIXES


def is_cpp(path: Path) -> bool:
    return path.suffix.lower() in CPP_SUFFIXES


def measure_header(path: Path) -> HeaderSize:
    text = path.read_text(encoding="utf-8", errors="ignore")
    return HeaderSize(
        lines=text.count("\n") + (1 if text else 0),
        bytes=len(text.encode("utf-8")),
    )


def iter_sources(repo: Path, scan_roots: tuple[str, ...]) -> list[Path]:
    files: list[Path] = []
    for rel in scan_roots:
        root = repo / rel
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES:
                files.append(path)
    return sorted(files)


def index_headers(repo: Path, include_roots: tuple[str, ...]) -> dict[str, Path]:
    headers: dict[str, Path] = {}
    for rel in include_roots:
        root = (repo / rel).resolve()
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if not path.is_file() or not is_header(path):
                continue
            key = path.relative_to(root).as_posix()
            headers.setdefault(key, path.resolve())
    return headers


def compiler_system_include_paths(compiler: str | None = None) -> list[Path]:
    """Parse ``#include <...> search starts here`` from the host C++ driver."""
    cxx = compiler or os.environ.get("CXX") or os.environ.get("CXX_COMPILER") or "c++"
    try:
        completed = subprocess.run(
            [cxx, "-E", "-x", "c++", "-", "-v"],
            input="",
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
    except FileNotFoundError:
        return []

    text = (completed.stderr or "") + "\n" + (completed.stdout or "")
    roots: list[Path] = []
    taking = False
    for line in text.splitlines():
        if "search starts here" in line:
            taking = True
            continue
        if taking and line.startswith("End of search list"):
            break
        if taking:
            candidate = line.strip()
            if candidate.startswith("("):
                # Framework annotations on Apple: keep the path portion.
                candidate = candidate.split("(", 1)[0].strip()
            if candidate:
                path = Path(candidate)
                if path.is_dir():
                    roots.append(path.resolve())
    return roots


def paths_from_compile_commands(path: Path) -> list[Path]:
    """Collect unique ``-I`` / ``-isystem`` / ``-iframework`` dirs from a CDB."""
    if not path.is_file():
        return []
    try:
        entries = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return []

    found: list[Path] = []
    seen: set[Path] = set()
    flag_with_value = {"-I", "-isystem", "-iframework", "-cxx-isystem"}
    for entry in entries:
        directory = Path(entry.get("directory") or ".")
        tokens: list[str]
        if "arguments" in entry:
            tokens = list(entry["arguments"])
        elif "command" in entry:
            tokens = shlex.split(entry["command"])
        else:
            continue
        index = 0
        while index < len(tokens):
            token = tokens[index]
            value: str | None = None
            if token in flag_with_value and index + 1 < len(tokens):
                value = tokens[index + 1]
                index += 2
            elif token.startswith("-I") and len(token) > 2:
                value = token[2:]
                index += 1
            elif token.startswith("-isystem") and len(token) > 8:
                value = token[8:]
                index += 1
            else:
                index += 1
                continue
            candidate = Path(value)
            if not candidate.is_absolute():
                candidate = directory / candidate
            try:
                resolved = candidate.resolve()
            except OSError:
                continue
            if resolved.is_dir() and resolved not in seen:
                seen.add(resolved)
                found.append(resolved)
    return found


def discover_external_roots(
    repo: Path,
    *,
    compiler: str | None = None,
    compile_commands: Path | None = None,
    extra: list[Path] | None = None,
) -> list[Path]:
    """Ordered search path for angle-bracket / third-party includes."""
    roots: list[Path] = []
    seen: set[Path] = set()

    def add(path: Path) -> None:
        try:
            resolved = path.resolve()
        except OSError:
            return
        if resolved.is_dir() and resolved not in seen:
            seen.add(resolved)
            roots.append(resolved)

    for item in extra or []:
        add(item)
    if compile_commands is not None:
        for item in paths_from_compile_commands(compile_commands):
            add(item)
    for rel in THIRD_PARTY_INCLUDE_CANDIDATES:
        add(repo / rel)
    # Nested prebuilt include dirs (SDL2/, freetype2/, ...) also appear as
    # -I .../include/SDL2 in some targets; keep the parent and the child.
    for rel in (
        "build/third-party-binary/linux-debug/include",
        "build/third-party-binary/linux/include",
    ):
        parent = repo / rel
        if parent.is_dir():
            for child in parent.iterdir():
                if child.is_dir():
                    add(child)
    for item in compiler_system_include_paths(compiler):
        add(item)
    return roots


def resolve_in_roots(include: str, roots: list[Path]) -> Path | None:
    for root in roots:
        candidate = root / include
        try:
            if candidate.is_file():
                return candidate.resolve()
        except OSError:
            continue
    return None


def resolve_include(
    include: str,
    quoted: bool,
    includer: Path,
    headers_by_key: dict[str, Path],
    project_roots: list[Path],
    system_roots: list[Path],
) -> tuple[str, Path] | None:
    """Return ``('project', path)`` or ``('system', path)``, or None."""
    if include in headers_by_key:
        return ("project", headers_by_key[include])

    if quoted:
        candidate = (includer.parent / include)
        try:
            if candidate.is_file():
                resolved = candidate.resolve()
                for root in project_roots:
                    try:
                        resolved.relative_to(root)
                        return ("project", resolved)
                    except ValueError:
                        continue
                return ("system", resolved)
        except OSError:
            pass
        project_hit = resolve_in_roots(include, project_roots)
        if project_hit is not None:
            return ("project", project_hit)

    # Angle brackets, or quoted miss: search external roots (and project roots
    # again for angle includes of project keys that were not exact map hits).
    project_hit = resolve_in_roots(include, project_roots)
    if project_hit is not None:
        return ("project", project_hit)
    system_hit = resolve_in_roots(include, system_roots)
    if system_hit is not None:
        return ("system", system_hit)
    return None


def path_to_project_key(path: Path, headers_by_key: dict[str, Path], key_by_path: dict[Path, str]) -> str | None:
    if path in key_by_path:
        return key_by_path[path]
    for key, candidate in headers_by_key.items():
        if candidate == path:
            return key
    return None


def scan_file(
    path: Path,
    *,
    repo: Path,
    headers_by_key: dict[str, Path],
    key_by_path: dict[Path, str],
    project_roots: list[Path],
    system_roots: list[Path],
    display: str | None = None,
) -> FileIncludes:
    text = path.read_text(encoding="utf-8", errors="ignore")
    try:
        rel = display or path.resolve().relative_to(repo.resolve()).as_posix()
    except ValueError:
        rel = display or str(path.resolve())
    result = FileIncludes(path=rel)
    seen_project: set[str] = set()
    seen_system: set[Path] = set()

    for quote, include in INCLUDE_RE.findall(text):
        resolved = resolve_include(
            include,
            quote == '"',
            path,
            headers_by_key,
            project_roots,
            system_roots,
        )
        if resolved is None:
            result.unresolved.append(include)
            continue
        kind, target = resolved
        if kind == "project":
            key = path_to_project_key(target, headers_by_key, key_by_path)
            if key is None:
                # Under repo but outside public roots (e.g. test helpers).
                try:
                    key = target.relative_to(repo.resolve()).as_posix()
                except ValueError:
                    result.system_paths.append(target)
                    result.system_names.append(include)
                    continue
            if key in seen_project:
                continue
            seen_project.add(key)
            result.project.append(key)
        else:
            if target in seen_system:
                continue
            seen_system.add(target)
            result.system_paths.append(target)
            result.system_names.append(include)

    # include_next: resolve against roots *after* the directory of the current
    # file so libc++ / libstdc++ chains are not dropped as unresolved.
    includer_dir = path.resolve().parent
    for include in INCLUDE_NEXT_RE.findall(text):
        taking = False
        hit: Path | None = None
        for root in system_roots:
            try:
                root_res = root.resolve()
            except OSError:
                continue
            if not taking:
                try:
                    includer_dir.relative_to(root_res)
                    taking = True
                except ValueError:
                    if root_res == includer_dir:
                        taking = True
                continue
            candidate = root_res / include
            if candidate.is_file():
                hit = candidate.resolve()
                break
        if hit is None or hit in seen_system:
            continue
        seen_system.add(hit)
        result.system_paths.append(hit)
        result.system_names.append(include)

    return result


def build_graph(
    repo: Path,
    include_roots: tuple[str, ...] = DEFAULT_INCLUDE_ROOTS,
    scan_roots: tuple[str, ...] = DEFAULT_SCAN_ROOTS,
    system_roots: list[Path] | None = None,
) -> IncludeGraph:
    project_roots = [(repo / rel).resolve() for rel in include_roots if (repo / rel).exists()]
    headers_by_key = index_headers(repo, include_roots)
    key_by_path = {path: key for key, path in headers_by_key.items()}
    files: dict[str, FileIncludes] = {}
    dependents: dict[str, set[str]] = collections.defaultdict(set)
    sizes: dict[Path, HeaderSize] = {}
    external = list(system_roots or [])

    for path in headers_by_key.values():
        sizes[path] = measure_header(path)

    graph = IncludeGraph(
        repo=repo,
        headers_by_key=headers_by_key,
        key_by_path=key_by_path,
        files=files,
        sizes=sizes,
        system_roots=external,
        project_roots=project_roots,
        dependents=dependents,
    )

    for path in iter_sources(repo, scan_roots):
        info = ensure_scanned(graph, path)
        # Prefer repo-relative display path as the files map key for sources.
        try:
            rel = path.resolve().relative_to(repo.resolve()).as_posix()
        except ValueError:
            rel = str(path.resolve())
        files[rel] = info
        info.path = rel
        for key in info.project:
            dependents[key].add(rel)

    return graph


def ensure_scanned(graph: IncludeGraph, path: Path) -> FileIncludes:
    resolved = path.resolve()
    cached = graph._scan_cache.get(resolved)
    if cached is not None:
        return cached
    display = None
    key = graph.key_by_path.get(resolved)
    if key is not None:
        display = key
    info = scan_file(
        resolved,
        repo=graph.repo,
        headers_by_key=graph.headers_by_key,
        key_by_path=graph.key_by_path,
        project_roots=graph.project_roots,
        system_roots=graph.system_roots,
        display=display,
    )
    graph._scan_cache[resolved] = info
    if resolved not in graph.sizes:
        graph.sizes[resolved] = measure_header(resolved)
    return info


def transitive_closure(graph: IncludeGraph, key: str) -> frozenset[Path]:
    """Unique absolute header paths reachable from ``key`` (including itself)."""
    cached = graph._closure_cache.get(key)
    if cached is not None:
        return cached

    start = graph.headers_by_key.get(key)
    if start is None:
        return frozenset()

    seen: set[Path] = set()
    stack = [start.resolve()]
    while stack:
        current = stack.pop()
        if current in seen:
            continue
        seen.add(current)
        info = ensure_scanned(graph, current)
        for child_key in info.project:
            child = graph.headers_by_key.get(child_key)
            if child is not None and child.resolve() not in seen:
                stack.append(child.resolve())
        for child in info.system_paths:
            resolved = child.resolve()
            if resolved not in seen:
                stack.append(resolved)

    result = frozenset(seen)
    graph._closure_cache[key] = result
    return result


def expansion_totals(
    graph: IncludeGraph, paths: frozenset[Path]
) -> tuple[int, int, int, int, int, int, int]:
    """Return (headers, project_headers, system_headers, lines, project_lines, system_lines, bytes)."""
    project_headers = 0
    system_headers = 0
    project_lines = 0
    system_lines = 0
    nbytes = 0
    for path in paths:
        size = graph.sizes.get(path) or measure_header(path)
        graph.sizes[path] = size
        nbytes += size.bytes
        if path in graph.key_by_path:
            project_headers += 1
            project_lines += size.lines
        else:
            system_headers += 1
            system_lines += size.lines
    return (
        project_headers + system_headers,
        project_headers,
        system_headers,
        project_lines + system_lines,
        project_lines,
        system_lines,
        nbytes,
    )


def header_stat(graph: IncludeGraph, key: str) -> HeaderStat | None:
    path = graph.headers_by_key.get(key)
    if path is None:
        return None
    users = graph.dependents.get(key, set())
    header_users = sum(1 for u in users if is_header(Path(u)))
    cpp_users = sum(1 for u in users if is_cpp(Path(u)))
    try:
        rel = path.resolve().relative_to(graph.repo.resolve()).as_posix()
    except ValueError:
        rel = str(path.resolve())

    # Prefer the scanned source entry under repo-relative key.
    outgoing = graph.files.get(rel) or ensure_scanned(graph, path)
    size = graph.sizes.get(path.resolve()) or measure_header(path)
    closure = transitive_closure(graph, key)
    (
        expand_headers,
        expand_project_headers,
        expand_system_headers,
        expand_lines,
        expand_project_lines,
        expand_system_lines,
        expand_bytes,
    ) = expansion_totals(graph, closure)
    include_lines = max(0, expand_lines - size.lines)
    include_bytes = max(0, expand_bytes - size.bytes)
    total_users = header_users + cpp_users
    return HeaderStat(
        path=rel,
        include_key=key,
        header_users=header_users,
        cpp_users=cpp_users,
        outgoing_project=len(outgoing.project),
        outgoing_system=len(outgoing.system_paths),
        unresolved=len(outgoing.unresolved),
        lines=size.lines,
        bytes=size.bytes,
        priority=header_users * len(outgoing.project),
        expand_headers=expand_headers,
        expand_project_headers=expand_project_headers,
        expand_system_headers=expand_system_headers,
        expand_lines=expand_lines,
        expand_project_lines=expand_project_lines,
        expand_system_lines=expand_system_lines,
        expand_bytes=expand_bytes,
        include_lines=include_lines,
        include_bytes=include_bytes,
        impact=total_users * expand_lines,
    )


def sort_key(stat: HeaderStat, sort: str):
    if sort == "expand":
        return (-stat.expand_lines, -stat.expand_system_lines, -stat.total_users, stat.path)
    if sort == "impact":
        return (-stat.impact, -stat.expand_lines, -stat.total_users, stat.path)
    if sort == "fanout":
        return (-stat.header_users, -stat.total_users, -stat.expand_lines, stat.path)
    return (
        -stat.priority,
        -stat.expand_lines,
        -stat.header_users,
        -stat.outgoing_project,
        -stat.total_users,
        stat.path,
    )


def all_stats(graph: IncludeGraph, sort: str = "priority") -> list[HeaderStat]:
    stats = []
    for key in graph.header_keys():
        stat = header_stat(graph, key)
        if stat is None:
            continue
        if stat.total_users == 0 and stat.outgoing_project == 0 and stat.outgoing_system == 0:
            continue
        stats.append(stat)
    return sorted(stats, key=lambda s: sort_key(s, sort))


def split_dependents(graph: IncludeGraph, key: str) -> tuple[list[str], list[str]]:
    users = sorted(graph.dependents.get(key, set()))
    headers = [u for u in users if is_header(Path(u))]
    cpps = [u for u in users if is_cpp(Path(u))]
    return headers, cpps


def print_summary(stats: list[HeaderStat], limit: int, sort: str, system_roots: list[Path]) -> None:
    print("Header include reverse-dependency summary")
    print(
        "  expand_lines = unique lines in the transitive #include closure "
        "(project + resolved system/third-party); "
        "projL/sysL split that total; incL = expand_lines - ownL; "
        "impact = total_users * expand_lines"
    )
    print(f"  sorted by: {sort}")
    print(f"  system/third-party include roots: {len(system_roots)}")
    print()
    print(
        f"  {'prio':>6} {'hdr':>5} {'cpp':>5} {'outP':>5} "
        f"{'expH':>5} {'expL':>7} {'projL':>7} {'sysL':>7} {'incL':>7} "
        f"{'ownL':>6} {'impact':>10}  header"
    )
    for stat in stats[:limit]:
        print(
            f"  {stat.priority:6} {stat.header_users:5} {stat.cpp_users:5} "
            f"{stat.outgoing_project:5} {stat.expand_headers:5} {stat.expand_lines:7} "
            f"{stat.expand_project_lines:7} {stat.expand_system_lines:7} "
            f"{stat.include_lines:7} {stat.lines:6} {stat.impact:10}  {stat.path}"
        )
    print()
    print(
        "Columns: hdr/cpp = direct includers; outP = direct project includes; "
        "expH/expL = transitive unique headers / lines; "
        "projL/sysL = project vs system/third-party share of expL."
    )


def print_header_detail(graph: IncludeGraph, key: str, list_limit: int | None) -> int:
    stat = header_stat(graph, key)
    if stat is None:
        for candidate_key, path in graph.headers_by_key.items():
            try:
                rel = path.relative_to(graph.repo).as_posix()
            except ValueError:
                rel = str(path)
            if rel == key or candidate_key == key:
                key = candidate_key
                stat = header_stat(graph, key)
                break
    if stat is None:
        print(f"error: unknown project header {key!r}", flush=True)
        return 1

    headers, cpps = split_dependents(graph, key)
    outgoing = graph.files.get(stat.path) or ensure_scanned(graph, graph.headers_by_key[key])
    closure = transitive_closure(graph, key)

    print(f"Header: {stat.path}")
    print(f"  include key     : {stat.include_key}")
    print(f"  size            : {stat.lines} lines, {stat.bytes / 1024:.1f} KiB")
    print(f"  direct users    : {stat.header_users} headers, {stat.cpp_users} cpp")
    print(
        f"  outgoing        : {stat.outgoing_project} project, "
        f"{stat.outgoing_system} system/third-party, "
        f"{stat.unresolved} unresolved"
    )
    print(
        f"  expand          : {stat.expand_headers} headers, "
        f"{stat.expand_lines} lines, {stat.expand_bytes / 1024:.1f} KiB "
        f"(project {stat.expand_project_headers}/{stat.expand_project_lines}, "
        f"system {stat.expand_system_headers}/{stat.expand_system_lines})"
    )
    print(
        f"  include payload : {stat.include_lines} lines, "
        f"{stat.include_bytes / 1024:.1f} KiB "
        f"(closure minus self)"
    )
    print(f"  priority        : {stat.priority}")
    print(f"  impact          : {stat.impact}  (total_users * expand_lines)")
    print()

    if outgoing.project:
        print("Direct project includes:")
        for inc in outgoing.project:
            child = graph.headers_by_key.get(inc)
            size = graph.sizes.get(child) if child else None
            if size is None:
                print(f"  + {inc}")
            else:
                print(f"  + {inc}  ({size.lines} lines)")
        print()
    if outgoing.system_paths:
        print("Direct system / third-party includes:")
        for name, target in zip(outgoing.system_names, outgoing.system_paths):
            size = graph.sizes.get(target) or measure_header(target)
            print(f"  < {name}  -> {graph.display_path(target)}  ({size.lines} lines)")
        print()
    if outgoing.unresolved:
        print("Unresolved includes:")
        for name in outgoing.unresolved[: list_limit or len(outgoing.unresolved)]:
            print(f"  ? {name}")
        if list_limit is not None and len(outgoing.unresolved) > list_limit:
            print(f"  ... {len(outgoing.unresolved) - list_limit} more")
        print()

    project_rows = []
    system_rows = []
    self_path = graph.headers_by_key[key].resolve()
    for child in closure:
        if child == self_path:
            continue
        size = graph.sizes.get(child) or measure_header(child)
        row = (size.lines, size.bytes, graph.display_path(child))
        if child in graph.key_by_path:
            project_rows.append(row)
        else:
            system_rows.append(row)
    project_rows.sort(reverse=True)
    system_rows.sort(reverse=True)

    def dump_rows(title: str, rows: list[tuple[int, int, str]], total_lines: int) -> None:
        print(f"{title} ({len(rows)} headers, {total_lines} lines):")
        shown = rows if list_limit is None else rows[:list_limit]
        for lines, nbytes, name in shown:
            print(f"  * {lines:6} lines  {nbytes / 1024:7.1f} KiB  {name}")
        if list_limit is not None and len(rows) > list_limit:
            print(f"  ... {len(rows) - list_limit} more")
        print()

    dump_rows(
        "Transitive project includes by size",
        project_rows,
        max(0, stat.expand_project_lines - stat.lines),
    )
    dump_rows(
        "Transitive system/third-party includes by size",
        system_rows,
        stat.expand_system_lines,
    )

    def dump(title: str, items: list[str]) -> None:
        print(f"{title} ({len(items)}):")
        shown_items = items if list_limit is None else items[:list_limit]
        for item in shown_items:
            print(f"  <- {item}")
        if list_limit is not None and len(items) > list_limit:
            print(f"  ... {len(items) - list_limit} more")
        print()

    dump("Header dependents", headers)
    dump("Cpp dependents", cpps)
    return 0


def graph_to_json(graph: IncludeGraph, stats: list[HeaderStat], limit: int | None) -> dict:
    selected = stats if limit is None else stats[:limit]
    headers_out = []
    for stat in selected:
        hdrs, cpps = split_dependents(graph, stat.include_key)
        outgoing = graph.files.get(stat.path) or ensure_scanned(
            graph, graph.headers_by_key[stat.include_key]
        )
        closure = sorted(graph.display_path(p) for p in transitive_closure(graph, stat.include_key))
        headers_out.append(
            {
                **asdict(stat),
                "dependents": {"headers": hdrs, "cpp": cpps},
                "includes": {
                    "project": list(outgoing.project),
                    "system": [
                        {"name": name, "path": graph.display_path(path)}
                        for name, path in zip(outgoing.system_names, outgoing.system_paths)
                    ],
                    "unresolved": list(outgoing.unresolved),
                },
                "transitive": closure,
            }
        )
    return {
        "header_count": len(graph.headers_by_key),
        "scanned_files": len(graph.files),
        "system_roots": [str(p) for p in graph.system_roots],
        "headers": headers_out,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--source-dir", type=Path, default=Path.cwd())
    parser.add_argument("--limit", type=int, default=40)
    parser.add_argument("--header", metavar="KEY")
    parser.add_argument("--list-limit", type=int, default=50)
    parser.add_argument("--min-header-users", type=int, default=0)
    parser.add_argument("--min-outgoing", type=int, default=0)
    parser.add_argument("--min-expand-lines", type=int, default=0)
    parser.add_argument("--sort", choices=SORT_CHOICES, default="priority")
    parser.add_argument("--no-test", action="store_true")
    parser.add_argument(
        "--compile-commands",
        type=Path,
        help="compile_commands.json used to harvest -I/-isystem roots",
    )
    parser.add_argument(
        "--include-path",
        action="append",
        default=[],
        type=Path,
        help="extra system/third-party include root (repeatable)",
    )
    parser.add_argument(
        "--compiler",
        help="C++ driver for default system include paths (default: $CXX or c++)",
    )
    parser.add_argument(
        "--no-system",
        action="store_true",
        help="do not resolve/expand system or third-party headers",
    )
    parser.add_argument("--json", type=Path)
    args = parser.parse_args(argv)

    repo = args.source_dir.resolve()
    scan_roots = ("src",) if args.no_test else DEFAULT_SCAN_ROOTS
    if args.no_system:
        system_roots: list[Path] = []
    else:
        compile_commands = args.compile_commands
        if compile_commands is not None and not compile_commands.is_absolute():
            compile_commands = (repo / compile_commands).resolve()
        system_roots = discover_external_roots(
            repo,
            compiler=args.compiler,
            compile_commands=compile_commands,
            extra=list(args.include_path),
        )

    graph = build_graph(repo, scan_roots=scan_roots, system_roots=system_roots)
    stats = all_stats(graph, sort=args.sort)
    if args.min_header_users or args.min_outgoing or args.min_expand_lines:
        stats = [
            s
            for s in stats
            if s.header_users >= args.min_header_users
            and s.outgoing_project >= args.min_outgoing
            and s.expand_lines >= args.min_expand_lines
        ]

    limit = None if args.limit <= 0 else args.limit
    list_limit = None if args.list_limit <= 0 else args.list_limit

    if args.header:
        status = print_header_detail(graph, args.header, list_limit)
    else:
        print_summary(stats, limit or len(stats), args.sort, system_roots)
        status = 0

    if args.json:
        payload = graph_to_json(graph, stats, limit)
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
        print(f"JSON report: {args.json}")

    return status


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
