"""Verify joint limits through the live editor MCP, including UI/Agent undo.

Run in a fresh Control Rig example with --mcp-port=7549. Leaves limited chest
animation playing. Replaces this control's keys through one document transaction.
"""
import argparse
import copy
import json
import math
from pathlib import Path
import time

from agent_demo import call
from agent_sequence_demo import rotate_parent_z


def run(port, output):
    def tool(name, **arguments):
        return call(port, name, arguments)

    def click(host, widget):
        tool("eve_ui_click", host=host, widget=widget)
        time.sleep(0.25)

    def snapshot():
        return tool("eve_editor_inspect", target=target)["target"]

    target = next(t["id"] for t in tool("eve_editor_target_list")["targets"] if t["type"] == "control-rig")
    click("Timeline", "edit")
    before = snapshot()
    candidate = copy.deepcopy(before["snapshot"])
    assert candidate["version"] == 2
    index = next(i for i, c in enumerate(candidate["controls"]) if c["id"] == "chest")
    control = candidate["controls"][index]
    reference = control["limits"]["reference"]
    lock = {"mode": "locked", "min": 0, "max": 0}
    control["limits"]["translation"] = [copy.deepcopy(lock) for _ in range(3)]
    control["limits"]["rotation"] = [copy.deepcopy(lock), copy.deepcopy(lock),
                                      {"mode": "limited", "min": -12, "max": 12}]
    control["limits"]["scale"] = {"mode": "locked", "min": 1, "max": 1}
    control["keys"] = []
    for seconds, angle in [(0, 0), (0.5, 80), (1, 0), (1.5, -80), (2, 0)]:
        transform = rotate_parent_z(reference, angle)
        transform[0] += 10  # Must never detach from the parent.
        transform[7:10] = [2, 2, 2]
        control["keys"].append({"time": seconds, "transform": transform})
    plan = tool("eve_editor_plan", target=target, command="animation.rig.replace.v1", payload=candidate,
                expectedRevision=before["descriptor"]["revision"])
    assert snapshot()["snapshot"] == before["snapshot"]
    receipt = tool("eve_editor_commit", planId=plan["planId"])
    assert receipt["status"] == "applied", receipt
    limited = snapshot()["snapshot"]
    for key in limited["controls"][index]["keys"]:
        assert key["transform"][:3] == reference[:3]
        assert all(abs(a-b) < 1e-5 for a, b in zip(key["transform"][7:10], reference[7:10]))
        dot = sum(a*b for a, b in zip(key["transform"][3:7], reference[3:7]))
        angle = math.degrees(2*math.acos(min(1.0, abs(dot))))
        assert angle <= 12.01, angle
    # The same limit configuration is editable through UI and undoable by Agent.
    click("Controls", f"control-{index}")
    try:
        tool("eve_ui_get", host="Solve", widget="limit-title")
    except RuntimeError:
        click("Solve", "toggle-limits")
    for attempt in range(20):
        try:
            tool("eve_ui_get", host="Solve", widget="hinge-limits")
            break
        except RuntimeError:
            if attempt == 19:
                raise
            time.sleep(0.1)
    click("Solve", "hinge-limits")
    hinge = snapshot()["snapshot"]
    assert hinge["controls"][index]["limits"]["rotation"][0]["mode"] == "limited"
    tool("eve_editor_undo", target=target)
    assert snapshot()["snapshot"] == limited
    click("Timeline", "undo")
    assert snapshot()["snapshot"] == before["snapshot"]
    tool("eve_editor_redo", target=target)
    assert snapshot()["snapshot"] == limited
    invalid = copy.deepcopy(limited)
    invalid["controls"][index]["limits"]["rotation"][2]["min"] = 30
    revision = snapshot()["descriptor"]["revision"]
    try:
        rejected = tool("eve_editor_plan", target=target, command="animation.rig.replace.v1", payload=invalid)
    except RuntimeError:
        rejected = {"rejected": True}
    assert "planId" not in rejected, rejected
    assert snapshot()["descriptor"]["revision"] == revision
    assert snapshot()["snapshot"] == limited
    extension = copy.deepcopy(limited)
    sliding = extension["controls"][index]
    sliding["limits"]["translation"] = [{"mode": "free", "min": 0, "max": 0} for _ in range(3)]
    sliding["limits"].update(extension=True, minLength=0.8, maxLength=1.2)
    for key in sliding["keys"]:
        key["transform"][:3] = [4*v for v in reference[:3]]
    receipt_extension = tool("eve_editor_execute", target=target, command="animation.rig.replace.v1", payload=extension)
    assert receipt_extension["status"] == "applied", receipt_extension
    for key in snapshot()["snapshot"]["controls"][index]["keys"]:
        assert all(abs(a-1.2*b) < 1e-5 for a, b in zip(key["transform"][:3], reference[:3]))
    click("Timeline", "undo")
    assert snapshot()["snapshot"] == limited
    click("Timeline", "bake")
    assert tool("eve_ui_get", host="Pose", widget="status")["text"] == "Playing baked native AnimClip"
    output.mkdir(parents=True, exist_ok=True)
    frame = (output / "limits.png").resolve()
    frame.unlink(missing_ok=True)
    for _ in range(8):
        tool("eve_screenshot", path=str(frame))
        if frame.exists():
            break
        time.sleep(0.2)
    assert frame.exists(), "Engine did not produce the requested screenshot"
    (output / "limited.rig.json").write_text(json.dumps(limited, indent=2), encoding="utf-8")
    result = {"passed": True, "lockedTranslationAndScale": True, "boundedExtension": True, "uiAgentSharedUndo": True,
              "invalidRangeRejectedAtomically": True, "baked": True, "receipt": receipt}
    (output / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=7549)
    parser.add_argument("--output", type=Path, default=Path("../../build/control-rig-limits"))
    args = parser.parse_args()
    run(args.port, args.output)
