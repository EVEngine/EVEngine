import unittest
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from agent_repair_router import (  # noqa: E402
    decide,
    GitHub,
    label_value,
    next_attempt,
    parse_state,
    sign_state,
    valid_state,
)


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

    def test_signed_state_is_bound_to_repository_pr_and_payload(self):
        state = sign_state(
            {
                "schema": "evengine.agent-repair/v1",
                "repository": "EVEngine/EVEngine",
                "pr": 12,
                "head_sha": "abc",
                "owner": "a",
                "provider": "codex",
                "attempt": 1,
                "max_attempts": 3,
                "source_key": "workflow_run:42",
                "reason": "CI failed",
            },
            "secret",
        )
        self.assertTrue(valid_state(state, "secret", "EVEngine/EVEngine", 12))
        self.assertFalse(valid_state(dict(state, reason="forged"), "secret", "EVEngine/EVEngine", 12))
        self.assertFalse(valid_state(state, "secret", "other/repo", 12))

    def test_comment_pagination_reaches_later_pages(self):
        class FakeGitHub(GitHub):
            def __init__(self):
                pass

            def request(self, method, path, payload=None):
                self.assert_request(method, payload)
                return [{"id": value} for value in range(100)] if path.endswith("page=1") else [{"id": 100}]

            @staticmethod
            def assert_request(method, payload):
                if method != "GET" or payload is not None:
                    raise AssertionError("unexpected paginated request")

        self.assertEqual(len(FakeGitHub().get_all("/issues/12/comments")), 101)


if __name__ == "__main__":
    unittest.main()
