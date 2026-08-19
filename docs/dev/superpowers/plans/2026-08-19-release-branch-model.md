# Release Branch Model Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 落地 `main`（正式版）/ `dev`（日常）/ `vX.X.X`（发布隔离）分支模型，用 `scripts/release.py` 做全部 git/gh 变更，Pre-release 触发严测 + SDK 后升正式版。

**Architecture:** `release.py` 是唯一会改版本号、分支、tag、Release、PR 的入口（`gh` 缺失即失败）。现有 `ci.yml` 改为可复用并盯 `dev`；`sdk-release.yml` 改为 `workflow_call` 且不再升正式版；新建 `release.yml` 串 `start → 严测 → SDK → finish`，新建 `sync-docs.yml` 把 `main` 上的文档热修同步回 `dev`。

**Tech Stack:** Python 3 标准库（`unittest`，不引入 pytest）、GitHub Actions、`gh` CLI、根目录 `CMakeLists.txt` 四行版本变量。

**Spec:** [docs/dev/superpowers/specs/2026-08-19-release-branch-model-design.md](../specs/2026-08-19-release-branch-model-design.md)

## Global Constraints

- 版本只改根 `CMakeLists.txt` 的 `EVENGINE_MAJOR_VERSION` / `MINOR` / `PATCH` / `DEV_VERSION`；不手改 `EVENGINE_VERSION`。
- tag 必须匹配 `^v([0-9]+)\.([0-9]+)\.([0-9]+)$`；Pre-release tag 是本次 MAJOR.MINOR.PATCH 的唯一来源。
- 允许 tag 比树上数字新；禁止降级（三元组字典序更小）。
- `finish` 回写 **同一组数字** 的 `-dev`（`0.1.0` → `0.1.0-dev`），不自动 +1。
- 提交信息固定：`release: X.X.X` 与 `release: X.X.X-dev`。
- `gh` 不在 PATH 则非零退出；不引入 GitHub App。
- 不改 `make sdk/*`、`test-sdk.sh`、`zip-sdk.py` 的打包内容。
- 文档白名单：`README.md`、`Readme.md`、`Readme.en.md`、`docs/**`、`Doxyfile`。
- 测试不访问真 GitHub：注入 `Runner`。跑测：`python -m unittest discover -s scripts/tests -p "test_*.py"`。
- 未明确要求时不要 `git commit`（本仓库提交规则优先于本 skill 的 frequent-commits）。
- 实现时不要创建或推送真实的 `dev` / `vX.X.X`（一次性仓库设置只写进文档，由维护者执行）。

## File Structure

| File | Role |
|------|------|
| `scripts/release.py` | CLI + 版本纯函数 + `start` / `finish` / `sync-docs` |
| `scripts/tests/__init__.py` | 空包，便于 `unittest discover` |
| `scripts/tests/test_release.py` | 不访问网络的单测 |
| `.github/workflows/ci.yml` | 日常盯 `dev`；`workflow_call` + `build_type` + Release 作业 |
| `.github/workflows/sdk-release.yml` | 改为 `workflow_call`；去掉升正式版 |
| `.github/workflows/release.yml` | 新建：编排 start / 严测 / SDK / finish |
| `.github/workflows/sync-docs.yml` | 新建：`main` 文档 push 后 `sync-docs` |
| `docs/dev/发布流程.md` | 维护者操作说明 |
| `docs/dev/README.md`、`docs/usr/README.md`、`Readme.md`、`Readme.en.md` | 克隆 `main` / 开发走 `dev` |
| spec 状态行 | 改为「实现中」→ 全部任务完成后改「已实施」 |

---

### Task 1: 版本 / tag / 文档白名单纯函数

**Files:**
- Create: `scripts/release.py`（仅数据与纯函数，尚无 CLI）
- Create: `scripts/tests/__init__.py`
- Create: `scripts/tests/test_release.py`

**Interfaces:**
- Consumes: 无
- Produces:
  - `class Version`：`major: int`, `minor: int`, `patch: int`, `dev: str`
  - `Version.triple() -> tuple[int, int, int]`
  - `Version.display() -> str`（如 `0.1.0` / `0.1.0-dev`）
  - `Version.official() -> Version`（`dev=""`）
  - `Version.as_dev() -> Version`（`dev="-dev"`）
  - `parse_tag(tag: str) -> Version`（非法则 `ValueError`）
  - `is_downgrade(current: Version, incoming: Version) -> bool`
  - `read_version(cmake_text: str) -> Version`
  - `write_version(cmake_text: str, version: Version) -> str`
  - `is_doc_path(path: str) -> bool`
  - `DOC_FILES`：`frozenset` of `README.md`, `Readme.md`, `Readme.en.md`, `Doxyfile`

- [ ] **Step 1: 写失败单测**

`scripts/tests/__init__.py` 为空文件。

`scripts/tests/test_release.py`：

