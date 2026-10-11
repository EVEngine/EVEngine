"""Tests for scripts/utf8_stdio.py and the convention it enforces.

The failure being guarded is Windows-local and depends on redirection: CI runs
the checkers on Linux where stdout is UTF-8, but on a Windows host a *piped*
stdout is encoded with the ANSI code page from the locale -- cp936 (GBK) on a
Chinese Windows.  A checker that prints text read back out of the tree (an
example README title containing "↔", for instance) then dies with
``UnicodeEncodeError`` instead of reporting its verdict.

Two halves are covered here:

* the helper's behaviour, including a real child process running under a forced
  CP936 stdio encoding, and
* the repository conventions -- every ``scripts/*.py`` program (a module with a
  ``__main__`` guard) calls the helper, every module that is *not* a program
  keeps its import side-effect free by not printing at all, and no script or
  script test inherits the host locale for text file I/O.  That last rule is the
  input-side twin of the stdio problem: ``Path.read_text()`` without
  ``encoding=`` decodes UTF-8 repository text as CP936 on a Chinese Windows and
  raises ``UnicodeDecodeError``.

The test-runner side (``python3 -m unittest``) is covered by the ``-X utf8``
flag on the Makefile/CI invocations, because a test module is imported by the
runner rather than being its own entry point.
"""

from __future__ import annotations

import ast
import codecs
import os
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "scripts"
sys.path.insert(0, str(SCRIPTS))

import utf8_stdio  # noqa: E402

# Text that CP936 cannot encode, so the encoding in effect is observable.
ARROW_TEXT = "2D \u2194 2.5D"
GBK = "cp936"


def have_gbk() -> bool:
    try:
        codecs.lookup(GBK)
    except LookupError:
        return False
    return True


def run_child(source: str) -> subprocess.CompletedProcess[bytes]:
    """Run ``source`` in a child interpreter whose stdio encoding is CP936."""
    env = dict(os.environ)
    env["PYTHONIOENCODING"] = GBK
    env.pop("PYTHONUTF8", None)
    parts = [str(SCRIPTS), env.get("PYTHONPATH", "")]
    env["PYTHONPATH"] = os.pathsep.join(part for part in parts if part)
    return subprocess.run(
        [sys.executable, "-c", source],
        capture_output=True,
        cwd=str(ROOT),
        env=env,
        check=False,
    )


def module_guard(tree: ast.Module) -> ast.If | None:
    """The top-level ``if __name__ == "__main__":`` block, when present."""
    for node in tree.body:
        if isinstance(node, ast.If) and "__main__" in ast.dump(node.test):
            return node
    return None


def calls_enable_utf8(node: ast.AST) -> bool:
    """True when ``node`` contains a call to ``<encoding module>.enable_utf8_stdio()``."""
    for child in ast.walk(node):
        if not isinstance(child, ast.Call):
            continue
        func = child.func
        if isinstance(func, ast.Attribute) and func.attr == "enable_utf8_stdio":
            return True
    return False


def prints_or_writes_streams(node: ast.AST) -> list[int]:
    """Line numbers of ``print(...)`` / ``sys.stdout`` / ``sys.stderr`` uses."""
    lines: list[int] = []
    for child in ast.walk(node):
        if isinstance(child, ast.Call) and isinstance(child.func, ast.Name):
            if child.func.id == "print":
                lines.append(child.lineno)
        elif isinstance(child, ast.Attribute) and child.attr in {"stdout", "stderr"}:
            if isinstance(child.value, ast.Name) and child.value.id == "sys":
                lines.append(child.lineno)
    return sorted(set(lines))


def is_binary_open(node: ast.Call) -> bool:
    """True when an ``open(...)`` call explicitly asks for a binary mode."""
    mode = ""
    for index, arg in enumerate(node.args):
        if index == 1 and isinstance(arg, ast.Constant) and isinstance(arg.value, str):
            mode = arg.value
    for keyword in node.keywords:
        if keyword.arg == "mode" and isinstance(keyword.value, ast.Constant):
            mode = str(keyword.value.value)
    return any(char in mode for char in "bx")


def text_io_without_encoding(tree: ast.Module) -> list[str]:
    """Text file I/O that would inherit the host locale encoding.

    ``Image.open`` and ``ZipFile.open`` share the name ``open`` but are not text
    file I/O, so attribute calls are ignored; a module-local ``read_text`` helper
    is not ``Path.read_text`` and is excluded as well.
    """
    local = {
        node.name
        for node in tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
    }
    found: list[str] = []
    for node in ast.walk(tree):
        if not isinstance(node, ast.Call):
            continue
        if any(keyword.arg == "encoding" for keyword in node.keywords):
            continue
        func = node.func
        if isinstance(func, ast.Attribute) and func.attr in {"read_text", "write_text"}:
            if func.attr not in local:
                found.append(f"{node.lineno}: {func.attr}() without encoding")
        elif isinstance(func, ast.Name) and func.id == "open" and func.id not in local:
            if not is_binary_open(node):
                found.append(f"{node.lineno}: open() without encoding")
    return sorted(found)


