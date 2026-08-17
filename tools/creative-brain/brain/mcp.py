"""Python MCP client over the engine's TCP MCP server, plus a batch driver.

Wire protocol (matches ``src/engine/devtools/McpServer.cpp``):
  - TCP, newline-delimited JSON-RPC 2.0 messages.
  - handshake: ``initialize`` -> ``notifications/initialized`` -> ``tools/list``.
  - call: ``tools/call`` with ``params`` = {"name": ..., "arguments": {...}}.
  - result: {"result": {"content": [{"type": "text", "text": ...}]}}.

Batch generation pushes a :class:`brain.schema.GenerationPlan` into the live VM
as Squirrel via the ``eve_run_script`` tool, then runs one snippet per step.
Snippets are defensive: if the engine API for an action is absent they no-op
with a trace line instead of crashing the VM.
"""

from __future__ import annotations

import json
import socket
from pathlib import Path
from typing import Any, Dict, List, Optional

from brain.schema import GenerationPlan

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 7529
CONNECT_TIMEOUT_S = 5.0
READ_TIMEOUT_S = 10.0


class McpError(RuntimeError):
    pass


class McpClient:
    """Minimal blocking JSON-RPC client for the EVEngine MCP server."""

    def __init__(self, host: str = DEFAULT_HOST, port: int = DEFAULT_PORT):
        self.host = host
        self.port = port
        self._sock: Optional[socket.socket] = None
        self._buf = ""

    def connect(self) -> None:
        self._sock = socket.create_connection(
            (self.host, self.port), timeout=CONNECT_TIMEOUT_S
        )
        self._sock.settimeout(READ_TIMEOUT_S)
        self._send({"jsonrpc": "2.0", "id": 1, "method": "initialize",
                    "params": {"protocolVersion": "2025-06-18",
                               "clientInfo": {"name": "creative-brain"}}})
        self._read_response(1)
        self._send({"jsonrpc": "2.0", "method": "notifications/initialized"})

    def close(self) -> None:
        if self._sock:
            try:
                self._sock.close()
            finally:
                self._sock = None

    def __enter__(self) -> "McpClient":
        self.connect()
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def _send(self, msg: Dict[str, Any]) -> None:
        if not self._sock:
            raise McpError("not connected")
        self._sock.sendall((json.dumps(msg) + "\n").encode("utf-8"))

    def _read_line(self) -> str:
        assert self._sock
        while "\n" not in self._buf:
            chunk = self._sock.recv(4096)
            if not chunk:
                raise McpError("connection closed by engine")
            self._buf += chunk.decode("utf-8", "replace")
        line, self._buf = self._buf.split("\n", 1)
        return line.strip()

    def _read_response(self, expected_id: int) -> Dict[str, Any]:
        while True:
            line = self._read_line()
            if not line:
                continue
            msg = json.loads(line)
            if msg.get("id") == expected_id:
                return msg

    def call_tool(self, name: str, arguments: Optional[Dict[str, Any]] = None) -> str:
        """Call an MCP tool and return the concatenated text content."""
        rid = hash((name, json.dumps(arguments or {}, sort_keys=True))) & 0xFFFFFF
        self._send({"jsonrpc": "2.0", "id": rid, "method": "tools/call",
                    "params": {"name": name, "arguments": arguments or {}}})
        msg = self._read_response(rid)
        if "error" in msg and msg["error"]:
            raise McpError(f"{name} error: {msg['error']}")
        result = msg.get("result") or {}
        content = result.get("content") or []
        return "\n".join(
            c.get("text", "") for c in content if c.get("type") == "text"
        )

    def run_script(self, source: str) -> str:
        """Run a Squirrel snippet in the live VM (wraps ``eve_run_script``)."""
        return self.call_tool("eve_run_script", {"source": source})

    def status(self) -> str:
        return self.call_tool("eve_status")


def _squirrel_literal(obj: Any) -> str:
    """Encode a Python object as a Squirrel table/array literal (best effort)."""
    if isinstance(obj, bool):
        return "true" if obj else "false"
    if isinstance(obj, (int, float)):
        return str(obj)
    if isinstance(obj, str):
        return '"' + obj.replace("\\", "\\\\").replace('"', '\\"') + '"'
    if isinstance(obj, dict):
        items = ", ".join(
            _squirrel_literal(k) + " = " + _squirrel_literal(v) for k, v in obj.items()
        )
        return "{" + items + "}"
    if isinstance(obj, (list, tuple)):
        return "[" + ", ".join(_squirrel_literal(v) for v in obj) + "]"
    return "null"