```python
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import release


CMAKE = """cmake_minimum_required(VERSION 3.21)
project(EVEngine)

set(EVENGINE_MAJOR_VERSION "0")
set(EVENGINE_MINOR_VERSION "1")
set(EVENGINE_PATCH_VERSION "0")
set(EVENGINE_DEV_VERSION "-dev")

set(EVENGINE_VERSION "${EVENGINE_MAJOR_VERSION}.${EVENGINE_MINOR_VERSION}.${EVENGINE_PATCH_VERSION}${EVENGINE_DEV_VERSION}")
"""


class ParseTagTest(unittest.TestCase):
    def test_ok(self):
        v = release.parse_tag("v0.1.0")
        self.assertEqual(v.triple(), (0, 1, 0))
        self.assertEqual(v.dev, "")
        self.assertEqual(v.display(), "0.1.0")

    def test_rejects_v_prefix_missing(self):
        with self.assertRaises(ValueError):
            release.parse_tag("0.1.0")

    def test_rejects_prerelease_suffix(self):
        with self.assertRaises(ValueError):
            release.parse_tag("v0.1.0-rc1")


class DowngradeTest(unittest.TestCase):
    def test_same_is_not_downgrade(self):
        a = release.parse_tag("v0.1.0")
        self.assertFalse(release.is_downgrade(a, a))

    def test_newer_patch_ok(self):
        cur = release.parse_tag("v0.1.0")
        incoming = release.parse_tag("v0.1.1")
        self.assertFalse(release.is_downgrade(cur, incoming))

    def test_older_patch_is_downgrade(self):
        cur = release.parse_tag("v0.1.1")
        incoming = release.parse_tag("v0.1.0")
        self.assertTrue(release.is_downgrade(cur, incoming))


class CMakeVersionTest(unittest.TestCase):
    def test_read_dev(self):
        v = release.read_version(CMAKE)
        self.assertEqual(v.display(), "0.1.0-dev")

    def test_write_official_roundtrip(self):
        official = release.parse_tag("v0.1.0")
        text = release.write_version(CMAKE, official)
        self.assertEqual(release.read_version(text).display(), "0.1.0")
        self.assertIn('set(EVENGINE_DEV_VERSION "")', text)
        self.assertIn('set(EVENGINE_VERSION "${EVENGINE_MAJOR_VERSION}', text)

    def test_write_bump_then_dev(self):
        v = release.parse_tag("v0.2.0")
        text = release.write_version(CMAKE, v)
        text = release.write_version(text, release.read_version(text).as_dev())
        self.assertEqual(release.read_version(text).display(), "0.2.0-dev")


class DocPathTest(unittest.TestCase):
    def test_whitelist(self):
        for p in (
            "README.md",
            "Readme.md",
            "Readme.en.md",
            "Doxyfile",
            "docs/usr/README.md",
            "docs/dev/发布流程.md",
        ):
            self.assertTrue(release.is_doc_path(p), p)

    def test_rejects_engine(self):
        self.assertFalse(release.is_doc_path("CMakeLists.txt"))
        self.assertFalse(release.is_doc_path("src/engine/main.cpp"))
        self.assertFalse(release.is_doc_path("docs-extra/foo.md"))


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: 跑测，确认失败**

```bash
python -m unittest discover -s scripts/tests -p "test_*.py" -v
```

Expected: `ModuleNotFoundError` 或 `ImportError: No module named 'release'`。

- [ ] **Step 3: 写最小实现**

`scripts/release.py`：

```python
from __future__ import annotations

import re
from dataclasses import dataclass

TAG_RE = re.compile(r"^v([0-9]+)\.([0-9]+)\.([0-9]+)$")
DOC_FILES = frozenset({"README.md", "Readme.md", "Readme.en.md", "Doxyfile"})
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
```

- [ ] **Step 4: 再跑测**

```bash
python -m unittest discover -s scripts/tests -p "test_*.py" -v
```

Expected: 全部 PASS。

---

### Task 2: `Runner`、`require_gh`、`cmd_start`

**Files:**
- Modify: `scripts/release.py`
- Modify: `scripts/tests/test_release.py`

**Interfaces:**
- Consumes: Task 1 的 `Version` / `parse_tag` / `read_version` / `write_version` / `is_downgrade`
- Produces:
  - `class CommandError(RuntimeError)`：`args: list[str]`, `returncode: int`, `stderr: str`
  - `class Runner`：`run(self, argv: list[str], *, check: bool = True) -> str`（返回 stdout）
  - `class FakeRunner`：测试用，记录 `calls`，按 argv 前缀匹配预设结果
  - `require_gh(which: callable | None = None) -> None`（缺失则 `SystemExit`）
  - `cmd_start(runner: Runner, *, tag: str, cmake_path: Path, repo_root: Path, ci: bool, dry_run: bool = False) -> None`

- [ ] **Step 1: 追加失败单测**（先不要实现 `cmd_start`）

在 `test_release.py` 末尾、`if __name__` 之前追加：

```python
class RequireGhTest(unittest.TestCase):
    def test_missing_exits(self):
        with self.assertRaises(SystemExit) as ctx:
            release.require_gh(which=lambda _: None)
        self.assertNotEqual(ctx.exception.code, 0)


class StartTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(self.id().replace(".", "_"))
        # tests use FakeRunner; cmake lives in a temp-like in-memory path via fake read
        self.cmake = CMAKE
        self.runner = release.FakeRunner()
        self.runner.cmake_text = CMAKE

    def _wire_happy(self, *, is_prerelease="true", cmake=CMAKE):
        r = self.runner
        r.cmake_text = cmake
        r.when(["gh", "release", "view", "v0.1.0"], stdout='{"tagName":"v0.1.0","isPrerelease":%s}\n' % is_prerelease)
        r.when(["git", "status", "--porcelain"], stdout="")
        r.when(["git", "fetch", "origin", "tag", "v0.1.0", "--force"], stdout="")
        r.when(["git", "rev-parse", "v0.1.0"], stdout="aaa111\n")
        r.when(["git", "checkout", "-B", "v0.1.0", "v0.1.0"], stdout="")
        r.when(["git", "add", "CMakeLists.txt"], stdout="")
        r.when(["git", "diff", "--cached", "--quiet"], rc=1)  # has staged changes
        r.when(["git", "commit", "-m", "release: 0.1.0"], stdout="")
        r.when(["git", "tag", "-f", "v0.1.0"], stdout="")
        r.when(["git", "push", "-u", "origin", "v0.1.0"], stdout="")
        r.when(["git", "push", "--force", "origin", "refs/tags/v0.1.0"], stdout="")
        r.when(["git", "rev-parse", "HEAD"], stdout="bbb222\n")
        return r

    def test_start_writes_official_and_moves_tag(self):
        r = self._wire_happy()
        root = ROOT
        release.cmd_start(r, tag="v0.1.0", cmake_path=root / "CMakeLists.txt", repo_root=root, ci=True)
        self.assertEqual(release.read_version(r.cmake_text).display(), "0.1.0")
        self.assertIn(["git", "commit", "-m", "release: 0.1.0"], r.calls)
        self.assertIn(["git", "tag", "-f", "v0.1.0"], r.calls)
        self.assertIn(["git", "push", "--force", "origin", "refs/tags/v0.1.0"], r.calls)

    def test_start_idempotent_when_already_official(self):
        official = release.write_version(CMAKE, release.parse_tag("v0.1.0"))
        r = self._wire_happy(cmake=official)
        r.when(["git", "diff", "--cached", "--quiet"], rc=0)  # override: no changes
        release.cmd_start(r, tag="v0.1.0", cmake_path=ROOT / "CMakeLists.txt", repo_root=ROOT, ci=True)
        commit_calls = [c for c in r.calls if c[:2] == ["git", "commit"]]
        self.assertEqual(commit_calls, [])

    def test_start_rejects_downgrade(self):
        newer = release.write_version(CMAKE, release.parse_tag("v0.2.0").as_dev())
        r = self._wire_happy(cmake=newer)
        with self.assertRaises(SystemExit):
            release.cmd_start(r, tag="v0.1.0", cmake_path=ROOT / "CMakeLists.txt", repo_root=ROOT, ci=True)
        self.assertFalse(any(c[:2] == ["git", "commit"] for c in r.calls))

    def test_start_formal_mismatch_fails(self):
        r = self._wire_happy(is_prerelease="false")
        with self.assertRaises(SystemExit):
            release.cmd_start(r, tag="v0.1.0", cmake_path=ROOT / "CMakeLists.txt", repo_root=ROOT, ci=True)
