"""Minimal newline JSON-RPC client for the EVEngine TCP MCP server."""

from __future__ import annotations

import json
import socket
import time
from typing import Any, Dict, Optional


class McpError(RuntimeError):
    pass


class McpClient:
    def __init__(self, host: str = "127.0.0.1", port: int = 7529, timeout_s: float = 30.0):
        self.host = host
        self.port = port
        self.timeout_s = timeout_s
        self._sock: Optional[socket.socket] = None
        self._buf = b""
        self._seq = 0

    def connect(self) -> None:
        self._sock = socket.create_connection((self.host, self.port), timeout=self.timeout_s)
        self._sock.settimeout(self.timeout_s)
        self._buf = b""
        self.call(
            "initialize",
            {
                "protocolVersion": "2025-06-18",
                "clientInfo": {"name": "whitebox-playtest", "version": "1.0"},
            },
        )
        self.send({"jsonrpc": "2.0", "method": "notifications/initialized"})

    def close(self) -> None:
        if self._sock:
            try:
                self._sock.close()
            finally:
                self._sock = None

    def __enter__(self) -> "McpClient":
        self.connect()
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()

    def send(self, obj: Dict[str, Any]) -> None:
        if not self._sock:
            raise McpError("not connected")
        self._sock.sendall((json.dumps(obj, ensure_ascii=False) + "\n").encode("utf-8"))

    def _recv(self, req_id: int) -> Dict[str, Any]:
        assert self._sock
        while True:
            while b"\n" in self._buf:
                line, self._buf = self._buf.split(b"\n", 1)
                if not line.strip():
                    continue
                msg = json.loads(line.decode("utf-8"))
                if msg.get("id") == req_id:
                    return msg
            chunk = self._sock.recv(65536)
            if not chunk:
                raise McpError("connection closed by engine")
            self._buf += chunk

    def call(self, method: str, params: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        self._seq += 1
        req: Dict[str, Any] = {"jsonrpc": "2.0", "id": self._seq, "method": method}
        if params is not None:
            req["params"] = params
        self.send(req)
        return self._recv(self._seq)

    def tool(self, name: str, arguments: Optional[Dict[str, Any]] = None) -> Any:
        resp = self.call("tools/call", {"name": name, "arguments": arguments or {}})
        if resp.get("error"):
            raise McpError(f"{name}: {resp['error']}")
        result = resp.get("result") or {}
        texts = [
            c.get("text", "")
            for c in result.get("content", [])
            if isinstance(c, dict) and c.get("type") == "text"
        ]
        text = "\n".join(texts)
        if text.startswith("error:"):
            raise McpError(text)
        try:
            return json.loads(text)
        except json.JSONDecodeError:
            return text

    def poll_input_until_ready(self, batch_id: Optional[int] = None, timeout_s: float = 30.0) -> Dict[str, Any]:
        deadline = time.monotonic() + timeout_s
        args: Dict[str, Any] = {}
        if batch_id is not None:
            args["id"] = batch_id
        last: Dict[str, Any] = {}
        while time.monotonic() < deadline:
            last = self.tool("eve_input_poll", args)
            if isinstance(last, dict) and last.get("status") == "ready":
                return last
            time.sleep(0.05)
        raise McpError(f"timed out waiting for input batch: {last}")
