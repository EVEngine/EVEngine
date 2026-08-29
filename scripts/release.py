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
PROMOTE_HEAD_RE = re.compile(r"^promote/v[0-9]+\.[0-9]+\.[0-9]+$")
VERSION_BRANCH_RE = re.compile(r"^v([0-9]+)\.([0-9]+)\.([0-9]+)$")
PROMOTE_BRANCH_RE = re.compile(r"^promote/v([0-9]+)\.([0-9]+)\.([0-9]+)$")
REBASE_BRANCH_RE = re.compile(r"^rebase/v([0-9]+)\.([0-9]+)\.([0-9]+)$")
DOC_FILES = frozenset({"README.md", "Readme.md", "Readme.en.md", "Doxyfile"})
DEV_BRANCH = "dev"
MAIN_BRANCH = "main"
MAIN_GATE_CHECK = "main-gate"
# Non-doc files the release pipeline (`start`) rewrites on main. sync-docs must
# not reject them: the same changes reach `dev` through the release rebase PR.
RELEASE_PATHS = frozenset(
    {
        "CMakeLists.txt",
        "platform/android/apk/app/build.gradle.kts",
        "src/engine/devtools/McpServer.cpp",
        "examples/basic/root.nut",
        "platform/ios/game-shell/root.nut",
        "platform/android/game-shell/root.nut",
    }
)
_SET_RE = {
    "major": re.compile(r'(set\(EVENGINE_MAJOR_VERSION\s+")([^"]*)("\))'),
    "minor": re.compile(r'(set\(EVENGINE_MINOR_VERSION\s+")([^"]*)("\))'),
    "patch": re.compile(r'(set\(EVENGINE_PATCH_VERSION\s+")([^"]*)("\))'),
    "dev": re.compile(r'(set\(EVENGINE_DEV_VERSION\s+")([^"]*)("\))'),
}


@dataclass(frozen=True)
class VersionTouchpoint:
    """A file that carries the engine version outside CMakeLists.txt.

    `kind` is either "required" (synced by `start`, enforced by
    `check-versions` in CI) or "tracked" (standalone tools that version
    independently; only reported as warnings).
    `patterns` is a list of (compiled regex, value-kind) pairs. Each regex has
    three groups: prefix, current value, suffix. Value-kind is one of
    "major"/"minor"/"patch"/"triple"/"version_code".
    """

    path: str
    kind: str
    patterns: tuple[tuple[re.Pattern[str], str], ...]


_VER_CODE = re.compile(r'(versionCode = )(\d+)([^\n]*)')
_VER_NAME = re.compile(r'(versionName = ")([^"]*)(")')
_DOC_VER = {
    key: re.compile(rf'(set\(EVENGINE_{key.upper()}_VERSION ")([^"]*)("\))')
    for key in ("major", "minor", "patch")
}
_PRINT_VER = re.compile(r"(EVEngine v)([0-9][0-9.]*)([^\n]*)")
_MCP_VER = re.compile(r'(EVEngine MCP\\",\\"version\\":\\")([0-9.]+)(\\")')
_JSON_VER = re.compile(r'("version": ")([^"]*)(")')
_PY_VER = re.compile(r'(__version__ = ")([^"]*)(")')


