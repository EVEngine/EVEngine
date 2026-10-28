"""外部素材管线 Agent：资源素材获取 & 自动化标准化管线入口。

用法：
    python -m asset_pipeline_agent --config config.example.json --request "..." [--dry-run] [--trace]
"""

from __future__ import annotations

import argparse
import json
import sys
from typing import List, Optional

from .config import load_config
from .models import OpenAIClient
from .report import AssetRequest


def _build_client(cfg, tier: str):
    spec = cfg["models"][tier]
    if not spec.get("enabled") or not spec.get("model"):
        return None
    try:
        return OpenAIClient(spec)
    except Exception as e:  # noqa: BLE001
        print(f"[warn] skip {tier}: {e}", file=sys.stderr)
        return None


def build_runtime(cfg):
    from .blender_pipeline import BlenderPipeline
    from .cache import AssetCache
    from .parser import RequirementParser
    from .react import AssetPipelineBrain
    from .registry import AssetRegistry
    from .sources import DryRunSource, build_sources

    dry = bool(cfg.get("dry_run", False))
    cache = AssetCache(cfg["cache"]["dir"], keep_archives=cfg["cache"].get("keep_archives", True))
    if dry:
        sources = [DryRunSource({})]
    else:
        sources = build_sources(cfg)
    blender = BlenderPipeline(cfg, cache, dry_run=dry)
    registry = AssetRegistry(cfg, dry_run=dry)
    parser = RequirementParser(None if dry else _build_client(cfg, "parser"), cfg)
    selector = None if dry else _build_client(cfg, "selector")
    brain = AssetPipelineBrain(parser, sources, cache, blender, registry, selector, cfg, trace=bool(cfg.get("trace", False)))
    return brain, registry


def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(
        prog="asset-pipeline-agent",
        description="外部素材管线：检索合规素材 -> Headless Blender 标准化 -> 入库 & 回调")
    ap.add_argument("--config", default="config.example.json")
    ap.add_argument("--request", required=True, help="场景 Agent 的素材需求文本")
    ap.add_argument("--dry-run", action="store_true", help="不连引擎/API/Blender，使用仿真源闭合闭环")
    ap.add_argument("--trace", action="store_true", help="打印调度轨迹")
    ap.add_argument("--report", default=None, help="报告输出 JSON 路径（覆盖配置）")
    ap.add_argument("--up-axis", default=None, help="坐标系上轴 Y|Z")
    ap.add_argument("--max-triangles", type=int, default=None, help="面数上限")
    ap.add_argument("--callback", default=None, help="就绪/失败异步回调标识")
    args = ap.parse_args(argv)

    overrides = {"dry_run": args.dry_run}
    if args.trace:
        overrides["output"] = {"trace": True}
    if args.callback:
        overrides["callback"] = args.callback
    cfg = load_config(args.config, overrides)
    if args.trace:
        cfg.raw["trace"] = True

    request = AssetRequest()
    if args.up_axis:
        request.up_axis = args.up_axis
    if args.max_triangles:
        request.max_triangles = args.max_triangles
    if args.callback:
        request.callback = args.callback

    brain, registry = build_runtime(cfg)
    report = brain.run(args.request, request)

    print(json.dumps({
        "accepted": report.accepted,
        "total": report.total,
        "summary": report.summary,
        "outcomes": [o.to_dict() for o in report.outcomes],
    }, ensure_ascii=False))
    registry.close()

    out_path = args.report or cfg.get("output", {}).get("report_path", "asset_pipeline_report.json")
    try:
        with open(out_path, "w", encoding="utf-8") as fh:
            fh.write(json.dumps({
                "config_source": cfg.source,
                "dry_run": args.dry_run,
                "report": report.to_dict(),
            }, ensure_ascii=False, indent=2))
        print(f"[info] report written to {out_path}", file=sys.stderr)
    except OSError as e:
        print(f"[warn] cannot write report: {e}", file=sys.stderr)

    return 0 if report.accepted > 0 else 1


if __name__ == "__main__":
    sys.exit(main())
