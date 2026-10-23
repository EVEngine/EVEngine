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
            "windows-release": 300,
            "android-release": 180,
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
            ("windows-release", "Build win32 release"): 210,
            ("android", "Build android debug"): 85,
            ("macos", "Build macosx debug"): 75,
            ("macos", "Run macosx unit tests"): 90,
            ("macos", "Build native Dawn Metal lane"): 75,
            ("windows", "Build native Dawn parity lane"): 150,
            ("linux-release", "Build linux release"): 105,
            ("android-release", "Build android release"): 120,
            ("profile-matrix", "Build independent profile smoke target"): 60,
            ("linux-asan", "Build linux RelWithDebInfo (ASan+UBSan)"): 180,
            # Full example smoke under Lavapipe has reached the former 20-minute
            # guard after a green 150/150 PASS summary; keep margin.
            ("linux", "Smoke all examples (headless, >=2s alive each)"): 30,
        }
        for (job, step), minimum in minimums.items():
            with self.subTest(job=job, step=step):
                self.assertGreaterEqual(step_timeout(job, step, self.workflow), minimum)

    def test_multi_phase_job_budgets_cover_observed_cold_runs(self) -> None:
        minimums = {
            "windows": 300,
            "android": 120,
            "macos": 240,
            "linux-coverage": 150,
            "profile-matrix": 130,
            "linux-asan": 330,
        }
        for job, minimum in minimums.items():
            with self.subTest(job=job):
                block = job_block(self.workflow, job)
                match = re.search(r"^    timeout-minutes: (\d+)$", block, re.MULTILINE)
                self.assertIsNotNone(match)
                self.assertGreaterEqual(int(match.group(1)), minimum)

    def test_android_jobs_start_sccache_before_the_build(self) -> None:
        # Android uniquely starts sccache before the NDK build so a GHA/ghac
        # storage outage is visible early. The helper retries transient errors
        # and switches to a local disk cache when needed.
        helper = ROOT / ".github" / "scripts" / "start-sccache.sh"
        self.assertTrue(helper.is_file(), msg=f"missing {helper}")
        helper_text = helper.read_text(encoding="utf-8")
        self.assertIn("sccache --start-server", helper_text)
        self.assertIn("SCCACHE_GHA_ENABLED=false", helper_text)

        for job in ("android", "android-release"):
            with self.subTest(job=job):
                block = job_block(self.workflow, job)
                start = block.index("      - name: Start C/C++ compilation cache\n")
                build_name = "android debug" if job == "android" else "android release"
                build = block.index(f"      - name: Build {build_name}\n")
                self.assertLess(start, build)
                self.assertIn(
                    "        run: bash .github/scripts/start-sccache.sh\n",
                    block[start:build],
                )

    def test_benchmark_filter_is_part_of_the_ctest_command(self) -> None:
        for job in ("windows-release", "macos-release", "linux-release"):
            with self.subTest(job=job):
                block = job_block(self.workflow, job)
                sample = block.split("      - name: Sample settlement release benchmark\n", 1)[1]
                sample = sample.split("      - name:", 1)[0]
                logical_lines = sample.replace("\\\n", "").splitlines()
                command = next(line for line in logical_lines if line.strip().startswith("ctest "))
                self.assertIn("-R '^settlement\\.benchmark\\.'", command)
                self.assertIn("| tee build/settlement-benchmark-", command)


if __name__ == "__main__":
    unittest.main()