```

`FakeRunner` 尚未存在，这些测试应在 import 或调用时报错。

- [ ] **Step 2: 跑测，确认新用例失败**

```bash
python -m unittest scripts.tests.test_release.StartTest.test_start_writes_official_and_moves_tag -v
```

在仓库根目录若 `scripts` 不是包，改用：

```bash
python -m unittest discover -s scripts/tests -p "test_*.py" -v
```

Expected: `AttributeError: module 'release' has no attribute 'FakeRunner'` 或 `cmd_start`。

- [ ] **Step 3: 实现 Runner 与 cmd_start**

在 `scripts/release.py` 追加（保留 Task 1 的内容）：

```python
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Callable


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
    """Match commands by argv prefix. Optional cmake_text simulates CMakeLists.txt I/O."""

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

    if not ci:
        dirty = runner.run(["git", "status", "--porcelain"])
        if dirty.strip():
            print("error: working tree is dirty; commit or stash first", file=sys.stderr)
            raise SystemExit(1)
    else:
        runner.run(["git", "status", "--porcelain"], check=False)

    runner.run(["git", "fetch", "origin", "tag", tag, "--force"])
    runner.run(["git", "checkout", "-B", tag, tag])

    if isinstance(runner, FakeRunner):
        text = runner.cmake_text
    else:
        text = cmake_path.read_text(encoding="utf-8")
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
    if isinstance(runner, FakeRunner):
        runner.cmake_text = new_text
    elif not dry_run:
        cmake_path.write_text(new_text, encoding="utf-8")

    runner.run(["git", "add", str(cmake_path.relative_to(repo_root) if cmake_path.is_absolute() else cmake_path)])
    # FakeRunner tests pass "CMakeLists.txt"
    if isinstance(runner, FakeRunner):
        runner.calls[-1] = ["git", "add", "CMakeLists.txt"]

    if changed:
        try:
            runner.run(["git", "diff", "--cached", "--quiet"])
            has_staged = False
        except CommandError:
            has_staged = True
        if has_staged:
            runner.run(["git", "commit", "-m", f"release: {incoming.display()}"])
    runner.run(["git", "tag", "-f", tag])
    runner.run(["git", "push", "-u", "origin", tag])
    runner.run(["git", "push", "--force", "origin", f"refs/tags/{tag}"])
    _write_github_output(tag, tag)
```

`cmd_start` 里对 `FakeRunner` 的 `git add` 改写有点脆。改成 **始终** `git add CMakeLists.txt`（相对仓库根，版本文件固定在根目录）：

把 `git add` 那几行换成：

```python
    runner.run(["git", "add", "CMakeLists.txt"])
```

并在函数开头 `os.chdir` 不需要——调用方保证 cwd 为仓库根。

`test_start_formal_mismatch_fails`：`isPrerelease=false` 且 cmake 仍是 `0.1.0-dev` → 失败。`_wire_happy` 默认 cmake 是 `-dev`，符合。

`test_start_idempotent_when_already_official`：正式版 + 已是 formal 也应成功（幂等）。给 `_wire_happy(is_prerelease="false", cmake=official)` 一条额外用例更好。在 `test_start_idempotent` 里保持 `is_prerelease=true` 即可测「不再 commit」。再加：

```python
    def test_start_formal_and_already_official_ok(self):
        official = release.write_version(CMAKE, release.parse_tag("v0.1.0"))
        r = self._wire_happy(is_prerelease="false", cmake=official)
        r.when(["git", "diff", "--cached", "--quiet"], rc=0)
        release.cmd_start(r, tag="v0.1.0", cmake_path=ROOT / "CMakeLists.txt", repo_root=ROOT, ci=True)
