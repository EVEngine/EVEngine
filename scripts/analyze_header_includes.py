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


@dataclass
class FileIncludes:
    """Direct includes resolved from one source file."""

    path: str
    project: list[str] = field(default_factory=list)
    system: list[str] = field(default_factory=list)
    unresolved: list[str] = field(default_factory=list)


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
    # header include-key -> relative paths that include it directly
    dependents: dict[str, set[str]] = field(default_factory=lambda: collections.defaultdict(set))

    def header_keys(self) -> list[str]:
        return sorted(self.headers_by_key)


def build_graph(
    repo: Path,
    include_roots: tuple[str, ...] = DEFAULT_INCLUDE_ROOTS,
    scan_roots: tuple[str, ...] = DEFAULT_SCAN_ROOTS,
) -> IncludeGraph:
    headers_by_key = index_headers(repo, include_roots)
    files: dict[str, FileIncludes] = {}
    dependents: dict[str, set[str]] = collections.defaultdict(set)

    for path in iter_sources(repo, scan_roots):
        info = scan_file(path, repo, headers_by_key, include_roots)
        files[info.path] = info
        for key in info.project:
            dependents[key].add(info.path)

    return IncludeGraph(
        repo=repo,
        headers_by_key=headers_by_key,
        files=files,
        dependents=dependents,
    )


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
    text = path.read_text(encoding="utf-8", errors="ignore")
    size = len(text.encode("utf-8"))
    return HeaderStat(
        path=rel,
        include_key=key,
        header_users=header_users,
        cpp_users=cpp_users,
        outgoing_project=outgoing_project,
        outgoing_system=outgoing_system,
        lines=text.count("\n") + (1 if text else 0),
        bytes=size,
        priority=header_users * outgoing_project,
    )


def all_stats(graph: IncludeGraph) -> list[HeaderStat]:
    stats = []
    for key in graph.header_keys():
        # Skip headers that nothing includes and that pull nothing — noise.
        stat = header_stat(graph, key)
        if stat is None:
            continue
        if stat.total_users == 0 and stat.outgoing_project == 0:
            continue
        stats.append(stat)
    return sorted(
        stats,
        key=lambda s: (-s.priority, -s.header_users, -s.outgoing_project, -s.total_users, s.path),
    )


def split_dependents(graph: IncludeGraph, key: str) -> tuple[list[str], list[str]]:
    users = sorted(graph.dependents.get(key, set()))
    headers = [u for u in users if is_header(Path(u))]
    cpps = [u for u in users if is_cpp(Path(u))]
    return headers, cpps


def print_summary(stats: list[HeaderStat], limit: int) -> None:
    print("Header include reverse-dependency summary")
    print(
        "  priority = header_users * outgoing_project  "
        "(high => move heavy #includes into the .cpp / forward-declare)"
    )
    print()
    print(
        f"  {'prio':>6} {'hdr':>5} {'cpp':>5} {'outP':>5} {'outS':>5} "
        f"{'lines':>7} {'KiB':>7}  header"
    )
    for stat in stats[:limit]:
        print(
            f"  {stat.priority:6} {stat.header_users:5} {stat.cpp_users:5} "
            f"{stat.outgoing_project:5} {stat.outgoing_system:5} "
            f"{stat.lines:7} {stat.bytes / 1024:7.1f}  {stat.path}"
        )
    print()
    print(
        "Columns: hdr/cpp = direct includers; outP/outS = project/system "
        "includes inside this header."
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

    print(f"Header: {stat.path}")
    print(f"  include key     : {stat.include_key}")
    print(f"  size            : {stat.lines} lines, {stat.bytes / 1024:.1f} KiB")
    print(f"  direct users    : {stat.header_users} headers, {stat.cpp_users} cpp")
    print(f"  outgoing        : {stat.outgoing_project} project, {stat.outgoing_system} system")
    print(f"  priority        : {stat.priority}")
    print()

    if outgoing and outgoing.project:
        print("Project includes pulled in by this header:")
        for inc in outgoing.project:
            print(f"  + {inc}")
        print()
    if outgoing and outgoing.system:
        print("System / third-party includes in this header:")
        for inc in outgoing.system[: list_limit or len(outgoing.system)]:
            print(f"  < {inc}")
        if list_limit is not None and len(outgoing.system) > list_limit:
            print(f"  ... {len(outgoing.system) - list_limit} more")
        print()

    def dump(title: str, items: list[str]) -> None:
        print(f"{title} ({len(items)}):")
        shown = items if list_limit is None else items[:list_limit]
        for item in shown:
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
        headers_out.append(
            {
                **asdict(stat),
                "dependents": {"headers": hdrs, "cpp": cpps},
                "includes": {
                    "project": list(outgoing.project) if outgoing else [],
                    "system": list(outgoing.system) if outgoing else [],
                    "unresolved": list(outgoing.unresolved) if outgoing else [],
                },
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
    stats = all_stats(graph)
    if args.min_header_users or args.min_outgoing:
        stats = [
            s
            for s in stats
            if s.header_users >= args.min_header_users and s.outgoing_project >= args.min_outgoing
        ]

    limit = None if args.limit <= 0 else args.limit
    list_limit = None if args.list_limit <= 0 else args.list_limit

    if args.header:
        status = print_header_detail(graph, args.header, list_limit)
    else:
        print_summary(stats, limit or len(stats))
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
