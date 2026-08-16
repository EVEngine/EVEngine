"""工具注册表 dry-run 仿真测试。"""

import unittest

from scene_qc_agent.tools import ToolRegistry
from scene_qc_agent.config import load_config


class TestTools(unittest.TestCase):
    def setUp(self):
        cfg = load_config(None, {"dry_run": True})
        self.reg = ToolRegistry(None, cfg, dry_run=True)

    def test_camera_generate(self):
        cams = self.reg.camera_generate(["hero"], 4)
        self.assertEqual(len(cams), 4)
        self.assertIn("id", cams[0])

    def test_screenshot_returns_b64(self):
        _, b64, _ = self.reg.screenshot({"id": "cam_0"})
        self.assertTrue(b64)

    def test_scene_info_surfaces_issues(self):
        gi = self.reg.scene_info("cam_0", ["hero"])
        self.assertLessEqual(gi.score, 1.0)
        self.assertTrue(isinstance(gi.objects, list))

    def test_scene_modify_improves(self):
        before = self.reg.dry.quality
        self.reg.scene_modify("move_object", "hero", {"hint": "x"})
        self.assertGreater(self.reg.dry.quality, before)


if __name__ == "__main__":
    unittest.main()