```

- [ ] **Step 4: 跑测**

```bash
python -m unittest discover -s scripts/tests -p "test_*.py" -v
```

Expected: 全部 PASS。若 `git add` 路径断言失败，以测试里的 `["git", "add", "CMakeLists.txt"]` 为准改实现。

---

### Task 3: `cmd_finish`

**Files:**
- Modify: `scripts/release.py`
- Modify: `scripts/tests/test_release.py`

**Interfaces:**
- Consumes: Task 2 的 `Runner` / `CommandError` / 版本函数
- Produces: `cmd_finish(runner: Runner, *, tag: str, cmake_path: Path, repo_root: Path, dry_run: bool = False) -> None`

行为（按顺序，缺一不可）：

1. `gh release edit <tag> --prerelease=false`（已是正式版时 `gh` 仍成功，视为幂等）。
2. `git fetch origin main dev <tag>`。
3. `official = git rev-parse <tag>`（此时 tag 已在正式版 commit）。
4. `git checkout main`；先 `git merge --ff-only <official>`，若 `CommandError` 则 `git merge --no-ff <official> -m "release: merge <tag> into main"`。
5. `git push origin main`。
6. `git checkout <tag>`；把 CMake 写成 **同一组数字** 的 `-dev`；若有变更则 `git commit -m "release: X.X.X-dev"`。
7. `git push origin <tag>`。
8. `git checkout dev`；`git rebase <tag>`。
   - 成功：`git push --force-with-lease origin dev`。
   - `CommandError`：`git rebase --abort`；`git checkout -B rebase/<tag> <tag>`；`git push -u origin rebase/<tag>`；`gh pr create --base dev --head rebase/<tag> --title ... --body ...`。**不得**出现 `git push --force-with-lease origin dev`。

- [ ] **Step 1: 写失败单测**

```python
class FinishTest(unittest.TestCase):
    def _base(self):
        r = release.FakeRunner()
        official = release.write_version(CMAKE, release.parse_tag("v0.1.0"))
        r.cmake_text = official
        r.when(["gh", "release", "edit", "v0.1.0", "--prerelease=false"], stdout="")
        r.when(["git", "fetch", "origin", "main", "dev", "v0.1.0"], stdout="")
        r.when(["git", "rev-parse", "v0.1.0"], stdout="off123\n")
        r.when(["git", "checkout", "main"], stdout="")
        r.when(["git", "merge", "--ff-only", "off123"], stdout="")
        r.when(["git", "push", "origin", "main"], stdout="")
        r.when(["git", "checkout", "v0.1.0"], stdout="")
        r.when(["git", "add", "CMakeLists.txt"], stdout="")
        r.when(["git", "diff", "--cached", "--quiet"], rc=1)
        r.when(["git", "commit", "-m", "release: 0.1.0-dev"], stdout="")
        r.when(["git", "push", "origin", "v0.1.0"], stdout="")
        r.when(["git", "checkout", "dev"], stdout="")
        return r

    def test_finish_ff_and_rebase(self):
        r = self._base()
        r.when(["git", "rebase", "v0.1.0"], stdout="")
        r.when(["git", "push", "--force-with-lease", "origin", "dev"], stdout="")
        release.cmd_finish(r, tag="v0.1.0", cmake_path=ROOT / "CMakeLists.txt", repo_root=ROOT)
        self.assertEqual(release.read_version(r.cmake_text).display(), "0.1.0-dev")
        self.assertIn(["gh", "release", "edit", "v0.1.0", "--prerelease=false"], r.calls)
        self.assertIn(["git", "push", "--force-with-lease", "origin", "dev"], r.calls)
        self.assertFalse(any(c[:3] == ["gh", "pr", "create"] for c in r.calls))

    def test_finish_merge_when_ff_fails(self):
        r = self._base()
        r.when(["git", "merge", "--ff-only", "off123"], rc=1)
        r.when(
            ["git", "merge", "--no-ff", "off123", "-m", "release: merge v0.1.0 into main"],
            stdout="",
        )
        r.when(["git", "rebase", "v0.1.0"], stdout="")
        r.when(["git", "push", "--force-with-lease", "origin", "dev"], stdout="")
        release.cmd_finish(r, tag="v0.1.0", cmake_path=ROOT / "CMakeLists.txt", repo_root=ROOT)
        self.assertIn(
            ["git", "merge", "--no-ff", "off123", "-m", "release: merge v0.1.0 into main"],
            r.calls,
        )

    def test_finish_rebase_conflict_opens_pr(self):
        r = self._base()
        r.when(["git", "rebase", "v0.1.0"], rc=1)
        r.when(["git", "rebase", "--abort"], stdout="")
        r.when(["git", "checkout", "-B", "rebase/v0.1.0", "v0.1.0"], stdout="")
        r.when(["git", "push", "-u", "origin", "rebase/v0.1.0"], stdout="")
        r.when(["gh", "pr", "create"], stdout="https://example/pr/1\n")
        release.cmd_finish(r, tag="v0.1.0", cmake_path=ROOT / "CMakeLists.txt", repo_root=ROOT)
        self.assertTrue(any(c[:3] == ["gh", "pr", "create"] for c in r.calls))
        self.assertFalse(
            any(c == ["git", "push", "--force-with-lease", "origin", "dev"] for c in r.calls)
        )
        pr = next(c for c in r.calls if c[:3] == ["gh", "pr", "create"])
        self.assertIn("--base", pr)
        self.assertIn("dev", pr)
        self.assertIn("--head", pr)
        self.assertIn("rebase/v0.1.0", pr)
