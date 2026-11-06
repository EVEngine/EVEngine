"""Review order and stale-review protection for the C++ audit queue."""

from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import rank_cpp_call_audit as ranker  # noqa: E402


class PriorityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        (self.root / "src").mkdir()
        (self.root / "src/test.cpp").write_text("void work() {}\n", encoding="utf-8")

    def finding(self, rule, message, line=1, verified=True):
        return dict(
            file="src/test.cpp",
            line=line,
            column=1,
            rule=rule,
            message=message,
            verified_translation_units=["src/test.cpp"] if verified else [],
        )

    def report(self, findings):
        return {
            "clang_tidy": dict(
                findings=findings, analyzed_units=1, failed_units=[], missing_units=[]
            )
        }

    def test_container_precedes_style_and_unverified_copy(self):
        rows = ranker.rank_report(
            self.report(
                [
                    self.finding("performance-enum-size", "enum could be smaller"),
                    self.finding(
                        "performance-unnecessary-value-param", "std::vector<Large> copied", 2
                    ),
                    self.finding(
                        "performance-unnecessary-copy-initialization",
                        "std::vector<Large> copied",
                        3,
                        False,
                    ),
                ]
            ),
            self.root,
        )["queue"]
        self.assertEqual([row["line"] for row in rows], [2, 1, 3])
        self.assertEqual(rows[-1]["priority_score"], 0)
        self.assertTrue(all(row["benefit_evidence"] == "unmeasured" for row in rows))

    def test_callable_returning_vector_is_not_a_container_copy(self):
        row = ranker.rank_report(
            self.report(
                [
                    self.finding(
                        "performance-unnecessary-value-param",
                        "std::function<std::vector<Large>()> copied",
                    )
                ]
            ),
            self.root,
        )["queue"][0]
        self.assertEqual(row["priority_score"], 55)

    def test_shared_handle_does_not_claim_deep_copy(self):
        row = ranker.rank_report(
            self.report(
                [
                    self.finding(
                        "performance-unnecessary-copy-initialization",
                        "Poco::SharedPtr<Large> copied",
                    )
                ]
            ),
            self.root,
        )["queue"][0]
        self.assertEqual(row["priority_score"], 15)

    def test_hot_path_requires_source_or_directory_declaration(self):
        finding = self.finding("performance-unnecessary-copy-initialization", "Thing copied")
        baseline = ranker.rank_report(self.report([finding]), self.root)["queue"][0]
        self.assertEqual(baseline["frequency_evidence"], "unknown")
        (self.root / "src/.eve-hot-path").write_text(
            "eve.hot-path-directory/v1:*.cpp\n", encoding="utf-8"
        )
        hot = ranker.rank_report(self.report([finding]), self.root)["queue"][0]
        self.assertEqual(hot["priority_score"], baseline["priority_score"] + 15)
        self.assertEqual(hot["frequency_evidence"], "declared-hot-path")

    def test_changed_source_reopens_retained_review(self):
        finding = self.finding("performance-unnecessary-value-param", "std::vector<Large> copied")
        row = ranker.rank_report(self.report([finding]), self.root)["queue"][0]
        decision = dict(
            finding_id=row["finding_id"],
            source_sha256=row["source_sha256"],
            status="retained",
            reason="Caller moves; no element copy",
        )
        reviewed = ranker.rank_report(self.report([finding]), self.root, [decision])
        self.assertEqual(reviewed["counts"], {"retained": 1})
        (self.root / "src/test.cpp").write_text("void work() { changed(); }\n", encoding="utf-8")
        reopened = ranker.rank_report(self.report([finding]), self.root, [decision])
        self.assertEqual(reopened["counts"], {"unreviewed": 1})

    def test_outdated_location_is_unmatched_and_duplicate_decisions_fail(self):
        finding = self.finding("performance-unnecessary-value-param", "std::vector<Large> copied")
        row = ranker.rank_report(self.report([finding]), self.root)["queue"][0]
        decision = dict(
            finding_id=row["finding_id"],
            source_sha256=row["source_sha256"],
            status="retained",
            reason="Reviewed",
        )
        finding["line"] = 2
        report = ranker.rank_report(self.report([finding]), self.root, [decision])
        self.assertEqual(report["unmatched_decisions"], [decision["finding_id"]])
        with self.assertRaises(ValueError):
            ranker.rank_report(self.report([finding]), self.root, [decision, decision])


if __name__ == "__main__":
    unittest.main()