def snippet_install_plan(plan: GenerationPlan) -> str:
    """Publish the plan JSON into the VM roottable for traceability.

    Execution itself goes through `::scene_director.modify` (see
    `snippet_for_step`); the plan is kept as a roottable string so downstream
    tools (`eve_ai_log`, scene-qc) can attribute the build to a story intent.
    """
    payload = json.dumps(plan.to_dict(), ensure_ascii=False)
    lit = '"' + payload.replace("\\", "\\\\").replace('"', '\\"') + '"'
    return (
        "::_cb_plan_json <- " + lit + ";\n"
        "::_cb_plan <- null;\n"
        'print("creative-brain: plan installed, steps=' + str(len(plan.steps)) + '");\n'
    )


def _modify_snippet(action: str, target: str, params: Dict[str, Any]) -> str:
    """Build a snippet that dispatches through the scene_director kit."""
    act = _squirrel_literal(action)
    tgt = _squirrel_literal(target)
    par = _squirrel_literal(params or {})
    return (
        "if (\"scene_director\" in getroottable() && scene_director != null) {\n"
        f"  return ::scene_director.modify({act}, {tgt}, {par});\n"
        "} else {\n"
        '  return { ok = false, error = "scene_director not installed" };\n'
        "}\n"
    )


def _cell_to_world(x: int, y: int, width: int, height: int, tile: float = 2.0) -> Dict[str, float]:
    """Map a creative-brain layout cell (grid) to a 3D stage position (Y-up)."""
    return {
        "x": round((x - width / 2.0) * tile, 3),
        "y": 0.0,
        "z": round((y - height / 2.0) * tile, 3),
    }


def snippet_for_step(action: str, target: str, params: Dict[str, Any]) -> str:
    """Translate one GenerationPlan step into a real scene-director action."""
    p = dict(params or {})

    if action == "terrain":
        w = int(p.get("width", 32))
        h = int(p.get("height", 32))
        return _modify_snippet(
            "add_object",
            "terrain",
            {
                "id": "terrain",
                "kind": "ground",
                "pos": [0.0, 0.0, 0.0],
                "scale": [max(8.0, w * 2.0), 1.0, max(8.0, h * 2.0)],
                "tint": [0.42, 0.46, 0.40],
                "roughness": 0.95,
            },
        )

    if action == "set_lighting":
        return _modify_snippet("lighting", "scene", {
            "timeOfDay": p.get("timeOfDay", "day"),
            "atmosphere": p.get("atmosphere", "sunny midday"),
            "intensity": float(p.get("intensity", 1.0)),
        })

    if action == "place":
        w = int(p.get("mapWidth", 32))
        h = int(p.get("mapHeight", 32))
        pos = _cell_to_world(int(p.get("x", 0)), int(p.get("y", 0)), w, h)
        return _modify_snippet(
            "add_object",
            str(target),
            {
                "id": f"{target}_{p.get('x', 0)}_{p.get('y', 0)}",
                "kind": str(target),
                "pos": [pos["x"], pos["y"], pos["z"]],
                "seed": int(p.get("seed", 1)),
            },
        )

    # Fallback: forward the raw action through the kit's dispatcher.
    return _modify_snippet(action, str(target), p)


def run_batch(plan: GenerationPlan, mcp: McpClient) -> List[dict]:
    """Push a plan + run its steps against the engine, returning per-step results."""
    results: List[dict] = []
    install_result = install_kit(mcp)
    results.append({"action": "install_scene_director", "ok": install_result})
    mcp.run_script(snippet_install_plan(plan))
    results.append({"action": "install_plan", "ok": True})
    for step in plan.steps:
        src = snippet_for_step(step.action, step.target, step.params)
        try:
            out = mcp.run_script(src)
            results.append({"action": step.action, "target": step.target, "ok": True, "out": out})
        except McpError as e:
            results.append({"action": step.action, "target": step.target, "ok": False, "error": str(e)})
    return results


def install_kit(mcp: McpClient) -> bool:
    """Push the scene-director Squirrel kit into the live VM (idempotent)."""
    kit = Path(__file__).resolve().parents[3] / "src" / "scripts" / "scene_director.nut"
    try:
        source = kit.read_text(encoding="utf-8")
    except OSError:
        return False
    try:
        mcp.run_script(source)
        return True
    except McpError:
        return False