```

`FakeRunner.when(["gh", "pr", "create"])` 按前缀匹配，因此完整 argv 仍会命中。

- [ ] **Step 2: 跑测，确认失败**

```bash
python -m unittest discover -s scripts/tests -p "test_*.py" -k FinishTest -v
```

Expected: `AttributeError: cmd_finish`。

- [ ] **Step 3: 实现 cmd_finish**

```python
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
    runner.run(["git", "fetch", "origin", "main", "dev", tag])
    official = runner.run(["git", "rev-parse", tag]).strip()
    runner.run(["git", "checkout", "main"])
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
                f"release: merge {tag} into main",
            ]
        )
    runner.run(["git", "push", "origin", "main"])

    runner.run(["git", "checkout", tag])
    if isinstance(runner, FakeRunner):
        text = runner.cmake_text
    else:
        text = cmake_path.read_text(encoding="utf-8")
    new_text = write_version(text, incoming.as_dev())
    if isinstance(runner, FakeRunner):
        runner.cmake_text = new_text
    elif new_text != text:
        cmake_path.write_text(new_text, encoding="utf-8")
    runner.run(["git", "add", "CMakeLists.txt"])
    try:
        runner.run(["git", "diff", "--cached", "--quiet"])
    except CommandError:
        runner.run(["git", "commit", "-m", f"release: {incoming.as_dev().display()}"])
    runner.run(["git", "push", "origin", tag])

    runner.run(["git", "checkout", "dev"])
    try:
        runner.run(["git", "rebase", tag])
    except CommandError:
        runner.run(["git", "rebase", "--abort"])
        head = f"rebase/{tag}"
        runner.run(["git", "checkout", "-B", head, tag])
        runner.run(["git", "push", "-u", "origin", head])
        body = (
            f"Automatic rebase of `dev` onto `{tag}` failed.\n\n"
            f"Resolve locally:\n\n"
            f"```\n"
            f"git fetch origin\n"
            f"git checkout dev\n"
            f"git rebase {tag}\n"
            f"# fix conflicts, then:\n"
            f"git push --force-with-lease origin dev\n"
            f"```\n"
        )
        runner.run(
            [
                "gh",
                "pr",
                "create",
                "--base",
                "dev",
                "--head",
                head,
                "--title",
                f"rebase dev onto {tag}",
                "--body",
                body,
            ]
        )
        return
    runner.run(["git", "push", "--force-with-lease", "origin", "dev"])
```

- [ ] **Step 4: 跑全部单测**

```bash
python -m unittest discover -s scripts/tests -p "test_*.py" -v
```

Expected: 全部 PASS。

---

### Task 4: `cmd_sync_docs` + CLI

**Files:**
- Modify: `scripts/release.py`
- Modify: `scripts/tests/test_release.py`

**Interfaces:**
- Consumes: `is_doc_path`, `Runner`, `require_gh`
- Produces:
  - `cmd_sync_docs(runner: Runner, *, dry_run: bool = False) -> None`
  - `main(argv: list[str] | None = None) -> None`：`start|finish|sync-docs`，`--tag`，`--dry-run`
  - 本地 `start` 未传 `--tag`：`gh release list --json tagName,isPrerelease --limit 20`，取第一条 `isPrerelease==true`

`cmd_sync_docs`：

1. `git fetch origin main dev`
2. `git log --reverse --pretty=%H origin/dev..origin/main` 得到 SHA 列表；空则打印 `nothing to sync` 并返回。
3. 对每个 SHA：`git diff-tree --no-commit-id --name-only -r <sha>`；任一路径 `not is_doc_path` → `SystemExit`，stderr 写明走 `dev`。
4. `git checkout dev`；`git cherry-pick <shas...>`。
   - 成功：`git push origin dev`。
   - 失败：`git cherry-pick --abort`；`git checkout -B sync-docs/from-main origin/dev`；`git push -u origin sync-docs/from-main`；`gh pr create --base dev --head sync-docs/from-main`，body 列出 SHA 与 `git cherry-pick` 命令。不 force-push `dev`。

- [ ] **Step 1: 写失败单测**

```python
class SyncDocsTest(unittest.TestCase):
    def test_rejects_non_doc_commit(self):
        r = release.FakeRunner()
        r.when(["git", "fetch", "origin", "main", "dev"], stdout="")
        r.when(["git", "log", "--reverse", "--pretty=%H", "origin/dev..origin/main"], stdout="abc\n")
        r.when(["git", "diff-tree", "--no-commit-id", "--name-only", "-r", "abc"], stdout="CMakeLists.txt\n")
        with self.assertRaises(SystemExit):
            release.cmd_sync_docs(r)
        self.assertFalse(any(c[:2] == ["git", "cherry-pick"] for c in r.calls))

    def test_cherry_pick_success(self):
        r = release.FakeRunner()
        r.when(["git", "fetch", "origin", "main", "dev"], stdout="")
        r.when(["git", "log", "--reverse", "--pretty=%H", "origin/dev..origin/main"], stdout="abc\n")
        r.when(["git", "diff-tree", "--no-commit-id", "--name-only", "-r", "abc"], stdout="README.md\n")
        r.when(["git", "checkout", "dev"], stdout="")
        r.when(["git", "cherry-pick", "abc"], stdout="")
        r.when(["git", "push", "origin", "dev"], stdout="")
        release.cmd_sync_docs(r)
        self.assertIn(["git", "cherry-pick", "abc"], r.calls)
        self.assertIn(["git", "push", "origin", "dev"], r.calls)

    def test_conflict_opens_pr(self):
        r = release.FakeRunner()
        r.when(["git", "fetch", "origin", "main", "dev"], stdout="")
        r.when(["git", "log", "--reverse", "--pretty=%H", "origin/dev..origin/main"], stdout="abc\n")
        r.when(["git", "diff-tree", "--no-commit-id", "--name-only", "-r", "abc"], stdout="docs/usr/README.md\n")
        r.when(["git", "checkout", "dev"], stdout="")
        r.when(["git", "cherry-pick", "abc"], rc=1)
        r.when(["git", "cherry-pick", "--abort"], stdout="")
        r.when(["git", "checkout", "-B", "sync-docs/from-main", "origin/dev"], stdout="")
        r.when(["git", "push", "-u", "origin", "sync-docs/from-main"], stdout="")
        r.when(["gh", "pr", "create"], stdout="https://example/pr/2\n")
        release.cmd_sync_docs(r)
        self.assertTrue(any(c[:3] == ["gh", "pr", "create"] for c in r.calls))
        self.assertFalse(any(c == ["git", "push", "--force-with-lease", "origin", "dev"] for c in r.calls))


class CliTest(unittest.TestCase):
    def test_require_gh_before_start(self):
        with self.assertRaises(SystemExit):
            release.main(["start", "--tag", "v0.1.0"], which=lambda _: None)
```

- [ ] **Step 2: 跑测，确认失败**

```bash
python -m unittest discover -s scripts/tests -p "test_*.py" -k "SyncDocsTest or CliTest" -v
```

Expected: `cmd_sync_docs` / `main` 不存在。

- [ ] **Step 3: 实现 sync-docs 与 main**

```python
import argparse


