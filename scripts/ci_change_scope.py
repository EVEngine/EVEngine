#!/usr/bin/env python3
"""Classify changed paths into conservative CI build scopes."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path

SCOPES = ("windows", "android", "macos", "ios", "linux", "asan", "fuzz", "webgpu")
ALL = frozenset(SCOPES)


def classify_path(path: str) -> set[str]:
    path = path.replace("\\", "/")
    if path.startswith("platform/android/"):
        return {"android"}
    if path.startswith("platform/ios/"):
        return {"ios"}
    if path.startswith("platform/macos"):
        return {"macos"}
    if path.startswith(("web/", "platform/web", "cmake/webgpu")) or "webgpu" in path.lower():
        return {"webgpu"}
    if path.startswith(("src/", "test/", "external/", "third-party/")) or path in {
        "CMakeLists.txt",
        "Makefile",
    } or path.startswith(("cmake/", ".github/")):
        return set(ALL)
    if path.startswith("docs/") or ("/" not in path and path.lower().endswith(".md")):
        return set()
    if path.startswith(("scripts/", "tools/")) and path.endswith((".py", ".json")):
        return set()
    # New top-level areas and unfamiliar build inputs are safer to build fully.
    return set(ALL)


def classify(paths: list[str], force_all: bool = False) -> dict[str, bool]:
    selected = set(ALL) if force_all else set().union(*(classify_path(path) for path in paths))
    return {scope: scope in selected for scope in SCOPES}


def changed_paths(base: str, head: str) -> list[str]:
    result = subprocess.run(
        ["git", "diff", "--name-only", f"{base}...{head}"],
        check=True,
        capture_output=True,
        text=True,
    )
    return [line for line in result.stdout.splitlines() if line]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base")
    parser.add_argument("--head", default="HEAD")
    parser.add_argument("--all", action="store_true", dest="force_all")
    parser.add_argument("--github-output", type=Path)
    args = parser.parse_args()
    if not args.force_all and not args.base:
        parser.error("--base is required unless --all is used")

    paths = [] if args.force_all else changed_paths(args.base, args.head)
    scopes = classify(paths, force_all=args.force_all)
    lines = [f"{scope}={'true' if enabled else 'false'}" for scope, enabled in scopes.items()]
    lines.append(f"changed_count={len(paths)}")
    if args.github_output:
        with args.github_output.open("a", encoding="utf-8") as output:
            output.write("\n".join(lines) + "\n")
    else:
        print("\n".join(lines))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
