"""资产入库接口与异步状态回调（经 EVEngine MCP）。

- ingest: 处理成功 -> 调用 MCP 入库，自动生成资产标签与元数据，返回引擎资产 URI。
- notify: 处理成功 / 失败 -> 异步回调通知场景生成 Agent 素材就绪 / 获取失败。
支持 bridge（tools/eve-mcp/server.js stdio 桥）与 tcp 两种传输；不可用时降级 dry-run。
"""

from __future__ import annotations

import json
import os
import socket
import subprocess
import threading
import time
from typing import Any, Dict, Optional

from .report import AssetOutcome, license_label


class McpError(RuntimeError):
    pass


class _JsonRpcTransport:
    def send_recv(self, payload: Dict[str, Any]) -> Dict[str, Any]:
        raise NotImplementedError

    def close(self) -> None:
        raise NotImplementedError


class TcpTransport(_JsonRpcTransport):
    def __init__(self, host: str, port: int, timeout_ms: int):
        self.host, self.port = host, port
        self.timeout = timeout_ms / 1000.0
        self._sock: Optional[socket.socket] = None
        self._buf = ""

    def connect(self) -> None:
        self._sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
        self._sock.settimeout(self.timeout)
        self._buf = ""

    def _read_line(self) -> str:
        while "\n" not in self._buf:
            chunk = self._sock.recv(65536)
            if not chunk:
                raise McpError("connection closed by engine")
            self._buf += chunk.decode("utf-8", "replace")
        line, self._buf = self._buf.split("\n", 1)
        return line.strip()

    def send_recv(self, payload: Dict[str, Any]) -> Dict[str, Any]:
        rid = payload.get("id")
        self._sock.sendall((json.dumps(payload) + "\n").encode("utf-8"))
        if rid is None:
            return {}
        while True:
            line = self._read_line()
            if not line:
                continue
            try:
                resp = json.loads(line)
            except json.JSONDecodeError:
                continue
            if resp.get("id") == rid or rid is None:
                return resp

    def close(self) -> None:
        if self._sock:
            try:
                self._sock.close()
            finally:
                self._sock = None


class BridgeTransport(_JsonRpcTransport):
    def __init__(self, node: str, bridge_js: str, host: str, port: int, timeout_ms: int):
        self.node, self.bridge_js = node, bridge_js
        self.host, self.port = host, port
        self.timeout_ms = timeout_ms
        self._proc: Optional[subprocess.Popen] = None
        self._lock = threading.Lock()
        self._pending: Dict[int, Dict[str, Any]] = {}
        self._next_id = 1

    def _ensure(self) -> None:
        if self._proc and self._proc.poll() is None:
            return
        env = dict(os.environ)
        env.update({"EVE_MCP_HOST": self.host, "EVE_MCP_PORT": str(self.port),
                    "EVE_MCP_CONNECT_TIMEOUT_MS": str(self.timeout_ms)})
        self._proc = subprocess.Popen(
            [self.node, self.bridge_js], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, env=env, text=True, bufsize=1)
        self._pending = {}
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self) -> None:
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


