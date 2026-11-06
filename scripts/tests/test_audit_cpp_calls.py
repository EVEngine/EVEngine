"""Regression coverage for candidate masking and semantic coverage accounting."""

import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import audit_cpp_calls as audit  # noqa: E402


class CallAuditTests(unittest.TestCase):
    def test_diagnostics_keep_only_owned_locations_and_check_names(self):
        root = Path(__file__).resolve().parents[2]
        owned = root / "src" / "example.cpp"
        external = root / "third-party" / "example.h"
        output = (
            f"{owned}:12:7: warning: copy is unnecessary [performance-unnecessary-copy-initialization]\n"
            f"{external}:2:1: warning: external warning [performance-foo]\n"
            f"{owned}:13:2: note: previous declaration\n"
        )
        findings = audit.parse_diagnostics(output, root)
        self.assertEqual(len(findings), 1)
        self.assertEqual(findings[0]["file"], "src/example.cpp")
        self.assertEqual(findings[0]["line"], 12)
        self.assertEqual(findings[0]["rule"], "performance-unnecessary-copy-initialization")

    def test_literals_comments_and_move_member_are_not_candidates(self):
        source = """// return std::move(x);
/* for (auto x : things) */
auto s = R"tag(return std::move(x);)tag";
auto t = "return std::move(x);";
return std::move(result).takeValue();
return std::move(local);
"""
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = root / "sample.cpp"
            path.write_text(source, encoding="utf-8")
            findings = audit.scan(path, root)
        self.assertEqual([(f["rule"], f["line"]) for f in findings], [("return-move", 6)])

    def test_reference_loops_and_move_push_are_not_candidates(self):
        source = """for (const auto &x : xs) {}
for (auto &&x : xs) {}
for (auto x : xs) {}
for (const auto [a, b] : xs) {}
xs.push_back(std::move(x));
xs.push_back(Widget(1));
"""
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = root / "sample.cpp"
            path.write_text(source, encoding="utf-8")
            findings = audit.scan(path, root)
        self.assertEqual(
            [f["rule"] for f in findings], ["range-value", "range-value", "push-temporary"]
        )

    def test_missing_compilation_unit_is_reported(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "compile_commands.json").write_text("[]", encoding="utf-8")
            with patch.object(audit.subprocess, "run") as run:
                report = audit.run_tidy(root, [root / "a.cpp"], root, "clang-tidy")
            run.assert_not_called()
        self.assertEqual(report["missing_units"], ["a.cpp"])
        self.assertEqual(report["analyzed_units"], 0)

    def test_compiler_failure_remains_visible(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "compile_commands.json").write_text(
                json.dumps([{"file": "a.cpp", "directory": str(root)}]), encoding="utf-8"
            )
            with patch.object(audit.subprocess, "run") as run:
                run.return_value.returncode = 1
                run.return_value.stdout = "parse error"
                run.return_value.stderr = ""
                report = audit.run_tidy(root, [root / "a.cpp"], root, "clang-tidy")
        self.assertEqual(report["failed_units"], ["a.cpp"])

    def test_msvc_analysis_preserves_native_header_order_without_editing_build_database(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            original = json.dumps(
                [
                    {
                        "file": "a.cpp",
                        "directory": str(root),
                        "command": "cmake/msvc-cl.cmd -Ivendor -Isdk /external:Ivendor /WX -c a.cpp",
                    }
                ]
            )
            database = root / "compile_commands.json"
            database.write_text(original, encoding="utf-8")
            with patch.object(audit.subprocess, "run") as run:
                run.return_value.returncode = 0
                run.return_value.stdout = run.return_value.stderr = ""
                report = audit.run_tidy(root, [root / "a.cpp"], root, "clang-tidy")
            derived = Path(report["effective_database"]) / "compile_commands.json"
            normalized = json.loads(derived.read_text(encoding="utf-8"))[0]["command"]
            self.assertNotIn("/external:I", normalized)
            self.assertLess(normalized.index("-Ivendor"), normalized.index("-Isdk"))
            self.assertEqual(database.read_text(encoding="utf-8"), original)
            arguments = run.call_args.args[0]
            self.assertIn("--extra-arg-before=--driver-mode=cl", arguments)
            self.assertIn("--extra-arg=/WX-", arguments)

    def test_failed_unit_cannot_verify_a_header_diagnostic(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "compile_commands.json").write_text(
                json.dumps([{"file": "a.cpp", "directory": str(root)}]), encoding="utf-8"
            )
            output = f"{root / 'src' / 'a.h'}:1:2: warning: unnecessary copy [performance-for-range-copy]\n"
            with patch.object(audit.subprocess, "run") as run:
                run.return_value.returncode = 1
                run.return_value.stdout, run.return_value.stderr = output, ""
                report = audit.run_tidy(root, [root / "a.cpp"], root, "clang-tidy")
            self.assertEqual(report["findings"][0]["verified_translation_units"], [])

    def test_compiled_only_reports_exclusions_without_claiming_them_analyzed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "compile_commands.json").write_text("[]", encoding="utf-8")
            report = audit.run_tidy(
                root, [root / "other_backend.cpp"], root, "clang-tidy", compiled_only=True
            )
            self.assertEqual(report["analyzed_units"], 0)
            self.assertEqual(report["excluded_units"], ["other_backend.cpp"])
            self.assertEqual(report["missing_units"], [])
