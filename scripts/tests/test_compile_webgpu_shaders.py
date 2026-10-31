"""Failure propagation for the optional GLSL-to-WGSL build step."""

import contextlib
import io
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import compile_webgpu_shaders  # noqa: E402


class CompileWebgpuShadersTests(unittest.TestCase):
    def run_with_failure(self, failure, successful_calls=()):
        scratch = ROOT / "build" / "webgpu-shader-tests"
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as directory, patch.object(tempfile, "tempdir", str(scratch)):
            shader = Path(directory) / "broken.frag"
            shader.write_text("#version 450\nvoid main() {}\n", encoding="utf-8")
            stderr = io.StringIO()
            argv = ["compile_webgpu_shaders.py", "--shaders-dir", directory,
                    "--glslc", "fake-glslc", "--tint", "fake-tint"]
            with patch.object(sys, "argv", argv), patch(
                "compile_webgpu_shaders.subprocess.run",
                side_effect=[*successful_calls, failure],
            ), contextlib.redirect_stderr(stderr):
                result = compile_webgpu_shaders.main()
            self.assertEqual(result, 1)
            self.assertFalse(shader.with_suffix(".frag_wgsl.inc").exists())
            return stderr.getvalue()

    def test_glsl_compiler_failure_is_fatal_and_reports_stderr(self):
        failure = subprocess.CalledProcessError(1, ["fake-glslc"], stderr=b"GLSL parse error on line 2")
        error = self.run_with_failure(failure)
        self.assertIn("broken.frag", error)
        self.assertIn("GLSL parse error on line 2", error)

    def test_tint_failure_is_fatal_and_reports_stderr(self):
        success = subprocess.CompletedProcess(["fake-glslc"], 0)
        failure = subprocess.CalledProcessError(1, ["fake-tint"], stderr=b"WGSL validation failed")
        error = self.run_with_failure(failure, [success])
        self.assertIn("broken.frag", error)
        self.assertIn("WGSL validation failed", error)


if __name__ == "__main__":
    unittest.main()
