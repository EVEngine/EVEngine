"""Tests for the script binding arity scanner.

The contract: a script binding cannot honour a C++ default argument, so each binding that
relies on the pass-every-argument convention must be recorded. These tests pin the scanner
itself (including the false positive that a same-named method on another class would cause)
and the repository's frozen baseline.
"""

from __future__ import annotations

import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import script_binding_arity as arity

HEADER_WITH_DEFAULT = """
class Foo {
public:
    void bar(int a, int b = 1);
};
"""

HEADER_WITHOUT_DEFAULT = """
class Foo {
public:
    void bar(int a, int b);
};
"""

HEADER_SAME_NAME_ELSEWHERE = """
class Foo {
public:
    void bar(int a, int b = 1);
};

class Baz {
public:
    void bar(int a, int b);
};
"""

CPP_BINDING = """
void expose(ssq::Class& cls) {
    cls.addFunc("bar", &Foo::bar);
}
"""


class ScanTest(unittest.TestCase):
    def scan(self, header: str, cpp: str = CPP_BINDING) -> list[arity.Offender]:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "src").mkdir()
            (root / "src" / "Foo.h").write_text(header, encoding="utf-8")
            (root / "src" / "Foo.cpp").write_text(cpp, encoding="utf-8")
            return arity.scan(root)

    def test_default_argument_is_reported(self) -> None:
        offenders = self.scan(HEADER_WITH_DEFAULT)
        self.assertEqual(len(offenders), 1)
        self.assertEqual(offenders[0].class_name, "Foo")
        self.assertEqual(offenders[0].method, "bar")

    def test_no_default_argument_is_clean(self) -> None:
        self.assertEqual(self.scan(HEADER_WITHOUT_DEFAULT), [])

    def test_same_named_method_on_another_class_is_not_reported(self) -> None:
        # `Baz::bar` has no default, so binding `Foo::bar` against it would be a false
        # positive -- the scanner tracks the enclosing class for exactly this reason.
        cpp = """
void expose(ssq::Class& cls) {
    cls.addFunc("bar", &Baz::bar);
}
"""
        self.assertEqual(self.scan(HEADER_SAME_NAME_ELSEWHERE, cpp), [])

    def test_definition_style_declaration_is_matched(self) -> None:
        header = """
class Foo {
public:
    void bar(int a, int b = 1) {}
};
"""
        self.assertEqual(len(self.scan(header)), 1)


class BaselineTest(unittest.TestCase):
    def test_repository_is_clean_against_its_baseline(self) -> None:
        offenders = arity.scan(arity.REPO)
        entries = [offender.key for offender in offenders]
        baseline = arity.load_baseline()
        new, resolved = arity.compare(entries, baseline)
        self.assertEqual(
            new,
            [],
            "new script bindings on a C++ declaration with a default argument: "
            "either pass every argument from script, add a script-side wrapper, or freeze "
            "the binding with scripts/script_binding_arity.py --write-baseline",
        )
        self.assertEqual(resolved, [], "baseline entries no longer apply; rerun --write-baseline")

    def test_baseline_is_sorted_and_unique(self) -> None:
        baseline = arity.load_baseline()
        self.assertEqual(baseline, sorted(set(baseline)))
        self.assertTrue(baseline, "the repository has bindings that depend on the convention")

    def test_baseline_file_is_valid_json(self) -> None:
        data = json.loads(io.open(arity.BASELINE, encoding="utf-8").read())
        self.assertEqual(data["schema_version"], 1)
        self.assertIsInstance(data["entries"], list)


if __name__ == "__main__":
    unittest.main()
