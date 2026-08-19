from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

TAG_RE = re.compile(r"^v([0-9]+)\.([0-9]+)\.([0-9]+)$")
DOC_FILES = frozenset({"README.md", "Readme.md", "Readme.en.md", "Doxyfile"})
DEV_BRANCH = "dev"
MAIN_BRANCH = "main"
_SET_RE = {
    "major": re.compile(r'(set\(EVENGINE_MAJOR_VERSION\s+")([^"]*)("\))'),
    "minor": re.compile(r'(set\(EVENGINE_MINOR_VERSION\s+")([^"]*)("\))'),
    "patch": re.compile(r'(set\(EVENGINE_PATCH_VERSION\s+")([^"]*)("\))'),
    "dev": re.compile(r'(set\(EVENGINE_DEV_VERSION\s+")([^"]*)("\))'),
}


@dataclass(frozen=True)
class Version:
    major: int
    minor: int
    patch: int
    dev: str = ""

    def triple(self) -> tuple[int, int, int]:
        return (self.major, self.minor, self.patch)

    def display(self) -> str:
        return f"{self.major}.{self.minor}.{self.patch}{self.dev}"

    def official(self) -> Version:
        return Version(self.major, self.minor, self.patch, "")

    def as_dev(self) -> Version:
        return Version(self.major, self.minor, self.patch, "-dev")


def parse_tag(tag: str) -> Version:
    m = TAG_RE.fullmatch(tag)
    if not m:
        raise ValueError(f"tag must match vMAJOR.MINOR.PATCH, got {tag!r}")
    return Version(int(m.group(1)), int(m.group(2)), int(m.group(3)), "")


def is_downgrade(current: Version, incoming: Version) -> bool:
    return incoming.triple() < current.triple()


def read_version(cmake_text: str) -> Version:
    def grab(key: str) -> str:
        m = _SET_RE[key].search(cmake_text)
        if not m:
            raise ValueError(f"CMakeLists.txt missing EVENGINE_{key.upper()}_VERSION")
        return m.group(2)

    return Version(int(grab("major")), int(grab("minor")), int(grab("patch")), grab("dev"))


def write_version(cmake_text: str, version: Version) -> str:
    values = {
        "major": str(version.major),
        "minor": str(version.minor),
        "patch": str(version.patch),
        "dev": version.dev,
    }

    def repl(key: str, text: str) -> str:
        new, n = _SET_RE[key].subn(rf"\g<1>{values[key]}\g<3>", text, count=1)
        if n != 1:
            raise ValueError(f"CMakeLists.txt missing EVENGINE_{key.upper()}_VERSION")
        return new

    for key in ("major", "minor", "patch", "dev"):
        cmake_text = repl(key, cmake_text)
    return cmake_text


def is_doc_path(path: str) -> bool:
    norm = path.replace("\\", "/").lstrip("./")
    if norm in DOC_FILES:
        return True
    return norm == "docs" or norm.startswith("docs/")


class CommandError(RuntimeError):
    def __init__(self, argv: list[str], returncode: int, stderr: str):
        super().__init__(f"command failed ({returncode}): {' '.join(argv)}\n{stderr}")
        self.argv = argv
        self.returncode = returncode
        self.stderr = stderr


class Runner:
    def run(self, argv: list[str], *, check: bool = True) -> str:
        raise NotImplementedError


class RealRunner(Runner):
    def __init__(self, *, dry_run: bool = False):
        self.dry_run = dry_run

    def run(self, argv: list[str], *, check: bool = True) -> str:
        if self.dry_run:
            print("+", " ".join(argv))
            return ""
        proc = subprocess.run(argv, capture_output=True, text=True)
        if check and proc.returncode != 0:
            raise CommandError(argv, proc.returncode, proc.stderr)
        return proc.stdout


class FakeRunner(Runner):
    """Match commands by argv prefix. cmake_text simulates CMakeLists.txt I/O."""

    def __init__(self):
        self.calls: list[list[str]] = []
        self._rules: list[tuple[list[str], str, int]] = []
        self.cmake_text: str = ""

    def when(self, prefix: list[str], *, stdout: str = "", rc: int = 0) -> None:
        self._rules.append((prefix, stdout, rc))

    def run(self, argv: list[str], *, check: bool = True) -> str:
        self.calls.append(list(argv))
        for prefix, stdout, rc in reversed(self._rules):
            if argv[: len(prefix)] == prefix:
                if rc != 0 and check:
                    raise CommandError(argv, rc, stdout)
                return stdout
        raise AssertionError(f"unexpected command: {argv}")


def require_gh(which: Callable[[str], str | None] | None = None) -> None:
    lookup = which or shutil.which
    if lookup("gh"):
        return
    print(
        "error: GitHub CLI (gh) not found on PATH. Install from https://cli.github.com/",
        file=sys.stderr,
    )
    raise SystemExit(2)