class HelperBehaviourTests(unittest.TestCase):
    def test_piped_output_is_utf8_under_a_cp936_locale(self):
        """With the guard, text CP936 cannot encode survives a CP936 stdout."""
        if not have_gbk():
            self.skipTest(f"{GBK} codec unavailable")
        source = (
            "import utf8_stdio\n"
            "utf8_stdio.enable_utf8_stdio()\n"
            f"print({ARROW_TEXT!r})\n"
        )
        result = run_child(source)
        self.assertEqual(0, result.returncode, result.stderr.decode("utf-8", "replace"))
        self.assertEqual(ARROW_TEXT, result.stdout.decode("utf-8").strip())

    def test_without_the_guard_the_same_line_aborts(self):
        """Pins the motivation: the un-guarded child dies instead of printing."""
        if not have_gbk():
            self.skipTest(f"{GBK} codec unavailable")
        result = run_child(f"print({ARROW_TEXT!r})\n")
        self.assertNotEqual(0, result.returncode)
        self.assertIn(b"UnicodeEncodeError", result.stderr)

    def test_stderr_is_covered_too(self):
        if not have_gbk():
            self.skipTest(f"{GBK} codec unavailable")
        source = (
            "import sys\n"
            "import utf8_stdio\n"
            "utf8_stdio.enable_utf8_stdio()\n"
            "print('error: ' + %r, file=sys.stderr)\n" % ARROW_TEXT
        )
        result = run_child(source)
        self.assertEqual(0, result.returncode, result.stderr.decode("utf-8", "replace"))
        self.assertIn(ARROW_TEXT, result.stderr.decode("utf-8"))

    def test_enable_is_idempotent(self):
        utf8_stdio.enable_utf8_stdio()
        utf8_stdio.enable_utf8_stdio()
        encoding = (sys.stdout.encoding or "").lower().replace("_", "-")
        self.assertTrue(encoding.startswith("utf-8"), encoding)

    def test_plain_streams_without_reconfigure_are_ignored(self):
        class Plain:
            def write(self, text):
                return len(text)

        original_out, original_err = sys.stdout, sys.stderr
        try:
            sys.stdout = Plain()  # type: ignore[assignment]
            sys.stderr = Plain()  # type: ignore[assignment]
            utf8_stdio.enable_utf8_stdio()
        finally:
            sys.stdout, sys.stderr = original_out, original_err

    def test_a_reconfigure_that_fails_is_not_fatal(self):
        class Detached:
            def reconfigure(self, **kwargs):
                raise ValueError("underlying buffer has been detached")

        original = sys.stdout
        try:
            sys.stdout = Detached()  # type: ignore[assignment]
            utf8_stdio.enable_utf8_stdio()
        finally:
            sys.stdout = original


class RepositoryConventionTests(unittest.TestCase):
    """scripts/*.py programs must guard their own stdio, and no Python code under
    scripts/ may inherit the host locale for text file I/O."""

    def script_paths(self) -> list[Path]:
        return [
            path
            for path in sorted(SCRIPTS.glob("*.py"))
            if path.name != "utf8_stdio.py"
        ]

    def python_paths(self) -> list[Path]:
        """Scripts plus their tests: every module that touches repository text."""
        return self.script_paths() + sorted((SCRIPTS / "tests").glob("*.py"))

    def test_no_module_inherits_the_locale_for_text_io(self):
        offenders: list[str] = []
        for path in self.python_paths():
            tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
            for problem in text_io_without_encoding(tree):
                offenders.append(f"{path.name} -> {problem}")
        self.assertEqual(
            [],
            offenders,
            "text file I/O must pass encoding='utf-8': on a Chinese Windows the "
            "locale default is cp936, which mangles or rejects UTF-8 repository "
            "text (an unencoded Path.read_text() there broke three tests in "
            "scripts/resource_format_suite.py)",
        )

    def test_every_script_program_enables_utf8_stdio(self):
        missing: list[str] = []
        for path in self.script_paths():
            tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
            guard = module_guard(tree)
            if guard is None:
                continue
            if not calls_enable_utf8(guard):
                missing.append(path.name)
        self.assertEqual(
            [],
            missing,
            "these scripts/*.py programs must call "
            "utf8_stdio.enable_utf8_stdio() in their __main__ block",
        )

    def test_the_guard_is_not_installed_as_an_import_side_effect(self):
        offenders: list[str] = []
        for path in self.script_paths():
            tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
            for node in tree.body:
                if node is module_guard(tree):
                    continue
                if calls_enable_utf8(node):
                    offenders.append(path.name)
        self.assertEqual(
            [],
            offenders,
            "calling utf8_stdio.enable_utf8_stdio() at import time would make "
            "importing a checker module reconfigure its importer's stdio",
        )

    def test_modules_without_an_entry_point_do_not_print(self):
        """The exemption for non-program modules has to stay justified."""
        offenders: list[str] = []
        for path in self.script_paths():
            tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
            if module_guard(tree) is not None:
                continue
            lines = prints_or_writes_streams(tree)
            if lines:
                offenders.append(f"{path.name}:{lines[0]}")
        self.assertEqual(
            [],
            offenders,
            "a scripts/*.py module that writes to stdout/stderr needs a "
            "__main__ guard plus utf8_stdio.enable_utf8_stdio()",
        )


if __name__ == "__main__":
    unittest.main()
