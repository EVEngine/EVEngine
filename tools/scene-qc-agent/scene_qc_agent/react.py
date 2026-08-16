"""ReAct 范式调度大脑：Thought -> Action(工具) -> Observation，带迭代硬上限。"""

from __future__ import annotations

import json
from typing import Any, Dict, List, Optional, Tuple

from .fixer import Fixer
from .models import OpenAIClient
from .pipeline import QcPipeline
from .report import SceneReport

TOOL_INSPECT = "inspect_scene"
TOOL_APPLY = "apply_fix"
TOOL_FINALIZE = "finalize"

AVAILABLE_TOOLS = [
    {"name": TOOL_INSPECT, "desc": "重新执行一轮完整巡检（机位->快照->筛查->VLM->融合）"},
    {"name": TOOL_APPLY, "desc": "根据报告生成并应用修改计划，随后自动重检（Observation）"},
    {"name": TOOL_FINALIZE, "desc": "判定当前结果可交付，结束迭代"},
]


def parse_react(raw: str) -> Tuple[str, Dict[str, Any]]:
    """解析模型输出的 ReAct 块：提取 Thought 与尾部 ACTION JSON。"""
    thought = raw
    action: Dict[str, Any] = {}
    idx = raw.rfind("ACTION")
    if idx != -1:
        thought = raw[:idx].strip()
        s = raw.find("{", idx)
        if s != -1:
            try:
                import re

                depth, e = 0, -1
                for p in range(s, len(raw)):
                    if raw[p] == "{":
                        depth += 1
                    elif raw[p] == "}":
                        depth -= 1
                        if depth == 0:
                            e = p + 1
                            break
                if e != -1:
                    action = json.loads(raw[s:e])
            except json.JSONDecodeError:
                action = {}
    return thought, action


class SceneQCBrain:
    """ReAct 调度中枢，串起巡检/修复/终判三工具并强制执行多轮迭代硬上限。"""

    def __init__(self, pipeline: QcPipeline, fixer: Fixer, brain: Optional[OpenAIClient],
                 cfg, trace: bool = False):
        self.pipeline = pipeline
        self.fixer = fixer
        self.brain = brain
        self.cfg = cfg
        self.trace = trace
        self.max_rounds = cfg.max_rounds
        self.max_react_steps = int(cfg["iteration"].get("max_react_steps", 8))

    def _log(self, msg: str) -> None:
        if self.trace:
            print(msg)

    def _react_prompt(self, mission: str, report: SceneReport, history: List[dict]) -> str:
        tools = "\n".join(f"- {t['name']}: {t['desc']}" for t in AVAILABLE_TOOLS)
        return (
            f"任务：{mission}\n"
            "你是场景质检 ReAct 大脑。输出一行 Thought 说明推理，再输出一行 "
            'ACTION {"tool":"inspect_scene|apply_fix|finalize","input":{...}}。\n'
            f"可用工具：\n{tools}\n"
            f"当前巡检报告：{json.dumps(report.to_dict(), ensure_ascii=False)[:4000]}\n"
            f"历史（计划+结果）：{json.dumps(history, ensure_ascii=False)[:2000]}\n"
            "规则：质量不达标就 apply_fix；已达标就 finalize。避免重复无意义的 inspect。"
        )

    def _decide(self, mission: str, report: SceneReport, history: List[dict]) -> Tuple[str, Dict[str, Any]]:
        if self.brain is None:
            return ("fallback", {"tool": TOOL_APPLY if not report.passed else TOOL_FINALIZE, "input": {}})
        try:
            raw = self.brain.complete([{"role": "user", "content": self._react_prompt(mission, report, history)}])
        except Exception as e:
            self._log(f"[brain error] {e}")
            return ("fallback", {"tool": TOOL_APPLY if not report.passed else TOOL_FINALIZE, "input": {}})
        thought, action = parse_react(raw)
        self._log(f"[brain] {thought}")
        tool = action.get("tool")
        if tool not in (TOOL_INSPECT, TOOL_APPLY, TOOL_FINALIZE):
            tool = TOOL_APPLY if not report.passed else TOOL_FINALIZE
        return thought, {"tool": tool, "input": action.get("input", {})}

    def run(self, scene_id: str, targets: List[str], mission: str = "") -> Tuple[SceneReport, List[dict], int]:
        """完整多轮迭代闭环。返回 (最终报告, 历史轨迹, 实际轮数)。"""
        mission = mission or f"对场景 {scene_id} 做多视角质检并优化至达标"
        history: List[dict] = []
        rounds_used = 0

        report = self.pipeline.inspect_scene(scene_id, 0, targets)
        history.append({"round": 0, "phase": "baseline", "report": report.to_dict(), "passed": report.passed})
        self._log(report.summary)

        while not report.passed and rounds_used < self.max_rounds:
            rounds_used += 1
            self._log(f"=== round {rounds_used}/{self.max_rounds} ===")

            _, decision = self._decide(mission, report, history)
            tool = decision["tool"]

            if tool == TOOL_INSPECT:
                self._log(f"[act] re-inspect")
                report = self.pipeline.inspect_scene(scene_id, rounds_used, targets)
                history.append({"round": rounds_used, "phase": "inspect", "report": report.to_dict(),
                                "passed": report.passed})
                self._log(report.summary)
                continue

            if tool == TOOL_FINALIZE:
                self._log("[act] finalize")
                break

            # apply_fix：生成计划 -> 应用 -> 重检（Observation）
            plan = self.fixer.generate_plan(report, history)
            self._log(f"[act] apply_fix plan={json.dumps(plan, ensure_ascii=False)[:500]}")
            results = self.fixer.apply_plan(plan)
            report = self.pipeline.inspect_scene(scene_id, rounds_used, targets)
            history.append({"round": rounds_used, "phase": "apply_fix", "plan": plan,
                            "results": results, "report": report.to_dict(), "passed": report.passed})
            self._log(report.summary)

        if not report.passed:
            self._log(f"[info] reached hard cap {self.max_rounds} rounds; stopping")
            report.meta["exhausted"] = True
        report.meta["rounds_used"] = rounds_used
        report.meta["passed_final"] = report.passed
        return report, history, rounds_used
