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


class ReverseDirectionTests(unittest.TestCase):
    """The reverse check only counts member calls in Squirrel fences as promises.

    Chapters use `` `name()` `` inline code for prose mentions of C++-only APIs and for
    helpers a sample defines for itself, so counting every mention produced 221 names
    with one actionable item; these tests pin what is counted instead.
    """

    def test_squirrel_fences_exclude_other_languages(self):
        text = "prose `foo()`\n```nut\nx.bar()\n```\n```cpp\ny.baz()\n```\n"
        code = checker.squirrel_fences(text)
        self.assertIn("bar", code)
        self.assertNotIn("baz", code)

    def test_cpp_fence_between_nut_fences_does_not_leak_prose(self):
        # A ```cpp block's closing fence used to be read as an unlabelled opening one, so
        # the prose that followed it was captured as script code (this is how editor.md's
        # mention of `session.constraints()` was reported as a script promise).
        text = (
            "```nut\nx.a()\n```\n"
            "prose mentioning y.leak()\n"
            "```cpp\nz.b()\n```\n"
            "```nut\nw.c()\n```\n"
        )
        code = checker.squirrel_fences(text)
        self.assertIn("a(", code)
        self.assertIn("c(", code)
        self.assertNotIn("leak", code)
        self.assertNotIn("b(", code)

    def test_commented_out_calls_are_not_promises(self):
        text = "```nut\n// x.commented()\nx.live()\n```\n"
        code = checker.squirrel_fences(text)
        self.assertIn("live", code)
        self.assertNotIn("commented", code)

    def test_only_lowercase_instance_receivers_are_promises(self):
        matches = checker.DOC_MEMBER_CALL_RE.findall("a.method()\nEve.Other()\n")
        # `Eve.Other()` is a class/factory access, not an instance method call.
        self.assertEqual(matches, [("a", "method")])
        # ...and the caller drops the `eve` root table plus Squirrel's own methods.
        self.assertIn("tostring", checker.SQUIRREL_BUILTINS)

    def test_raw_closure_binding_is_recognised(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Bindings.cpp"
            path.write_text(
                'sq_pushstring(vm, "streamAround", -1);\nsq_newclosure(vm, streamAround, 0);\n',
                encoding="utf-8",
            )
            self.assertIn("streamAround", checker.bound_names(Path(directory), set()))

    def test_pushstring_without_a_closure_is_not_a_binding(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Bindings.cpp"
            path.write_text(
                'sq_pushstring(vm, "notAMember", -1);\nsq_throwerror(vm, "boom");\n',
                encoding="utf-8",
            )
            self.assertNotIn("notAMember", checker.bound_names(Path(directory), set()))

    def test_repository_reverse_list_is_short_enough_to_read(self):
        # 221 -> 10 after the script-definition, nested-dir, engine-core and raw-closure
        # scans. The remainder is a named list a maintainer can triage by hand.
        self.assertLessEqual(len(checker.doc_only_names()), 20)


class GeneratedCoverageTests(unittest.TestCase):
    """The generated SDK catalog covers a binding, but only from its own source file.

    A catalog entry counts only for the file it was extracted from, so a same-named
    method in another module cannot hide a missing binding here.
    """

    def test_same_method_in_another_module_does_not_cover_binding(self):
        own = checker.ROOT / "src" / "modules" / "window" / "Window.cpp"
        other = {"src/modules/ui/UI.cpp": {"getName"}}
        self.assertEqual(checker.generated_names([own], other), set())

    def test_only_selected_module_sources_count(self):
        own = checker.ROOT / "src" / "modules" / "window" / "Window.cpp"
        contracts = {
            "src/modules/window/Window.cpp": {"getWidth", "getHeight"},
            "src/modules/ui/UI.cpp": {"create"},
        }
        self.assertEqual(checker.generated_names([own], contracts), {"getWidth", "getHeight"})

    def test_empty_module_is_not_covered(self):
        self.assertEqual(checker.generated_names([], {}), set())


if __name__ == "__main__":
    unittest.main()
