#!/usr/bin/env python3
"""Classify changed paths into conservative CI build scopes."""

from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path
import utf8_stdio

ROOT = Path(__file__).resolve().parent.parent
BUILD_SCOPES = ("windows", "android", "macos", "ios", "linux", "asan", "fuzz", "webgpu")
SCOPES = (*BUILD_SCOPES, "tools")
ALL = frozenset(SCOPES)
TEST_CASE_RE = re.compile(r"\bTEST_CASE(?:_FIXTURE)?\s*\(")


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
    if path.startswith("tools/"):
        return {"tools"}
    if path.startswith(".github/"):
        return set(ALL)
    if path.startswith(("src/", "test/", "external/", "third-party/")) or path in {
        "CMakeLists.txt",
        "Makefile",
    } or path.startswith("cmake/"):
        return set(BUILD_SCOPES)
    if path.startswith("docs/") or ("/" not in path and path.lower().endswith(".md")):
        return set()
    if path.startswith("scripts/") and path.endswith((".py", ".json")):
        return set()
    # New top-level areas and unfamiliar build inputs are safer to build fully.
    return set(ALL)


def classify(paths: list[str], force_all: bool = False) -> dict[str, bool]:
    selected = set(ALL) if force_all else set().union(*(classify_path(path) for path in paths))
    return {scope: scope in selected for scope in SCOPES}


def classify_tests(paths: list[str], force_all: bool = False) -> tuple[str, str]:
    """Return (mode, CTest label regex) for the changed paths.

    Only a diff made exclusively of zeroerr test translation units is safe to
    narrow without a production-module dependency closure. Everything else
    fails closed to the full suite. The source labels come from zeroerr's own
    discovery output, so renamed TEST_CASE strings do not invalidate selection.
    """

    if force_all:
        return "full", ""

    relevant = [path.replace("\\", "/") for path in paths if classify_path(path)]
    if not relevant:
        return "none", ""

    test_sources: list[str] = []
    for path in relevant:
        if not (path.startswith("test/") and path.endswith(".cpp")):
            return "full", ""
        source = ROOT / path
        # A deletion can change shared registrations or fixtures in ways the
        # remaining source labels cannot represent. Fail closed even when the
        # same diff also modifies surviving test sources.
        if not source.exists():
            return "full", ""
        # New/modified helper translation units without zeroerr declarations
        # can affect arbitrary tests, so they also select the full suite.
        if not TEST_CASE_RE.search(source.read_text(encoding="utf-8")):
            return "full", ""
        test_sources.append(source.name)

    alternatives = "|".join(
        re.escape(name).replace(r"\.", "[.]") for name in sorted(set(test_sources))
    )
    return "selected", rf"^source:({alternatives})$"


def changed_paths(
    base: str,
    head: str,
    diff_mode: str = "merge-base",
    cwd: Path | None = None,
) -> list[str]:
    """Return changed paths using PR merge-base or direct endpoint semantics."""

    comparison = f"{base}...{head}" if diff_mode == "merge-base" else f"{base}..{head}"
    result = subprocess.run(
        ["git", "diff", "--name-only", comparison],
        check=True,
        capture_output=True,
        text=True,
        cwd=cwd,
        # git writes UTF-8; the locale default (cp936 on a Chinese Windows)
        # would mangle or reject a non-ASCII path instead.
        encoding="utf-8",
        errors="replace",
    )
    return [line for line in result.stdout.splitlines() if line]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base")
    parser.add_argument("--head", default="HEAD")
    parser.add_argument(
        "--diff-mode",
        choices=("merge-base", "direct"),
        default="merge-base",
        help="merge-base for pull requests; direct for push before/after SHAs",
    )
    parser.add_argument("--all", action="store_true", dest="force_all")
    parser.add_argument("--github-output", type=Path)
    args = parser.parse_args()
    if not args.force_all and not args.base:
        parser.error("--base is required unless --all is used")

    paths = (
        []
        if args.force_all
        else changed_paths(args.base, args.head, diff_mode=args.diff_mode)
    )
    scopes = classify(paths, force_all=args.force_all)
    test_mode, test_label_regex = classify_tests(paths, force_all=args.force_all)
    lines = [f"{scope}={'true' if enabled else 'false'}" for scope, enabled in scopes.items()]
    lines.append(f"changed_count={len(paths)}")
    lines.append(f"test_mode={test_mode}")
    lines.append(f"test_label_regex={test_label_regex}")
    if args.github_output:
        with args.github_output.open("a", encoding="utf-8") as output:
            output.write("\n".join(lines) + "\n")
    else:
        print("\n".join(lines))
    return 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
