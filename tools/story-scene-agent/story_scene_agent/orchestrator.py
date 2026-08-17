"""Orchestrator: story -> stage -> plan -> build -> QC loop -> finalize.

Reuses the existing AI tools as building blocks:
  - creative-brain (brain.plan.build_plan) for intent / assets / layout / plan
  - scene-qc-agent (SceneQCBrain + QcPipeline + Fixer) for quality iteration
  - the scene_director kit for placing / lighting / camera
"""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional

from .config import (
    CREATIVE_BRAIN_DIR,
    SCENE_QC_DIR,
    DefaultConfig,
    load_config,
    load_qc_config,
)
from .executor import McpSceneExecutor, MemorySceneExecutor, SceneExecutor
from .story import StageBrief, parse_stage

# Make sibling tool packages importable (creative-brain / scene-qc-agent).
for _pkg in (str(CREATIVE_BRAIN_DIR), str(SCENE_QC_DIR)):
    if _pkg not in sys.path:
        sys.path.insert(0, _pkg)


class StorySceneBrain:
    """End-to-end story -> qualified scene director."""

    def __init__(self, cfg: Dict[str, Any], dry_run: bool = False, trace: bool = False):
        import json as _json

        base = _json.loads(_json.dumps(DefaultConfig))
        self.cfg = _deep_merge(base, cfg)
        self.dry_run = dry_run
        self.trace = trace

    # ------------------------------------------------------------------ build
    def build_plan(self, story: str, stage: StageBrief) -> Any:
        """Creative-brain plan + stage-director overrides."""
        from brain.plan import build_plan  # type: ignore

        catalog = self.cfg["creative"].get("catalog") or DefaultConfig["creative"]["catalog"]
        seed = int(self.cfg["creative"].get("seed", 2026))
        plan = build_plan(story, catalog_path=catalog, seed=seed)
        # Stage-director intent wins over the generic lighting guess.
        plan.config.lighting.timeOfDay = stage.timeOfDay
        plan.config.lighting.atmosphere = stage.atmosphere
        plan.config.name = stage.name or plan.config.name
        return plan

    def build_scene(self, executor: SceneExecutor, plan: Any, stage: StageBrief) -> List[dict]:
        """Execute the plan + stage brief against the live/in-memory scene."""
        log: List[dict] = []
        tile = float(self.cfg["creative"].get("tile", 2.0))

        executor.reset()
        log.append({"step": "reset", "ok": True})

        # Terrain ground (from plan.terrain, sized by config width/height).
        terr = plan.config.terrain
        w = int(terr.width or 32)
        h = int(terr.height or 32)
        r = executor.spawn({
            "id": "terrain",
            "kind": "ground",
            "pos": [0.0, 0.0, 0.0],
            "scale": [max(8.0, w * tile), 1.0, max(8.0, h * tile)],
            "tint": [0.42, 0.46, 0.40],
            "roughness": 0.95,
        })
        log.append({"step": "terrain", "ok": bool(r.get("ok"))})

        # Lighting (stage overrides from story atmosphere / time-of-day).
        lit = executor.set_lighting({
            "timeOfDay": plan.config.lighting.timeOfDay,
            "atmosphere": plan.config.lighting.atmosphere,
            "intensity": float(plan.config.lighting.intensity),
        })
        log.append({"step": "lighting", "ok": bool(lit.get("ok"))})

        # Place props from the layout steps (grid cells -> world coords).
        placed = 0
        for step in plan.steps:
            if step.action != "place":
                continue
            x = int(step.params.get("x", 0))
            y = int(step.params.get("y", 0))
            pid = f"{step.target}_{x}_{y}"
            res = executor.spawn({
                "id": pid,
                "kind": str(step.target),
                "pos": _cell_to_world(x, y, w, h, tile),
                "seed": int(step.params.get("seed", 1)),
            })
            if res.get("ok"):
                placed += 1
            log.append({"step": "place", "id": pid, "ok": bool(res.get("ok"))})

        # Focal centerpiece from the story (deliberate, at stage origin).
        centerpiece = stage.centerpiece
        if centerpiece:
            res = executor.spawn({
                "id": "centerpiece",
                "kind": centerpiece,
                "pos": [0.0, 0.0, 0.0],
                "scale": [1.6, 1.6, 1.6],
                "yaw_deg": 0.0,
                "seed": 7,
            })
            log.append({"step": "centerpiece", "kind": centerpiece, "ok": bool(res.get("ok"))})

        # Default framing camera.
        cam = self.cfg["stage"].get("default_camera", DefaultConfig["stage"]["default_camera"])
        camres = executor.set_camera(dict(cam))
        log.append({"step": "camera", "ok": bool(camres.get("ok"))})

        info = executor.info()
        log.append({"step": "info", "count": int(info.get("count", 0))})
        return log

    # ------------------------------------------------------------------- QC
    def _qc_runtime(self, client, screenshot_dir: str = ""):
        from scene_qc_agent.config import load_config as qc_load_config  # type: ignore
        from scene_qc_agent.fixer import Fixer  # type: ignore
        from scene_qc_agent.models import OpenAIClient  # type: ignore
        from scene_qc_agent.pipeline import QcPipeline  # type: ignore
        from scene_qc_agent.react import SceneQCBrain  # type: ignore
        from scene_qc_agent.tools import ToolRegistry  # type: ignore

        qc_path = self.cfg["qc"].get("config")
        overrides = {
            "dry_run": self.dry_run,
            "iteration": {"max_rounds": int(self.cfg["qc"].get("max_rounds", 3))},
            "output": {"trace": bool(self.trace or self.cfg.get("trace", False)),
                       "save_snapshots": bool(self.cfg["qc"].get("save_snapshots", False))},
        }
        if screenshot_dir:
            overrides["output"]["screenshot_dir"] = os.path.abspath(screenshot_dir)
        qc_cfg = qc_load_config(qc_path, overrides)

        dry = self.dry_run
        registry = ToolRegistry(client if not dry else None, qc_cfg, dry_run=dry)

        def _model(tier: str):
            spec = qc_cfg["models"].get(tier) or {}
            if dry or not spec.get("enabled") or not spec.get("model"):
                return None
            try:
                return OpenAIClient(spec)
            except Exception:
                return None

        local = _model("local_screen")
        vlm = _model("vlm_review")
        planner = _model("fix_planner")
        brain = _model("brain") if qc_cfg["models"].get("brain") else None

        pipeline = QcPipeline(registry, local, vlm, qc_cfg)
        fixer = Fixer(registry, planner, qc_cfg)
        react = SceneQCBrain(pipeline, fixer, brain, qc_cfg, trace=bool(self.trace))
        return pipeline, fixer, react

    def run_qc(self, client, scene_id: str, targets: List[str], screenshot_dir: str = ""):
        _, _, react = self._qc_runtime(client, screenshot_dir=screenshot_dir)
        report, history, rounds = react.run(scene_id, targets)
        return report, history, rounds

    # ------------------------------------------------------------- finalize
    def finalize(self, executor: SceneExecutor, stage: StageBrief,
                 qc_report, out_dir: str) -> Dict[str, Any]:
        os.makedirs(out_dir, exist_ok=True)
        shots: List[dict] = []
        if not self.dry_run:
            try:
                cams = executor.cameras(2)
                for i, cam in enumerate(cams[:2]):
                    path = os.path.join(out_dir, f"shot_{i}.png")
                    res = executor.screenshot(path)
                    shots.append({"camera": cam.get("id", f"cam_{i}"), "path": res.get("path", path)})
            except Exception as e:  # screenshot is best-effort
                shots.append({"error": str(e)})
        return {"out_dir": out_dir, "shots": shots,
                "info": executor.info()}

    # ---------------------------------------------------------------- run
    def run(self, story: str, scene_id: str = "", out_dir: str = "") -> Dict[str, Any]:
        stage = parse_stage(story, name=scene_id or "story-scene")
        plan = self.build_plan(story, stage)

        executor: SceneExecutor
        if self.dry_run:
            executor = MemorySceneExecutor()
        else:
            executor = McpSceneExecutor(self.cfg["engine"]["host"], int(self.cfg["engine"]["port"]))

        try:
            if not self.dry_run:
                executor.connect()
            build_log = self.build_scene(executor, plan, stage)

            out_dir = out_dir or self.cfg["output"].get("out_dir", "ai_stage_out")
            targets = list(self.cfg["qc"].get("targets", ["scene"]))
            qc_client = executor.client if not self.dry_run else None
            qc_report, qc_history, rounds = self.run_qc(
                qc_client, stage.name or scene_id, targets, screenshot_dir=out_dir)
            final = self.finalize(executor, stage, qc_report, out_dir)

            result = {
                "story": story,
                "stage": stage.to_dict(),
                "plan": plan.to_dict(),
                "build": build_log,
                "qc": {
                    "passed": bool(qc_report.passed),
                    "score": float(qc_report.score),
                    "rounds_used": int(rounds),
                    "summary": qc_report.summary,
                    "report": qc_report.to_dict(),
                    "history": qc_history,
                },
                "final": final,
                "dry_run": self.dry_run,
            }
            self._save_report(result, out_dir)
            return result
        finally:
            executor.close()

    def _save_report(self, result: Dict[str, Any], out_dir: str) -> str:
        os.makedirs(out_dir, exist_ok=True)
        path = os.path.join(out_dir, self.cfg["output"].get("report", "story_scene_report.json"))
        with open(path, "w", encoding="utf-8") as f:
            json.dump(result, f, ensure_ascii=False, indent=2)
        return path


