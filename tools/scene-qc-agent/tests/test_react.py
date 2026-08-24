"""ReAct 解析与多轮迭代硬上限测试。"""

import unittest

from scene_qc_agent.react import parse_react, SceneQCBrain
from scene_qc_agent.report import SceneReport
from scene_qc_agent.config import load_config


class TestParseReact(unittest.TestCase):
    def test_parse_action(self):
        raw = "Thought: 画质不达标。\nACTION {\"tool\":\"apply_fix\",\"input\":{\"hint\":\"x\"}}"
        thought, action = parse_react(raw)
        self.assertIn("画质不达标", thought)
        self.assertEqual(action["tool"], "apply_fix")
        self.assertEqual(action["input"]["hint"], "x")

    def test_parse_fallback_no_action(self):
        thought, action = parse_react("just a thought")
        self.assertEqual(action, {})


class _FailingPipeline:
    def __init__(self, pass_after):
        self.pass_after = pass_after

    def inspect_scene(self, scene_id, round_, targets):
        passed = round_ >= self.pass_after
        return SceneReport(scene_id=scene_id, round=round_, frames=[],
                           passed=passed, score=1.0 if passed else 0.3,
                           summary=f"r{round_} {'PASS' if passed else 'FAIL'}")


class _NoopFixer:
    def generate_plan(self, report, history):
        return [{"action": "scene_modify", "target": "x", "params": {}, "reason": "t"}]

    def apply_plan(self, plan):
        return [{"action": "scene_modify", "target": "x", "result": "ok"}]


class TestHardCap(unittest.TestCase):
    def test_converges_before_cap(self):
        cfg = load_config(None, {"iteration": {"max_rounds": 3, "max_react_steps": 8}})
        brain = SceneQCBrain(_FailingPipeline(2), _NoopFixer(), None, cfg)
        report, history, rounds = brain.run("s", ["hero"])
        self.assertTrue(report.passed)
        self.assertEqual(rounds, 2)

    def test_stops_at_hard_cap(self):
        cfg = load_config(None, {"iteration": {"max_rounds": 3, "max_react_steps": 8}})
        brain = SceneQCBrain(_FailingPipeline(99), _NoopFixer(), None, cfg)
        report, history, rounds = brain.run("s", ["hero"])
        self.assertFalse(report.passed)
        self.assertEqual(rounds, 3)
        self.assertTrue(report.meta.get("exhausted"))
        self.assertEqual(report.meta.get("rounds_used"), 3)

    def test_finalize_when_passed_baseline(self):
        cfg = load_config(None, {"iteration": {"max_rounds": 3, "max_react_steps": 8}})
        brain = SceneQCBrain(_FailingPipeline(0), _NoopFixer(), None, cfg)
        report, history, rounds = brain.run("s", ["hero"])
        self.assertTrue(report.passed)
        self.assertEqual(rounds, 0)


if __name__ == "__main__":
    unittest.main()