def _write_github_output(tag: str, branch: str) -> None:
    path = os.environ.get("GITHUB_OUTPUT")
    if not path:
        return
    with open(path, "a", encoding="utf-8") as fh:
        fh.write(f"tag={tag}\nbranch={branch}\n")


def _load_cmake(runner: Runner, cmake_path: Path) -> str:
    if isinstance(runner, FakeRunner):
        return runner.cmake_text
    return cmake_path.read_text(encoding="utf-8")


def _store_cmake(runner: Runner, cmake_path: Path, text: str, *, dry_run: bool) -> None:
    if isinstance(runner, FakeRunner):
        runner.cmake_text = text
    elif not dry_run:
        cmake_path.write_text(text, encoding="utf-8")


def cmd_start(
    runner: Runner,
    *,
    tag: str,
    cmake_path: Path,
    repo_root: Path,
    ci: bool,
    dry_run: bool = False,
) -> None:
    incoming = parse_tag(tag)
    view = runner.run(["gh", "release", "view", tag, "--json", "tagName,isPrerelease"])
    meta = json.loads(view) if view.strip() else {"tagName": tag, "isPrerelease": True}
    is_pre = bool(meta.get("isPrerelease"))

    dirty = runner.run(["git", "status", "--porcelain"], check=False)
    if not ci and dirty.strip():
        print("error: working tree is dirty; commit or stash first", file=sys.stderr)
        raise SystemExit(1)

    runner.run(["git", "fetch", "origin", "tag", tag, "--force"])
    runner.run(["git", "checkout", "-B", tag, tag])

    text = _load_cmake(runner, cmake_path)
    current = read_version(text)
    if is_downgrade(current, incoming):
        print(
            f"error: refusing to downgrade {current.display()} to {incoming.display()}",
            file=sys.stderr,
        )
        raise SystemExit(1)

    already_official = current.triple() == incoming.triple() and current.dev == ""
    if not is_pre and not already_official:
        print(
            "error: release is already formal but the tree is not the official version; "
            "will not rewrite a published tag",
            file=sys.stderr,
        )
        raise SystemExit(1)

    new_text = write_version(text, incoming.official())
    changed = new_text != text
    _store_cmake(runner, cmake_path, new_text, dry_run=dry_run)
    runner.run(["git", "add", "CMakeLists.txt"])
    if changed:
        try:
            runner.run(["git", "diff", "--cached", "--quiet"])
        except CommandError:
            runner.run(["git", "commit", "-m", f"release: {incoming.display()}"])
    runner.run(["git", "tag", "-f", tag])
    runner.run(["git", "push", "-u", "origin", tag])
    runner.run(["git", "push", "--force", "origin", f"refs/tags/{tag}"])
    _write_github_output(tag, tag)


def cmd_finish(
    runner: Runner,
    *,
    tag: str,
    cmake_path: Path,
    repo_root: Path,
    dry_run: bool = False,
) -> None:
    incoming = parse_tag(tag)
    runner.run(["gh", "release", "edit", tag, "--prerelease=false"])
    runner.run(["git", "fetch", "origin", MAIN_BRANCH, DEV_BRANCH, tag])
    official = runner.run(["git", "rev-parse", tag]).strip()
    runner.run(["git", "checkout", MAIN_BRANCH])
    try:
        runner.run(["git", "merge", "--ff-only", official])
    except CommandError:
        runner.run(
            [
                "git",
                "merge",
                "--no-ff",
                official,
                "-m",
                f"release: merge {tag} into {MAIN_BRANCH}",
            ]
        )
    runner.run(["git", "push", "origin", MAIN_BRANCH])

    runner.run(["git", "checkout", tag])
    text = _load_cmake(runner, cmake_path)
    new_text = write_version(text, incoming.as_dev())
    _store_cmake(runner, cmake_path, new_text, dry_run=dry_run)
    runner.run(["git", "add", "CMakeLists.txt"])
    try:
        runner.run(["git", "diff", "--cached", "--quiet"])
    except CommandError:
        runner.run(["git", "commit", "-m", f"release: {incoming.as_dev().display()}"])
    runner.run(["git", "push", "origin", tag])

    runner.run(["git", "checkout", DEV_BRANCH])
    try:
        runner.run(["git", "rebase", tag])
    except CommandError:
        runner.run(["git", "rebase", "--abort"])
        head = f"rebase/{tag}"
        runner.run(["git", "checkout", "-B", head, tag])
        runner.run(["git", "push", "-u", "origin", head])
        body = (
            f"Automatic rebase of `{DEV_BRANCH}` onto `{tag}` failed.\n\n"
            f"Resolve locally:\n\n"
            f"```\n"
            f"git fetch origin\n"
            f"git checkout {DEV_BRANCH}\n"
            f"git rebase {tag}\n"
            f"# fix conflicts, then:\n"
            f"git push --force-with-lease origin {DEV_BRANCH}\n"
            f"```\n"
        )
        runner.run(
            [
                "gh",
                "pr",
                "create",
                "--base",
                DEV_BRANCH,
                "--head",
                head,
                "--title",
                f"rebase {DEV_BRANCH} onto {tag}",
                "--body",
                body,
            ]
        )
        return
    runner.run(["git", "push", "--force-with-lease", "origin", DEV_BRANCH])


