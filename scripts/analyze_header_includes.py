#!/usr/bin/env python3
"""Reverse-include report for project headers.

Maps every project header to the ``.h`` / ``.cpp`` files that include it
directly, and ranks headers by how much moving their includes into a ``.cpp``
(or replacing them with forward declarations) would shrink the compile cascade.

Typical use when hunting object-size / compile-time cost:

    python3 scripts/analyze_header_includes.py
    python3 scripts/analyze_header_includes.py --limit 40
    python3 scripts/analyze_header_includes.py --header graphics/Graphics.h
    python3 scripts/analyze_header_includes.py --json /tmp/headers.json
    make stats/headers

``priority`` is ``header_users * outgoing_project``: headers that both pull in
many project includes *and* are themselves included by many other headers are
the cheapest wins for "hide details in the ``.cpp``".

``expand_lines`` is the unique project-header lines in the transitive
``#include`` closure (including the header itself); ``include_lines`` is that
total minus the header's own size — i.e. how much code a single ``#include``
of this header brings in from other project headers. System / third-party
headers are counted by name only (``outgoing_system`` / transitive system set),
not by line count, because their sources are outside the tree.
"""

from __future__ import annotations

import argparse
import collections
import json
import re
from dataclasses import asdict, dataclass, field
from pathlib import Path

import utf8_stdio

SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}
HEADER_SUFFIXES = {".h", ".hh", ".hpp", ".hxx"}
CPP_SUFFIXES = {".c", ".cc", ".cpp", ".cxx"}
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*([<"])([^">]+)[">]', re.MULTILINE)

# Engine and module public includes resolve against these roots (same layout the
# compile commands use for ``-I src/engine -I src/modules``).
DEFAULT_INCLUDE_ROOTS = ("src/engine", "src/modules")
DEFAULT_SCAN_ROOTS = ("src", "test")
SORT_CHOICES = ("priority", "expand", "impact", "fanout")


@dataclass
class FileIncludes:
    """Direct includes resolved from one source file."""

    path: str
    project: list[str] = field(default_factory=list)
    system: list[str] = field(default_factory=list)
    unresolved: list[str] = field(default_factory=list)


@dataclass(frozen=True)
class HeaderSize:
    """Cached size of one header file."""

    lines: int
    bytes: int


@dataclass(frozen=True)
class HeaderStat:
    """Aggregate fan-in / fan-out for one project header."""

    path: str
    include_key: str
    header_users: int
    cpp_users: int
    outgoing_project: int
    outgoing_system: int
    lines: int
    bytes: int
    priority: int
    # Transitive unique project-header closure (including self).
    expand_headers: int
    expand_lines: int
    expand_bytes: int
    # expand_* minus this header itself — code brought in by its #includes.
    include_lines: int
    include_bytes: int
    # total_users * expand_lines: rough "lines recompiled because of this header".
    impact: int

    @property
    def total_users(self) -> int:
        return self.header_users + self.cpp_users


def is_header(path: Path) -> bool:
    return path.suffix.lower() in HEADER_SUFFIXES


def is_cpp(path: Path) -> bool:
    return path.suffix.lower() in CPP_SUFFIXES


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
    """Map include-key (posix, relative to an include root) -> absolute path.

    When the same relative key exists under more than one root, the earlier
    root in ``include_roots`` wins (engine before modules).
    """
    headers: dict[str, Path] = {}
    for rel in include_roots:
        root = repo / rel
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if not path.is_file() or not is_header(path):
                continue
            key = path.relative_to(root).as_posix()
            headers.setdefault(key, path)
    return headers


def path_to_include_key(repo: Path, path: Path, include_roots: tuple[str, ...]) -> str | None:
    for rel in include_roots:
        root = repo / rel
        try:
            return path.resolve().relative_to(root.resolve()).as_posix()
        except ValueError:
            continue
    return None


def resolve_include(
    include: str,
    quoted: bool,
    includer: Path,
    headers_by_key: dict[str, Path],
    repo: Path,
    include_roots: tuple[str, ...],
) -> Path | None:
    """Resolve a project include to a real header path, or None if not ours."""
    if include in headers_by_key:
        return headers_by_key[include]

    # Same-directory / relative quoted includes ("cmdline.h", "../Foo.h").
    if quoted:
        candidate = (includer.parent / include).resolve()
        if candidate.is_file() and is_header(candidate):
            return candidate
        for rel in include_roots:
            candidate = (repo / rel / include).resolve()
            if candidate.is_file() and is_header(candidate):
                return candidate
    return None


def scan_file(
    path: Path,
    repo: Path,
    headers_by_key: dict[str, Path],
    include_roots: tuple[str, ...],
) -> FileIncludes:
    text = path.read_text(encoding="utf-8", errors="ignore")
    rel = path.relative_to(repo).as_posix()
    result = FileIncludes(path=rel)
    seen_project: set[str] = set()
    for quote, include in INCLUDE_RE.findall(text):
        quoted = quote == '"'
        if not quoted:
            # Angle-bracket includes are treated as system / third-party unless
            # they happen to match a project include key (rare but possible).
            resolved = headers_by_key.get(include)
            if resolved is None:
                result.system.append(include)
                continue
        else:
            resolved = resolve_include(
                include, True, path, headers_by_key, repo, include_roots
            )
            if resolved is None:
                result.unresolved.append(include)
                continue

        key = path_to_include_key(repo, resolved, include_roots)
        if key is None:
            # Header outside the public include roots (e.g. under test/).
            key = resolved.relative_to(repo).as_posix()
        if key in seen_project:
            continue
        seen_project.add(key)
        result.project.append(key)
    return result


@dataclass
class IncludeGraph:
    """Direct include edges and reverse dependents for project headers."""

    repo: Path
    headers_by_key: dict[str, Path]
    files: dict[str, FileIncludes]
    sizes: dict[str, HeaderSize]
    # header include-key -> relative paths that include it directly
    dependents: dict[str, set[str]] = field(default_factory=lambda: collections.defaultdict(set))
    _closure_cache: dict[str, frozenset[str]] = field(default_factory=dict, repr=False)

    def header_keys(self) -> list[str]:
        return sorted(self.headers_by_key)

    def key_to_rel(self, key: str) -> str | None:
        path = self.headers_by_key.get(key)
        if path is None:
            return None
        return path.relative_to(self.repo).as_posix()


def measure_header(path: Path) -> HeaderSize:
    text = path.read_text(encoding="utf-8", errors="ignore")
    return HeaderSize(
        lines=text.count("\n") + (1 if text else 0),
        bytes=len(text.encode("utf-8")),
    )


def build_graph(
    repo: Path,
    include_roots: tuple[str, ...] = DEFAULT_INCLUDE_ROOTS,
    scan_roots: tuple[str, ...] = DEFAULT_SCAN_ROOTS,
) -> IncludeGraph:
    headers_by_key = index_headers(repo, include_roots)
    files: dict[str, FileIncludes] = {}
    dependents: dict[str, set[str]] = collections.defaultdict(set)
    sizes: dict[str, HeaderSize] = {}

    for key, path in headers_by_key.items():
        sizes[key] = measure_header(path)

    for path in iter_sources(repo, scan_roots):
        info = scan_file(path, repo, headers_by_key, include_roots)
        files[info.path] = info
        for key in info.project:
            dependents[key].add(info.path)

    return IncludeGraph(
        repo=repo,
        headers_by_key=headers_by_key,
        files=files,
        sizes=sizes,
        dependents=dependents,
    )


def transitive_project_closure(graph: IncludeGraph, key: str) -> frozenset[str]:
    """Unique project include-keys reachable from ``key`` (including itself)."""
    cached = graph._closure_cache.get(key)
    if cached is not None:
        return cached

    seen: set[str] = set()
    stack = [key]
    while stack:
        current = stack.pop()
        if current in seen:
            continue
        seen.add(current)
        rel = graph.key_to_rel(current)
        if rel is None:
            continue
        outgoing = graph.files.get(rel)
        if outgoing is None:
            continue
        for child in outgoing.project:
            # Only walk headers indexed under the public include roots; keys that
            # point at test/ fixtures are dependents, not expansion inputs.
            if child in graph.headers_by_key and child not in seen:
                stack.append(child)

    result = frozenset(seen)
    graph._closure_cache[key] = result
    return result


def expansion_totals(graph: IncludeGraph, keys: frozenset[str]) -> tuple[int, int]:
    lines = 0
    nbytes = 0
    for key in keys:
        size = graph.sizes.get(key)
        if size is None:
            continue
        lines += size.lines
        nbytes += size.bytes
    return lines, nbytes


def header_stat(graph: IncludeGraph, key: str) -> HeaderStat | None:
    path = graph.headers_by_key.get(key)
    if path is None:
        return None
    users = graph.dependents.get(key, set())
    header_users = sum(1 for u in users if is_header(Path(u)))
    cpp_users = sum(1 for u in users if is_cpp(Path(u)))
    # Outgoing edges: what this header itself includes.
    rel = path.relative_to(graph.repo).as_posix()
    outgoing = graph.files.get(rel)
    outgoing_project = len(outgoing.project) if outgoing else 0
    outgoing_system = len(outgoing.system) if outgoing else 0
    size = graph.sizes.get(key) or measure_header(path)
    closure = transitive_project_closure(graph, key)
    expand_lines, expand_bytes = expansion_totals(graph, closure)
    include_lines = max(0, expand_lines - size.lines)
    include_bytes = max(0, expand_bytes - size.bytes)
    total_users = header_users + cpp_users
    return HeaderStat(
        path=rel,
        include_key=key,
        header_users=header_users,
        cpp_users=cpp_users,
        outgoing_project=outgoing_project,
        outgoing_system=outgoing_system,
        lines=size.lines,
        bytes=size.bytes,
        priority=header_users * outgoing_project,
        expand_headers=len(closure),
        expand_lines=expand_lines,
        expand_bytes=expand_bytes,
        include_lines=include_lines,
        include_bytes=include_bytes,
        impact=total_users * expand_lines,
    )


def sort_key(stat: HeaderStat, sort: str):
    if sort == "expand":
        return (-stat.expand_lines, -stat.header_users, -stat.total_users, stat.path)
    if sort == "impact":
        return (-stat.impact, -stat.expand_lines, -stat.total_users, stat.path)
    if sort == "fanout":
        return (-stat.header_users, -stat.total_users, -stat.expand_lines, stat.path)
    # priority (default)
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
        # Skip headers that nothing includes and that pull nothing — noise.
        stat = header_stat(graph, key)
        if stat is None:
            continue
        if stat.total_users == 0 and stat.outgoing_project == 0:
            continue
        stats.append(stat)
    return sorted(stats, key=lambda s: sort_key(s, sort))


def split_dependents(graph: IncludeGraph, key: str) -> tuple[list[str], list[str]]:
    users = sorted(graph.dependents.get(key, set()))
    headers = [u for u in users if is_header(Path(u))]
    cpps = [u for u in users if is_cpp(Path(u))]
    return headers, cpps


def print_summary(stats: list[HeaderStat], limit: int, sort: str) -> None:
    print("Header include reverse-dependency summary")
    print(
        "  priority = header_users * outgoing_project;  "
        "expand_lines = unique project-header lines in the transitive #include closure;  "
        "include_lines = expand_lines minus this header;  "
        "impact = total_users * expand_lines"
    )
    print(f"  sorted by: {sort}")
    print()
    print(
        f"  {'prio':>6} {'hdr':>5} {'cpp':>5} {'outP':>5} "
        f"{'expH':>5} {'expL':>7} {'incL':>7} {'ownL':>6} {'impact':>10}  header"
    )
    for stat in stats[:limit]:
        print(
            f"  {stat.priority:6} {stat.header_users:5} {stat.cpp_users:5} "
            f"{stat.outgoing_project:5} {stat.expand_headers:5} {stat.expand_lines:7} "
            f"{stat.include_lines:7} {stat.lines:6} {stat.impact:10}  {stat.path}"
        )
    print()
    print(
        "Columns: hdr/cpp = direct includers; outP = direct project includes; "
        "expH/expL = transitive unique project headers / lines; "
        "incL = lines brought in by #includes (expL - ownL); ownL = this file."
    )


def print_header_detail(graph: IncludeGraph, key: str, list_limit: int | None) -> int:
    stat = header_stat(graph, key)
    if stat is None:
        # Allow passing a repo-relative path as well as an include key.
        for candidate_key, path in graph.headers_by_key.items():
            if path.relative_to(graph.repo).as_posix() == key or candidate_key == key:
                key = candidate_key
                stat = header_stat(graph, key)
                break
    if stat is None:
        print(f"error: unknown project header {key!r}", flush=True)
        return 1

    headers, cpps = split_dependents(graph, key)
    outgoing = graph.files.get(stat.path)
    closure = transitive_project_closure(graph, key)

    print(f"Header: {stat.path}")
    print(f"  include key     : {stat.include_key}")
    print(f"  size            : {stat.lines} lines, {stat.bytes / 1024:.1f} KiB")
    print(f"  direct users    : {stat.header_users} headers, {stat.cpp_users} cpp")
    print(f"  outgoing        : {stat.outgoing_project} project, {stat.outgoing_system} system")
    print(
        f"  expand          : {stat.expand_headers} project headers, "
        f"{stat.expand_lines} lines, {stat.expand_bytes / 1024:.1f} KiB "
        f"(unique transitive closure, including self)"
    )
    print(
        f"  include payload : {stat.include_lines} lines, "
        f"{stat.include_bytes / 1024:.1f} KiB "
        f"(closure minus self — what one #include pulls in)"
    )
    print(f"  priority        : {stat.priority}")
    print(f"  impact          : {stat.impact}  (total_users * expand_lines)")
    print()

    if outgoing and outgoing.project:
        print("Direct project includes:")
        for inc in outgoing.project:
            size = graph.sizes.get(inc)
            if size is None:
                print(f"  + {inc}")
            else:
                print(f"  + {inc}  ({size.lines} lines)")
        print()
    if outgoing and outgoing.system:
        print("System / third-party includes in this header:")
        for inc in outgoing.system[: list_limit or len(outgoing.system)]:
            print(f"  < {inc}")
        if list_limit is not None and len(outgoing.system) > list_limit:
            print(f"  ... {len(outgoing.system) - list_limit} more")
        print()

    # Transitive closure ranked by size — the actual compile payload.
    closure_rows = []
    for child in closure:
        if child == key:
            continue
        size = graph.sizes.get(child)
        if size is None:
            continue
        closure_rows.append((size.lines, size.bytes, child))
    closure_rows.sort(reverse=True)
    print(
        f"Transitive project includes by size "
        f"({len(closure_rows)} headers, {stat.include_lines} lines):"
    )
    shown = closure_rows if list_limit is None else closure_rows[:list_limit]
    for lines, nbytes, child in shown:
        print(f"  * {lines:6} lines  {nbytes / 1024:7.1f} KiB  {child}")
    if list_limit is not None and len(closure_rows) > list_limit:
        print(f"  ... {len(closure_rows) - list_limit} more")
    print()

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
        outgoing = graph.files.get(stat.path)
        closure = sorted(transitive_project_closure(graph, stat.include_key))
        headers_out.append(
            {
                **asdict(stat),
                "dependents": {"headers": hdrs, "cpp": cpps},
                "includes": {
                    "project": list(outgoing.project) if outgoing else [],
                    "system": list(outgoing.system) if outgoing else [],
                    "unresolved": list(outgoing.unresolved) if outgoing else [],
                },
                "transitive_project": closure,
            }
        )
    return {
        "header_count": len(graph.headers_by_key),
        "scanned_files": len(graph.files),
        "headers": headers_out,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--source-dir",
        type=Path,
        default=Path.cwd(),
        help="repository root (default: cwd)",
    )
    parser.add_argument(
        "--limit",
        type=int,
        default=40,
        help="rows in the summary / entries in JSON (default: 40; 0 = all)",
    )
    parser.add_argument(
        "--header",
        metavar="KEY",
        help="detail one header (include key or repo-relative path)",
    )
    parser.add_argument(
        "--list-limit",
        type=int,
        default=50,
        help="max dependents/includes listed in --header detail (0 = all)",
    )
    parser.add_argument(
        "--min-header-users",
        type=int,
        default=0,
        help="only show headers included by at least this many other headers",
    )
    parser.add_argument(
        "--min-outgoing",
        type=int,
        default=0,
        help="only show headers that themselves include at least this many project headers",
    )
    parser.add_argument(
        "--min-expand-lines",
        type=int,
        default=0,
        help="only show headers whose transitive project closure has at least this many lines",
    )
    parser.add_argument(
        "--sort",
        choices=SORT_CHOICES,
        default="priority",
        help="summary order: priority (default), expand, impact, fanout",
    )
    parser.add_argument(
        "--no-test",
        action="store_true",
        help="do not scan test/ as includers",
    )
    parser.add_argument(
        "--json",
        type=Path,
        help="write the (filtered) report as JSON",
    )
    args = parser.parse_args(argv)

    repo = args.source_dir.resolve()
    scan_roots = ("src",) if args.no_test else DEFAULT_SCAN_ROOTS
    graph = build_graph(repo, scan_roots=scan_roots)
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
        print_summary(stats, limit or len(stats), args.sort)
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
