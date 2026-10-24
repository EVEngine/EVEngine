"""Exercise live Control Rig editing through the engine's existing Editor MCP.
Run the example with --debug --mcp-port=7549, then python agent_demo.py.
This intentionally edits the open example and saves knight.rig.json through its UI.
"""
import argparse
import copy
import json
import math
from pathlib import Path
import socket
import time


def call(port, name, arguments):
    with socket.create_connection(("127.0.0.1", port), timeout=10) as connection:
        connection.settimeout(30)
        request = {"jsonrpc": "2.0", "id": 1, "method": "tools/call", "params": {"name": name, "arguments": arguments}}
        connection.sendall((json.dumps(request) + "\n").encode("utf-8"))
        with connection.makefile("r", encoding="utf-8") as stream:
            for line in stream:
                response = json.loads(line)
                if response.get("id") != 1:
                    continue
                if "error" in response:
                    raise RuntimeError(response)
                result = response["result"]
                if result.get("isError"):
                    raise RuntimeError(result)
                text = "\n".join(item.get("text", "") for item in result.get("content", []))
                return json.loads(text)
    raise RuntimeError("MCP connection closed without response")


def run(port, output):
    output.mkdir(parents=True, exist_ok=True)
    def tool(name, **args):
        return call(port, name, args)
    targets = tool("eve_editor_target_list")
    target = next(item["id"] for item in targets["targets"] if item.get("type") == "control-rig")
    command = next(item for item in tool("eve_editor_commands")["commands"] if item["id"] == "animation.rig.replace.v1")
    schema = command["inputSchema"]
    assert schema["additionalProperties"] is False
    assert "keys" in schema["properties"]["controls"]["items"]["properties"]
    (output / "command-schema.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
    def inspect():
        return tool("eve_editor_inspect", target=target)["target"]
    def document():
        return inspect()["snapshot"]
    def click(widget):
        tool("eve_ui_click", host="Timeline", widget=widget)
        time.sleep(0.3)
    def capture(name):
        (output / name).unlink(missing_ok=True)
        for _ in range(8):
            try:
                result = tool("eve_screenshot", path=str((output / name).resolve()))
                if (output / name).exists():
                    return result
            except RuntimeError:
                pass
            time.sleep(0.2)
        raise RuntimeError("No engine-owned screenshot produced")
    before = inspect()
    original = before["snapshot"]
    capture("agent-before.png")
    candidate = copy.deepcopy(original)
    control = next(item for item in candidate["controls"] if item["id"] == "chest")
    control["initial"][0] += 0.12
    for key in control["keys"]:
        key["transform"][0] += 0.12
    plan = tool("eve_editor_plan", target=target, command=command["id"], payload=candidate,
                expectedRevision=before["descriptor"]["revision"])
    assert document() == original, "planning mutated live document"
    receipt = tool("eve_editor_commit", planId=plan["planId"])
    assert receipt["status"] == "applied", receipt
    actual = document()
    def equivalent(a, b):
        if isinstance(a, (int, float)) and isinstance(b, (int, float)):
            return math.isclose(a, b, rel_tol=1e-6, abs_tol=1e-6)
        if isinstance(a, dict) and isinstance(b, dict):
            return a.keys() == b.keys() and all(equivalent(a[k], b[k]) for k in a)
        if isinstance(a, list) and isinstance(b, list):
            return len(a) == len(b) and all(equivalent(x, y) for x, y in zip(a, b))
        return a == b
    assert equivalent(actual, candidate), "committed document differs beyond float tolerance"
    candidate = actual
    time.sleep(0.5)
    capture("agent-after.png")
    click("undo")
    assert document() == original, "UI did not undo Agent transaction"
    tool("eve_editor_redo", target=target)
    assert document() == candidate
    click("save")
    click("undo")
    click("load")  # UI import is one transaction on the same target.
    assert document() == candidate, "saved document did not round-trip"
    tool("eve_editor_undo", target=target)
    assert document() == original, "Agent did not undo UI import"
    click("redo")
    assert document() == candidate
    invalid = copy.deepcopy(candidate)
    invalid["nodes"][0]["bone"] = "__missing_bone__"
    rejected = tool("eve_editor_execute", target=target, command=command["id"], payload=invalid)
    assert rejected["status"] != "applied" and document() == candidate
    capture("agent-restored.png")
    result = {"passed": True, "target": target, "schema": True, "planCommit": True,
              "uiUndoAgentEdit": True, "agentUndoUiImport": True, "saveLoad": True,
              "invalidEditAtomic": True, "receipt": receipt}
    (output / "agent-result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, ensure_ascii=True))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=7549)
    parser.add_argument("--output", type=Path, default=Path("evidence"))
    arguments = parser.parse_args()
    run(arguments.port, arguments.output)
