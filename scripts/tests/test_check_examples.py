"""Tests for the examples/ layout contract enforced by scripts/check_examples.py."""

import shutil
import sys
import tempfile
import unittest
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import check_examples  # noqa: E402

MAIN_NUT = "eve_init = function() {}\n"
CONFIG_NUT = "config <- { width = 800 height = 600 }\n"


def scratch_parent() -> Path:
    """A writable scratch parent, preferring the (git-ignored) build/ directory."""
    build = ROOT / "build"
    try:
        build.mkdir(parents=True, exist_ok=True)
    except OSError:
        return Path(tempfile.gettempdir())
    return build


class ExampleLayoutTests(unittest.TestCase):
    def setUp(self):
        self.root = scratch_parent() / f"check-examples-{uuid.uuid4().hex[:8]}"
        (self.root / "examples").mkdir(parents=True)
        self.index = self.root / "examples" / "README.md"

    def tearDown(self):
        shutil.rmtree(self.root, ignore_errors=True)

    def add_example(self, name, *, main=True, config=CONFIG_NUT, readme=True):
        path = self.root / "examples" / name
        path.mkdir(parents=True, exist_ok=True)
        if main:
            (path / "main.nut").write_text(MAIN_NUT, encoding="utf-8")
        if config is not None:
            (path / "config.nut").write_text(config, encoding="utf-8")
        if readme:
            (path / "README.md").write_text(f"# {name}\n\n说明\n", encoding="utf-8")
        return path

    def write_index(self, names, extra=""):
        rows = "\n".join(f"| [{name}]({name}/README.md) | 说明 |" for name in names)
        self.index.write_text(
            f"# 示例总览\n\n{extra}\n| 示例 | 演示能力 |\n|---|---|\n{rows}\n",
            encoding="utf-8",
        )

    def findings(self, exemptions=None):
        return check_examples.conformance_findings(self.root, exemptions or {})

    def assert_reports(self, needle, exemptions=None):
        findings = self.findings(exemptions)
        self.assertTrue(
            any(needle in finding for finding in findings),
            f"expected a finding containing {needle!r}, got {findings}",
        )

    def test_conforming_tree_reports_nothing(self):
        self.add_example("alpha")
        self.add_example("beta", main=False, config=None)
        self.write_index(["alpha", "beta"])
        self.assertEqual([], self.findings({"beta": "C++ plugin sample"}))

    def test_runnable_example_needs_config_nut(self):
        self.add_example("alpha", config=None)
        self.write_index(["alpha"])
        self.assert_reports("alpha/config.nut: runnable example has no config.nut")

    def test_config_nut_must_assign_the_config_table(self):
        self.add_example("alpha", config='window_title <- "alpha";\nwindow_width <- 960;\n')
        self.write_index(["alpha"])
        self.assert_reports("alpha/config.nut: does not assign the engine `config` table")

    def test_config_field_assignment_is_accepted(self):
        self.add_example("alpha", config="config.width <- 1024;\nconfig.title <- 'alpha';\n")
        self.write_index(["alpha"])
        self.assertEqual([], self.findings())

    def test_every_example_needs_a_readme(self):
        self.add_example("alpha", readme=False)
        self.write_index(["alpha"])
        self.assert_reports("alpha/README.md: missing example README")

    def test_directory_without_main_nut_must_be_exempted(self):
        self.add_example("orphan", main=False, config=None)
        self.write_index(["orphan"])
        self.assert_reports("examples/orphan/: has no main.nut")

    def test_exemption_without_reason_is_reported(self):
        self.add_example("beta", main=False, config=None)
        self.write_index(["beta"])
        self.assert_reports("NON_RUNNABLE_EXAMPLES entry 'beta' needs a reason", {"beta": "  "})

    def test_stale_exemption_is_reported(self):
        self.add_example("alpha")
        self.write_index(["alpha"])
        self.assert_reports(
            "NON_RUNNABLE_EXAMPLES entry 'ghost' has no matching examples/ directory",
            {"ghost": "removed long ago"},
        )

    def test_exemption_for_a_runnable_example_is_reported(self):
        self.add_example("alpha")
        self.write_index(["alpha"])
        self.assert_reports("entry 'alpha' is stale", {"alpha": "now runnable"})

    def test_example_missing_from_the_overview_is_reported(self):
        self.add_example("alpha")
        self.add_example("beta")
        self.write_index(["alpha"])
        self.assert_reports("examples/beta is not listed in the overview table")

    def test_dead_index_link_is_reported(self):
        self.add_example("alpha")
        self.write_index(["alpha", "ghost"])
        self.assert_reports("links examples/ghost/README.md but that directory does not exist")

    def test_missing_overview_file_is_reported(self):
        self.add_example("alpha")
        self.assert_reports("examples/README.md: missing example overview")

    def test_linked_examples_reads_only_example_readme_links(self):
        text = (
            "[Character Motion Lab](character-motion-lab/README.md)\n"
            "[docs](../Readme.md)\n"
            "[nested](tools/gizmo/README.md)\n"
            "[index](README.md)\n"
        )
        self.assertEqual(["character-motion-lab"], check_examples.linked_examples(text))

    def test_missing_index_rows_are_ready_to_paste(self):
        self.add_example("alpha")
        self.write_index([])
        self.assertEqual(
            ["| [alpha](alpha/README.md) | alpha |"],
            check_examples.missing_index_rows(self.root),
        )

    def test_repository_examples_satisfy_the_contract(self):
        self.assertEqual([], check_examples.conformance_findings(ROOT))

    def test_repository_exemptions_describe_real_directories(self):
        discovered = {example.name for example in check_examples.discover(ROOT)}
        for name, reason in check_examples.NON_RUNNABLE_EXAMPLES.items():
            self.assertIn(name, discovered)
            self.assertTrue(reason.strip(), f"{name} needs a reason")


if __name__ == "__main__":
    unittest.main()
