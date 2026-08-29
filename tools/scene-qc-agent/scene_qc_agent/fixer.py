"""修复方案生成与落地：把融合报告 -> 结构化修改计划 -> 调用 MCP 调整场景。"""

from __future__ import annotations

import json
from typing import List, Optional

from .models import OpenAIClient, parse_json_block
from .report import SceneReport
from .tools import ToolRegistry

# 动作 -> scene_modify 的 action 参数（无真实引擎时可由此对齐）
ACTION_KINDS = ("move_object", "move_camera", "remove_object", "add_object",
                "material", "lighting", "scale", "rotation", "spawn_effect", "scene_modify")


class Fixer:
    def __init__(self, registry: ToolRegistry, planner: Optional[OpenAIClient], cfg):
        self.reg = registry
        self.planner = planner
        self.cfg = cfg

    def generate_plan(self, report: SceneReport, history: List[dict]) -> List[dict]:
        """由修复规划模型产出结构化修改计划；不可用时退回启发式方案。"""
        if self.planner is not None:
            prompt = (
                "你是场景质检修复规划 Agent（ReAct 决策核心）。根据巡检报告与历史优化记录，生成最"
                "有效的修改计划，让下一轮评分提升。\n"
                "只输出JSON数组：[{\"action\":\"move_object|remove_object|material|lighting|scene_modify|"
                "run_script\",\"target\":\"...\",\"params\":{...},\"reason\":\"...\"}]\n"
                "action=scene_modify 时 params 需含引擎约定的修改参数；action=run_script 时 params.source 为"
                "Squirrel 片段。\n"
                f"巡检报告={json.dumps(report.to_dict(), ensure_ascii=False)}"
                f"\n历史={json.dumps(history, ensure_ascii=False)[:2000]}"
            )
            try:
                raw = self.planner.complete([{"role": "user", "content": prompt}])
                parsed = parse_json_block(raw)
                if isinstance(parsed, list) and parsed:
                    return [p for p in parsed if isinstance(p, dict)]
            except Exception:
                pass
        return self._fallback_plan(report)

    @staticmethod
    def _fallback_plan(report: SceneReport) -> List[dict]:
        """启发式：按缺陷 target 与 suggestion 生成最保守的调整。"""
        plan: List[dict] = []
        seen: set = set()
        for i in report.defects:
            key = (i.target, i.suggestion)
            if key in seen:
                continue
            seen.add(key)
            params = {"reason": i.suggestion or i.message}
            if i.suggestion:
                params["hint"] = i.suggestion
            plan.append({"action": "scene_modify", "target": i.target or "scene",
                         "params": params, "reason": i.message})
        if not plan:
            plan.append({"action": "scene_modify", "target": "scene",
                         "params": {"reason": "quality pass attempt"}, "reason": "fallback"})
        return plan

    def apply_plan(self, plan: List[dict]) -> List[dict]:
        results: List[dict] = []
        for step in plan:
            action = str(step.get("action", "scene_modify"))
            target = str(step.get("target", "scene"))
            params = step.get("params") or {}
            try:
                if action == "run_script":
                    src = params.get("source", "")
                    res = self.reg.run_script(src) if src else "empty script skipped"
                else:
                    if action not in ACTION_KINDS:
                        action = "scene_modify"
                    res = self.reg.scene_modify(action, target, params)
                results.append({"action": action, "target": target, "result": str(res)})
            except Exception as e:
                results.append({"action": action, "target": target, "error": str(e)})
        return results
