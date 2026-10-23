"""CLI entry for white-box batch scene + automatic playtest."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from .recipe import default_recipe, load_recipe
from .runner import run_recipe


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="whitebox-playtest",
        description="Batch white-box scene setup/adjust and automatic playtest via EVEngine MCP.",
    )
    parser.add_argument(
        "recipe",
        nargs="?",
        help="Path to eve.whitebox.playtest-recipe JSON (default built-in corridor blockout)",
    )
    parser.add_argument("--host", default="127.0.0.1", help="Engine MCP host")
    parser.add_argument("--port", type=int, default=7529, help="Engine MCP port")
    parser.add_argument("--dry-run", action="store_true", help="No engine; in-memory scene + simulated input")
    parser.add_argument("--mcp-stdio", action="store_true", help="Serve as stdio MCP (for Cursor / Claude)")
    parser.add_argument("--report", type=Path, help="Write report JSON to this path")
    parser.add_argument("--trace", action="store_true", help="Pretty-print the report")
    return parser


def main(argv: list[str] | None = None) -> int:
    # Keep import-time free of stdio mutation; enable at program entry only.
    scripts = Path(__file__).resolve().parents[3] / "scripts"
    if str(scripts) not in sys.path:
        sys.path.insert(0, str(scripts))
    import utf8_stdio  # noqa: WPS433

    utf8_stdio.enable_utf8_stdio()

    args = build_parser().parse_args(argv)
    if args.mcp_stdio:
        from .server import serve_stdio

        return serve_stdio(host=args.host, port=args.port, dry_run=args.dry_run)

    recipe = load_recipe(args.recipe) if args.recipe else default_recipe()
    report = run_recipe(recipe, host=args.host, port=args.port, dry_run=args.dry_run)
    text = json.dumps(report, ensure_ascii=False, indent=2 if args.trace else None)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(text + "\n", encoding="utf-8")
    print(text)
    return 0 if report.get("passed") else 1


if __name__ == "__main__":
    raise SystemExit(main())