def cmd_sync_docs(runner: Runner, *, dry_run: bool = False) -> None:
    runner.run(["git", "fetch", "origin", "main", "dev"])
    log = runner.run(["git", "log", "--reverse", "--pretty=%H", "origin/dev..origin/main"])
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
                "error: main is ahead of dev with non-doc files: "
                + ", ".join(bad)
                + ". Land those changes on dev instead.",
                file=sys.stderr,
            )
            raise SystemExit(1)
    runner.run(["git", "checkout", "dev"])
    try:
        runner.run(["git", "cherry-pick", *shas])
    except CommandError:
        runner.run(["git", "cherry-pick", "--abort"], check=False)
        runner.run(["git", "checkout", "-B", "sync-docs/from-main", "origin/dev"])
        runner.run(["git", "push", "-u", "origin", "sync-docs/from-main"])
        sha_list = " ".join(shas)
        body = (
            "Could not cherry-pick documentation commits from `main` onto `dev`.\n\n"
            f"SHAs: {sha_list}\n\n"
            "```\n"
            "git checkout dev\n"
            f"git cherry-pick {sha_list}\n"
            "```\n"
        )
        runner.run(
            [
                "gh",
                "pr",
                "create",
                "--base",
                "dev",
                "--head",
                "sync-docs/from-main",
                "--title",
                "sync docs from main",
                "--body",
                body,
            ]
        )
        return
    runner.run(["git", "push", "origin", "dev"])


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
    parser = argparse.ArgumentParser(description="EVEngine release / branch helper")
    parser.add_argument("--dry-run", action="store_true")
    sub = parser.add_subparsers(dest="cmd", required=True)
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
```

- [ ] **Step 4: 跑全部单测 + 缺 gh 的 CLI**

```bash
python -m unittest discover -s scripts/tests -p "test_*.py" -v
```

Expected: 全部 PASS。另：在能改 PATH 的环境可手动确认 `python scripts/release.py start --tag v0.1.0` 在无 `gh` 时退出码 2（可选）。

---

### Task 5: 改造 `ci.yml`

**Files:**
- Modify: `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: 无代码 API
- Produces: 日常触发 `dev`；`workflow_call` 入参 `build_type`（`debug`|`release`|`both`，默认 `debug`）与 `ref`（默认空）；layering 作业跑 `release.py` 单测；`release`/`both` 时增加桌面 Release 构建+`make test/<plat>`，以及 Android/iOS Release 构建

- [ ] **Step 1: 改文件头注释与 `on:`**

把顶部说明改成：日常在 `dev` 上跑 Debug；发布 workflow 以 `build_type=both` 调用。将

```yaml
on:
  push:
    branches: [main]
  pull_request:
    branches: [main]
  schedule:
    # Weekly: Monday 00:00 CST ≈ Sunday 16:00 UTC
    - cron: "0 16 * * 0"
  workflow_dispatch:
```

换成：

```yaml
on:
  workflow_call:
    inputs:
      build_type:
        description: debug | release | both
        type: string
        default: debug
      ref:
        description: git ref to check out (release branch or moved tag)
        type: string
        default: ""
  push:
    branches: [dev]
  pull_request:
    branches: [dev]
  schedule:
    # Weekly: Monday 00:00 CST ≈ Sunday 16:00 UTC
    - cron: "0 16 * * 0"
  workflow_dispatch:
    inputs:
      build_type:
        description: debug | release | both
        type: choice
        default: debug
        options: [debug, release, both]
```

- [ ] **Step 2: 统一 checkout ref 与作业开关**

在 `env:` 增加：

```yaml
  BUILD_TYPE: ${{ inputs.build_type || 'debug' }}
  CHECKOUT_REF: ${{ inputs.ref }}
```

每个 `actions/checkout@v4` 增加（已有 `submodules` 的保留）：

```yaml
        with:
          ref: ${{ inputs.ref || github.ref }}
          submodules: recursive
```

`layering` 的 checkout 无 submodules，写成：

```yaml
        with:
          ref: ${{ inputs.ref || github.ref }}
```

给现有构建作业（`windows` / `android` / `macos` / `ios` / `linux` / `webgpu`）加：

```yaml
    if: ${{ github.event_name != 'workflow_call' || inputs.build_type == 'debug' || inputs.build_type == 'both' }}
```

`layering` **不加** 这个 if（任何调用都跑）。在 layering 的 depgraph 步骤之后插入：

```yaml
      - name: Test release.py
        run: python3 -m unittest discover -s scripts/tests -p "test_*.py" -v
```

- [ ] **Step 3: 增加 Release 作业**

复制现有桌面/移动作业骨架，名字加 ` (Release)`，`if` 为：

```yaml
    if: ${{ inputs.build_type == 'release' || inputs.build_type == 'both' }}
```

命令与现有 Debug 作业的差别仅此（其余 Vulkan / 签名 / xvfb 步骤保持一致）：

| Job id | Build | Test |
|--------|--------|------|
| `windows-release` | `make build/win32 VS_GENERATOR="$VS_GENERATOR" JOBS=...` | `ALSOFT_DRIVERS=null make test/win32` |
| `linux-release` | `make build/linux JOBS="$(nproc)"` | `ALSOFT_DRIVERS=null xvfb-run -a make test/linux` |
| `macos-release` | `make build/macosx JOBS="$(sysctl -n hw.ncpu)"` | `ALSOFT_DRIVERS=null make test/macosx` |
| `android-release` | `make build/android`（参数与现有 android debug 相同，只换目标） | 无 |
| `ios-release` | `make build/ios`（签名分支与现有 ios debug 相同，只换目标） | 无 |

不要再加第二个 webgpu 作业：现有 `webgpu` 已是 Release。`build_type=release` 时仍应跑 webgpu——把 webgpu 的 `if` 改为：

