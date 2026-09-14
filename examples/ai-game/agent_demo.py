#!/usr/bin/env python3
"""AI Game agent demo — drive a live EVEngine game through eve_play (TCP MCP).

Usage:
    1. Start the game with MCP enabled:
         eve run --debug --mcp-port=7529 examples/ai-game
    2. In another terminal:
         python examples/ai-game/agent_demo.py 7529

The script walks the Play Host loop and exits 0 (PASS) only if every
assertion holds: pause -> observe -> step frames -> checkpoint -> restore.
It does not use eve_eval or eve_run_script.
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


def play(client, **request):
    request.setdefault("schemaId", "evengine.play-request")
    request.setdefault("schemaVersion", 1)
    text = client.tool("eve_play", {"request": request})
    if text.startswith("error:"):
        raise RuntimeError(text)
    return json.loads(text)


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_PORT
    c = McpClient(HOST, port)

    c.call(
        "initialize",
        {
            "protocolVersion": "2025-06-18",
            "clientInfo": {"name": "ai-game-agent-demo", "version": "1.0"},
        },
    )
    c.send({"jsonrpc": "2.0", "method": "notifications/initialized"})
    time.sleep(0.2)

    status = play(c, op="status")
    print("[1] eve_play status ->", json.dumps(status)[:180])
    assert status.get("contractId") == "examples/ai-game", status
    development = json.loads(c.tool("eve_agent_session_start", {
        "objective": "Verify the live game through the Play Host contract",
        "criteria": [
            {"id": "state", "description": "Declared combat-alive fields are observable"},
            {"id": "recovery", "description": "Checkpoint restore returns to the captured tick"},
            {"id": "visual", "description": "A rendered frame is captured", "required": False},
        ],
    }))
    assert development.get("status") == "applied", development
    development_session = development["sessionId"]

    shot_ok = False
    try:
        captured = play(c, op="capture", path="ai_game_agent.png")
        print("[2] capture ->", captured)
        shot_ok = "path" in captured
    except RuntimeError as exc:
        print("[2] capture unavailable:", exc)

    print("[3]", play(c, op="clock", mode="pause"))
    c.tool("eve_agent_session_advance", {"sessionId": development_session, "phase": "modify"})
    c.tool("eve_agent_session_advance", {"sessionId": development_session, "phase": "run"})

    observed = play(c, op="observe", observation="combat-alive")
    state0 = observed["state"]
    tick0 = int(state0["tick"])
    print(f"[4] observe combat-alive -> {state0}")
    assert "enemy.hp" in state0 and "player.hp" in state0, state0
    c.tool("eve_agent_session_advance", {"sessionId": development_session, "phase": "observe"})

    checkpoint = play(c, op="checkpoint", mode="capture")
    snap = checkpoint["json"]
    print(f"[5] checkpoint captured ({len(snap)} bytes)")

    stepped = play(c, op="step", clock="frame", count=8)
    print("[6] step ->", stepped)
    observed1 = play(c, op="observe", observation="combat-alive")
    tick1 = int(observed1["state"]["tick"])
    print(f"[6] after step tick {tick0} -> {tick1}")
    assert tick1 > tick0, f"expected tick to advance, got {tick0} then {tick1}"

    restored = play(c, op="checkpoint", mode="restore", json=snap)
    print("[7] restore ->", restored)
    observed2 = play(c, op="observe", observation="combat-alive")
    tick2 = int(observed2["state"]["tick"])
    print(f"[8] after restore tick -> {tick2}")
    assert tick2 == tick0, f"expected {tick0} after restore, got {tick2}"

    c.tool("eve_agent_session_advance", {"sessionId": development_session, "phase": "verify"})
    evidence = [
        ("state", "runtime-observation", "combat-alive fields were observed", ""),
        ("recovery", "checkpoint", "snapshot restore returned the captured tick", ""),
    ]
    if shot_ok:
        evidence.append(("visual", "screenshot", "live frame captured", "ai_game_agent.png"))
    for criterion, kind, summary, artifact in evidence:
        receipt = json.loads(c.tool("eve_agent_session_evidence", {
            "sessionId": development_session,
            "criterionId": criterion,
            "kind": kind,
            "status": "pass",
            "summary": summary,
            "artifact": artifact,
        }))
        assert receipt.get("status") == "applied", receipt
    completed = json.loads(c.tool("eve_agent_session_complete", {
        "sessionId": development_session,
        "summary": "Play Host observe/step/checkpoint loop verified",
    }))
    assert completed.get("phase") == "complete", completed

    c.tool("eve_ai_note", {"text": "agent demo: eve_play observe/step/checkpoint PASS"})
    print("[9]", play(c, op="clock", mode="play"))
    print("\nPASS: evidence-gated Play Host session verified against the live game.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:  # noqa: BLE001 - demo script wants a loud failure
        print(f"\nFAIL: {exc}")
        sys.exit(1)
