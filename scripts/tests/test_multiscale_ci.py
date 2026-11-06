"""Run the CI selection command against real CTest fixtures, including missing families."""

import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import textwrap
import unittest

from scripts import utf8_stdio


ROOT = Path(__file__).resolve().parents[2]
CASES = (
    "spatial.cellPosition.probe",
    "physics.field.probe",
    "physics.fieldCoupling.probe",
    "procgen.recursive.probe",
    "procgen.recursiveGraphics.probe",
    "graphics.multiscale.probe",
)


class MultiscaleCiSelectionTests(unittest.TestCase):
    def setUp(self):
        workflow = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
        job = workflow.split("\n  linux:\n", 1)[1].split("\n  linux-release:\n", 1)[0]
        step = job.split("      - name: Verify opt-in multiscale composition\n", 1)[1]
        step = step.split("\n      - name:", 1)[0]
        self.script = textwrap.dedent(step.split("        run: |\n", 1)[1])
        self.bash = "bash"
        if os.name == "nt":
            git = shutil.which("git")
            if git is None:
                raise RuntimeError("Git for Windows is required for the CI Bash contract")
            self.bash = str(next(
                parent / "bin/bash.exe"
                for parent in Path(git).resolve().parents
                if (parent / "bin/bash.exe").is_file()
            ))
        cmake = shutil.which("cmake")
        if cmake is None:
            raise RuntimeError("CMake/CTest are required for the CI selection contract")
        self.cmake = Path(cmake).as_posix()

    def run_fixture(self, *, missing=None, failing=None):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            lines = [
                f'add_test("{name}" "{self.cmake}" "-E" '
                f'"{"false" if name == failing else "true"}")'
                for name in CASES if name != missing
            ]
            (directory / "CTestTestfile.cmake").write_text("\n".join(lines), encoding="utf-8")
            script = self.script.replace("build/linux-debug", shlex.quote(directory.as_posix()))
            # Fixtures do not open windows. Only replace the display wrapper;
            # keep the workflow's actual shell loop, CTest executable and filters.
            script = 'xvfb-run() { shift; "$@"; }\n' + script
            return subprocess.run(
                [self.bash, "-e", "-s"], input=script, text=True, encoding="utf-8",
                capture_output=True, check=False,
            )

    def test_every_family_executes_with_real_ctest(self):
        result = self.run_fixture()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for name in CASES:
            self.assertIn(name, result.stdout)

    def test_missing_family_is_failure_instead_of_zero_test_success(self):
        for name in CASES:
            with self.subTest(missing=name):
                result = self.run_fixture(missing=name)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("No tests were found", result.stdout + result.stderr)

    def test_failed_early_family_cannot_be_hidden_by_later_success(self):
        result = self.run_fixture(failing=CASES[0])
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn(CASES[-1], result.stdout)


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    unittest.main()
