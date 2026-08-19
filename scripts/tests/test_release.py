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


class RequireGhTest(unittest.TestCase):
    def test_missing_exits(self):
        with self.assertRaises(SystemExit) as ctx:
            release.require_gh(which=lambda _: None)
        self.assertNotEqual(ctx.exception.code, 0)


class StartTest(unittest.TestCase):
    def setUp(self):
        self.runner = release.FakeRunner()
        self.runner.cmake_text = CMAKE

    def _wire_happy(self, *, is_prerelease="true", cmake=CMAKE):
        r = self.runner
        r.cmake_text = cmake
        r.when(
            ["gh", "release", "view", "v0.1.0"],
            stdout='{"tagName":"v0.1.0","isPrerelease":%s}\n' % is_prerelease,
        )
        r.when(["git", "status", "--porcelain"], stdout="")
        r.when(["git", "fetch", "origin", "tag", "v0.1.0", "--force"], stdout="")
        r.when(["git", "rev-parse", "v0.1.0"], stdout="aaa111\n")
        r.when(["git", "checkout", "-B", "v0.1.0", "v0.1.0"], stdout="")
        r.when(["git", "add", "CMakeLists.txt"], stdout="")
        r.when(["git", "diff", "--cached", "--quiet"], rc=1)
        r.when(["git", "commit", "-m", "release: 0.1.0"], stdout="")
        r.when(["git", "tag", "-f", "v0.1.0"], stdout="")
        r.when(["git", "push", "-u", "origin", "v0.1.0"], stdout="")
        r.when(["git", "push", "--force", "origin", "refs/tags/v0.1.0"], stdout="")
        r.when(["git", "rev-parse", "HEAD"], stdout="bbb222\n")
        return r

    def test_start_writes_official_and_moves_tag(self):
        r = self._wire_happy()
        release.cmd_start(
            r, tag="v0.1.0", cmake_path=ROOT / "CMakeLists.txt", repo_root=ROOT, ci=True
        )
        self.assertEqual(release.read_version(r.cmake_text).display(), "0.1.0")
        self.assertIn(["git", "commit", "-m", "release: 0.1.0"], r.calls)
        self.assertIn(["git", "tag", "-f", "v0.1.0"], r.calls)
        self.assertIn(["git", "push", "--force", "origin", "refs/tags/v0.1.0"], r.calls)

    def test_start_idempotent_when_already_official(self):
        official = release.write_version(CMAKE, release.parse_tag("v0.1.0"))
        r = self._wire_happy(cmake=official)
        r.when(["git", "diff", "--cached", "--quiet"], rc=0)
        release.cmd_start(
            r, tag="v0.1.0", cmake_path=ROOT / "CMakeLists.txt", repo_root=ROOT, ci=True
        )
        commit_calls = [c for c in r.calls if c[:2] == ["git", "commit"]]
        self.assertEqual(commit_calls, [])

    def test_start_rejects_downgrade(self):
        newer = release.write_version(CMAKE, release.parse_tag("v0.2.0").as_dev())
        r = self._wire_happy(cmake=newer)
        with self.assertRaises(SystemExit):
            release.cmd_start(
                r, tag="v0.1.0", cmake_path=ROOT / "CMakeLists.txt", repo_root=ROOT, ci=True
            )
        self.assertFalse(any(c[:2] == ["git", "commit"] for c in r.calls))

    def test_start_formal_mismatch_fails(self):
        r = self._wire_happy(is_prerelease="false")
        with self.assertRaises(SystemExit):
            release.cmd_start(
                r, tag="v0.1.0", cmake_path=ROOT / "CMakeLists.txt", repo_root=ROOT, ci=True
            )

    def test_start_formal_and_already_official_ok(self):
        official = release.write_version(CMAKE, release.parse_tag("v0.1.0"))
        r = self._wire_happy(is_prerelease="false", cmake=official)
        r.when(["git", "diff", "--cached", "--quiet"], rc=0)
        release.cmd_start(
            r, tag="v0.1.0", cmake_path=ROOT / "CMakeLists.txt", repo_root=ROOT, ci=True
        )


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


class SyncDocsTest(unittest.TestCase):
    def test_rejects_non_doc_commit(self):
        r = release.FakeRunner()
        r.when(["git", "fetch", "origin", "main", "dev"], stdout="")
        r.when(
            ["git", "log", "--reverse", "--pretty=%H", "origin/dev..origin/main"],
            stdout="abc\n",
        )
        r.when(
            ["git", "diff-tree", "--no-commit-id", "--name-only", "-r", "abc"],
            stdout="CMakeLists.txt\n",
        )
        with self.assertRaises(SystemExit):
            release.cmd_sync_docs(r)
        self.assertFalse(any(c[:2] == ["git", "cherry-pick"] for c in r.calls))

    def test_cherry_pick_success(self):
        r = release.FakeRunner()
        r.when(["git", "fetch", "origin", "main", "dev"], stdout="")
        r.when(
            ["git", "log", "--reverse", "--pretty=%H", "origin/dev..origin/main"],
            stdout="abc\n",
        )
        r.when(
            ["git", "diff-tree", "--no-commit-id", "--name-only", "-r", "abc"],
            stdout="README.md\n",
        )
        r.when(["git", "checkout", "dev"], stdout="")
        r.when(["git", "cherry-pick", "abc"], stdout="")
        r.when(["git", "push", "origin", "dev"], stdout="")
        release.cmd_sync_docs(r)
        self.assertIn(["git", "cherry-pick", "abc"], r.calls)
        self.assertIn(["git", "push", "origin", "dev"], r.calls)

    def test_conflict_opens_pr(self):
        r = release.FakeRunner()
        r.when(["git", "fetch", "origin", "main", "dev"], stdout="")
        r.when(
            ["git", "log", "--reverse", "--pretty=%H", "origin/dev..origin/main"],
            stdout="abc\n",
        )
        r.when(
            ["git", "diff-tree", "--no-commit-id", "--name-only", "-r", "abc"],
            stdout="docs/usr/README.md\n",
        )
        r.when(["git", "checkout", "dev"], stdout="")
        r.when(["git", "cherry-pick", "abc"], rc=1)
        r.when(["git", "cherry-pick", "--abort"], stdout="")
        r.when(["git", "checkout", "-B", "sync-docs/from-main", "origin/dev"], stdout="")
        r.when(["git", "push", "-u", "origin", "sync-docs/from-main"], stdout="")
        r.when(["gh", "pr", "create"], stdout="https://example/pr/2\n")
        release.cmd_sync_docs(r)
        self.assertTrue(any(c[:3] == ["gh", "pr", "create"] for c in r.calls))
        self.assertFalse(
            any(c == ["git", "push", "--force-with-lease", "origin", "dev"] for c in r.calls)
        )


class CliTest(unittest.TestCase):
    def test_require_gh_before_start(self):
        with self.assertRaises(SystemExit):
            release.main(["start", "--tag", "v0.1.0"], which=lambda _: None)


if __name__ == "__main__":
    unittest.main()



