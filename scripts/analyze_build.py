#!/usr/bin/env python3
"""Report object-file size and high-impact header usage for local builds."""

from __future__ import annotations

import argparse
import collections
import json
import re
from dataclasses import dataclass
from pathlib import Path


SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}
HEADER_SUFFIXES = {".h", ".hh", ".hpp", ".hxx"}
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*["<]([^">]+)[">]', re.MULTILINE)
TEMPLATE_RE = re.compile(r"\btemplate\s*<")


@dataclass(frozen=True)
class HeaderMetric:
    path: str
    fanout: int
    lines: int
    bytes: int
    templates: int

    @property
    def impact(self) -> int:
        return self.fanout * self.bytes


def source_files(source_dir: Path) -> list[Path]:
    roots = [source_dir / "src", source_dir / "test"]
    return sorted(
        path
        for root in roots
        if root.exists()
        for path in root.rglob("*")
        if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES
    )


def direct_header_metrics(source_dir: Path) -> list[HeaderMetric]:
    files = source_files(source_dir)
    include_roots = [source_dir / "src" / "engine", source_dir / "src" / "modules"]
    headers: dict[str, Path] = {}
    for root in include_roots:
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if path.is_file() and path.suffix.lower() in HEADER_SUFFIXES:
                headers.setdefault(path.relative_to(root).as_posix(), path)

    users: dict[str, set[Path]] = collections.defaultdict(set)
    for path in files:
        text = path.read_text(encoding="utf-8", errors="ignore")
        for include in INCLUDE_RE.findall(text):
            if include in headers:
                users[include].add(path)

    metrics = []
    for include, consumers in users.items():
        path = headers[include]
        text = path.read_text(encoding="utf-8", errors="ignore")
        metrics.append(
            HeaderMetric(
                path=path.relative_to(source_dir).as_posix(),
                fanout=len(consumers),
                lines=text.count("\n") + 1,
                bytes=len(text.encode("utf-8")),
                templates=len(TEMPLATE_RE.findall(text)),
            )
        )
    return sorted(metrics, key=lambda metric: (-metric.impact, -metric.fanout, metric.path))


def object_metrics(build_dir: Path) -> list[tuple[int, Path]]:
    if not build_dir.exists():
        return []
    return sorted(
        (
            (path.stat().st_size, path)
            for path in build_dir.rglob("*")
            if path.is_file() and path.suffix.lower() in {".o", ".obj"}
        ),
        reverse=True,
    )


def mib(value: int) -> str:
    return f"{value / (1024 * 1024):.2f}"


def report(source_dir: Path, build_dir: Path, limit: int) -> dict[str, object]:
    headers = direct_header_metrics(source_dir)
    objects = object_metrics(build_dir)
    template_headers = sorted(
        (metric for metric in headers if metric.templates),
        key=lambda metric: (-metric.impact, -metric.templates, metric.path),
    )

    print(f"Build directory: {build_dir}")
    if objects:
        total = sum(size for size, _ in objects)
        print(f"Objects: {len(objects)} files, {mib(total)} MiB total")
        print("\nLargest object files (MiB):")
        for size, path in objects[:limit]:
            print(f"  {mib(size):>10}  {path.relative_to(build_dir)}")
    else:
        print("Objects: none found (header analysis is still available)")

    print("\nHigh-impact directly included project headers:")
    print("  fanout templates    lines      KiB  header")
    for metric in headers[:limit]:
        print(
            f"  {metric.fanout:6} {metric.templates:9} {metric.lines:8} "
            f"{metric.bytes / 1024:8.1f}  {metric.path}"
        )

    print("\nTemplate-bearing headers, ordered by direct fanout x source bytes:")
    print("  fanout templates    lines      KiB  header")
    for metric in template_headers[:limit]:
        print(
            f"  {metric.fanout:6} {metric.templates:9} {metric.lines:8} "
            f"{metric.bytes / 1024:8.1f}  {metric.path}"
        )

    return {
        "build_dir": str(build_dir),
        "objects": {
            "count": len(objects),
            "bytes": sum(size for size, _ in objects),
            "largest": [
                {"path": path.relative_to(build_dir).as_posix(), "bytes": size}
                for size, path in objects[:limit]
            ],
        },
        "headers": [metric.__dict__ | {"impact": metric.impact} for metric in headers],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, default=Path.cwd())
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--limit", type=int, default=30)
    parser.add_argument("--json", type=Path, help="also write the complete report as JSON")
    args = parser.parse_args()

    source_dir = args.source_dir.resolve()
    build_dir = args.build_dir.resolve()
    result = report(source_dir, build_dir, max(1, args.limit))
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(f"\nJSON report: {args.json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
