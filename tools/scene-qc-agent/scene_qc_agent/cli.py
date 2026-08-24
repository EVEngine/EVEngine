"""命令行入口。"""

from __future__ import annotations

import argparse
import json
import sys
from typing import List, Optional

from .config import load_config
from .models import OpenAIClient


def _build_client(cfg, tier: str):
    spec = cfg["models"][tier]
    if not spec.get("enabled"):
        return None
    if not spec.get("model"):
        return None
    try:
        return OpenAIClient(spec)
    except Exception as e:
        print(f"[warn] skip {tier}: {e}", file=sys.stderr)
        return None


def build_runtime(cfg):
    from .mcp_client import EvMcpClient, McpError
    from .tools import ToolRegistry

    dry = bool(cfg.get("dry_run", False))
    client = None
    if not dry:
        try:
            client = EvMcpClient(cfg)
            client.initialize()
        except McpError as e:
            print(f"[warn] engine MCP unavailable ({e}); falling back to dry-run", file=sys.stderr)
            client = None
            dry = True
    registry = ToolRegistry(client, cfg, dry_run=dry)
    local = None if dry else _build_client(cfg, "local_screen")
    vlm = None if dry else _build_client(cfg, "vlm_review")
    planner = None if dry else _build_client(cfg, "fix_planner")
    return dry, registry, local, vlm, planner


def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(prog="scene-qc-agent",
                                 description="场景质检大脑：ReAct 多视角巡检 + 大小模型分级调度 + 迭代优化")
    ap.add_argument("--config", default="config.example.json", help="配置文件路径（默认 config.example.json）")
    ap.add_argument("--scene-id", default="scene_001", help="场景标识")
    ap.add_argument("--targets", nargs="*", default=["hero"], help="待巡检目标，如 hero enemy")
    ap.add_argument("--max-rounds", type=int, default=None, help="优化迭代硬上限（覆盖配置）")
    ap.add_argument("--dry-run", action="store_true", help="不连引擎/模型，使用仿真闭环")
    ap.add_argument("--trace", action="store_true", help="打印 ReAct 思维与动作轨迹")
    ap.add_argument("--report", default=None, help="报告输出 JSON 路径（覆盖配置）")
    args = ap.parse_args(argv)

    overrides = {}
    if args.max_rounds:
        overrides["iteration"] = {"max_rounds": args.max_rounds}
    overrides["dry_run"] = args.dry_run
    if args.trace:
        overrides["output"] = {"trace": True}

    cfg = load_config(args.config, overrides)
    trace = bool(args.trace or cfg.get("output", {}).get("trace"))

    dry, registry, local, vlm, planner = build_runtime(cfg)
    if dry:
        print("[info] dry-run mode: 仿真引擎与模型，用于演示闭环", file=sys.stderr)

    from .pipeline import QcPipeline
    from .fixer import Fixer
    from .react import SceneQCBrain

    pipeline = QcPipeline(registry, local, vlm, cfg)
    fixer = Fixer(registry, planner, cfg)
    brain = SceneQCBrain(pipeline, fixer, planner, cfg, trace=trace)

    final_report, history, rounds = brain.run(args.scene_id, list(args.targets))

    print(json.dumps({"passed": final_report.passed, "score": final_report.score,
                      "rounds_used": rounds, "summary": final_report.summary}, ensure_ascii=False))
    if not final_report.passed:
        print(json.dumps({"defects": [d.to_dict() for d in final_report.defects][:20]},
                         ensure_ascii=False), file=sys.stderr)

    out_path = args.report or cfg.get("output", {}).get("report_path", "scene_qc_report.json")
    try:
        with open(out_path, "w", encoding="utf-8") as fh:
            fh.write(json.dumps({
                "config_source": cfg.source,
                "dry_run": dry,
                "final": final_report.to_dict(),
                "history": history,
            }, ensure_ascii=False, indent=2))
        print(f"[info] report written to {out_path}", file=sys.stderr)
    except OSError as e:
        print(f"[warn] cannot write report: {e}", file=sys.stderr)

    return 0 if final_report.passed else 1


if __name__ == "__main__":
    sys.exit(main())
