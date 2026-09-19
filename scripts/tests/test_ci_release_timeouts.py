"""Contracts for CI cold-build and release job budgets."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github" / "workflows" / "ci.yml"


def job_block(workflow: str, job: str) -> str:
    start = workflow.index(f"  {job}:\n")
    match = re.search(r"^  [a-z0-9-]+:\n", workflow[start + 1 :], re.MULTILINE)
    if match is None:
        return workflow[start:]
    return workflow[start : start + 1 + match.start()]


def step_timeout(job: str, step: str, workflow: str) -> int:
    block = job_block(workflow, job)
    match = re.search(
        rf"^      - name: {re.escape(step)}\n(?:.*\n){{0,8}}?"
        r"^        timeout-minutes: (\d+)$",
        block,
        re.MULTILINE,
    )
    if match is None:
        raise AssertionError(f"missing timeout for {job} / {step}")
    return int(match.group(1))


class ReleaseJobTimeoutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.workflow = WORKFLOW.read_text(encoding="utf-8")

    def test_release_job_budgets_cover_cold_builds_and_tests(self) -> None:
        minimums = {
            "windows-release": 180,
            "android-release": 150,
            "macos-release": 150,
            "linux-release": 150,
        }
        for job, minimum in minimums.items():
            with self.subTest(job=job):
                block = job_block(self.workflow, job)
                match = re.search(r"^    timeout-minutes: (\d+)$", block, re.MULTILINE)
                self.assertIsNotNone(match)
                self.assertGreaterEqual(int(match.group(1)), minimum)

    def test_cold_native_build_steps_have_observed_headroom(self) -> None:
        minimums = {
            ("windows", "Build win32 debug"): 100,
            ("windows-release", "Build win32 release"): 120,
            ("macos", "Build macosx debug"): 75,
        }
        for (job, step), minimum in minimums.items():
            with self.subTest(job=job, step=step):
                self.assertGreaterEqual(step_timeout(job, step, self.workflow), minimum)

    def test_android_jobs_start_sccache_before_the_build(self) -> None:
        for job in ("android", "android-release"):
            with self.subTest(job=job):
                block = job_block(self.workflow, job)
                start = block.index("      - name: Start C/C++ compilation cache\n")
                build_name = "android debug" if job == "android" else "android release"
                build = block.index(f"      - name: Build {build_name}\n")
                self.assertLess(start, build)
                self.assertIn("        run: sccache --start-server\n", block[start:build])


if __name__ == "__main__":
    unittest.main()
