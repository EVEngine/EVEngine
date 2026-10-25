#!/usr/bin/env python3
"""Verify the live arena through declared gameplay actions and engine readback.

Start eve run --debug --mcp-port=7537 in this example, then run:
    python3 examples/impact-arena/agent_check.py [port] [artifact-directory]
"""

import importlib.util
import json
from pathlib import Path
import sys
import time


def main():
    helper = Path(__file__).resolve().parents[1] / "ai-game" / "agent_demo.py"
    spec = importlib.util.spec_from_file_location("arena_mcp", helper)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    client = module.McpClient("127.0.0.1", int(sys.argv[1]) if len(sys.argv) > 1 else 7537)
    output = Path(sys.argv[2] if len(sys.argv) > 2 else "/tmp/impact-arena-evidence").resolve()
    output.mkdir(parents=True, exist_ok=True)
    client.call(
        "initialize",
        {
            "protocolVersion": "2025-06-18",
            "clientInfo": {"name": "impact-arena-contract-check", "version": "1"},
        },
    )
    client.send({"jsonrpc": "2.0", "method": "notifications/initialized"})

    def play(**request):
        return module.play(client, **request)

    def tool(name, **arguments):
        return json.loads(client.tool(name, arguments))

    def observe():
        return play(op="observe", observation="arena")["state"]

    def capture(name):
        for _ in range(3):
            try:
                return play(op="capture", path=str(output / name))["path"]
            except RuntimeError as error:
                if "readback was just enabled" not in str(error):
                    raise
                time.sleep(0.2)
        raise AssertionError("Engine readback did not become ready")

    assert play(op="status")["contractId"] == "examples/impact-arena"
    started = tool(
        "eve_agent_session_start",
        objective="Verify arena combat and recovery through normal gameplay",
        criteria=[
            {"id": key, "description": description}
            for key, description in [
                ("victory", "Three waves can be defeated using regular demo inputs"),
                ("recovery", "Idle player dies and reset restores a fresh session"),
                ("combat", "Dodge, critical and clamped settlement amounts are observed"),
                ("visual", "Damage numbers appear in an engine-readback frame"),
            ]
        ],
    )
    assert started.get("status") == "applied", started
    session = started["sessionId"]
    for phase in ("modify", "run"):
        tool("eve_agent_session_advance", sessionId=session, phase=phase)
    play(op="clock", mode="play")
    play(op="act", action="demo")
    observations = []
    saw_clamp = False
    combat_image = None
    deadline = time.monotonic() + 180
    while time.monotonic() < deadline:
        state = observe()
        observations.append(state)
        saw_clamp |= 0 < state["lastApplied"] < state["lastRequested"]
        if combat_image is None and state["popupCount"] > 0 and state["criticals"] > 0:
            combat_image = capture("combat.png")
        if state["phase"] != "playing":
            break
        time.sleep(0.15)
    assert state["phase"] == "victory", state
    assert state["kills"] == 12 and state["criticals"] > 0 and state["dodges"] > 0, state
    assert saw_clamp and state["lastStages"] >= 6, state
    assert combat_image is not None
    capture("victory.png")
    print("PASS victory:", state, flush=True)
    play(op="act", action="reset")
    time.sleep(0.1)
    fresh = observe()
    assert fresh["phase"] == "playing" and fresh["kills"] == 0 and fresh["playerHealth"] > 0, fresh
    play(op="act", action="dodge")
    time.sleep(0.2)
    assert observe()["dodges"] == 1
    deadline = time.monotonic() + 120
    while time.monotonic() < deadline:
        state = observe()
        if state["phase"] == "defeat":
            break
        time.sleep(0.2)
    assert state["phase"] == "defeat" and state["playerHealth"] == 0, state
    capture("defeat.png")
    print("PASS defeat:", state, flush=True)
    play(op="act", action="reset")
    time.sleep(0.1)
    restored = observe()
    assert restored["playerHealth"] == fresh["playerHealth"] and restored["kills"] == 0, restored
    assert restored["phase"] == "playing" and restored["popupCount"] == 0, restored
    artifact = output / "observations.json"
    artifact.write_text(
        json.dumps({"demo": observations, "defeat": state, "reset": restored}, indent=2),
        encoding="utf-8",
    )
    for phase in ("observe", "verify"):
        tool("eve_agent_session_advance", sessionId=session, phase=phase)
    for criterion, kind, summary, path in [
        ("victory", "runtime-observation", "12 enemies defeated across three waves", str(artifact)),
        (
            "recovery",
            "runtime-observation",
            "Death and reset verified through normal actions",
            str(artifact),
        ),
        (
            "combat",
            "runtime-observation",
            "Critical, dodge and overkill clamp verified",
            str(artifact),
        ),
        ("visual", "screenshot", "Live combat frame captured for visual inspection", combat_image),
    ]:
        receipt = tool(
            "eve_agent_session_evidence",
            sessionId=session,
            criterionId=criterion,
            kind=kind,
            status="pass",
            summary=summary,
            artifact=path,
        )
        assert receipt.get("status") == "applied", receipt
    completed = tool(
        "eve_agent_session_complete", sessionId=session, summary="Arena gameplay contract verified"
    )
    assert completed.get("phase") == "complete", completed
    print("PASS arena session:", session, "artifacts:", output)


if __name__ == "__main__":
    main()
