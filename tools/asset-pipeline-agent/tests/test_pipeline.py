"""外部素材管线单元测试：解析 / 检索过滤 / 缓存MD5 / 干跑处理 / 大脑回退闭环。"""

from __future__ import annotations

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from asset_pipeline_agent.cache import AssetCache, md5_of
from asset_pipeline_agent.config import load_config
from asset_pipeline_agent.models import parse_json_block
from asset_pipeline_agent.parser import RequirementParser
from asset_pipeline_agent.report import AssetRequest, Candidate
from asset_pipeline_agent.sources import DryRunSource


class ParserTest(unittest.TestCase):
    def test_parse_sets_fields_and_license(self):
        p = RequirementParser(None, load_config())
        req = p.parse("需要一个写实风格的石头道具，面数 5000-20000，CC0，导出 glb，Z 轴朝上")
        self.assertEqual(req.style, "realistic")
        self.assertEqual(req.purpose, "prop")
        self.assertEqual(req.license, "cc0")
        self.assertEqual(req.output_format, "glb")
        self.assertEqual(req.up_axis, "Z")
        self.assertLessEqual(req.min_triangles, req.max_triangles)

    def test_illegal_license_corrected(self):
        p = RequirementParser(None, load_config())
        req = p.parse("要一个模型，付费商用，可以随便用")
        self.assertIn(req.license, ("cc0", "cc-by", "cc-by-sa"))

    def test_request_validate(self):
        req = AssetRequest(license="proprietary", output_format="dae")
        self.assertTrue(req.validate())


class SourcesTest(unittest.TestCase):
    def test_dry_run_respects_triangle_budget(self):
        req = AssetRequest(purpose="prop", description="stone", style="low_poly", max_triangles=1500)
        src = DryRunSource({})
        cands = src.search(req)
        self.assertTrue(cands)
        for c in cands:
            self.assertEqual(c.license, "cc0")
            self.assertLessEqual(c.triangles, 1500)
            self.assertTrue(c.attribution)

    def test_candidate_attribution_has_license(self):
        src = DryRunSource({})
        cands = src.search(AssetRequest(purpose="env", description="rock", style="realistic"))
        self.assertTrue(all("CC0" in c.attribution or c.license in c.attribution for c in cands))


class CacheTest(unittest.TestCase):
    def test_md5_roundtrip(self):
        with tempfile.TemporaryDirectory() as d:
            cache = AssetCache(os.path.join(d, "cache"))
            path = cache.local_path("abc", ".zip")
            with open(path, "wb") as fh:
                fh.write(b"hello world")
            self.assertEqual(md5_of(path), "5eb63bbbe01eeed093cb22bb8f5acdc3")

    def test_dry_download_and_reuse(self):
        with tempfile.TemporaryDirectory() as d:
            cache = AssetCache(os.path.join(d, "cache"))
            p1, m1 = cache.download("dry://asset/prop_0", "prop_0", ".zip")
            p2, m2 = cache.download("dry://asset/prop_0", "prop_0", ".zip")
            self.assertEqual(p1, p2)
            self.assertEqual(m1, m2)


class ModelsTest(unittest.TestCase):
    def test_parse_json_block_fenced(self):
        self.assertEqual(parse_json_block('```json\n{"a":1}\n```'), {"a": 1})
        self.assertEqual(parse_json_block('text {"a":2} tail'), {"a": 2})


class BrainDryRunTest(unittest.TestCase):
    def _build(self, tmp):
        from asset_pipeline_agent.blender_pipeline import BlenderPipeline
        from asset_pipeline_agent.parser import RequirementParser
        from asset_pipeline_agent.react import AssetPipelineBrain
        from asset_pipeline_agent.registry import AssetRegistry

        cfg = load_config()
        cfg.raw["sources"] = {"dry_run": {"enabled": True}}
        cfg.raw["cache"]["dir"] = os.path.join(tmp, "cache")
        cfg.raw["output"]["work_dir"] = os.path.join(tmp, "work")
        cache = AssetCache(cfg["cache"]["dir"])
        blender = BlenderPipeline(cfg, cache, dry_run=True)
        registry = AssetRegistry(cfg, dry_run=True)
        parser = RequirementParser(None, cfg)
        brain = AssetPipelineBrain(parser, [DryRunSource({})], cache, blender, registry, None, cfg, trace=True)
        return brain

    def test_success_ingest(self):
        with tempfile.TemporaryDirectory() as tmp:
            brain = self._build(tmp)
            report = brain.run("需要一块写实的岩石道具，CC0，低模，1000 面，导出 glb")
            self.assertEqual(report.accepted, 1)
            self.assertTrue(report.succeeded)
            self.assertEqual(report.outcomes[0].status, "ACCEPTED")
            self.assertTrue(report.outcomes[0].ingested)
            self.assertIn("engine://asset/", report.outcomes[0].asset_uri)

    def test_fallback_on_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            brain = self._build(tmp)
            from asset_pipeline_agent.blender_pipeline import BlenderPipeline

            class FailingBlender(BlenderPipeline):
                calls = 0

                def process(self, *a, **k):
                    FailingBlender.calls += 1
                    from asset_pipeline_agent.report import BlenderResult
                    return BlenderResult(ok=False, error="模拟处理失败")

            brain.blender = FailingBlender(brain.cfg, brain.blender.cache, dry_run=True)
            report = brain.run("需要一块岩石道具，CC0，1000 面，导出 glb")
            self.assertEqual(report.accepted, 0)
            self.assertGreaterEqual(report.meta.get("attempted", 0), 1)
            self.assertTrue(report.meta.get("exhausted"))


if __name__ == "__main__":
    unittest.main()
