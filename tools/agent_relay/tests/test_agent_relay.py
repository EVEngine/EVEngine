import json
import tempfile
import unittest
from pathlib import Path

import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from agent_relay import Adapter, build_prompt, load_adapters, parse_state  # noqa: E402


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


if __name__ == "__main__":
    unittest.main()
