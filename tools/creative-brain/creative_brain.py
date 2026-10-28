#!/usr/bin/env python3
"""Creative Brain CLI — scenario intent parsing & asset arrangement for EVEngine.

Pipeline:
    intent -> asset match -> layout rules -> generation plan -> MCP batch.

Commands:
    parse     Parse a natural-language request into a structured SceneConfig.
    plan      Full pipeline -> standardized generation plan (JSON).
    assets    List catalog categories / assets (debug helper).
    request   Enqueue an async resource-agent fetch (see docs protocol).
    status    Query a resource request status.
    mcp       Push a plan to the engine and run its batch steps via MCP.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from brain import intent  # noqa: E402
from brain.assets import Catalog  # noqa: E402
from brain.mcp import McpClient, run_batch  # noqa: E402
from brain.plan import DEFAULT_CATALOG, build_plan, save_plan  # noqa: E402
from brain.resource_proto import ResourceBroker  # noqa: E402
from brain.schema import SCENE_CONFIG_SCHEMA  # noqa: E402


def _add_common(p: argparse.ArgumentParser) -> None:
    p.add_argument("--catalog", default=str(DEFAULT_CATALOG), help="Asset catalog JSON path.")
    p.add_argument("--seed", type=int, default=None, help="Deterministic seed.")


def cmd_parse(args) -> int:
    cfg = intent.parse_intent(args.prompt, seed=args.seed)
    print(json.dumps(cfg.to_dict(), indent=2, ensure_ascii=False))
    return 0


def cmd_plan(args) -> int:
    plan = build_plan(args.prompt, catalog_path=args.catalog, seed=args.seed)
    if plan.missing_assets:
        print(f"[info] missing assets (enqueue via 'request'): {plan.missing_assets}",
              file=sys.stderr)
    if args.out:
        save_plan(plan, args.out)
        print(f"[ok] plan saved -> {args.out}")
    else:
        print(plan.to_json())
    return 0


def cmd_assets(args) -> int:
    catalog = Catalog.load(args.catalog)
    if args.category:
        for a in catalog.query(category=args.category):
            print(f"{a.id}\t[{a.category}]\tstyle={a.style_tags}\ttags={a.tags}")
    else:
        print("categories:", ", ".join(catalog.categories()))
        print(f"assets ({len(catalog.assets)}):")
        for a in catalog.assets:
            print(f"  {a.id}\t[{a.category}]\tstyle={a.style_tags}")
    return 0


def cmd_request(args) -> int:
    broker = ResourceBroker()
    req = broker.request(args.asset, style=args.style)
    print(json.dumps(req.to_dict(), indent=2, ensure_ascii=False))
    print("[ok] enqueued; resource agent will process asynchronously.", file=sys.stderr)
    return 0


def cmd_status(args) -> int:
    broker = ResourceBroker()
    st = broker.status(args.request_id)
    if st is None:
        print(f"unknown request_id: {args.request_id}", file=sys.stderr)
        return 1
    print(st)
    return 0


def cmd_mcp(args) -> int:
    plan = build_plan(args.prompt, catalog_path=args.catalog, seed=args.seed)
    print(f"[info] connecting to engine MCP at {args.host}:{args.port}", file=sys.stderr)
    with McpClient(host=args.host, port=args.port) as mcp:
        status = mcp.status()
        print(f"[ok] engine status: {status}", file=sys.stderr)
        results = run_batch(plan, mcp)
    print(json.dumps(results, indent=2, ensure_ascii=False))
    return 0


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="creative-brain",
                                description="EVEngine Creative Brain agent.")
    sub = p.add_subparsers(dest="command", required=True)

    sp = sub.add_parser("parse", help="Parse intent -> SceneConfig JSON.")
    _add_common(sp)
    sp.add_argument("prompt", help="Natural-language scene request.")
    sp.set_defaults(func=cmd_parse)

    sp = sub.add_parser("plan", help="Full pipeline -> generation plan.")
    _add_common(sp)
    sp.add_argument("prompt", help="Natural-language scene request.")
    sp.add_argument("--out", help="Write plan JSON to this path.")
    sp.set_defaults(func=cmd_plan)

    sp = sub.add_parser("assets", help="List catalog categories / assets.")
    sp.add_argument("--catalog", default=str(DEFAULT_CATALOG))
    sp.add_argument("--category", default=None)
    sp.set_defaults(func=cmd_assets)

    sp = sub.add_parser("request", help="Enqueue async resource-agent fetch.")
    sp.add_argument("asset", help="Asset id to fetch.")
    sp.add_argument("--style", default="generic")
    sp.set_defaults(func=cmd_request)

    sp = sub.add_parser("status", help="Query resource request status.")
    sp.add_argument("request_id")
    sp.set_defaults(func=cmd_status)

    sp = sub.add_parser("mcp", help="Push plan + run batch via engine MCP.")
    _add_common(sp)
    sp.add_argument("prompt", help="Natural-language scene request.")
    sp.add_argument("--host", default="127.0.0.1")
    sp.add_argument("--port", type=int, default=7529)
    sp.set_defaults(func=cmd_mcp)

    p.add_argument("--schema", action="store_true", help="Print scene config JSON schema.")
    return p


def main(argv=None) -> int:
    p = build_parser()
    args = p.parse_args(argv)
    if getattr(args, "schema", False):
        print(json.dumps(SCENE_CONFIG_SCHEMA, indent=2, ensure_ascii=False))
        return 0
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
