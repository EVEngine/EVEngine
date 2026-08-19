"""Real-git integration tests for scripts/release.py (start / finish).

The unit tests in test_release.py drive a FakeRunner, so they can never catch
refspec resolution bugs. These tests execute the *actual* git commands against
a local bare "origin" (no network, no GitHub), stubbing only the `gh` calls.

In particular they guard the two failure modes that previously broke releases:
  1. `git push origin vX.Y.Z` with both refs/heads/vX.Y.Z and refs/tags/vX.Y.Z
     present fails with "src refspec matches more than one".
  2. `git rebase vX.Y.Z` resolves to the tag (official commit), not the branch
     (the -dev write-back), so dev silently loses the -dev suffix.
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
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

TAG = "v0.1.0"


def _run(argv: list[str], cwd: Path | None = None) -> str:
    proc = subprocess.run(argv, cwd=cwd, capture_output=True, text=True)
    if proc.returncode != 0:
        raise AssertionError(
            f"command failed ({proc.returncode}): {' '.join(argv)}\n"
            f"stdout: {proc.stdout}\nstderr: {proc.stderr}"
        )
    return proc.stdout.strip()


class FakeGhRunner(release.RealRunner):
    """RealRunner that executes real git but stubs every `gh` call."""

    def __init__(self, cwd: Path):
        super().__init__(cwd=cwd)

    def run(self, argv: list[str], *, check: bool = True) -> str:
        if argv[:3] == ["gh", "release", "view"]:
            tag = argv[3]
            return f'{{"tagName":"{tag}","isPrerelease":true}}\n'
        if argv[:3] == ["gh", "release", "edit"]:
            return ""
        if argv[:3] == ["gh", "release", "list"]:
            return "[]\n"
        if argv[:3] == ["gh", "pr", "list"]:
            return "[]\n"
        if argv[:3] == ["gh", "pr", "create"]:
            return "https://example/pr/1\n"
        if argv[:2] == ["gh", "api"]:
            return '{"id":1}\n'
        return super().run(argv, check=check)


class ReleaseGitIntegrationTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.base = Path(self.tmp.name)
        self.origin = self.base / "origin.git"
        self.seed = self.base / "seed"
        self._init_repo()

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def _init_repo(self) -> None:
        """Seed origin with main + dev + pre-release tag v0.1.0 (like the docs)."""
        _run(["git", "init", "-q", "--bare", "-b", "main", str(self.origin)])
        self.seed.mkdir()
        _run(["git", "init", "-q", "-b", "main"], cwd=self.seed)
        _run(["git", "config", "user.name", "seed"], cwd=self.seed)
        _run(["git", "config", "user.email", "seed@example.com"], cwd=self.seed)
        (self.seed / "CMakeLists.txt").write_text(CMAKE, encoding="utf-8")
        self._write_required_files(self.seed)
        _run(["git", "add", "-A"], cwd=self.seed)
        _run(["git", "commit", "-q", "-m", "init main"], cwd=self.seed)
        _run(["git", "remote", "add", "origin", str(self.origin)], cwd=self.seed)
        _run(["git", "push", "-q", "origin", "main"], cwd=self.seed)

        _run(["git", "checkout", "-q", "-b", "dev"], cwd=self.seed)
        (self.seed / "dev.txt").write_text("dev work", encoding="utf-8")
        _run(["git", "add", "dev.txt"], cwd=self.seed)
        _run(["git", "commit", "-q", "-m", "dev work"], cwd=self.seed)
        _run(["git", "push", "-q", "-u", "origin", "dev"], cwd=self.seed)
        self.pre_release = _run(["git", "rev-parse", "HEAD"], cwd=self.seed)
        self.main_sha = _run(["git", "rev-parse", "main"], cwd=self.seed)
        _run(["git", "tag", TAG], cwd=self.seed)
        _run(["git", "push", "-q", "origin", f"refs/tags/{TAG}"], cwd=self.seed)

    def _write_required_files(self, target: Path) -> None:
        """Write minimal required version touchpoints (0.1.0) into a repo tree."""
        (target / "Readme.md").write_text("# EVEngine\n", encoding="utf-8")
        (target / "docs").mkdir(parents=True, exist_ok=True)
        (target / "docs/CMakeLists.txt").write_text(
            'set(EVENGINE_MAJOR_VERSION "0")\n'
            'set(EVENGINE_MINOR_VERSION "1")\n'
            'set(EVENGINE_PATCH_VERSION "0")\n',
            encoding="utf-8",
        )
        gradle = target / "platform/android/apk/app/build.gradle.kts"
        gradle.parent.mkdir(parents=True, exist_ok=True)
        gradle.write_text('versionCode = 100\nversionName = "0.1.0"\n', encoding="utf-8")
        mcp = target / "src/engine/devtools/McpServer.cpp"
        mcp.parent.mkdir(parents=True, exist_ok=True)
        mcp.write_text(
            '"\\"title\\":\\"EVEngine MCP\\",\\"version\\":\\"0.1.0\\""\n',
            encoding="utf-8",
        )
        for rel in ("examples/basic", "platform/ios/game-shell", "platform/android/game-shell"):
            path = target / rel
            path.mkdir(parents=True, exist_ok=True)
            (path / "root.nut").write_text('print("EVEngine v0.1.0")\n', encoding="utf-8")

    def _fresh_clone(self) -> Path:
        work = self.base / ("work-" + str(len(list(self.base.glob("work-*")))))
        _run(["git", "clone", "-q", str(self.origin), str(work)])
        _run(["git", "config", "user.name", "ci"], cwd=work)
        _run(["git", "config", "user.email", "ci@example.com"], cwd=work)
        return work

    def _remote_ref(self, refname: str) -> str | None:
        out = _run(["git", "ls-remote", str(self.origin), refname])
        if not out:
            return None
        return out.split("\t")[0]

    def _version_at(self, work: Path, refname: str) -> str:
        text = _run(["git", "show", f"{refname}:CMakeLists.txt"], cwd=work)
        return release.read_version(text).display()

    def test_start_then_finish_happy_path(self) -> None:
        work = self._fresh_clone()
        release.cmd_start(
            FakeGhRunner(cwd=work),
            tag=TAG,
            cmake_path=work / "CMakeLists.txt",
            repo_root=work,
            ci=True,
        )

        official = self._remote_ref("refs/heads/v0.1.0")
        self.assertTrue(official)
        # start must leave branch and tag on the SAME official commit.
        self.assertEqual(official, self._remote_ref("refs/tags/v0.1.0"))
        self.assertEqual(self._version_at(work, "refs/tags/v0.1.0"), "0.1.0")
        # start must not touch main/dev.
        self.assertEqual(self.main_sha, self._remote_ref("refs/heads/main"))
        self.assertEqual(self.pre_release, self._remote_ref("refs/heads/dev"))

        # finish runs in a fresh checkout, like CI.
        work2 = self._fresh_clone()
        release.cmd_finish(
            FakeGhRunner(cwd=work2),
            tag=TAG,
            cmake_path=work2 / "CMakeLists.txt",
            repo_root=work2,
        )

        # The tag must stay on the official commit (never the -dev write-back).
        self.assertEqual(official, self._remote_ref("refs/tags/v0.1.0"))
        self.assertEqual(self._version_at(work2, "refs/tags/v0.1.0"), "0.1.0")

        # The release branch carries the -dev write-back.
        dev_writeback = self._remote_ref("refs/heads/v0.1.0")
        self.assertNotEqual(official, dev_writeback)
        self.assertEqual(self._version_at(work2, "refs/remotes/origin/v0.1.0"), "0.1.0-dev")

        # The rebase branch must be based on the -dev write-back, NOT the tag.
        rebase = self._remote_ref("refs/heads/rebase/v0.1.0")
        self.assertTrue(rebase)
        self.assertEqual(rebase, dev_writeback)
        self.assertEqual(self._version_at(work2, "refs/heads/rebase/v0.1.0"), "0.1.0-dev")

        # finish must not push main or dev directly (PRs instead).
        self.assertEqual(self.main_sha, self._remote_ref("refs/heads/main"))
        self.assertEqual(self.pre_release, self._remote_ref("refs/heads/dev"))
        # The promote PR branch points at the official commit.
        self.assertEqual(official, self._remote_ref("refs/heads/promote/v0.1.0"))

        gradle = _run(
            ["git", "show", "refs/heads/v0.1.0:platform/android/apk/app/build.gradle.kts"],
            cwd=work,
        )
        self.assertIn('versionName = "0.1.0"', gradle)
        self.assertIn("versionCode = 100", gradle)

    def test_start_is_idempotent(self) -> None:
        work = self._fresh_clone()
        runner = FakeGhRunner(cwd=work)
        release.cmd_start(
            runner,
            tag=TAG,
            cmake_path=work / "CMakeLists.txt",
            repo_root=work,
            ci=True,
        )
        first = self._remote_ref("refs/heads/v0.1.0")
        tag_first = self._remote_ref("refs/tags/v0.1.0")

        release.cmd_start(
            runner,
            tag=TAG,
            cmake_path=work / "CMakeLists.txt",
            repo_root=work,
            ci=True,
        )
        # No new commit: branch and tag refs must be unchanged.
        self.assertEqual(first, self._remote_ref("refs/heads/v0.1.0"))
        self.assertEqual(tag_first, self._remote_ref("refs/tags/v0.1.0"))

    def test_finish_rebase_conflict_keeps_dev_untouched(self) -> None:
        # A real conflict needs the tag to bump a number that dev also changes:
        # base 0.1.0-dev, tag writes 0.2.0, dev concurrently sets minor to 3.
        conflict_tag = "v0.2.0"
        _run(["git", "tag", conflict_tag], cwd=self.seed)
        _run(["git", "push", "-q", "origin", f"refs/tags/{conflict_tag}"], cwd=self.seed)

        work = self._fresh_clone()
        release.cmd_start(
            FakeGhRunner(cwd=work),
            tag=conflict_tag,
            cmake_path=work / "CMakeLists.txt",
            repo_root=work,
            ci=True,
        )
        official = self._remote_ref("refs/heads/v0.2.0")
        gradle = _run(
            ["git", "show", "refs/heads/v0.2.0:platform/android/apk/app/build.gradle.kts"],
            cwd=work,
        )
        self.assertIn('versionName = "0.2.0"', gradle)
        self.assertIn("versionCode = 200", gradle)

        # dev gains a commit bumping minor to 3, conflicting with the 0.2.0 release.
        _run(["git", "checkout", "-q", "dev"], cwd=self.seed)
        conflicting = CMAKE.replace('set(EVENGINE_MINOR_VERSION "1")', 'set(EVENGINE_MINOR_VERSION "3")')
        (self.seed / "CMakeLists.txt").write_text(conflicting, encoding="utf-8")
        _run(["git", "add", "CMakeLists.txt"], cwd=self.seed)
        _run(["git", "commit", "-q", "-m", "dev minor bump"], cwd=self.seed)
        dev_conflict = _run(["git", "rev-parse", "HEAD"], cwd=self.seed)
        _run(["git", "push", "-q", "origin", "dev"], cwd=self.seed)

        work2 = self._fresh_clone()
        release.cmd_finish(
            FakeGhRunner(cwd=work2),
            tag=conflict_tag,
            cmake_path=work2 / "CMakeLists.txt",
            repo_root=work2,
        )

        # Rebase failed, so a rebase/v0.2.0 PR branch was pushed pointing at the
        # -dev write-back, and dev was NOT force-pushed.
        self.assertEqual(
            self._remote_ref("refs/heads/rebase/v0.2.0"),
            self._remote_ref("refs/heads/v0.2.0"),
        )
        self.assertNotEqual(official, self._remote_ref("refs/heads/v0.2.0"))
        self.assertEqual(self._version_at(work2, "refs/heads/rebase/v0.2.0"), "0.2.0-dev")
        self.assertEqual(dev_conflict, self._remote_ref("refs/heads/dev"))
        self.assertEqual(self.main_sha, self._remote_ref("refs/heads/main"))

    def test_cleanup_branches_after_prs_merged(self) -> None:
        work = self._fresh_clone()
        release.cmd_start(
            FakeGhRunner(cwd=work),
            tag=TAG,
            cmake_path=work / "CMakeLists.txt",
            repo_root=work,
            ci=True,
        )
        work2 = self._fresh_clone()
        release.cmd_finish(
            FakeGhRunner(cwd=work2),
            tag=TAG,
            cmake_path=work2 / "CMakeLists.txt",
            repo_root=work2,
        )

        self._merge_release_prs(work2)

        work3 = self._fresh_clone()
        release.cmd_cleanup_branches(FakeGhRunner(cwd=work3))

        for ref in (
            "refs/heads/v0.1.0",
            "refs/heads/promote/v0.1.0",
            "refs/heads/rebase/v0.1.0",
        ):
            self.assertIsNone(self._remote_ref(ref), ref)
        self.assertIsNotNone(self._remote_ref("refs/heads/main"))
        self.assertIsNotNone(self._remote_ref("refs/heads/dev"))
        self.assertIsNotNone(self._remote_ref("refs/tags/v0.1.0"))

    def _merge_release_prs(self, work: Path) -> None:
        """Simulate the human merging the promote + rebase PRs."""
        official = _run(["git", "rev-parse", "refs/tags/v0.1.0"], cwd=work)
        _run(["git", "checkout", "-q", "-B", "main", "refs/remotes/origin/main"], cwd=work)
        _run(["git", "merge", "-q", "--no-ff", official, "-m", "promote v0.1.0"], cwd=work)
        _run(["git", "push", "-q", "origin", "main"], cwd=work)
        _run(["git", "checkout", "-q", "-B", "dev", "refs/remotes/origin/dev"], cwd=work)
        _run(["git", "merge", "-q", "--ff-only", "refs/remotes/origin/rebase/v0.1.0"], cwd=work)
        _run(["git", "push", "-q", "origin", "dev"], cwd=work)

    def test_sync_docs_after_release_does_not_fail(self) -> None:
        work = self._fresh_clone()
        release.cmd_start(
            FakeGhRunner(cwd=work),
            tag=TAG,
            cmake_path=work / "CMakeLists.txt",
            repo_root=work,
            ci=True,
        )
        work2 = self._fresh_clone()
        release.cmd_finish(
            FakeGhRunner(cwd=work2),
            tag=TAG,
            cmake_path=work2 / "CMakeLists.txt",
            repo_root=work2,
        )
        self._merge_release_prs(work2)

        work3 = self._fresh_clone()
        # main is ahead of dev by release content (CMakeLists etc.) only.
        release.cmd_sync_docs(FakeGhRunner(cwd=work3))
        self.assertIsNone(self._remote_ref("refs/heads/sync-docs/from-main"))

    def test_sync_docs_copies_doc_commit_via_pr(self) -> None:
        work = self._fresh_clone()
        release.cmd_start(
            FakeGhRunner(cwd=work),
            tag=TAG,
            cmake_path=work / "CMakeLists.txt",
            repo_root=work,
            ci=True,
        )
        work2 = self._fresh_clone()
        release.cmd_finish(
            FakeGhRunner(cwd=work2),
            tag=TAG,
            cmake_path=work2 / "CMakeLists.txt",
            repo_root=work2,
        )
        self._merge_release_prs(work2)
        dev_before = self._remote_ref("refs/heads/dev")

        # A documentation commit lands on main (e.g. via a doc-only PR).
        _run(["git", "checkout", "-q", "main"], cwd=work2)
        (work2 / "Readme.md").write_text("# EVEngine\n\nUpdated docs for the release.\n", encoding="utf-8")
        _run(["git", "add", "Readme.md"], cwd=work2)
        _run(["git", "commit", "-q", "-m", "docs: update readme"], cwd=work2)
        _run(["git", "push", "-q", "origin", "main"], cwd=work2)

        work3 = self._fresh_clone()
        release.cmd_sync_docs(FakeGhRunner(cwd=work3))

        sync_sha = self._remote_ref("refs/heads/sync-docs/from-main")
        self.assertTrue(sync_sha)
        _run(["git", "fetch", "origin"], cwd=work3)
        readme = _run(
            ["git", "show", "refs/remotes/origin/sync-docs/from-main:Readme.md"],
            cwd=work3,
        )
        self.assertIn("Updated docs for the release.", readme)
        # dev was not direct-pushed; only the sync branch carries the change.
        self.assertEqual(dev_before, self._remote_ref("refs/heads/dev"))


if __name__ == "__main__":
    unittest.main()
