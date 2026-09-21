import unittest
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from agent_repair_router import decide, label_value, next_attempt, parse_state  # noqa: E402


def pr(repo="EVEngine/EVEngine", sha="abc", labels=None):
    return {
        "state": "open",
        "head": {"sha": sha, "repo": {"full_name": repo}},
        "labels": [{"name": label} for label in (labels or [])],
    }


class AgentRepairRouterTest(unittest.TestCase):
    def test_fork_is_rejected_even_when_opted_in(self):
        result = decide(
            "workflow_run",
            {"workflow_run": {"name": "CI", "event": "pull_request", "conclusion": "failure", "head_sha": "abc"}},
            pr("someone/fork", labels=["agent:auto-fix", "agent:owner:a", "agent:provider:codex"]),
            "EVEngine/EVEngine",
        )
        self.assertFalse(result.eligible)
        self.assertIn("fork", result.reason)

    def test_stale_run_is_rejected(self):
        result = decide(
            "workflow_run",
            {"workflow_run": {"name": "CI", "event": "pull_request", "conclusion": "failure", "head_sha": "old"}},
            pr(labels=["agent:auto-fix", "agent:owner:a", "agent:provider:codex"]),
            "EVEngine/EVEngine",
        )
        self.assertFalse(result.eligible)
        self.assertIn("stale", result.reason)

    def test_failed_ci_is_eligible_with_complete_opt_in(self):
        result = decide(
            "workflow_run",
            {"workflow_run": {"id": 42, "name": "CI", "event": "pull_request", "conclusion": "failure", "head_sha": "abc"}},
            pr(labels=["agent:auto-fix", "agent:owner:a", "agent:provider:deepseek"]),
            "EVEngine/EVEngine",
        )
        self.assertTrue(result.eligible)
        self.assertEqual(result.source_key, "workflow_run:42")

    def test_ambiguous_owner_is_rejected(self):
        labels = {"agent:owner:a", "agent:owner:b"}
        self.assertIsNone(label_value(labels, "agent:owner:"))

    def test_invalid_state_comment_is_ignored(self):
        self.assertIsNone(parse_state("ordinary comment"))

    def test_attempt_limit_is_bounded(self):
        self.assertEqual(next_attempt(2, 3), (3, "pending"))
        self.assertEqual(next_attempt(3, 3), (3, "exhausted"))


if __name__ == "__main__":
    unittest.main()
