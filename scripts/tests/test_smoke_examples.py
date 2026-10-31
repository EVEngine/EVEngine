"""Exercise startup and gameplay deadlines with controlled engine processes."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which("bash") and os.name != "nt", "requires POSIX Bash processes")
class SmokeExamplesTests(unittest.TestCase):
    def run_smoke(self, body, *, startup=5):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "scripts").mkdir()
            (root / "examples" / "probe").mkdir(parents=True)
            (root / "examples" / "probe" / "main.nut").write_text("", encoding="utf-8")
            script = root / "scripts" / "smoke_examples.sh"
            shutil.copyfile(ROOT / "scripts" / "smoke_examples.sh", script)
            engine = root / "engine"
            engine.write_text(f"#!{sys.executable}\nimport time\n" + body, encoding="utf-8")
            engine.chmod(0o755)
            env = dict(os.environ, EVE=str(engine), STARTUP_SECONDS=str(startup),
                       RUN_SECONDS="1", MIN_RUN_SECONDS="1", SMOKE_LOG_DIR=str(root / "logs"))
            result = subprocess.run([shutil.which("bash"), str(script), "probe"], env=env,
                                    capture_output=True, encoding="utf-8", timeout=15, check=False)
            log = (root / "logs" / "probe.log").read_text(encoding="utf-8")
            return result, log

    def test_slow_initialization_gets_its_own_budget(self):
        result, log = self.run_smoke(
            'time.sleep(2)\nprint("[startup] first present", flush=True)\ntime.sleep(20)\n')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("first present", log)

    def test_stuck_initialization_fails_even_while_alive(self):
        result, _ = self.run_smoke('time.sleep(20)\n', startup=1)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("did not finish initialization", result.stdout)

    def test_early_clean_exit_is_a_failure(self):
        result, _ = self.run_smoke('print("[startup] first present", flush=True)\ntime.sleep(0.2)\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("liveness window", result.stdout)

    def test_initialization_error_is_not_hidden_by_liveness(self):
        result, log = self.run_smoke(
            'print("eve_init failed: broken shader", flush=True)\n'
            'print("[startup] first present", flush=True)\ntime.sleep(20)\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("eve_init failed", result.stdout)
        self.assertIn("broken shader", log)

    def test_entering_update_loop_without_present_is_not_ready(self):
        result, _ = self.run_smoke(
            'print("[startup] first frame begins", flush=True)\ntime.sleep(20)\n', startup=1)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("did not finish initialization", result.stdout)