VERSION_TOUCHPOINTS: tuple[VersionTouchpoint, ...] = (
    # Engine release artifacts: must carry the official MAJOR.MINOR.PATCH.
    VersionTouchpoint(
        "docs/CMakeLists.txt",
        "required",
        ((_DOC_VER["major"], "major"), (_DOC_VER["minor"], "minor"), (_DOC_VER["patch"], "patch")),
    ),
    VersionTouchpoint(
        "platform/android/apk/app/build.gradle.kts",
        "required",
        ((_VER_NAME, "triple"), (_VER_CODE, "version_code")),
    ),
    VersionTouchpoint(
        "src/engine/devtools/McpServer.cpp",
        "required",
        ((_MCP_VER, "triple"),),
    ),
    VersionTouchpoint("examples/basic/root.nut", "required", ((_PRINT_VER, "triple"),)),
    VersionTouchpoint("platform/ios/game-shell/root.nut", "required", ((_PRINT_VER, "triple"),)),
    VersionTouchpoint("platform/android/game-shell/root.nut", "required", ((_PRINT_VER, "triple"),)),
    # Standalone tools: version independently, reported but never rewritten.
    VersionTouchpoint("tools/eve-mcp/package.json", "tracked", ((_JSON_VER, "triple"),)),
    VersionTouchpoint("tools/vscode-eve-debug/package.json", "tracked", ((_JSON_VER, "triple"),)),
    VersionTouchpoint(
        "modelconverter/python/eve_blender_converter/__init__.py", "tracked", ((_PY_VER, "triple"),)
    ),
    VersionTouchpoint("tools/asset-pipeline-agent/asset_pipeline_agent/__init__.py", "tracked", ((_PY_VER, "triple"),)),
    VersionTouchpoint("tools/scene-qc-agent/scene_qc_agent/__init__.py", "tracked", ((_PY_VER, "triple"),)),
    VersionTouchpoint("tools/vision-prefilter/vision_prefilter/__init__.py", "tracked", ((_PY_VER, "triple"),)),
)


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


def _touchpoint_value(kind: str, version: Version) -> str:
    if kind == "major":
        return str(version.major)
    if kind == "minor":
        return str(version.minor)
    if kind == "patch":
        return str(version.patch)
    if kind == "version_code":
        return str(version.major * 10000 + version.minor * 100 + version.patch)
    if kind == "triple":
        return f"{version.major}.{version.minor}.{version.patch}"
    raise ValueError(f"unknown touchpoint value kind: {kind}")


def _check_touchpoint(root: Path, tp: VersionTouchpoint, version: Version) -> list[str]:
    """Return a list of problems for one touchpoint (empty means consistent)."""
    path = root / tp.path
    if not path.exists():
        return [f"{tp.path}: file missing"]
    text = path.read_text(encoding="utf-8")
    problems: list[str] = []
    for pattern, kind in tp.patterns:
        expected = _touchpoint_value(kind, version)
        matches = pattern.findall(text)
        if not matches:
            problems.append(f"{tp.path}: version pattern not found ({pattern.pattern})")
            continue
        for groups in matches:
            current = groups[1]
            if current != expected:
                problems.append(
                    f"{tp.path}: {current!r} != expected {expected!r} ({kind})"
                )
    return problems


def sync_version_files(root: Path, version: Version, *, dry_run: bool = False) -> list[str]:
    """Rewrite all required version touchpoints under `root` to `version`.

    Returns the repo-relative paths that actually changed. Missing required
    files are treated as a hard error: a release must not silently ship
    without a version-bearing artifact.
    """
    changed: list[str] = []
    for tp in VERSION_TOUCHPOINTS:
        if tp.kind != "required":
            continue
        path = root / tp.path
        if not path.exists():
            print(f"error: required version file missing: {tp.path}", file=sys.stderr)
            raise SystemExit(1)
        text = path.read_text(encoding="utf-8")
        new_text = text
        for pattern, kind in tp.patterns:
            value = _touchpoint_value(kind, version)
            new_text = pattern.sub(rf"\g<1>{value}\g<3>", new_text)
        if new_text != text:
            if dry_run:
                print(f"+ update {tp.path} -> {version.display()}")
            else:
                path.write_text(new_text, encoding="utf-8")
            changed.append(tp.path)
    return changed


def cmd_check_versions(runner: Runner, *, root: Path) -> None:
    """Validate that every version touchpoint matches CMakeLists.txt."""
    version = read_version((root / "CMakeLists.txt").read_text(encoding="utf-8"))
    errors: list[str] = []
    warnings: list[str] = []
    for tp in VERSION_TOUCHPOINTS:
        problems = _check_touchpoint(root, tp, version)
        if tp.kind == "required":
            errors += problems
        else:
            warnings += problems
    for warning in warnings:
        print(f"WARN: {warning}")
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        print(
            f"error: tree version {version.display()} is not consistent across required "
            "touchpoints (fix by hand or rerun `release.py start`)",
            file=sys.stderr,
        )
        raise SystemExit(1)
    required = sum(1 for tp in VERSION_TOUCHPOINTS if tp.kind == "required")
    print(f"OK: {required} required version touchpoints match {version.display()}")


