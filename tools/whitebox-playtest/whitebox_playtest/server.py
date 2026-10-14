"""stdio MCP server exposing white-box batch + playtest tools.

Cursor / Claude Desktop speak MCP over stdio. This process advertises high-level
tools and forwards them to a live EVEngine TCP MCP (`eve run --mcp-port`), or
runs dry-run recipes when EVE_WB_DRY_RUN=1.
"""

from __future__ import annotations

import json
import os
import sys
from typing import Any, Dict, Optional

from .recipe import default_recipe, validate_recipe
from .runner import run_recipe

TOOLS = [
    {
        "name": "wb_run",
        "description": "Run a full white-box playtest recipe: reset/batch scene, inject inputs, "
        "poll until ready, run eve_play requests, evaluate assert[]. "
        "Recipe schemaId=eve.whitebox.playtest-recipe schemaVersion=1.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "recipe": {"type": "object", "description": "Full recipe object"},
                "dryRun": {"type": "boolean"},
                "host": {"type": "string"},
                "port": {"type": "integer"},
            },
        },
    },
    {
        "name": "wb_apply_scene",
        "description": "Batch apply white-box scene actions (eve_scene_batch shape).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "actions": {"type": "array", "items": {"type": "object"}},
                "reset": {"type": "boolean"},
                "stopOnError": {"type": "boolean"},
                "dryRun": {"type": "boolean"},
                "host": {"type": "string"},
                "port": {"type": "integer"},
            },
            "required": ["actions"],
        },
    },
    {
        "name": "wb_playtest",
        "description": "Inject playtest inputs (and optional eve_play batch), poll to ready, return report.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "inputs": {"type": "array", "items": {"type": "object"}},
                "waitFrames": {"type": "integer"},
                "capture": {"type": "boolean"},
                "capturePath": {"type": "string"},
                "play": {"type": "array", "items": {"type": "object"}},
                "assert": {"type": "array", "items": {"type": "object"}},
                "dryRun": {"type": "boolean"},
                "host": {"type": "string"},
                "port": {"type": "integer"},
            },
            "required": ["inputs"],
        },
    },
    {
        "name": "wb_default_recipe",
        "description": "Return the built-in corridor blockout playtest recipe.",
        "inputSchema": {"type": "object", "properties": {}},
    },
]


def _text_result(payload: Any, is_error: bool = False) -> Dict[str, Any]:
    text = payload if isinstance(payload, str) else json.dumps(payload, ensure_ascii=False)
    return {"content": [{"type": "text", "text": text}], "isError": is_error}


class WhiteboxMcpServer:
    def __init__(self, host: str = "127.0.0.1", port: int = 7529, dry_run: bool = False):
        self.host = host
        self.port = port
        self.dry_run = dry_run or os.environ.get("EVE_WB_DRY_RUN", "").lower() in ("1", "true", "yes", "on")

    def handle(self, msg: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        method = msg.get("method")
        req_id = msg.get("id")
        params = msg.get("params") or {}
        if method == "initialize":
            return {
                "jsonrpc": "2.0",
                "id": req_id,
                "result": {
                    "protocolVersion": "2025-06-18",
                    "capabilities": {"tools": {}},
                    "serverInfo": {"name": "eve-whitebox-playtest", "version": "1.0.0"},
                },
            }
        if method == "notifications/initialized" or method is None and req_id is None:
            return None
        if method == "tools/list":
            return {"jsonrpc": "2.0", "id": req_id, "result": {"tools": TOOLS}}
        if method == "tools/call":
            name = params.get("name", "")
            arguments = params.get("arguments") or {}
            try:
                result = self._call_tool(name, arguments)
                return {"jsonrpc": "2.0", "id": req_id, "result": _text_result(result)}
            except Exception as exc:  # noqa: BLE001 — surface to MCP client
                return {
                    "jsonrpc": "2.0",
                    "id": req_id,
                    "result": _text_result({"ok": False, "error": str(exc)}, True),
                }
        if req_id is None:
            return None
        return {
            "jsonrpc": "2.0",
            "id": req_id,
            "error": {"code": -32601, "message": f"method not found: {method}"},
        }

    def _call_tool(self, name: str, arguments: Dict[str, Any]) -> Any:
        dry = bool(arguments.get("dryRun", self.dry_run))
        host = str(arguments.get("host", self.host))
        port = int(arguments.get("port", self.port))
        if name == "wb_default_recipe":
            return default_recipe()
        if name == "wb_run":
            recipe = arguments.get("recipe") or default_recipe()
            return run_recipe(validate_recipe(recipe), host=host, port=port, dry_run=dry)
        if name == "wb_apply_scene":
            recipe = {
                "schemaId": "eve.whitebox.playtest-recipe",
                "schemaVersion": 1,
                "reset": bool(arguments.get("reset", False)),
                "scene": list(arguments.get("actions") or []),
                "stopOnError": bool(arguments.get("stopOnError", True)),
                "assert": [{"path": "scene.ok", "op": "eq", "value": True}],
            }
            return run_recipe(recipe, host=host, port=port, dry_run=dry)
        if name == "wb_playtest":
            recipe = {
                "schemaId": "eve.whitebox.playtest-recipe",
                "schemaVersion": 1,
                "reset": False,
                "scene": [],
                "inputs": list(arguments.get("inputs") or []),
                "waitFrames": int(arguments.get("waitFrames") or 0),
                "capture": bool(arguments.get("capture") or False),
                "capturePath": arguments.get("capturePath"),
                "play": list(arguments.get("play") or []),
                "assert": list(arguments.get("assert") or [{"path": "ok", "op": "eq", "value": True}]),
            }
            return run_recipe(recipe, host=host, port=port, dry_run=dry)
        raise ValueError(f"unknown tool {name}")


def serve_stdio(host: str = "127.0.0.1", port: int = 7529, dry_run: bool = False) -> int:
    server = WhiteboxMcpServer(host=host, port=port, dry_run=dry_run)
    for raw in sys.stdin:
        line = raw.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except json.JSONDecodeError:
            continue
        reply = server.handle(msg)
        if reply is not None:
            sys.stdout.write(json.dumps(reply, ensure_ascii=False) + "\n")
            sys.stdout.flush()
    return 0
