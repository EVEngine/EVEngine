"""ReAct 调度大脑：需求解析 -> 检索 -> 候选选择 -> 下载校验 -> Blender 处理 -> 入库/回调。

候选失败自动回退下一候选，直至成功或触及 max_candidates 硬上限；全部失败则
异步通知场景 Agent 获取失败。
"""

from __future__ import annotations

import json
import os
from typing import Any, Dict, List, Optional, Tuple

from .blender_pipeline import BlenderPipeline
from .cache import AssetCache
from .models import OpenAIClient, parse_json_block
from .parser import RequirementParser
from .registry import AssetRegistry
from .report import ALLOWED_LICENSES, AssetOutcome, AssetRequest, PipelineReport
from .sources import build_sources


class AssetPipelineBrain:
    """外部素材管线调度中枢。"""

    def __init__(self, parser: RequirementParser, sources: List[Any], cache: AssetCache,
                 blender: BlenderPipeline, registry: AssetRegistry,
                 selector: Optional[OpenAIClient], cfg, trace: bool = False):
        self.parser = parser
        self.sources = sources
        self.cache = cache
        self.blender = blender
        self.registry = registry
        self.selector = selector
        self.cfg = cfg
        self.trace = trace

    def _log(self, msg: str) -> None:
        if self.trace:
            print(msg)

    # ---- 检索 + 合规过滤 + LLM 精排 ----
    def _search(self, request: AssetRequest) -> List[AssetOutcome]:
        outcomes: List[AssetOutcome] = []
        for source in self.sources:
            try:
                candidates = source.search(request)
            except Exception as e:  # noqa: BLE001
                self._log(f"[source:{type(source).__name__}] error: {e}")
                continue
            for c in candidates:
                if c.license not in ALLOWED_LICENSES:
                    continue  # 硬过滤：仅合规授权
                if request.max_triangles and c.triangles and c.triangles > request.max_triangles:
                    continue
                outcomes.append(AssetOutcome(candidate=c, phase="found"))
        return outcomes

    def _select(self, outcomes: List[AssetOutcome], request: AssetRequest) -> AssetOutcome:
        """挑选最优先候选；LLM 精排不可用时退回首个（按源返回顺序）。"""
        if self.selector is not None:
            try:
                prompt = (
                    "你是外部素材管线的候选选择 Agent。给定需求与候选清单（含来源/面数/风格/授权），"
                    "选择最匹配且合规（仅 cc0/cc-by/cc-by-sa）的一个。只输出 JSON："
                    '{"asset_id":"...","reason":"..."}。\n'
                    f"需求={json.dumps(request.to_dict(), ensure_ascii=False)}\n"
                    f"候选={json.dumps([o.candidate.to_dict() for o in outcomes], ensure_ascii=False)}"
                )
                raw = self.selector.complete([{"role": "user", "content": prompt}])
                parsed = parse_json_block(raw)
                if isinstance(parsed, dict):
                    aid = parsed.get("asset_id")
                    for o in outcomes:
                        if o.candidate.asset_id == aid:
                            o.reason = str(parsed.get("reason", ""))
                            return o
            except Exception as e:  # noqa: BLE001
                self._log(f"[selector] error: {e}")
        best = outcomes[0]
        best.reason = "首候选（按源返回顺序）"
        return best

    # ---- 单候选完整链路 ----
    def _run_candidate(self, outcome: AssetOutcome, request: AssetRequest) -> AssetOutcome:
        c = outcome.candidate
        try:
            outcome.phase = "downloading"
            # 用 URL 尾部作为归档扩展名。
            ext = os.path.splitext(c.download_url.split("?")[0])[1] or ".zip"
            archive, outcome.download_md5 = self.cache.download(c.download_url, c.asset_id, ext)
            outcome.phase = "processing"
            work_dir = self.cache.extract_dir(c.asset_id)
            out_dir = os.path.join(self.cfg.output["work_dir"], c.asset_id)
            os.makedirs(out_dir, exist_ok=True)
            output_path = os.path.join(out_dir, f"{c.asset_id}.{request.output_format}")
            params = {
                "up_axis": request.up_axis,
                "scale": request.scale,
                "max_triangles": request.max_triangles,
                "output_format": request.output_format,
            }
            outcome.blender = self.blender.process(archive, work_dir, output_path, c, params)
            if not outcome.blender.ok:
                outcome.phase = "failed"
                outcome.status = "FAILED"
                outcome.error = outcome.blender.error
                return outcome
            outcome.phase = "ingesting"
            uri = self.registry.ingest(outcome, output_path)
            outcome.phase = "done"
            outcome.status = "ACCEPTED"
            outcome.asset_uri = uri
        except Exception as e:  # noqa: BLE001
            outcome.phase = "failed"
            outcome.status = "FAILED"
            outcome.error = str(e)
        return outcome

    # ---- 主流程 ----
    def run(self, request_text: str, request: Optional[AssetRequest] = None) -> PipelineReport:
        req = self.parser.parse(request_text, request)
        # 回调标识：可从请求文本或初始 request 提取。
        if request and request.callback:
            self.cfg.raw.setdefault("callback", request.callback)
        elif self.cfg.get("callback"):
            req.callback = self.cfg.get("callback")

        report = PipelineReport(request=req)
        outcomes = self._search(req)
        self._log(f"[search] {len(outcomes)} 个合规候选")
        report.total = len(outcomes)

        if not outcomes:
            report.summary = "未检索到合规候选，素材获取失败"
            self.registry.notify(AssetOutcome(status="FAILED", error=report.summary))
            return report

        attempted = 0
        remaining = list(outcomes)
        while remaining and attempted < self.cfg.max_candidates:
            attempted += 1
            chosen = self._select(remaining, req)
            remaining.remove(chosen)
            self._log(f"[candidate {attempted}/{self.cfg.max_candidates}] {chosen.candidate.asset_id}")
            chosen = self._run_candidate(chosen, req)
            report.outcomes.append(chosen)
            if chosen.status == "ACCEPTED":
                self.registry.notify(chosen)
                report.accepted += 1
                report.summary = (
                    f"素材就绪：{chosen.candidate.name} ({chosen.asset_uri}) "
                    f"tri={chosen.blender.triangles if chosen.blender else 0}"
                )
                break
            # 失败 -> 尝试下一候选
            self._log(f"  -> failed: {chosen.error}; 尝试下一候选")

        if report.accepted == 0:
            report.summary = "所有候选处理均失败，素材获取失败"
            failed = report.outcomes[-1] if report.outcomes else AssetOutcome(status="FAILED",
                                                                             error=report.summary)
            self.registry.notify(failed)

        report.meta["attempted"] = attempted
        report.meta["exhausted"] = report.accepted == 0
        return report