def is_doc_path(path: str) -> bool:
    norm = path.replace("\\", "/").lstrip("./")
    if norm in DOC_FILES:
        return True
    return norm == "docs" or norm.startswith("docs/")


def is_release_path(path: str) -> bool:
    return path.replace("\\", "/").lstrip("./") in RELEASE_PATHS


def is_promote_head(head_ref: str) -> bool:
    ref = head_ref.replace("\\", "/")
    prefix = "refs/heads/"
    if ref.startswith(prefix):
        ref = ref[len(prefix) :]
    return bool(PROMOTE_HEAD_RE.fullmatch(ref))


def main_pr_allowed(head_ref: str, changed_files: list[str]) -> bool:
    if is_promote_head(head_ref):
        return True
    if not changed_files:
        return False
    return all(is_doc_path(p) for p in changed_files)


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
    def __init__(self, *, dry_run: bool = False, cwd: Path | None = None):
        self.dry_run = dry_run
        self.cwd = cwd

    def run(self, argv: list[str], *, check: bool = True) -> str:
        if self.dry_run:
            print("+", " ".join(argv))
            return ""
        proc = subprocess.run(argv, capture_output=True, text=True, cwd=self.cwd)
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


def _is_ancestor(runner: Runner, commit: str, ref: str) -> bool:
    try:
        runner.run(["git", "merge-base", "--is-ancestor", commit, ref])
        return True
    except CommandError:
        return False


def _existing_pr_url(runner: Runner, *, base: str, head: str) -> str:
    raw = runner.run(
        [
            "gh",
            "pr",
            "list",
            "--base",
            base,
            "--head",
            head,
            "--state",
            "open",
            "--json",
            "url",
        ]
    )
    if not raw.strip():
        return ""
    items = json.loads(raw)
    if not items:
        return ""
    return str(items[0].get("url") or "")


def _ensure_pr(
    runner: Runner,
    *,
    base: str,
    head: str,
    title: str,
    body: str,
) -> str:
    existing = _existing_pr_url(runner, base=base, head=head)
    if existing:
        print(f"PR already open: {existing}")
        return existing
    return runner.run(
        [
            "gh",
            "pr",
            "create",
            "--base",
            base,
            "--head",
            head,
            "--title",
            title,
            "--body",
            body,
        ]
    ).strip()


def post_check(
    runner: Runner,
    *,
    sha: str,
    conclusion: str,
    title: str,
    summary: str,
    name: str = MAIN_GATE_CHECK,
) -> None:
    if not sha:
        return
    runner.run(
        [
            "gh",
            "api",
            "--method",
            "POST",
            "repos/{owner}/{repo}/check-runs",
            "-f",
            f"name={name}",
            "-f",
            f"head_sha={sha}",
            "-f",
            "status=completed",
            "-f",
            f"conclusion={conclusion}",
            "-f",
            f"output[title]={title}",
            "-f",
            f"output[summary]={summary}",
        ]
    )


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
    # The release branch and the release tag share the same short name
    # (refs/heads/vX.Y.Z vs refs/tags/vX.Y.Z). Always use explicit refspecs:
    # a short refspec like `git push origin vX.Y.Z` is ambiguous and git
    # refuses it ("src refspec matches more than one").
    runner.run(["git", "checkout", "-B", tag, f"refs/tags/{tag}"])

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
    _store_cmake(runner, cmake_path, new_text, dry_run=dry_run)
    touched = ["CMakeLists.txt"]
    touched += sync_version_files(repo_root, incoming.official(), dry_run=dry_run)
    runner.run(["git", "add", *touched])
    try:
        runner.run(["git", "diff", "--cached", "--quiet"])
    except CommandError:
        runner.run(["git", "commit", "-m", f"release: {incoming.display()}"])
    runner.run(["git", "tag", "-f", tag])
    runner.run(["git", "push", "-u", "origin", f"refs/heads/{tag}"])
    runner.run(["git", "push", "--force", "origin", f"refs/tags/{tag}"])
    _write_github_output(tag, tag)


