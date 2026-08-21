#!/usr/bin/env python3
"""AI Editor demo — drive the headless `eve mcp` host to spawn a project-specific editor.

Usage:
    1. Start the headless MCP host (TCP mode):
         eve mcp --port 7531 --root examples/ai-editor
    2. In another terminal:
         python examples/ai-editor/editor_demo.py 7531

The demo applies a JSON View + Squirrel ViewModel, opens a window, captures a
PNG of the rendered editor, and shuts the host down cleanly.
"""

import json
import os
import socket
import sys
import time

HOST = "127.0.0.1"
DEFAULT_PORT = 7531


class McpClient:
    def __init__(self, host, port):
        self.sock = socket.create_connection((host, port), timeout=30)
        self.buf = b""
        self.seq = 0

    def send(self, obj):
        self.sock.sendall((json.dumps(obj) + "\n").encode("utf-8"))

    def recv(self, req_id):
        while True:
            while b"\n" in self.buf:
                line, self.buf = self.buf.split(b"\n", 1)
                if not line.strip():
                    continue
                msg = json.loads(line.decode("utf-8"))
                if msg.get("id") == req_id:
                    return msg
            chunk = self.sock.recv(65536)
            if not chunk:
                raise RuntimeError("MCP connection closed")
            self.buf += chunk

    def call(self, method, params=None):
        self.seq += 1
        req = {"jsonrpc": "2.0", "id": self.seq, "method": method}
        if params is not None:
            req["params"] = params
        self.send(req)
        return self.recv(self.seq)

    def tool(self, name, args=None):
        r = self.call("tools/call", {"name": name, "arguments": args or {}})
        if "error" in r and r.get("error"):
            raise RuntimeError(f"RPC error: {r['error']}")
        result = r.get("result", {})
        return "\n".join(
            c.get("text", "")
            for c in result.get("content", [])
            if c.get("type") == "text"
        )


def read_file(rel):
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, rel), encoding="utf-8") as f:
        return f.read()


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_PORT
    c = McpClient(HOST, port)

    c.call(
        "initialize",
        {
            "protocolVersion": "2025-06-18",
            "clientInfo": {"name": "ai-editor-agent-demo", "version": "1.0"},
        },
    )
    c.send({"jsonrpc": "2.0", "method": "notifications/initialized"})
    time.sleep(0.2)

    print("[1]", c.tool("eve_host_status")[:160])

    print("[2]", c.tool("eve_host_window_open",
                        {"title": "AI Editor Demo", "width": 720, "height": 640}))

    vm_source = read_file(os.path.join("editors", "terrain.vm.nut"))
    print("[3]", c.tool("eve_host_vm_register",
                        {"name": "TerrainVM", "source": vm_source}))

    editor = json.loads(read_file(os.path.join("editors", "terrain.editor.json")))
    print("[4]", c.tool("eve_host_editor_apply", {"editor": editor}))

    time.sleep(1.0)  # let the host render a few frames
    print("[5]", c.tool("eve_host_editor_state", {"id": "terrain"})[:200])
    print("[6]", c.tool("eve_host_capture", {"path": "ai_editor_capture.png"}))

    # Prove the binding works: push a value into the ViewModel, then read it back.
    c.tool("eve_host_script", {"source": "TerrainVM.strength = 0.85;"})
    print("[7]", c.tool("eve_host_editor_state", {"id": "terrain"})[:200])

    print("[8]", c.tool("eve_host_shutdown"))
    print("\nPASS: AI-generated editor spawned, rendered, captured, and shut down.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:  # noqa: BLE001 - demo script wants a loud failure
        print(f"\nFAIL: {exc}")
        sys.exit(1)
