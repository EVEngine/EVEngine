#!/usr/bin/env python3
"""AI Editor demo — drive the headless `eve mcp` host to spawn a project-specific editor.

Usage:
    1. Start the headless MCP host (TCP mode):
         eve mcp --port 7531 --root examples/ai-editor
    2. In another terminal:
         python examples/ai-editor/editor_demo.py 7531

The demo applies a JSON View + Squirrel ViewModel, creates a live SceneHost and
Renderable3D, binds both to Editor automation, edits them through transactions,
verifies Editor snapshots against independent engine queries plus undo/redo,
captures the rendered result, and shuts the host down cleanly.
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
    development = json.loads(c.tool("eve_agent_session_start", {
        "objective": "Build and verify an Agent-authored live scene and material edit",
        "criteria": [
            {"id": "runtime", "description": "Scene and material converge in the live engine"},
            {"id": "history", "description": "Undo and redo preserve the authoritative edit history"},
            {"id": "visual", "description": "The final frame is captured for visual inspection"},
        ],
    }))
    if development.get("status") != "applied":
        raise RuntimeError(f"development session did not start: {development}")
    development_session = development["sessionId"]
    c.tool("eve_agent_session_advance", {"sessionId": development_session, "phase": "modify"})

    print("[2]", c.tool("eve_host_window_open",
                        {"title": "AI Editor Demo", "width": 1000, "height": 640}))

    vm_source = read_file(os.path.join("editors", "terrain.vm.nut"))
    print("[3]", c.tool("eve_host_vm_register",
                        {"name": "TerrainVM", "source": vm_source}))

    editor = json.loads(read_file(os.path.join("editors", "terrain.editor.json")))
    print("[4]", c.tool("eve_host_editor_apply", {"editor": editor}))

    host = "ai-editor.live"
    scene_setup = (
        'AgentScene <- eve.Scene(); AgentScene.beginBuild(); '
        'AgentScene.beginNode("root", "Root"); AgentScene.addNode("player", "Player"); '
        f'AgentScene.end(); AgentScene.mountBuildAs("{host}"); AgentScene.select("{host}");'
    )
    print("[5]", c.tool("eve_host_script", {"source": scene_setup}))

    render_setup = (
        "AgentGfx <- eve.Graphics(); "
        "AgentCamera <- eve.Camera3D(); AgentCamera.setEye(3.2, 2.4, 4.2); "
        "AgentCamera.setTarget(0.0, 0.0, 0.0); AgentCamera.setAmbient(0.45, 0.45, 0.48); "
        "AgentCamera.setActive(true); "
        "AgentRenderable <- eve.Renderable3D(); AgentRenderable.setMesh(AgentGfx.newMeshCube(1.5)); "
        "AgentRenderable.setPosition(1.9, 0.0, 0.0); AgentRenderable.setYaw(0.45); "
        "AgentRenderable.setTint(0.75, 0.25, 0.18, 1.0); "
        "AgentRenderable.setRoughness(0.7); "
        "AgentGfx.setDirectionalLight(-0.4, -1.0, -0.5, 1.2, 1.1, 1.0); "
        "AgentGfx.setBackgroundColor(0.055, 0.07, 0.10, 1.0); "
        "eve_host_render <- function() { AgentGfx.render3D(); };"
    )
    print("[6]", c.tool("eve_host_script", {"source": render_setup}))

    target = "ai-editor.scene"
    print("[7]", c.tool("eve_editor_target_create",
                        {"target": target, "type": "scene-host", "host": host}))
    c.tool("eve_agent_session_advance", {"sessionId": development_session, "phase": "run"})
    c.tool("eve_agent_session_advance", {"sessionId": development_session, "phase": "observe"})
    scene_observer = json.loads(c.tool("eve_editor_observe_start", {
        "observer": "scene-node",
        "host": host,
        "node": "player",
        "expect": {"x": 4.0, "y": 2.0, "z": 1.0},
        "tolerance": 0.001,
    }))
    if (scene_observer.get("status") != "applied"
            or scene_observer.get("event", {}).get("converged") is not False):
        raise RuntimeError(f"scene RX observer did not capture the baseline: {scene_observer}")
    scene_session = scene_observer["sessionId"]
    missing_scene = json.loads(c.tool("eve_editor_execute_observe", {
        "target": target,
        "command": "scene.transform.set.v1",
        "payload": {"object": "player", "position": [9.0, 9.0, 9.0]},
        "observer": "scene-node",
        "host": host,
        "node": "missing",
    }))
    if missing_scene.get("status") != "not-found":
        raise RuntimeError(f"missing scene observer did not reject before mutation: {missing_scene}")
    moved = json.loads(c.tool("eve_editor_execute_observe", {
        "target": target,
        "command": "scene.transform.set.v1",
        "payload": {"object": "player", "position": [4.0, 2.0, 1.0]},
        "observer": "scene-node",
        "host": host,
        "node": "player",
        "expect": {"x": 4.0, "y": 2.0, "z": 1.0},
        "tolerance": 0.001,
    }))
    if (moved.get("status") != "observed"
            or moved.get("transaction", {}).get("status") != "applied"
            or moved.get("converged") is not True
            or moved.get("before", {}).get("x") != 0.0
            or moved.get("after", {}).get("x") != 4.0):
        raise RuntimeError(f"scene execute-observe failed: {moved}")
    scene_events = json.loads(c.tool("eve_editor_observe_poll", {"sessionId": scene_session}))
    if (len(scene_events.get("events", [])) != 1
            or scene_events["events"][0].get("converged") is not True
            or scene_events["events"][0].get("observation", {}).get("x") != 4.0):
        raise RuntimeError(f"scene RX observer missed the runtime change: {scene_events}")
    duplicate_scene = json.loads(c.tool("eve_editor_observe_poll", {"sessionId": scene_session}))
    if duplicate_scene.get("events") != []:
        raise RuntimeError(f"scene RX observer did not suppress a duplicate: {duplicate_scene}")
    print("[8]", json.dumps(moved, sort_keys=True))

    def player_x():
        state = json.loads(c.tool("eve_editor_inspect", {"target": target}))
        objects = state["target"]["snapshot"]["objects"]
        player = next(item for item in objects if item["id"] == "player")
        return player["transform"]["x"]

    if player_x() != 4.0:
        raise RuntimeError("authoritative scene did not receive the transaction")
    engine_player = json.loads(c.tool("eve_scene_node_get", {"id": "player"}))
    if engine_player["x"] != 4.0 or engine_player["y"] != 2.0:
        raise RuntimeError(f"engine SceneHost did not receive the Editor transaction: {engine_player}")
    undone = c.tool("eve_editor_undo", {"target": target})
    undone_x = player_x()
    if undone_x != 0.0:
        raise RuntimeError(f"scene undo did not restore the baseline: x={undone_x}, result={undone}")
    redone = c.tool("eve_editor_redo", {"target": target})
    redone_x = player_x()
    if redone_x != 4.0:
        raise RuntimeError(f"scene redo did not restore the Agent edit: x={redone_x}, result={redone}")
    scene_closed = json.loads(c.tool("eve_editor_observe_close", {"sessionId": scene_session}))
    if scene_closed.get("status") != "applied":
        raise RuntimeError(f"scene RX observer did not close: {scene_closed}")

    time.sleep(1.0)  # let the host render a few frames
    print("[9] engine SceneHost", c.tool("eve_scene_node_get", {"id": "player"}))

    def eval_number(expression):
        evaluated = json.loads(c.tool("eve_eval", {"expression": expression}))
        if not evaluated.get("ok"):
            raise RuntimeError(f"engine expression failed: {evaluated}")
        return float(evaluated["value"])

    material_target = "ai-editor.material"
    entity_id = int(eval_number("AgentRenderable.getEntityId()"))
    generation = int(eval_number("AgentRenderable.getEntityGeneration()"))
    print("[10]", c.tool("eve_editor_target_create", {
        "target": material_target,
        "type": "material-renderable3d",
        "entityId": entity_id,
        "generation": generation,
    }))
    def engine_material():
        state = json.loads(c.tool("eve_renderable3d_get", {
            "entityId": entity_id, "generation": generation,
        }))
        if state.get("status") != "ok":
            raise RuntimeError(f"live Renderable3D query failed: {state}")
        return state

    stale_probe = json.loads(c.tool("eve_editor_execute_observe", {
        "target": material_target,
        "command": "material.property.set.v1",
        "payload": {"path": "shading.tint", "value": [0.9, 0.1, 0.1, 1.0]},
        "entityId": entity_id,
        "generation": generation + 1,
    }))
    if stale_probe.get("status") != "stale" or abs(engine_material()["tint"][1] - 0.25) > 0.001:
        raise RuntimeError(f"stale execute-observe was not rejected before mutation: {stale_probe}")

    desired_tint = [0.12, 0.82, 0.30, 1.0]
    material_observer = json.loads(c.tool("eve_editor_observe_start", {
        "observer": "renderable3d",
        "entityId": entity_id,
        "generation": generation,
        "expect": {"tint": desired_tint},
        "tolerance": 0.001,
    }))
    if material_observer.get("status") != "applied":
        raise RuntimeError(f"material RX observer did not start: {material_observer}")
    material_session = material_observer["sessionId"]

    def submit_tint(tint):
        report = json.loads(c.tool("eve_editor_execute_observe", {
            "target": material_target,
            "command": "material.property.set.v1",
            "payload": {"path": "shading.tint", "value": tint},
            "entityId": entity_id,
            "generation": generation,
            "expect": {"tint": desired_tint},
            "tolerance": 0.001,
        }))
        if (report.get("status") != "observed"
                or report.get("transaction", {}).get("status") != "applied"
                or report.get("editor", {}).get("status") != "applied"
                or report.get("after", {}).get("status") != "ok"):
            raise RuntimeError(f"material execute-observe failed: {report}")
        return report

    imperfect_proposal = [0.12, 0.55, 0.30, 1.0]
    submit_tint(imperfect_proposal)
    correction_count = 0
    for attempt in range(2):
        polled = json.loads(c.tool("eve_editor_observe_poll", {"sessionId": material_session}))
        events = polled.get("events", [])
        if len(events) != 1:
            raise RuntimeError(f"material RX observer expected one changed event: {polled}")
        event = events[0]
        observed = event["observation"]["tint"]
        error = event["maxError"]
        print(f"[agent] observe attempt={attempt + 1} tint={observed} maxError={error:.3f}")
        if event.get("converged") is True:
            break
        correction_count += 1
        submit_tint(desired_tint)
    else:
        raise RuntimeError("Agent material correction loop did not converge")
    if correction_count != 1:
        raise RuntimeError(f"expected one observable correction, got {correction_count}")
    duplicate_material = json.loads(c.tool("eve_editor_observe_poll", {"sessionId": material_session}))
    if duplicate_material.get("events") != []:
        raise RuntimeError(f"material RX observer did not suppress a duplicate: {duplicate_material}")
    material_closed = json.loads(c.tool("eve_editor_observe_close", {"sessionId": material_session}))
    if material_closed.get("status") != "applied":
        raise RuntimeError(f"material RX observer did not close: {material_closed}")

    inspected_material = json.loads(c.tool("eve_editor_inspect", {"target": material_target}))
    tint = inspected_material["target"]["snapshot"]["properties"]["shading.tint"]
    if abs(tint[1] - 0.82) > 0.001:
        raise RuntimeError(f"Editor material snapshot diverged: {tint}")
    c.tool("eve_editor_undo", {"target": material_target})
    if abs(engine_material()["tint"][1] - 0.55) > 0.001:
        raise RuntimeError("first material undo did not restore the imperfect proposal")
    c.tool("eve_editor_undo", {"target": material_target})
    if abs(engine_material()["tint"][1] - 0.25) > 0.001:
        raise RuntimeError("second material undo did not restore the original live renderable")
    c.tool("eve_editor_redo", {"target": material_target})
    if abs(engine_material()["tint"][1] - 0.55) > 0.001:
        raise RuntimeError("first material redo did not replay the imperfect proposal")
    c.tool("eve_editor_redo", {"target": material_target})
    if abs(engine_material()["tint"][1] - 0.82) > 0.001:
        raise RuntimeError("second material redo did not replay the Agent correction")
    print("[11] engine material", json.dumps(engine_material(), sort_keys=True))

    # Project the authoritative Editor result back into the generated UI.
    c.tool("eve_host_script", {"source": f"TerrainVM.strength = {player_x() / 10.0};"})
    print("[12]", c.tool("eve_host_editor_state", {"id": "terrain"})[:200])
    print("[13]", c.tool("eve_host_capture", {"path": "ai_editor_capture.png"}))

    verified = json.loads(c.tool("eve_agent_session_advance", {
        "sessionId": development_session, "phase": "verify",
    }))
    if verified.get("status") != "applied":
        raise RuntimeError(f"development session did not enter verify: {verified}")
    for criterion, kind, summary, artifact in [
        ("runtime", "runtime-observation", "RX observations converged for scene and material", ""),
        ("history", "checkpoint", "Two-step material undo and redo reproduced both edits", ""),
        ("visual", "screenshot", "Engine-owned final frame captured", "ai_editor_capture.png"),
    ]:
        receipt = json.loads(c.tool("eve_agent_session_evidence", {
            "sessionId": development_session,
            "criterionId": criterion,
            "kind": kind,
            "status": "pass",
            "summary": summary,
            "artifact": artifact,
        }))
        if receipt.get("status") != "applied":
            raise RuntimeError(f"development evidence was rejected: {receipt}")
    completed = json.loads(c.tool("eve_agent_session_complete", {
        "sessionId": development_session,
        "summary": "Agent-authored Editor workflow converged with runtime, history, and visual proof",
    }))
    if completed.get("status") != "applied" or completed.get("phase") != "complete":
        raise RuntimeError(f"development session did not complete: {completed}")

    print("[14]", c.tool("eve_editor_target_close", {"target": material_target}))
    print("[15]", c.tool("eve_editor_target_close", {"target": target}))
    print("[16]", c.tool("eve_host_shutdown"))
    print("\nPASS: Agent completed an evidence-gated development session with RX observations, Editor history, and visual proof.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:  # noqa: BLE001 - demo script wants a loud failure
        print(f"\nFAIL: {exc}")
        sys.exit(1)