def cmd_sync_docs(runner: Runner, *, dry_run: bool = False) -> None:
    runner.run(["git", "fetch", "origin", MAIN_BRANCH, DEV_BRANCH])
    log = runner.run(
        ["git", "log", "--reverse", "--pretty=%H", f"origin/{DEV_BRANCH}..origin/{MAIN_BRANCH}"]
    )
    shas = [line.strip() for line in log.splitlines() if line.strip()]
    if not shas:
        print("nothing to sync")
        return
    for sha in shas:
        names = runner.run(["git", "diff-tree", "--no-commit-id", "--name-only", "-r", sha])
        files = [line.strip() for line in names.splitlines() if line.strip()]
        bad = [f for f in files if not is_doc_path(f)]
        if bad:
            print(
                f"error: {MAIN_BRANCH} is ahead of {DEV_BRANCH} with non-doc files: "
                + ", ".join(bad)
                + f". Land those changes on {DEV_BRANCH} instead.",
                file=sys.stderr,
            )
            raise SystemExit(1)
    runner.run(["git", "checkout", DEV_BRANCH])
    try:
        runner.run(["git", "cherry-pick", *shas])
    except CommandError:
        runner.run(["git", "cherry-pick", "--abort"], check=False)
        runner.run(["git", "checkout", "-B", "sync-docs/from-main", f"origin/{DEV_BRANCH}"])
        runner.run(["git", "push", "-u", "origin", "sync-docs/from-main"])
        sha_list = " ".join(shas)
        body = (
            f"Could not cherry-pick documentation commits from `{MAIN_BRANCH}` onto `{DEV_BRANCH}`.\n\n"
            f"SHAs: {sha_list}\n\n"
            "```\n"
            f"git checkout {DEV_BRANCH}\n"
            f"git cherry-pick {sha_list}\n"
            "```\n"
        )
        runner.run(
            [
                "gh",
                "pr",
                "create",
                "--base",
                DEV_BRANCH,
                "--head",
                "sync-docs/from-main",
                "--title",
                f"sync docs from {MAIN_BRANCH}",
                "--body",
                body,
            ]
        )
        return
    runner.run(["git", "push", "origin", DEV_BRANCH])


def _latest_prerelease_tag(runner: Runner) -> str:
    raw = runner.run(["gh", "release", "list", "--json", "tagName,isPrerelease", "--limit", "20"])
    items = json.loads(raw) if raw.strip() else []
    for item in items:
        if item.get("isPrerelease"):
            return str(item["tagName"])
    print("error: no open pre-release found; pass --tag", file=sys.stderr)
    raise SystemExit(1)


def main(
    argv: list[str] | None = None,
    *,
    which: Callable[[str], str | None] | None = None,
    runner: Runner | None = None,
) -> None:
    import argparse

    parser = argparse.ArgumentParser(description="EVEngine release / branch helper")
    parser.add_argument("--dry-run", action="store_true")
    sub = parser.add_subparsers(required=True)
    sub.dest = "cmd"
    p_start = sub.add_parser("start")
    p_start.add_argument("--tag")
    p_finish = sub.add_parser("finish")
    p_finish.add_argument("--tag", required=True)
    sub.add_parser("sync-docs")
    args = parser.parse_args(argv)

    require_gh(which=which)
    real = runner or RealRunner(dry_run=args.dry_run)
    root = Path(__file__).resolve().parent.parent
    cmake = root / "CMakeLists.txt"
    ci = os.environ.get("CI", "").lower() in {"1", "true", "yes"}

    if args.cmd == "start":
        tag = args.tag or _latest_prerelease_tag(real)
        cmd_start(real, tag=tag, cmake_path=cmake, repo_root=root, ci=ci, dry_run=args.dry_run)
    elif args.cmd == "finish":
        cmd_finish(real, tag=args.tag, cmake_path=cmake, repo_root=root, dry_run=args.dry_run)
    elif args.cmd == "sync-docs":
        cmd_sync_docs(real, dry_run=args.dry_run)


if __name__ == "__main__":
    main()



