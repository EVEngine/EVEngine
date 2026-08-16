"""融合与分级调度逻辑测试。"""

import unittest

from scene_qc_agent import fusion as F
from scene_qc_agent.report import GeometryInfo, Issue, LocalEval, VlmReview, SceneReport, FrameVerdict

W = {"geometry": 0.35, "local": 0.35, "vlm": 0.30}


class TestFusion(unittest.TestCase):
    def test_geometry_score_default(self):
        self.assertAlmostEqual(F.geometry_score(GeometryInfo()), 1.0)

    def test_geometry_score_penalty(self):
        gi = GeometryInfo(issues=[Issue("a", "x", "critical")])
        self.assertLess(F.geometry_score(gi), 1.0)

    def test_needs_escalation_low_local(self):
        gi = GeometryInfo()
        local = LocalEval(score=0.5)
        self.assertTrue(F.needs_escalation(gi, local, 0.7))

    def test_needs_escalation_critical_geom(self):
        gi = GeometryInfo(issues=[Issue("a", "x", "critical")])
        local = LocalEval(score=0.9)
        self.assertTrue(F.needs_escalation(gi, local, 0.7))

    def test_escalate_when_no_vlm(self):
        gi = GeometryInfo()
        local = LocalEval(score=0.5)
        v = F.fuse_frame("f", {}, gi, local, None, W, 0.7, 0.75)
        self.assertEqual(v.verdict, F.ESCALATE)

    def test_pass_when_vlm_ok(self):
        gi = GeometryInfo()
        local = LocalEval(score=0.8)
        vlm = VlmReview(score=0.9, passed=True)
        v = F.fuse_frame("f", {}, gi, local, vlm, W, 0.7, 0.75)
        self.assertEqual(v.verdict, F.PASS)

    def test_fail_when_vlm_rejects(self):
        gi = GeometryInfo()
        local = LocalEval(score=0.8)
        vlm = VlmReview(score=0.3, passed=False)
        v = F.fuse_frame("f", {}, gi, local, vlm, W, 0.7, 0.75)
        self.assertEqual(v.verdict, F.FAIL)

    def test_aggregate(self):
        frames = [FrameVerdict(frame_id="f1", verdict=F.FAIL, score=0.2),
                  FrameVerdict(frame_id="f2", verdict=F.PASS, score=0.9)]
        rep = F.aggregate_frames("s", 1, frames, 0.75)
        self.assertFalse(rep.passed)
        self.assertAlmostEqual(rep.score, 0.2)


if __name__ == "__main__":
    unittest.main()
