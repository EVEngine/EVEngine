"""External shader compiler failures retain process status even without stderr."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
COMPILER = shutil.which("clang++") or shutil.which("g++")


@unittest.skipUnless(COMPILER and os.name != "nt", "requires POSIX C++ compiler")
class GlslCompilerSubprocessTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.root = Path(cls.temporary.name)
        source = cls.root / "probe.cpp"
        source.write_text('''#include "graphics/vulkan/GlslCompiler.h"
#include <cstdio>
#include <exception>
int main() {
    try {
        const auto words = eve::graphics::compileGlslToSpirv("void main() {}",
            eve::graphics::GlslStage::eVertex, "probe.vert");
        return words.size() == 1 ? 0 : 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s", error.what());
        return 1;
    }
}
''', encoding="utf-8")
        cls.binary = cls.root / "probe"
        subprocess.run([COMPILER, "-std=c++20", "-I", str(ROOT / "src/modules"),
                        "-I", str(ROOT / "src/engine"), str(source),
                        str(ROOT / "src/modules/graphics/vulkan/GlslCompiler.cpp"), "-o", str(cls.binary)],
                       capture_output=True, encoding="utf-8", check=True)

    def compile_with(self, body):
        tool = self.root / "glslc"
        tool.write_text(f"#!{sys.executable}\n" + body, encoding="utf-8")
        tool.chmod(0o755)
        return subprocess.run([str(self.binary)], env=dict(os.environ, PATH=str(self.root)),
                              capture_output=True, encoding="utf-8", timeout=10, check=False)

    def test_successful_compiler_output_is_accepted(self):
        result = self.compile_with('import sys\nfrom pathlib import Path\n'
            'Path(sys.argv[sys.argv.index("-o") + 1]).write_bytes(bytes.fromhex("03022307"))\n')
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_empty_diagnostic_still_reports_exit_code(self):
        result = self.compile_with('raise SystemExit(7)\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("probe.vert (exit 7)", result.stderr)

    def test_interrupted_compiler_reports_signal(self):
        result = self.compile_with('import os, signal\nos.kill(os.getpid(), signal.SIGTERM)\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("probe.vert (signal 15)", result.stderr)

    def test_compiler_stderr_is_preserved(self):
        result = self.compile_with('import sys\nprint("invalid shader", file=sys.stderr)\nraise SystemExit(1)\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("exit 1", result.stderr)
        self.assertIn("invalid shader", result.stderr)