def _open_promote_pr(runner: Runner, *, tag: str, official: str) -> None:
    promote = f"promote/{tag}"
    if _is_ancestor(runner, official, f"origin/{MAIN_BRANCH}"):
        print(f"{MAIN_BRANCH} already contains {tag}")
        return
    runner.run(["git", "branch", "-f", promote, official])
    runner.run(["git", "push", "-u", "origin", promote, "--force-with-lease"])
    body = (
        f"Promote `{tag}` to `{MAIN_BRANCH}`.\n\n"
        f"This PR points at the official commit (no `-dev` suffix). "
        f"Merge it to update the default branch. Do not merge `{DEV_BRANCH}` into `{MAIN_BRANCH}`.\n"
    )
    _ensure_pr(
        runner,
        base=MAIN_BRANCH,
        head=promote,
        title=f"release: {tag}",
        body=body,
    )
    post_check(
        runner,
        sha=official,
        conclusion="success",
        title="release promote",
        summary=f"{promote} is allowed onto {MAIN_BRANCH}.",
    )


def _open_rebase_pr(runner: Runner, *, tag: str, conflict: bool) -> None:
    head = f"rebase/{tag}"
    if conflict:
        body = (
            f"Automatic rebase of `{DEV_BRANCH}` onto `{tag}` failed.\n\n"
            f"Resolve on this branch or locally:\n\n"
            f"```\n"
            f"git fetch origin\n"
            f"git checkout {DEV_BRANCH}\n"
            f"git rebase {tag}\n"
            f"# fix conflicts, then push this rebase branch (do not force-push {DEV_BRANCH} unless you intend to):\n"
            f"git push --force-with-lease origin {head}\n"
            f"```\n"
        )
    else:
        body = (
            f"Automatic rebase of `{DEV_BRANCH}` onto `{tag}` succeeded.\n\n"
            f"Merge this PR to update `{DEV_BRANCH}`. Do not merge `{DEV_BRANCH}` into `{MAIN_BRANCH}`.\n"
        )
    _ensure_pr(
        runner,
        base=DEV_BRANCH,
        head=head,
        title=f"rebase {DEV_BRANCH} onto {tag}",
        body=body,
    )


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
    runner.run(
        [
            "git",
            "fetch",
            "origin",
            MAIN_BRANCH,
            DEV_BRANCH,
            f"refs/heads/{tag}",
            f"refs/tags/{tag}",
        ]
    )
    official = runner.run(["git", "rev-parse", f"refs/tags/{tag}"]).strip()
    _open_promote_pr(runner, tag=tag, official=official)

    # Reset the local release branch to what CI pushed (refs/remotes/origin)
    # so the -dev write-back always lands on the branch, never on the tag.
    runner.run(["git", "checkout", "-B", tag, f"refs/remotes/origin/{tag}"])
    text = _load_cmake(runner, cmake_path)
    new_text = write_version(text, incoming.as_dev())
    _store_cmake(runner, cmake_path, new_text, dry_run=dry_run)
    runner.run(["git", "add", "CMakeLists.txt"])
    try:
        runner.run(["git", "diff", "--cached", "--quiet"])
    except CommandError:
        runner.run(["git", "commit", "-m", f"release: {incoming.as_dev().display()}"])
    runner.run(["git", "push", "origin", f"refs/heads/{tag}"])

    runner.run(["git", "checkout", "-B", DEV_BRANCH, f"origin/{DEV_BRANCH}"])
    rebase_head = f"rebase/{tag}"
    try:
        # Rebase dev onto the branch tip (which now carries the -dev write-back),
        # not onto the tag (which stays on the official commit).
        runner.run(["git", "rebase", f"refs/heads/{tag}"])
    except CommandError:
        runner.run(["git", "rebase", "--abort"])
        runner.run(["git", "checkout", "-B", rebase_head, f"refs/heads/{tag}"])
        runner.run(["git", "push", "-u", "origin", rebase_head, "--force-with-lease"])
        _open_rebase_pr(runner, tag=tag, conflict=True)
        return

    ahead_raw = runner.run(["git", "rev-list", "--count", f"origin/{DEV_BRANCH}..HEAD"]).strip()
    ahead = int(ahead_raw or "0")
    if ahead == 0:
        print(f"{DEV_BRANCH} already contains the rebased history")
        return
    runner.run(["git", "checkout", "-B", rebase_head, "HEAD"])
    runner.run(["git", "push", "-u", "origin", rebase_head, "--force-with-lease"])
    _open_rebase_pr(runner, tag=tag, conflict=False)


