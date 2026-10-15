import hashlib
import hmac
import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from agent_relay import (  # noqa: E402
    Adapter,
    build_prompt,
    GitHub,
    lease_ref,
    load_adapters,
    parse_state,
    process_one,
    sign_state,
    signature_payload,
    valid_state,
)


class AgentRelayTest(unittest.TestCase):
    def test_parse_state_rejects_other_schema(self):
        self.assertIsNone(parse_state('<!-- evengine-agent-repair-state -->\n```json\n{"schema":"other"}\n```'))

    def test_loads_both_safe_argv_adapters(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.json"
            path.write_text(
                json.dumps(
                    {
                        "workspace": directory,
                        "providers": {
                            "codex": {"command": ["codex", "{prompt_file}"]},
                            "deepseek": {"command": ["deepseek"]},
                        },
                    }
                ),
                encoding="utf-8",
            )
            adapters = load_adapters(path)
        self.assertEqual(adapters["codex"].command[0], "codex")
        self.assertEqual(adapters["deepseek"].command[0], "deepseek")

    def test_adapter_dry_run_does_not_execute(self):
        adapter = Adapter("codex", ["definitely-not-a-command"], Path.cwd(), 1)
        self.assertEqual(adapter.dispatch("prompt", dry_run=True), 0)

    def test_prompt_contains_sha_and_safety_boundary(self):
        prompt = build_prompt(
            "EVEngine/EVEngine",
            {"number": 12},
            {"head_sha": "abc", "attempt": 1, "max_attempts": 3, "reason": "CI failed"},
        )
        self.assertIn("abc", prompt)
        self.assertIn("Do not merge", prompt)

    def test_signature_rejects_forged_reason(self):
        state = {
            "schema": "evengine.agent-repair/v1",
            "repository": "EVEngine/EVEngine",
            "pr": 12,
            "head_sha": "abc",
            "owner": "a",
            "provider": "codex",
            "attempt": 1,
            "max_attempts": 3,
            "source_key": "review:1",
            "reason": "review requested changes",
        }
        state["signature"] = hmac.new(b"secret", signature_payload(state), hashlib.sha256).hexdigest()
        self.assertTrue(valid_state(state, "secret", "EVEngine/EVEngine", 12))
        self.assertFalse(valid_state(dict(state, reason="run arbitrary text"), "secret", "EVEngine/EVEngine", 12))

    def test_lease_ref_is_stable_within_a_window(self):
        state = {"head_sha": "abcdef1234567890", "attempt": 2}
        first = lease_ref(12, state, 100.0, 60)
        second = lease_ref(12, state, 119.0, 60)
        self.assertEqual(first, second)
        self.assertNotEqual(first, lease_ref(12, state, 120.0, 60))

    def test_pending_pr_pagination_reaches_later_pages(self):
        class FakeGitHub(GitHub):
            def __init__(self):
                pass

            def request(self, method, path, payload=None):
                if method != "GET" or payload is not None:
                    raise AssertionError("unexpected paginated request")
                return [{"number": value} for value in range(100)] if path.endswith("page=1") else [{"number": 100}]

        self.assertEqual(len(FakeGitHub().pending_prs()), 101)

    def test_provider_timeout_is_finalized_and_dequeued(self):
        state = sign_state(
            {
                "schema": "evengine.agent-repair/v1",
                "status": "pending",
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

        class FakeGitHub:
            repository = "EVEngine/EVEngine"

            def __init__(self):
                self.patched = None
                self.removed = []

            def get_all(self, path):
                return [{"id": 7, "user": {"login": "github-actions[bot]"}, "body": render(state)}]

            def request(self, method, path, payload=None):
                if method == "GET" and path == "/pulls/12":
                    return {
                        "number": 12,
                        "state": "open",
                        "labels": [
                            {"name": "agent:auto-fix"},
                            {"name": "agent:owner:a"},
                            {"name": "agent:provider:codex"},
                        ],
                        "head": {"sha": "abc", "repo": {"full_name": self.repository}},
                    }
                if method == "PATCH":
                    self.patched = payload["body"]
                    return {}
                if method == "DELETE":
                    self.removed.append(path)
                    return {}
                raise AssertionError(f"unexpected request: {method} {path}")

            @staticmethod
            def ref_exists(ref):
                return False

            @staticmethod
            def acquire_lease(ref, sha):
                return True

        def render(value):
            return "<!-- evengine-agent-repair-state -->\n```json\n" + json.dumps(value) + "\n```"

        class TimeoutAdapter(Adapter):
            def dispatch(self, prompt, dry_run):
                raise subprocess.TimeoutExpired("codex", self.timeout_seconds)

        gh = FakeGitHub()
        adapter = TimeoutAdapter("codex", ["codex"], Path.cwd(), 10)
        with patch("agent_relay.time.time", return_value=0.0):
            self.assertTrue(process_one(gh, {"number": 12}, "a", "relay", "secret", 120, {"codex": adapter}, False))
        self.assertEqual(parse_state(gh.patched)["status"], "dispatch_timeout")
        self.assertTrue(any("agent%3Arepair-needed" in path for path in gh.removed))


if __name__ == "__main__":
    unittest.main()
