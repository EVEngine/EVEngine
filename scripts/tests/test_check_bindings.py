"""Generated signatures must cover the correct binding source, not names alone."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import check_bindings


class GeneratedCoverageTests(unittest.TestCase):
    def test_same_method_in_another_module_does_not_cover_binding(self):
        own = check_bindings.ROOT / "src/modules/window/Window.cpp"
        other = {"src/modules/ui/UI.cpp": {"getName"}}
        self.assertEqual(check_bindings.generated_names([own], other), set())

    def test_only_selected_module_sources_count(self):
        own = check_bindings.ROOT / "src/modules/window/Window.cpp"
        contracts = {"src/modules/window/Window.cpp": {"getWidth", "getHeight"},
                     "src/modules/ui/UI.cpp": {"create"}}
        self.assertEqual(check_bindings.generated_names([own], contracts),
                         {"getWidth", "getHeight"})

    def test_empty_module_is_not_covered(self):
        self.assertEqual(check_bindings.generated_names([], {}), set())
