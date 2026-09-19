"""Contracts for release CI job budgets."""

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


if __name__ == "__main__":
    unittest.main()