```yaml
    if: ${{ always() && (github.event_name != 'workflow_call' || inputs.build_type == 'debug' || inputs.build_type == 'release' || inputs.build_type == 'both') }}
```

等价于「几乎总是跑」。更干净的写法：webgpu **不加** debug-only 的 if，这样 `release` 调用也会跑它。

`both` 时 debug 与 release 作业并行（同一 `ref`，结果等价于先 debug 再 release，墙钟更短）。

- [ ] **Step 4: 本地校验 YAML 与单测仍能跑**

```bash
python -m unittest discover -s scripts/tests -p "test_*.py" -v
python -c "import pathlib; t=pathlib.Path('.github/workflows/ci.yml').read_text(encoding='utf-8'); assert 'branches: [dev]' in t; assert 'workflow_call' in t; assert 'windows-release' in t; assert 'test_release' in t or 'test_*.py' in t"
```

Expected: unittest PASS；assert 不抛。

---

### Task 6: `sdk-release.yml` + `release.yml`

**Files:**
- Modify: `.github/workflows/sdk-release.yml`
- Create: `.github/workflows/release.yml`

**Interfaces:**
- Consumes: `scripts/release.py` 的 `start` / `finish`；`ci.yml` / `sdk-release.yml` 的 `workflow_call`
- Produces: Pre-release 触发编排；SDK 仍上传 zip，**不再** `gh release edit --prerelease=false`

- [ ] **Step 1: 改 sdk-release 触发与升版**

将文件头「promote … to a formal release」改成「attach SDK zips; formal promote is `release.py finish`」。

`on:` 换成：

```yaml
on:
  workflow_call:
    inputs:
      tag:
        required: true
        type: string
      ref:
        description: commit/branch to build (official vX.X.X after start)
        type: string
        default: ""
  workflow_dispatch:
    inputs:
      tag:
        description: Existing release tag to build and upload SDKs for
        required: true
        type: string
```

每个 checkout 增加：

```yaml
          ref: ${{ inputs.ref || inputs.tag }}
```

`TAG:` 环境变量统一为 `${{ inputs.tag || github.event.release.tag_name }}`（不再依赖 `prereleased` 事件）。`workflow_call` 时用 `inputs.tag`。

`publish` 作业改名 `Attach SDKs`，删除这两行：

```yaml
          gh release edit "$TAG" --repo "$REPO" --prerelease=false
```

以及注释 “Promote the pre-release…”。保留 `gh release upload`。

- [ ] **Step 2: 新建 release.yml**

```yaml
# Orchestrate a release started by a GitHub Pre-release:
#   start → strict tests (debug+release) → SDK zips → finish
# See docs/dev/发布流程.md and scripts/release.py.

name: Release

on:
  release:
    types: [prereleased]
  workflow_dispatch:
    inputs:
      tag:
        description: Pre-release tag (vMAJOR.MINOR.PATCH)
        required: true
        type: string

concurrency:
  group: release-${{ github.event.release.tag_name || inputs.tag }}
  cancel-in-progress: false

permissions:
  contents: write
  pull-requests: write

jobs:
  start:
    name: start (version + branch)
    runs-on: ubuntu-latest
    outputs:
      tag: ${{ steps.go.outputs.tag }}
      branch: ${{ steps.go.outputs.branch }}
    steps:
      - name: Checkout
        uses: actions/checkout@v4
        with:
          fetch-depth: 0
          ref: ${{ github.event.release.tag_name || inputs.tag }}
          token: ${{ secrets.GITHUB_TOKEN }}

      - name: Start release
        id: go
        env:
          GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}
          CI: "true"
        run: |
          set -euo pipefail
          TAG="${{ github.event.release.tag_name || inputs.tag }}"
          git config user.name "github-actions[bot]"
          git config user.email "41898282+github-actions[bot]@users.noreply.github.com"
          python3 scripts/release.py start --tag "$TAG"

  strict-tests:
    name: strict tests
    needs: start
    uses: ./.github/workflows/ci.yml
    with:
      build_type: both
      ref: ${{ needs.start.outputs.branch }}
    secrets: inherit

  sdk:
    name: SDK pack
    needs: [start, strict-tests]
    uses: ./.github/workflows/sdk-release.yml
    with:
      tag: ${{ needs.start.outputs.tag }}
      ref: ${{ needs.start.outputs.branch }}
    secrets: inherit

  finish:
    name: finish (promote + rebase dev)
    needs: [start, sdk]
    runs-on: ubuntu-latest
    steps:
      - name: Checkout
        uses: actions/checkout@v4
        with:
          fetch-depth: 0
          ref: ${{ needs.start.outputs.branch }}
          token: ${{ secrets.GITHUB_TOKEN }}

      - name: Finish release
        env:
          GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}
          CI: "true"
        run: |
          set -euo pipefail
          git config user.name "github-actions[bot]"
          git config user.email "41898282+github-actions[bot]@users.noreply.github.com"
          git fetch origin main dev ${{ needs.start.outputs.tag }} || true
          python3 scripts/release.py finish --tag "${{ needs.start.outputs.tag }}"
```

`cmd_start` 必须往 `GITHUB_OUTPUT` 写 `tag` 与 `branch`（Task 2 的 `_write_github_output`）。`start` 步骤的 `id: go` 才能读到 outputs。若 Actions 没收到 outputs，在 `start` 的 run 末尾显式追加：

```bash
echo "tag=$TAG" >> "$GITHUB_OUTPUT"
echo "branch=$TAG" >> "$GITHUB_OUTPUT"
```

- [ ] **Step 3: 校验发布 YAML**

```bash
python -c "
from pathlib import Path
rel = Path('.github/workflows/release.yml').read_text(encoding='utf-8')
sdk = Path('.github/workflows/sdk-release.yml').read_text(encoding='utf-8')
assert 'types: [prereleased]' in rel
assert 'build_type: both' in rel
assert 'scripts/release.py start' in rel
assert 'scripts/release.py finish' in rel
assert 'workflow_call' in sdk
assert 'prerelease=false' not in sdk
assert 'gh release upload' in sdk
print('ok')
"
```

