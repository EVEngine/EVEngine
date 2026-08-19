import sys
import shutil
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


class MainPrAllowedTest(unittest.TestCase):
    def test_promote_head_allows_engine_files(self):
        self.assertTrue(
            release.main_pr_allowed("promote/v0.1.0", ["CMakeLists.txt", "src/engine/main.cpp"])
        )

    def test_docs_only_allowed(self):
        self.assertTrue(release.main_pr_allowed("docs/readme-fix", ["README.md", "docs/usr/README.md"]))

    def test_dev_engine_pr_rejected(self):
        self.assertFalse(release.main_pr_allowed("dev", ["src/engine/main.cpp"]))

    def test_isolation_branch_not_a_promote_head(self):
        self.assertFalse(release.main_pr_allowed("v0.1.0", ["CMakeLists.txt"]))

    def test_empty_non_promote_rejected(self):
        self.assertFalse(release.main_pr_allowed("hotfix", []))


class CheckMainPrTest(unittest.TestCase):
    def test_docs_pr_posts_success_check(self):
        r = release.FakeRunner()
        r.when(["git", "fetch", "origin", "main"], stdout="")
        r.when(
            ["git", "diff", "--name-only", "origin/main...HEAD"],
            stdout="README.md\n",
        )
        r.when(["git", "rev-parse", "HEAD"], stdout="abc123\n")
        r.when(["gh", "api"], stdout='{"id":1}\n')
        release.cmd_check_main_pr(r, head_ref="docs/readme-fix")
        api = " ".join(next(c for c in r.calls if c[:2] == ["gh", "api"]))
        self.assertIn("main-gate", api)
        self.assertIn("success", api)

    def test_engine_pr_fails_and_posts_failure_check(self):
        r = release.FakeRunner()
        r.when(["git", "fetch", "origin", "main"], stdout="")
        r.when(
            ["git", "diff", "--name-only", "origin/main...HEAD"],
            stdout="src/engine/main.cpp\n",
        )
        r.when(["git", "rev-parse", "HEAD"], stdout="abc123\n")
        r.when(["gh", "api"], stdout='{"id":1}\n')
        with self.assertRaises(SystemExit) as ctx:
            release.cmd_check_main_pr(r, head_ref="dev")
        self.assertNotEqual(ctx.exception.code, 0)
        api = " ".join(next(c for c in r.calls if c[:2] == ["gh", "api"]))
        self.assertIn("failure", api)


class RequireGhTest(unittest.TestCase):
    def test_missing_exits(self):
        with self.assertRaises(SystemExit) as ctx:
            release.require_gh(which=lambda _: None)
        self.assertNotEqual(ctx.exception.code, 0)