def cmd_check_main_pr(runner: Runner, *, head_ref: str | None = None) -> None:
    head = head_ref or os.environ.get("GITHUB_HEAD_REF") or ""
    runner.run(["git", "fetch", "origin", MAIN_BRANCH], check=False)
    names = runner.run(["git", "diff", "--name-only", f"origin/{MAIN_BRANCH}...HEAD"])
    files = [line.strip() for line in names.splitlines() if line.strip()]
    sha = runner.run(["git", "rev-parse", "HEAD"]).strip()
    allowed = main_pr_allowed(head, files)
    post_check(
        runner,
        sha=sha,
        conclusion="success" if allowed else "failure",
        title="main gate",
        summary=(
            f"{head} is allowed onto {MAIN_BRANCH}."
            if allowed
            else f"{head} is not a docs-only or promote/vX.X.X PR."
        ),
    )
    if allowed:
        return
    print(
        f"error: PRs to {MAIN_BRANCH} must be documentation-only "
        f"or come from a `promote/vMAJOR.MINOR.PATCH` release branch. Got {head!r}: "
        + ", ".join(files),
        file=sys.stderr,
    )
    raise SystemExit(1)


def cmd_cleanup_branches(runner: Runner, *, dry_run: bool = False) -> None:
    """Delete release branches once their PRs have been merged.

    - vX.Y.Z: deleted when its -dev write-back reached `dev` (rebase PR merged)
    - promote/vX.Y.Z: deleted when its official commit reached `main`
    - rebase/vX.Y.Z: deleted when its tip reached `dev`

    The tag refs/tags/vX.Y.Z is never touched, so re-runs of the release
    pipeline can still recreate the branch. `main` / `dev` are never deleted.
    """
    runner.run(["git", "fetch", "origin", "--prune"])
    refs = runner.run(
        ["git", "for-each-ref", "--format=%(refname:short) %(objectname)", "refs/remotes/origin"]
    )
    deleted: list[str] = []
    for line in refs.splitlines():
        if not line.strip():
            continue
        short, sha = line.split(" ", 1)
        if not short.startswith("origin/"):
            continue
        name = short[len("origin/"):]
        if VERSION_BRANCH_RE.fullmatch(name):
            target = DEV_BRANCH
        elif PROMOTE_BRANCH_RE.fullmatch(name):
            target = MAIN_BRANCH
        elif REBASE_BRANCH_RE.fullmatch(name):
            target = DEV_BRANCH
        else:
            continue
        if not _is_ancestor(runner, sha, f"origin/{target}"):
            print(f"keep {name}: not merged into {target}")
            continue
        if dry_run:
            print(f"+ delete {name} (merged into {target})")
            continue
        runner.run(["git", "push", "origin", "--delete", f"refs/heads/{name}"])
        deleted.append(name)
        print(f"deleted {name} (merged into {target})")
    if not deleted and not dry_run:
        print("no release branches to clean up")


