"""与 EVEngine 引擎 MCP 的 JSON-RPC 客户端。

支持两种传输：
- tcp     : 直连引擎 TCP MCP 端口（eve run --mcp-port=7529）
- bridge  : 通过 tools/eve-mcp/server.js stdio 桥转发（默认）
协议：换行分帧 JSON-RPC 2.0。新增工具（机位/截图/场景）由引擎侧提供，
本客户端只做通用转发，能力名经 config.mcp_tools 对齐。
"""

from __future__ import annotations

import json
import os
import socket
import subprocess
import threading
import time
from typing import Any, Dict, Optional


class McpError(RuntimeError):
    pass


class _JsonRpcTransport:
    def send_recv(self, payload: Dict[str, Any]) -> Dict[str, Any]:
        raise NotImplementedError

    def close(self) -> None:
        raise NotImplementedError


class TcpTransport(_JsonRpcTransport):
    def __init__(self, host: str, port: int, timeout_ms: int):
        self.host = host
        self.port = port
        self.timeout = timeout_ms / 1000.0
        self._sock: Optional[socket.socket] = None
        self._buf = ""

    def connect(self) -> None:
        self._sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
        self._sock.settimeout(self.timeout)
        self._buf = ""

    def _read_line(self) -> str:
        assert self._sock is not None
        while "\n" not in self._buf:
            chunk = self._sock.recv(65536)
            if not chunk:
                raise McpError("connection closed by engine")
            self._buf += chunk.decode("utf-8", "replace")
        line, self._buf = self._buf.split("\n", 1)
        return line.strip()

    def send_recv(self, payload: Dict[str, Any]) -> Dict[str, Any]:
        req_id = payload.get("id")
        self._sock.sendall((json.dumps(payload) + "\n").encode("utf-8"))
        if req_id is None:
            return {}
        while True:
            line = self._read_line()
            if not line:
                continue
            try:
                resp = json.loads(line)
            except json.JSONDecodeError:
                continue
            if resp.get("id") == req_id or req_id is None:
                return resp

    def close(self) -> None:
        if self._sock:
            try:
                self._sock.close()
            finally:
                self._sock = None


class BridgeTransport(_JsonRpcTransport):
    def __init__(self, node: str, bridge_js: str, host: str, port: int, timeout_ms: int):
        self.node = node
        self.bridge_js = bridge_js
        self.host = host
        self.port = port
        self.timeout_ms = timeout_ms
        self._proc: Optional[subprocess.Popen] = None
        self._lock = threading.Lock()
        self._pending: Dict[int, Dict[str, Any]] = {}
        self._next_id = 1

    def _ensure(self) -> None:
        if self._proc and self._proc.poll() is None:
            return
        env = dict(os.environ)
        env["EVE_MCP_HOST"] = self.host
        env["EVE_MCP_PORT"] = str(self.port)
        env["EVE_MCP_CONNECT_TIMEOUT_MS"] = str(self.timeout_ms)
        self._proc = subprocess.Popen(
            [self.node, self.bridge_js],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            env=env,
            text=True,
            bufsize=1,
        )
        self._pending = {}
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self) -> None:
        assert self._proc and self._proc.stdout
        for line in self._proc.stdout:
            line = line.strip()
            if not line:
                continue
            try:
                resp = json.loads(line)
            except json.JSONDecodeError:
                continue
            rid = resp.get("id")
            if rid is not None:
                self._pending.pop(rid, None)

    def _alloc_id(self) -> int:
        rid = self._next_id
        self._next_id += 1
        return rid

    def send_recv(self, payload: Dict[str, Any]) -> Dict[str, Any]:
        with self._lock:
            self._ensure()
            rid = self._alloc_id()
            payload["id"] = rid
            self._pending[rid] = {}
            assert self._proc and self._proc.stdin
            self._proc.stdin.write(json.dumps(payload) + "\n")
            self._proc.stdin.flush()
            deadline = time.time() + self.timeout_ms / 1000.0
            while time.time() < deadline:
                if rid not in self._pending:
                    return {}
                time.sleep(0.01)
            self._pending.pop(rid, None)
            raise McpError(f"timeout waiting for id {rid}")

    def close(self) -> None:
        if self._proc:
            try:
                self._proc.terminate()
            finally:
                self._proc = None


class EvMcpClient:
    def __init__(self, cfg) -> None:
        eng = cfg["engine"]
        self.transport: _JsonRpcTransport
        if eng.get("transport") == "tcp":
            self.transport = TcpTransport(eng["host"], int(eng["port"]), int(eng.get("connect_timeout_ms", 5000)))
            self.transport.connect()  # type: ignore[attr-defined]
        else:
            bridge = os.path.join(os.path.dirname(__file__), "..", "..", "eve-mcp", "server.js")
            cfg_bridge = eng.get("bridge_js")
            if cfg_bridge:
                cand = cfg_bridge
                if not os.path.isabs(cand):
                    cand = os.path.normpath(
                        os.path.join(os.path.dirname(__file__), "..", "..", "..", cand)
                    )
                if os.path.exists(cand):
                    bridge = cand
            self.transport = BridgeTransport(
                eng.get("node", "node"), bridge, eng["host"], int(eng["port"]),
                int(eng.get("connect_timeout_ms", 5000)),
            )
        self._id = 1

    def _call(self, method: str, params: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        rid = self._id
        self._id += 1
        resp = self.transport.send_recv({"jsonrpc": "2.0", "id": rid, "method": method, "params": params or {}})
        if "error" in resp:
            raise McpError(f"{method}: {resp['error']}")
        return resp.get("result", {})

    def initialize(self) -> Dict[str, Any]:
        res = self._call("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "scene-qc-agent"}})
        self.transport.send_recv({"jsonrpc": "2.0", "method": "notifications/initialized"})
        return res

    def list_tools(self) -> list:
        res = self._call("tools/list")
        return res.get("tools", [])

    def call_tool(self, name: str, args: Optional[Dict[str, Any]] = None) -> str:
        res = self._call("tools/call", {"name": name, "arguments": args or {}})
        if isinstance(res, dict) and res.get("isError"):
            content = res.get("content") or []
            txt = " ".join(c.get("text", "") for c in content if c.get("type") == "text")
            raise McpError(f"tool {name}: {txt}")
        if isinstance(res, dict):
            content = res.get("content") or []
            return " ".join(c.get("text", "") for c in content if c.get("type") == "text")
        return str(res)

    def close(self) -> None:
        self.transport.close()