class McpClient:
    def __init__(self, cfg) -> None:
        m = cfg["mcp"]
        self.transport: _JsonRpcTransport
        if m.get("transport") == "tcp":
            self.transport = TcpTransport(m["host"], int(m["port"]), int(m.get("connect_timeout_ms", 5000)))
            self.transport.connect()
        else:
            bridge = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "eve-mcp", "server.js")
            cfg_bridge = m.get("bridge_js")
            if cfg_bridge:
                cand = cfg_bridge
                if not os.path.isabs(cand):
                    cand = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", cand))
                if os.path.exists(cand):
                    bridge = cand
            self.transport = BridgeTransport(
                m.get("node", "node"), bridge, m["host"], int(m["port"]),
                int(m.get("connect_timeout_ms", 5000)))
        self._id = 1

    def _call(self, method: str, params: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        rid = self._id
        self._id += 1
        resp = self.transport.send_recv({"jsonrpc": "2.0", "id": rid, "method": method, "params": params or {}})
        if "error" in resp:
            raise McpError(f"{method}: {resp['error']}")
        return resp.get("result", {})

    def initialize(self) -> Dict[str, Any]:
        res = self._call("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                        "clientInfo": {"name": "asset-pipeline-agent"}})
        self.transport.send_recv({"jsonrpc": "2.0", "method": "notifications/initialized"})
        return res

    def call_tool(self, name: str, args: Optional[Dict[str, Any]] = None) -> str:
        res = self._call("tools/call", {"name": name, "arguments": args or {}})
        if isinstance(res, dict) and res.get("isError"):
            content = res.get("content") or []
            txt = " ".join(c.get("text", "") for c in content if c.get("type") == "text")
            raise McpError(f"tool {name}: {txt}")
        if isinstance(res, dict):
            return " ".join(c.get("text", "") for c in (res.get("content") or []) if c.get("type") == "text")
        return str(res)

    def close(self) -> None:
        self.transport.close()


class AssetRegistry:
    """入库 + 状态回调门面；引擎 MCP 不可用时降级 dry-run 记录。"""

    def __init__(self, cfg, dry_run: bool = False):
        self.cfg = cfg
        self.dry_run = dry_run
        self.client: Optional[McpClient] = None
        if not dry_run:
            try:
                self.client = McpClient(cfg)
                self.client.initialize()
            except McpError as e:
                print(f"[warn] engine MCP unavailable ({e}); ingest/notify 降级 dry-run", file=__import__("sys").stderr)
                self.dry_run = True

    # ---- 标签与元数据自动生成 ----
    @staticmethod
    def build_metadata(outcome: AssetOutcome) -> Dict[str, Any]:
        c = outcome.candidate
        b = outcome.blender
        tags = ["合规", "external", c.license, c.source]
        if c.style:
            tags.append(str(c.style))
        tags = list(dict.fromkeys(tags))
        metadata = {
            "name": c.name,
            "source": c.source,
            "asset_id": c.asset_id,
            "license": license_label(c.license),
            "author": c.author,
            "author_url": c.author_url,
            "attribution": c.attribution,
            "up_axis": "Y",
            "stats": {"vertices": b.vertices, "triangles": b.triangles,
                      "materials": b.materials, "textures": b.textures},
        }
        return {"tags": tags, "metadata": metadata}

    def ingest(self, outcome: AssetOutcome, uri: str) -> str:
        """处理成功 -> 入库；返回引擎资产 URI（引用）。"""
        meta = self.build_metadata(outcome)
        if self.dry_run:
            asset_uri = f"engine://asset/{outcome.candidate.asset_id}"
            outcome.tags = meta["tags"]
            outcome.ingested = True
            outcome.asset_uri = asset_uri
            return asset_uri
        tool = self.cfg.mcp_tool("ingest")
        args = dict(tool.get("args", {}))
        args["uri"] = uri
        args["tags"] = meta["tags"]
        args["metadata"] = meta["metadata"]
        result = self.client.call_tool(tool["name"], args)
        outcome.tags = meta["tags"]
        outcome.ingested = True
        outcome.asset_uri = result.strip() or uri
        return outcome.asset_uri

    def notify(self, outcome: AssetOutcome) -> None:
        """异步状态回调：素材就绪 / 获取失败 -> 通知场景生成 Agent。"""
        callback = self.cfg.get("callback", outcome.reason) or ""
        status = "READY" if outcome.status == "ACCEPTED" else "FAILED"
        payload = {
            "asset_uri": outcome.asset_uri,
            "name": outcome.candidate.name,
            "error": outcome.error,
            "tags": outcome.tags,
            "attribution": outcome.candidate.attribution,
        }
        if self.dry_run:
            print(f"[notify::{status}] callback={callback} payload={json.dumps(payload, ensure_ascii=False)}")
            return
        tool = self.cfg.mcp_tool("notify")
        args = dict(tool.get("args", {}))
        args["callback"] = callback
        args["status"] = status
        args["payload"] = payload
        try:
            self.client.call_tool(tool["name"], args)
        except McpError as e:
            print(f"[warn] notify failed: {e}", file=__import__("sys").stderr)

    def close(self) -> None:
        if self.client:
            self.client.close()