def cmd_sync_docs(runner: Runner, *, dry_run: bool = False) -> None:
    """Sync documentation-only changes from `main` onto `dev`.

    - Only paths that changed on `main` since the merge-base with `dev` are
      considered, so `dev`-only work is never touched.
    - Non-doc changes on `main` are rejected unless they are release-version
      touchpoints (RELEASE_PATHS); those reach `dev` through the release
      pipeline's rebase PR, and treating them as errors would make every
      sync-docs run after a release fail spuriously.
    - The sync is PR-based (`sync-docs/from-main` -> `dev`), matching the
      "no direct push to dev" model. Doc content is copied from `main`, so
      merge / squash / rebase merges on `main` are all handled.
    """
    runner.run(["git", "fetch", "origin", MAIN_BRANCH, DEV_BRANCH])
    base = runner.run(
        ["git", "merge-base", f"origin/{MAIN_BRANCH}", f"origin/{DEV_BRANCH}"]
    ).strip()
    changed = runner.run(["git", "diff", "--name-only", base, f"origin/{MAIN_BRANCH}"])
    files = [line.strip() for line in changed.splitlines() if line.strip()]
    bad = [f for f in files if not is_doc_path(f) and not is_release_path(f)]
    if bad:
        print(
            f"error: {MAIN_BRANCH} is ahead of {DEV_BRANCH} with non-doc files: "
            + ", ".join(bad)
            + f". Land those changes on {DEV_BRANCH} instead.",
            file=sys.stderr,
        )
        raise SystemExit(1)
    doc_paths = [f for f in files if is_doc_path(f)]
    if not doc_paths:
        print("nothing to sync")
        return
    print(f"syncing {len(doc_paths)} documentation path(s) from {MAIN_BRANCH}:")
    for path in doc_paths:
        print(f"  {path}")
    if dry_run:
        return
    runner.run(["git", "checkout", "-B", "sync-docs/from-main", f"origin/{DEV_BRANCH}"])
    runner.run(["git", "checkout", f"origin/{MAIN_BRANCH}", "--", *doc_paths])
    try:
        runner.run(["git", "diff", "--cached", "--quiet"])
    except CommandError:
        runner.run(["git", "commit", "-m", f"docs: sync documentation from {MAIN_BRANCH}"])
    else:
        print("nothing to sync (docs already in sync)")
        return
    runner.run(["git", "push", "-u", "origin", "sync-docs/from-main", "--force-with-lease"])
    body = (
        f"Sync documentation changes from `{MAIN_BRANCH}` onto `{DEV_BRANCH}`.\n\n"
        f"Merge this PR to keep `{DEV_BRANCH}` docs in sync. "
        f"Do not merge `{DEV_BRANCH}` into `{MAIN_BRANCH}`.\n"
    )
    _ensure_pr(
        runner,
        base=DEV_BRANCH,
        head="sync-docs/from-main",
        title=f"docs: sync from {MAIN_BRANCH}",
        body=body,
    )


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
    sub.add_parser("check-main-pr")
    sub.add_parser("check-versions")
    sub.add_parser("cleanup-branches")
    args = parser.parse_args(argv)

    real = runner or RealRunner(dry_run=args.dry_run)
    root = Path(__file__).resolve().parent.parent
    cmake = root / "CMakeLists.txt"
    ci = os.environ.get("CI", "").lower() in {"1", "true", "yes"}

    if args.cmd == "start":
        require_gh(which=which)
        tag = args.tag or _latest_prerelease_tag(real)
        cmd_start(real, tag=tag, cmake_path=cmake, repo_root=root, ci=ci, dry_run=args.dry_run)
    elif args.cmd == "finish":
        require_gh(which=which)
        cmd_finish(real, tag=args.tag, cmake_path=cmake, repo_root=root, dry_run=args.dry_run)
    elif args.cmd == "sync-docs":
        require_gh(which=which)
        cmd_sync_docs(real, dry_run=args.dry_run)
    elif args.cmd == "check-main-pr":
        require_gh(which=which)
        cmd_check_main_pr(real)
    elif args.cmd == "check-versions":
        cmd_check_versions(real, root=root)
    elif args.cmd == "cleanup-branches":
        cmd_cleanup_branches(real, dry_run=args.dry_run)


if __name__ == "__main__":
    main()