class StartTest(unittest.TestCase):
    def setUp(self):
        self.runner = release.FakeRunner()
        self.runner.cmake_text = CMAKE
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        for tp in release.VERSION_TOUCHPOINTS:
            if tp.kind != "required":
                continue
            src = ROOT / tp.path
            dst = self.root / tp.path
            dst.parent.mkdir(parents=True, exist_ok=True)
            if src.exists():
                shutil.copy2(src, dst)

    def tearDown(self):
        self.tmp.cleanup()

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
        r.when(["git", "checkout", "-B", "v0.1.0", "refs/tags/v0.1.0"], stdout="")
        r.when(["git", "add"], stdout="")
        r.when(["git", "diff", "--cached", "--quiet"], rc=1)
        r.when(["git", "commit", "-m", "release: 0.1.0"], stdout="")
        r.when(["git", "tag", "-f", "v0.1.0"], stdout="")
        r.when(["git", "push", "-u", "origin", "refs/heads/v0.1.0"], stdout="")
        r.when(["git", "push", "--force", "origin", "refs/tags/v0.1.0"], stdout="")
        r.when(["git", "rev-parse", "HEAD"], stdout="bbb222\n")
        return r

    def test_start_writes_official_and_moves_tag(self):
        r = self._wire_happy()
        release.cmd_start(
            r, tag="v0.1.0", cmake_path=self.root / "CMakeLists.txt", repo_root=self.root, ci=True
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
            r, tag="v0.1.0", cmake_path=self.root / "CMakeLists.txt", repo_root=self.root, ci=True
        )
        commit_calls = [c for c in r.calls if c[:2] == ["git", "commit"]]
        self.assertEqual(commit_calls, [])

    def test_start_rejects_downgrade(self):
        newer = release.write_version(CMAKE, release.parse_tag("v0.2.0").as_dev())
        r = self._wire_happy(cmake=newer)
        with self.assertRaises(SystemExit):
            release.cmd_start(
                r, tag="v0.1.0", cmake_path=self.root / "CMakeLists.txt", repo_root=self.root, ci=True
            )
        self.assertFalse(any(c[:2] == ["git", "commit"] for c in r.calls))

    def test_start_formal_mismatch_fails(self):
        r = self._wire_happy(is_prerelease="false")
        with self.assertRaises(SystemExit):
            release.cmd_start(
                r, tag="v0.1.0", cmake_path=self.root / "CMakeLists.txt", repo_root=self.root, ci=True
            )

    def test_start_formal_and_already_official_ok(self):
        official = release.write_version(CMAKE, release.parse_tag("v0.1.0"))
        r = self._wire_happy(is_prerelease="false", cmake=official)
        r.when(["git", "diff", "--cached", "--quiet"], rc=0)
        release.cmd_start(
            r, tag="v0.1.0", cmake_path=self.root / "CMakeLists.txt", repo_root=self.root, ci=True
        )

    def test_start_syncs_required_version_touchpoints(self):
        # Corrupt every required touchpoint, then let start fix them to 0.1.0.
        (self.root / "docs/CMakeLists.txt").write_text(
            'set(EVENGINE_MAJOR_VERSION "9")\n'
            'set(EVENGINE_MINOR_VERSION "9")\n'
            'set(EVENGINE_PATCH_VERSION "9")\n',
            encoding="utf-8",
        )
        gradle = self.root / "platform/android/apk/app/build.gradle.kts"
        gradle.write_text('versionCode = 999\nversionName = "9.9.9"\n', encoding="utf-8")
        mcp = self.root / "src/engine/devtools/McpServer.cpp"
        mcp.write_text(
            '"\\"title\\":\\"EVEngine MCP\\",\\"version\\":\\"9.9.9\\""\n',
            encoding="utf-8",
        )
        for rel in (
            "examples/basic/root.nut",
            "platform/ios/game-shell/root.nut",
            "platform/android/game-shell/root.nut",
        ):
            (self.root / rel).write_text('print("EVEngine v9.9.9")\n', encoding="utf-8")

        r = self._wire_happy()
        release.cmd_start(
            r, tag="v0.1.0", cmake_path=self.root / "CMakeLists.txt", repo_root=self.root, ci=True
        )

        docs = (self.root / "docs/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn('set(EVENGINE_MAJOR_VERSION "0")', docs)
        self.assertIn('set(EVENGINE_PATCH_VERSION "0")', docs)
        gradle_text = gradle.read_text(encoding="utf-8")
        self.assertIn('versionName = "0.1.0"', gradle_text)
        self.assertIn("versionCode = 100", gradle_text)
        self.assertIn("0.1.0", mcp.read_text(encoding="utf-8"))
        self.assertIn(
            'print("EVEngine v0.1.0")',
            (self.root / "examples/basic/root.nut").read_text(encoding="utf-8"),
        )
        add_call = next(c for c in r.calls if c[:2] == ["git", "add"])
        self.assertIn("docs/CMakeLists.txt", add_call)
        self.assertIn("platform/android/apk/app/build.gradle.kts", add_call)


class CheckVersionsTest(unittest.TestCase):
    def test_current_tree_is_consistent(self):
        release.cmd_check_versions(release.FakeRunner(), root=ROOT)

    def test_required_mismatch_fails(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "CMakeLists.txt").write_text(CMAKE, encoding="utf-8")
            for tp in release.VERSION_TOUCHPOINTS:
                if tp.kind != "required":
                    continue
                dst = root / tp.path
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(ROOT / tp.path, dst)
            gradle = root / "platform/android/apk/app/build.gradle.kts"
            gradle.write_text('versionName = "9.9.9"\n', encoding="utf-8")
            with self.assertRaises(SystemExit):
                release.cmd_check_versions(release.FakeRunner(), root=root)


class CleanupBranchesTest(unittest.TestCase):
    def _runner(self):
        r = release.FakeRunner()
        r.when(["git", "fetch", "origin", "--prune"], stdout="")
        r.when(
            [
                "git",
                "for-each-ref",
                "--format=%(refname:short) %(objectname)",
                "refs/remotes/origin",
            ],
            stdout=(
                "origin/main mmm\n"
                "origin/dev ddd\n"
                "origin/v0.1.0 aaa\n"
                "origin/v0.2.0 bbb\n"
                "origin/promote/v0.1.0 ccc\n"
                "origin/rebase/v0.1.0 dd2\n"
                "origin/feature-x fff\n"
            ),
        )
        r.when(["git", "merge-base", "--is-ancestor", "aaa", "origin/dev"], rc=0)
        r.when(["git", "merge-base", "--is-ancestor", "bbb", "origin/dev"], rc=1)
        r.when(["git", "merge-base", "--is-ancestor", "ccc", "origin/main"], rc=0)
        r.when(["git", "merge-base", "--is-ancestor", "dd2", "origin/dev"], rc=0)
        r.when(["git", "push", "origin", "--delete"], stdout="")
        return r

    def test_deletes_merged_branches_only(self):
        r = self._runner()
        release.cmd_cleanup_branches(r)
        deletes = [c for c in r.calls if "--delete" in c]
        self.assertEqual(
            deletes,
            [
                ["git", "push", "origin", "--delete", "refs/heads/v0.1.0"],
                ["git", "push", "origin", "--delete", "refs/heads/promote/v0.1.0"],
                ["git", "push", "origin", "--delete", "refs/heads/rebase/v0.1.0"],
            ],
        )

    def test_dry_run_never_pushes(self):
        r = self._runner()
        release.cmd_cleanup_branches(r, dry_run=True)
        self.assertFalse(any(c[:2] == ["git", "push"] for c in r.calls))


class FinishTest(unittest.TestCase):
    def _base(self, *, on_main: bool = False):
        r = release.FakeRunner()
        official = release.write_version(CMAKE, release.parse_tag("v0.1.0"))
        r.cmake_text = official
        r.when(["gh", "release", "edit", "v0.1.0", "--prerelease=false"], stdout="")
        r.when(
            ["git", "fetch", "origin", "main", "dev", "refs/heads/v0.1.0", "refs/tags/v0.1.0"],
            stdout="",
        )
        r.when(["git", "rev-parse", "refs/tags/v0.1.0"], stdout="off123\n")
        r.when(
            ["git", "merge-base", "--is-ancestor", "off123", "origin/main"],
            rc=0 if on_main else 1,
        )
        r.when(["git", "branch", "-f", "promote/v0.1.0", "off123"], stdout="")
        r.when(["git", "push", "-u", "origin", "promote/v0.1.0"], stdout="")
        r.when(
            ["gh", "pr", "list", "--base", "main", "--head", "promote/v0.1.0"],
            stdout="[]\n",
        )
        r.when(["gh", "pr", "create"], stdout="https://example/pr/1\n")
        r.when(["gh", "api"], stdout='{"id":1}\n')
        r.when(["git", "checkout", "-B", "v0.1.0", "refs/remotes/origin/v0.1.0"], stdout="")
        r.when(["git", "add", "CMakeLists.txt"], stdout="")
        r.when(["git", "diff", "--cached", "--quiet"], rc=1)
        r.when(["git", "commit", "-m", "release: 0.1.0-dev"], stdout="")
        r.when(["git", "push", "origin", "refs/heads/v0.1.0"], stdout="")
        r.when(["git", "checkout", "-B", "dev", "origin/dev"], stdout="")
        return r

    def _wire_rebase_ok(self, r, *, ahead="2"):
        r.when(["git", "rebase", "refs/heads/v0.1.0"], stdout="")
        r.when(["git", "rev-list", "--count", "origin/dev..HEAD"], stdout=f"{ahead}\n")
        r.when(["git", "checkout", "-B", "rebase/v0.1.0", "HEAD"], stdout="")
        r.when(["git", "push", "-u", "origin", "rebase/v0.1.0"], stdout="")
        r.when(
            ["gh", "pr", "list", "--base", "dev", "--head", "rebase/v0.1.0"],
            stdout="[]\n",
        )

    def test_finish_opens_main_and_rebase_prs(self):
        r = self._base()
        self._wire_rebase_ok(r)
        release.cmd_finish(r, tag="v0.1.0", cmake_path=ROOT / "CMakeLists.txt", repo_root=ROOT)
        self.assertEqual(release.read_version(r.cmake_text).display(), "0.1.0-dev")
        self.assertIn(["gh", "release", "edit", "v0.1.0", "--prerelease=false"], r.calls)
        self.assertFalse(any(c == ["git", "push", "origin", "main"] for c in r.calls))
        self.assertFalse(
            any(c == ["git", "push", "--force-with-lease", "origin", "dev"] for c in r.calls)
        )
        prs = [c for c in r.calls if c[:3] == ["gh", "pr", "create"]]
        self.assertEqual(len(prs), 2)
        main_pr = next(c for c in prs if "main" in c and "promote/v0.1.0" in c)
        rebase_pr = next(c for c in prs if "dev" in c and "rebase/v0.1.0" in c)
        self.assertIn("--base", main_pr)
        self.assertIn("--head", rebase_pr)
        self.assertTrue(any(c[:2] == ["gh", "api"] for c in r.calls))

    def test_finish_skips_main_pr_when_already_on_main(self):
        r = self._base(on_main=True)
        self._wire_rebase_ok(r)
        release.cmd_finish(r, tag="v0.1.0", cmake_path=ROOT / "CMakeLists.txt", repo_root=ROOT)
        self.assertFalse(any("promote/v0.1.0" in c for c in r.calls if c[:2] == ["git", "push"]))
        prs = [c for c in r.calls if c[:3] == ["gh", "pr", "create"]]
        self.assertTrue(all("promote/v0.1.0" not in c for c in prs))
        self.assertTrue(any("rebase/v0.1.0" in c for c in prs))

    def test_finish_skips_rebase_pr_when_dev_up_to_date(self):
        r = self._base()
        self._wire_rebase_ok(r, ahead="0")
        release.cmd_finish(r, tag="v0.1.0", cmake_path=ROOT / "CMakeLists.txt", repo_root=ROOT)
        prs = [c for c in r.calls if c[:3] == ["gh", "pr", "create"]]
        self.assertEqual(len(prs), 1)
        self.assertIn("promote/v0.1.0", prs[0])
        self.assertFalse(any("rebase/v0.1.0" in c for c in prs))

    def test_finish_rebase_conflict_opens_pr(self):
        r = self._base()
        r.when(["git", "rebase", "refs/heads/v0.1.0"], rc=1)
        r.when(["git", "rebase", "--abort"], stdout="")
        r.when(["git", "checkout", "-B", "rebase/v0.1.0", "refs/heads/v0.1.0"], stdout="")
        r.when(["git", "push", "-u", "origin", "rebase/v0.1.0"], stdout="")
        r.when(
            ["gh", "pr", "list", "--base", "dev", "--head", "rebase/v0.1.0"],
            stdout="[]\n",
        )
        release.cmd_finish(r, tag="v0.1.0", cmake_path=ROOT / "CMakeLists.txt", repo_root=ROOT)
        prs = [c for c in r.calls if c[:3] == ["gh", "pr", "create"]]
        self.assertTrue(any("promote/v0.1.0" in c for c in prs))
        rebase_pr = next(c for c in prs if "rebase/v0.1.0" in c)
        self.assertIn("--base", rebase_pr)
        self.assertIn("dev", rebase_pr)
        self.assertFalse(
            any(c == ["git", "push", "--force-with-lease", "origin", "dev"] for c in r.calls)
        )

    def test_finish_reuses_existing_main_pr(self):
        r = self._base()
        r.when(
            ["gh", "pr", "list", "--base", "main", "--head", "promote/v0.1.0"],
            stdout='[{"url":"https://example/pr/9"}]\n',
        )
        self._wire_rebase_ok(r)
        release.cmd_finish(r, tag="v0.1.0", cmake_path=ROOT / "CMakeLists.txt", repo_root=ROOT)
        prs = [c for c in r.calls if c[:3] == ["gh", "pr", "create"]]
        self.assertTrue(all("promote/v0.1.0" not in c for c in prs))
        self.assertTrue(any(c[:2] == ["gh", "api"] for c in r.calls))


class SyncDocsTest(unittest.TestCase):
    def _wire(self, r, diff_out):
        r.when(["git", "fetch", "origin", "main", "dev"], stdout="")
        r.when(
            ["git", "merge-base", "origin/main", "origin/dev"],
            stdout="base123\n",
        )
        r.when(["git", "diff", "--name-only", "base123", "origin/main"], stdout=diff_out)
        return r

    def test_rejects_non_doc_change(self):
        r = self._wire(release.FakeRunner(), "src/engine/main.cpp\n")
        with self.assertRaises(SystemExit):
            release.cmd_sync_docs(r)
        self.assertFalse(any(c[:2] == ["git", "checkout"] for c in r.calls))

    def test_allows_release_commit_and_skips_sync(self):
        r = self._wire(release.FakeRunner(), "CMakeLists.txt\n")
        release.cmd_sync_docs(r)
        self.assertFalse(any("sync-docs/from-main" in c for c in r.calls))

    def test_nothing_to_sync(self):
        r = self._wire(release.FakeRunner(), "")
        release.cmd_sync_docs(r)

    def test_syncs_docs_via_pr(self):
        r = self._wire(release.FakeRunner(), "README.md\ndocs/usr/guide.md\n")
        r.when(["git", "checkout", "-B", "sync-docs/from-main", "origin/dev"], stdout="")
        r.when(
            ["git", "checkout", "origin/main", "--", "README.md", "docs/usr/guide.md"],
            stdout="",
        )
        r.when(["git", "diff", "--cached", "--quiet"], rc=1)
        r.when(["git", "commit", "-m", "docs: sync documentation from main"], stdout="")
        r.when(
            ["git", "push", "-u", "origin", "sync-docs/from-main", "--force-with-lease"],
            stdout="",
        )
        r.when(
            ["gh", "pr", "list", "--base", "dev", "--head", "sync-docs/from-main"],
            stdout="[]\n",
        )
        r.when(["gh", "pr", "create"], stdout="https://example/pr/2\n")
        release.cmd_sync_docs(r)
        self.assertIn(
            ["git", "push", "-u", "origin", "sync-docs/from-main", "--force-with-lease"],
            r.calls,
        )
        self.assertTrue(any(c[:3] == ["gh", "pr", "create"] for c in r.calls))
        self.assertFalse(any(c == ["git", "push", "origin", "dev"] for c in r.calls))

    def test_dry_run_never_mutates(self):
        r = self._wire(release.FakeRunner(), "README.md\n")
        release.cmd_sync_docs(r, dry_run=True)
        self.assertFalse(any(c[:2] == ["git", "push"] for c in r.calls))
        self.assertFalse(any(c[:2] == ["git", "checkout"] for c in r.calls))

    def test_reuses_existing_pr(self):
        r = self._wire(release.FakeRunner(), "README.md\n")
        r.when(["git", "checkout", "-B", "sync-docs/from-main", "origin/dev"], stdout="")
        r.when(["git", "checkout", "origin/main", "--", "README.md"], stdout="")
        r.when(["git", "diff", "--cached", "--quiet"], rc=1)
        r.when(["git", "commit", "-m", "docs: sync documentation from main"], stdout="")
        r.when(
            ["git", "push", "-u", "origin", "sync-docs/from-main", "--force-with-lease"],
            stdout="",
        )
        r.when(
            ["gh", "pr", "list", "--base", "dev", "--head", "sync-docs/from-main"],
            stdout='[{"url":"https://example/pr/9"}]\n',
        )
        release.cmd_sync_docs(r)
        self.assertFalse(any(c[:3] == ["gh", "pr", "create"] for c in r.calls))


class CliTest(unittest.TestCase):
    def test_require_gh_before_start(self):
        with self.assertRaises(SystemExit):
            release.main(["start", "--tag", "v0.1.0"], which=lambda _: None)


if __name__ == "__main__":
    unittest.main()



