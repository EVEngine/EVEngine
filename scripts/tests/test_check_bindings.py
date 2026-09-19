"""Unit tests for scripts/check_bindings.py's script-side definition scan.

The reverse direction of the binding check ("the doc names something that does not
exist") compares doc names against the bound surface. A large part of that surface is
defined with Squirrel syntax rather than `addFunc` -- bootstrap chunks inside C++ raw
strings, plus the scripts shipped in src/scripts -- and before this scan existed the
check reported 221 such names as drift. These tests pin the syntaxes it recognises.
"""

import importlib.util
import re
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

SPEC = importlib.util.spec_from_file_location(
    "check_bindings", ROOT / "scripts" / "check_bindings.py"
)
checker = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(checker)


class ScriptDefinedNamesTests(unittest.TestCase):
    def scan(self, source: str) -> set[str]:
        with tempfile.TemporaryDirectory() as directory:
            module_dir = Path(directory)
            (module_dir / "Bindings.cpp").write_text(source, encoding="utf-8")
            return checker.script_defined_names(module_dir, set())

    def test_function_member_is_recognised(self):
        # The shape used by the UIComponent / SceneEntity bootstrap chunks.
        source = "const char* kChunk = R\"SQ(\n    function ui() { return _ui }\n)SQ\";\n"
        self.assertIn("ui", self.scan(source))

    def test_slot_assignment_is_recognised(self):
        source = 'table["getNodeRef"] <- function(nodeId) { return nodeId }\n'
        self.assertIn("getNodeRef", self.scan(source))

    def test_variable_function_and_class_are_recognised(self):
        source = "eve.SceneEntity <- class extends eve.Entity {\n}\nhelper <- function() {}\n"
        names = self.scan(source)
        self.assertIn("SceneEntity", names)
        self.assertIn("helper", names)

    def test_addclass_is_recognised(self):
        source = 'auto cls = table.addClass<Sprite>("Sprite2D", factory, false);\n'
        self.assertIn("Sprite2D", self.scan(source))

    def test_plain_addfunc_is_not_a_script_definition(self):
        # addFunc names belong to bound_names(); the script scan must not need them.
        # (The scan also walks the shipped scripts, so assert on the name, not on
        # the whole set being empty.)
        self.assertNotIn("setBlend", self.scan('cls.addFunc("setBlend", &setBlend);\n'))

    def test_header_chunks_are_scanned(self):
        with tempfile.TemporaryDirectory() as directory:
            module_dir = Path(directory)
            (module_dir / "Bindings.h").write_text(
                "// function headerOnlyMember() {}\n", encoding="utf-8"
            )
            self.assertIn("headerOnlyMember", checker.script_defined_names(module_dir, set()))

    def test_shipped_scripts_are_scanned(self):
        # `asyncPost` is documented in thread.md but defined in src/scripts/async.nut,
        # so the scan has to reach outside the module directory.
        names = checker.script_defined_names(ROOT / "src" / "modules" / "thread", set())
        self.assertIn("asyncPost", names)

    def test_repository_reverse_check_stays_below_the_old_noise_floor(self):
        # Regression guard for the reason this scan exists: it was 221 before.
        doc_only = 0
        for module, directory, skip in checker.iter_modules():
            doc = checker.doc_for(module)
            if doc is None:
                continue
            names = checker.bound_names(directory, skip) | checker.script_defined_names(directory, skip)
            text = checker.doc_text(doc)
            doc_names = set(checker.ADDFUNC_RE.findall(text)) | {
                m for m in re.findall(r"`([a-zA-Z][a-zA-Z0-9_]*)\(\)", text)
            }
            doc_only += len(doc_names - names)
        self.assertLess(doc_only, 200)


if __name__ == "__main__":
    unittest.main()
