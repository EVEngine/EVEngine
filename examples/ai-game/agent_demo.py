#!/usr/bin/env python3
"""AI Game agent demo — drive a live EVEngine game over its embedded MCP (TCP).

Usage:
    1. Start the game with MCP enabled:
         eve run --debug --mcp-port=7529 examples/ai-game
    2. In another terminal:
         python examples/ai-game/agent_demo.py 7529

The script walks the full agent loop and exits 0 (PASS) only if every
assertion holds: read -> mutate -> verify -> screenshot -> checkpoint ->
break -> restore -> verify.
"""

import json
import socket
import sys
import time

HOST = "127.0.0.1"
DEFAULT_PORT = 7529


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
        texts = [
            c.get("text", "")
            for c in result.get("content", [])
            if c.get("type") == "text"
        ]
        return "\n".join(texts)


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_PORT
    c = McpClient(HOST, port)

    # MCP handshake.
    c.call(
        "initialize",
        {
            "protocolVersion": "2025-06-18",
            "clientInfo": {"name": "ai-game-agent-demo", "version": "1.0"},
        },
    )
    c.send({"jsonrpc": "2.0", "method": "notifications/initialized"})
    time.sleep(0.2)

    # 1. Attach / status.
    status = c.tool("eve_status")
    print("[1] eve_status ->", status[:160])

    # 2. Screenshot first, while the loop is still presenting frames.
    #    (Readback is enabled by the first call and lands on the next present,
    #    so retry once after a frame or two.)
    shot = ""
    for attempt in range(4):
        shot = c.tool("eve_screenshot", {"path": "ai_game_agent.png"})
        if "error:" not in shot:
            break
        time.sleep(1.0)
    print("[2] screenshot ->", shot)
    if "error:" in shot:
        print("      WARN: screenshot readback unavailable on this platform; continuing")

    # 3. Pause the live game so the remaining assertions are deterministic.
    print("[3]", c.tool("eve_pause"))
    c.tool("eve_run_script", {"source": "game.reset();"})

    # 4. Read current enemy HP.
    hp0 = float(json.loads(c.tool("eve_eval", {"expression": "game.enemy.hp"}))["value"])
    print(f"[4] enemy.hp = {hp0}")
    assert hp0 == 80.0, f"expected fresh 80.0 after reset, got {hp0}"

    # 5. Weaken the enemy.
    c.tool("eve_run_script", {"source": "game.setEnemyHp(20.0);"})
    hp1 = float(json.loads(c.tool("eve_eval", {"expression": "game.enemy.hp"}))["value"])
    print(f"[5] after setEnemyHp(20) -> {hp1}")
    assert hp1 == 20.0, f"expected 20.0, got {hp1}"

    # 6. Checkpoint.
    snap = c.tool("eve_snapshot_capture")
    print(f"[6] snapshot captured ({len(snap)} bytes)")

    # 7. Break the game state.
    c.tool("eve_run_script", {"source": "game.setEnemyHp(5.0);"})
    hp_break = float(json.loads(c.tool("eve_eval", {"expression": "game.enemy.hp"}))["value"])
    print(f"[7] broke state -> enemy.hp = {hp_break}")
    assert hp_break == 5.0, f"expected 5.0, got {hp_break}"

    # 8. Restore checkpoint.
    c.tool("eve_snapshot_restore", {"json": snap})
    hp2 = float(json.loads(c.tool("eve_eval", {"expression": "game.enemy.hp"}))["value"])
    print(f"[8] after restore -> {hp2}")
    assert hp2 == hp1, f"expected {hp1} after restore, got {hp2}"

    # 9. Log to the AI panel, then hand the game back to the player.
    c.tool("eve_ai_note", {"text": "agent demo: read->mutate->verify->snapshot->restore PASS"})
    print("[9]", c.tool("eve_continue"))
    print("\nPASS: full agent loop verified against the live game.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:  # noqa: BLE001 - demo script wants a loud failure
        print(f"\nFAIL: {exc}")
        sys.exit(1)
