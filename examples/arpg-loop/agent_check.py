#!/usr/bin/env python3
"""Exercise normal ARPG inputs through eve_play; no eval or state mutation shortcuts."""
import importlib.util
import json
from pathlib import Path
import sys
import time


def main():
    helper = Path(__file__).resolve().parents[1] / "ai-game" / "agent_demo.py"
    spec = importlib.util.spec_from_file_location("loop_mcp", helper)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    client = module.McpClient("127.0.0.1", int(sys.argv[1]) if len(sys.argv) > 1 else 7538)
    output = Path(sys.argv[2] if len(sys.argv) > 2 else "/tmp/arpg-loop-evidence")
    output.mkdir(parents=True, exist_ok=True)
    client.call("initialize", {"protocolVersion": "2025-06-18", "clientInfo": {"name": "arpg-loop-check", "version": "1"}})
    client.send({"jsonrpc": "2.0", "method": "notifications/initialized"})

    def play(**request):
        result = module.play(client, **request)
        if result.get("status") == "error":
            raise RuntimeError(result)
        return result

    def tool(name, **args):
        return json.loads(client.tool(name, args))

    def act(action):
        return play(op="act", action=action)

    def step(count=10):
        play(op="step", clock="frame", count=count)
        module.wait_until_paused(client)
        return play(op="observe", observation="loop")["state"]

    def capture(name):
        for _ in range(4):
            try:
                result = play(op="capture", path=str((output / name).resolve()))
                if "path" in result:
                    return result["path"]
            except RuntimeError as error:
                if "readback was just enabled" not in str(error):
                    raise
            step(1)
            time.sleep(0.2)
        raise AssertionError(result)

    assert play(op="status")["contractId"] == "examples/arpg-loop"
    session = tool("eve_agent_session_start", objective="Verify complete ARPG combat-growth-checkpoint loop", criteria=[
        {"id": "loop", "description": "Three normal-input kills reward XP, gold, quest and gear"},
        {"id": "restore", "description": "Pending loot and equipped growth survive checkpoint restore without duplication"},
        {"id": "defeat", "description": "Death blocks combat and save; restart and load clear transient actions"},
        {"id": "visual", "description": "Engine-owned frame displays the playable slice"},
    ])["sessionId"]
    for phase in ("modify", "run"):
        tool("eve_agent_session_advance", sessionId=session, phase=phase)
    play(op="clock", mode="pause")
    act("reset")
    state = step(1)
    observations = []

    def move_to(x, y):
        for _ in range(150):
            s = play(op="observe", observation="loop")["state"]
            if abs(s["px"] - x) > 15:
                act("right" if s["px"] < x else "left")
            elif abs(s["py"] - y) > 15:
                act("down" if s["py"] < y else "up")
            else:
                act("stop")
                return step(1)
            step(4)
        raise AssertionError("movement did not converge")

    for kill in range(1, 4):
        if kill > 1:
            act("next")
            state = step(1)
        for _ in range(250):
            if state["kills"] >= kill:
                break
            if abs(state["px"] - state["ex"]) > 70:
                act("right" if state["px"] < state["ex"] else "left")
            elif abs(state["py"] - state["ey"]) > 65:
                act("down" if state["py"] < state["ey"] else "up")
            else:
                act("stop")
                act("heavy")
                act("attack")
                if 0 < state["windup"] <= 20:
                    act("dodge")
            state = step(6)
            observations.append(state)
            assert state["hp"] > 0, state
        assert state["kills"] == kill and state["gold"] == kill * 10, state
        act("stop")
        if kill == 1:
            act("save")
            assert "saved" in step(1)["message"].lower()
        move_to(790, 170)
        act("pickup")
        state = step(1)
        if kill == 1:
            act("equip")
            assert step(1)["attack"] == 26, state
            act("load")
            state = step(1)
            assert state["items"] == 0 and state["attack"] == 18 and state["pendingLoot"] == 0, state
            move_to(790, 170)
            act("pickup")
            step(1)
            act("equip")
            state = step(1)
            assert state["attack"] == 26 and state["gold"] == 10, state
    assert state["quest"] == "completed" and state["level"] >= 2 and state["items"] == 2, state
    act("pickup")
    state = step(1)
    assert state["items"] == 2 and state["gold"] == 30 and state["pendingLoot"] == -1, state
    act("save")
    step(1)
    act("reset")
    fresh = step(1)
    assert fresh["hp"] == 100 and fresh["kills"] == 0 and fresh["gold"] == 0, fresh
    # Let ordinary AI attacks defeat the idle player; never edit HP through debug APIs.
    death_deadline = time.monotonic() + 90
    while time.monotonic() < death_deadline:
        defeated = step(60)
        if defeated["hp"] == 0:
            break
    assert defeated["hp"] == 0 and defeated["kills"] == 0, defeated
    act("attack")
    act("heavy")
    act("dodge")
    act("right")
    after_death = step(30)
    assert after_death["enemyHp"] == defeated["enemyHp"], after_death
    assert after_death["px"] == defeated["px"] and after_death["dodge"] == 0, after_death
    act("save")
    assert "save after a kill" in step(1)["message"].lower()
    capture("defeat.png")
    act("reset")
    restarted = step(1)
    assert restarted["hp"] == 100 and restarted["kills"] == 0, restarted
    # Establish real cooldowns and held movement, then queue more inputs before load.
    act("heavy")
    act("dodge")
    act("right")
    active = step(2)
    assert active["heavyCooldown"] > 0 and active["dodgeCooldown"] > 0, active
    act("attack")
    act("load")
    state = step(1)
    assert state["px"] == 300 and state["py"] == 360, state
    for field in ("cooldown", "heavyCooldown", "dodgeCooldown", "strike", "dodge", "windup", "inputX", "inputY"):
        assert state[field] == 0, (field, state)
    assert state["kills"] == 3 and state["items"] == 2 and state["attack"] == 26 and state["quest"] == "completed", state
    picture = capture("victory-restored.png")
    artifact = output / "observations.json"
    artifact.write_text(json.dumps({"combat": observations, "defeated": defeated, "restarted": restarted, "beforeLoad": active, "restored": state}, indent=2), encoding="utf-8")
    for phase in ("observe", "verify"):
        tool("eve_agent_session_advance", sessionId=session, phase=phase)
    for criterion, kind, path in [("loop", "runtime-observation", str(artifact.resolve())), ("restore", "runtime-observation", str(artifact.resolve())), ("defeat", "runtime-observation", str(artifact.resolve())), ("visual", "screenshot", picture)]:
        tool("eve_agent_session_evidence", sessionId=session, criterionId=criterion, kind=kind, status="pass", summary="Normal input acceptance completed", artifact=path)
    completed = tool("eve_agent_session_complete", sessionId=session, summary="ARPG combat-growth-save loop verified")
    assert completed.get("phase") == "complete", completed
    play(op="clock", mode="play")
    print("PASS ARPG loop:", json.dumps(state), "evidence:", output)


if __name__ == "__main__":
    main()