Expected: 打印 `ok`。

---

### Task 7: `sync-docs.yml` 与文档

**Files:**
- Create: `.github/workflows/sync-docs.yml`
- Create: `docs/dev/发布流程.md`
- Modify: `docs/dev/README.md`（架构与工程约定列表加发布流程链接）
- Modify: `docs/usr/README.md`（克隆段说明默认 `main` 为正式版，开发切 `dev`）
- Modify: `Readme.md` 与 `Readme.en.md` 的 `git clone` 段（各加两句，不改其它结构）
- Modify: `docs/dev/superpowers/specs/2026-08-19-release-branch-model-design.md` 状态行 → `已实施`

**Interfaces:**
- Consumes: `cmd_sync_docs`
- Produces: `main` 文档 push 自动同步；维护者知道如何发版、如何热修文档、如何建 `dev`

- [ ] **Step 1: 写 sync-docs.yml**

```yaml
# Cherry-pick documentation commits from main onto dev.
# Non-doc files on main (relative to dev) fail the job.

name: Sync docs

on:
  push:
    branches: [main]
    paths:
      - README.md
      - Readme.md
      - Readme.en.md
      - Doxyfile
      - docs/**

permissions:
  contents: write
  pull-requests: write

jobs:
  sync:
    name: sync-docs
    runs-on: ubuntu-latest
    steps:
      - name: Checkout
        uses: actions/checkout@v4
        with:
          fetch-depth: 0
          token: ${{ secrets.GITHUB_TOKEN }}

      - name: Sync documentation to dev
        env:
          GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}
          CI: "true"
        run: |
          set -euo pipefail
          git config user.name "github-actions[bot]"
          git config user.email "41898282+github-actions[bot]@users.noreply.github.com"
          python3 scripts/release.py sync-docs
```

- [ ] **Step 2: 写 docs/dev/发布流程.md**

内容必须包含（完整写成该文件，不要留空节）：

1. 分支：`main` 正式版 / `dev` 日常 / `vX.X.X` 发布。
2. 一次性：`git branch dev main && git push -u origin dev`；默认分支保持 `main`；建议保护规则（`main` 文档白名单 + 发布 bot；`dev` 走 PR）。脚本不改 GitHub 设置。
3. 发版：在 `dev` HEAD 上创建 Pre-release，tag `vMAJOR.MINOR.PATCH` → 等 Release workflow。失败可对同一 tag `workflow_dispatch` 重跑。rebase 冲突会开 `rebase/vX.X.X` PR。
4. 改展示：对 `main` 开文档白名单 PR；合并后 `sync-docs` 会 cherry-pick 到 `dev`。
5. 本地：`gh` 必装；`python scripts/release.py --dry-run start --tag v0.1.0`。
6. 链到 spec：`docs/dev/superpowers/specs/2026-08-19-release-branch-model-design.md`。

- [ ] **Step 3: 挂链接并改克隆说明**

`docs/dev/README.md` 在「架构与工程约定」列表加入：

```markdown
- [发布流程](发布流程.md)（`main` / `dev` / `vX.X.X`，Pre-release 发版）
```

`docs/usr/README.md` 在「获取源码」代码块后加：

```markdown
默认克隆的是 `main`（最新正式版，可直接编译）。要改引擎请：

```sh
git checkout dev
git pull
```
```

`Readme.md` 在 `git clone` 示例后加同样两句（中文）。`Readme.en.md` 加对应英文：default clone is `main` (latest release); engine work happens on `dev`.

- [ ] **Step 4: 收尾校验**

```bash
python -m unittest discover -s scripts/tests -p "test_*.py" -v
python -c "
from pathlib import Path
assert Path('docs/dev/发布流程.md').exists()
assert 'git checkout dev' in Path('docs/usr/README.md').read_text(encoding='utf-8')
assert 'branches: [dev]' in Path('.github/workflows/ci.yml').read_text(encoding='utf-8')
assert Path('.github/workflows/release.yml').exists()
assert Path('.github/workflows/sync-docs.yml').exists()
print('ok')
"
```

Expected: unittest PASS；打印 `ok`。把 spec 状态改成 `已实施`。

---

## Self-review (spec coverage)

| Spec 要求 | 任务 |
|-----------|------|
| `main` / `dev` / `vX.X.X` / rebase 分支职责 | Task 7 文档 + Task 2–3 脚本 |
| 文档白名单 PR + cherry-pick / 冲突开 PR | Task 1 `is_doc_path` + Task 4 + Task 7 workflow |
| `main` 快进或 merge 正式版 commit | Task 3 |
| Pre-release → start → 严测 → SDK → finish | Task 6 |
| tag 为版本来源、禁止降级、同号 `-dev` 回写 | Task 1–3 |
| `start` 建分支、去 `-dev`、挪 tag、幂等 / 已正式且不一致则失败 | Task 2 |
| `finish` 升正式版、回写 `-dev`、rebase 或 `rebase/vX.X.X` PR | Task 3 |
| `gh` 缺失报错 | Task 2 + Task 4 CLI |
| `ci.yml` 盯 `dev`、`workflow_call`、`both` = Debug + Release 测试 | Task 5 |
| `sdk-release.yml` 不升正式版 | Task 6 |
| `sync-docs.yml` | Task 7 |
| `scripts/tests/test_release.py` 挂在 layering | Task 5 |
| 一次性建 `dev`、不改 GitHub 设置 | Task 7 文档 |
| 不改 SDK 打包内容 | 无任务触碰 `zip-sdk.py` / `test-sdk.sh` / `make sdk` 规则 |

无 TBD/TODO。函数名在后续任务中与 Task 1–4 的 Produces 一致：`parse_tag`、`cmd_start`、`cmd_finish`、`cmd_sync_docs`、`require_gh`、`FakeRunner.run`。