def _cell_to_world(x: int, y: int, width: int, height: int, tile: float) -> List[float]:
    return [round((x - width / 2.0) * tile, 3), 0.0, round((y - height / 2.0) * tile, 3)]


def _deep_merge(base: Dict[str, Any], override: Dict[str, Any]) -> Dict[str, Any]:
    out = dict(base)
    for k, v in override.items():
        if isinstance(v, dict) and isinstance(out.get(k), dict):
            out[k] = _deep_merge(out[k], v)
        else:
            out[k] = v
    return out


def main(argv: Optional[List[str]] = None) -> int:
    import argparse

    ap = argparse.ArgumentParser(
        prog="story-scene-agent",
        description="剧情 -> 合格舞台：故事解析 -> 生成计划 -> 搭台 -> 质检迭代 -> 交付")
    ap.add_argument("story", help="玩家/导演的一句话剧情，如“月光下骑士与巨龙在古堡前对峙”")
    ap.add_argument("--config", default="", help="配置文件（默认内置）")
    ap.add_argument("--scene-id", default="story-scene", help="场景标识")
    ap.add_argument("--port", type=int, default=None, help="引擎 MCP 端口（覆盖配置）")
    ap.add_argument("--max-rounds", type=int, default=None, help="质检迭代硬上限（覆盖配置）")
    ap.add_argument("--dry-run", action="store_true", help="不连引擎/模型，使用仿真闭环")
    ap.add_argument("--trace", action="store_true", help="打印 ReAct 轨迹")
    ap.add_argument("--out-dir", default="", help="输出目录（报告 + 截图）")
    args = ap.parse_args(argv)

    cfg = load_config(args.config)
    if args.port:
        cfg["engine"]["port"] = args.port
    if args.max_rounds:
        cfg["qc"]["max_rounds"] = args.max_rounds
    if args.trace:
        cfg["trace"] = True

    brain = StorySceneBrain(cfg, dry_run=args.dry_run, trace=args.trace)
    result = brain.run(args.story, scene_id=args.scene_id, out_dir=args.out_dir)

    summary = {
        "passed": result["qc"]["passed"],
        "score": result["qc"]["score"],
        "rounds_used": result["qc"]["rounds_used"],
        "props": result["final"]["info"].get("count", 0),
        "dry_run": result["dry_run"],
        "report": os.path.join(args.out_dir or cfg["output"]["out_dir"],
                               cfg["output"]["report"]),
    }
    print(json.dumps(summary, ensure_ascii=False))
    if result["qc"]["history"]:
        print(json.dumps([{ "round": h["round"], "phase": h["phase"], "passed": h["passed"] }
                          for h in result["qc"]["history"]], ensure_ascii=False))
    return 0 if result["qc"]["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())