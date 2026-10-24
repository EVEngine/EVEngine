"""Build and verify a whitebox through project MCP tools, without eval.

Run against a fresh level-designer: python agent_demo.py --port 58217
Only the Python standard library is needed. The same tools work via the engine's
stdio MCP bridge. This demo intentionally leaves its authored scene in the UI.
"""

import argparse
import json
from pathlib import Path
import socket
import time


class WhiteboxClient:
    def __init__(self, port):
        self.port = port

    def rpc(self, method, params=None):
        request = {"jsonrpc": "2.0", "id": 1, "method": method}
        if params is not None:
            request["params"] = params
        with socket.create_connection(("127.0.0.1", self.port), timeout=60) as sock:
            sock.sendall((json.dumps(request) + "\n").encode("utf-8"))
            with sock.makefile("r", encoding="utf-8") as stream:
                response = json.loads(stream.readline())
        if "error" in response:
            raise RuntimeError(response["error"])
        return response["result"]

    def call(self, name, arguments=None, *, allow_error=False):
        result = self.rpc("tools/call", {"name": name, "arguments": arguments or {}})
        if result.get("isError"):
            raise RuntimeError(result)
        value = json.loads(
            next(item["text"] for item in result["content"] if item["type"] == "text")
        )
        if value.get("ok") is False and not allow_error:
            raise RuntimeError(value)
        return value


def run_demo(client, screenshot):
    tools = client.rpc("tools/list")["tools"]
    assert len([t for t in tools if t["name"].startswith("whitebox_")]) == 9
    scene = client.call("whitebox_scene_v1")
    if scene["value"]["objects"]:
        raise RuntimeError("Demo requires an empty editor; existing work was not changed")
    recipes = client.call("whitebox_catalog_v1")["value"]["recipes"]
    assert "prototype.stairs" in recipes and "prototype.wall-door" in recipes
    schema = client.call("whitebox_catalog_v1", {"recipe": "prototype.cube"})
    assert schema["value"]["params"]
    client.call("whitebox_flat_surface_v1")

    # Complete specifications are the builder input. Coordinates are metres;
    # rotation is Euler radians. Each item is one native scene undo transaction.
    parts = [
        ("floor", "cube", [0, 0.05, 0], [18, 0.4, 14]),
        ("back-wall", "cube", [0, 0.45, -6.5], [18, 3, 0.5]),
        ("left-wall", "cube", [-8.75, 0.45, 0], [0.5, 3, 13]),
        ("right-wall", "cube", [8.75, 0.45, 0], [0.5, 3, 13]),
        ("gate", "wall-door", [0, 0.45, 6], [2, 1.5, 1]),
        ("stairs", "stairs", [-5, 0.45, -1], [2, 1, 2]),
        ("ramp", "ramp", [4, 0.45, -1], [2, 1.5, 3]),
        ("platform", "cube", [-5, 0.45, -4], [4, 2, 3]),
        ("pillar-a", "pillar", [-4, 0.45, 4], [1, 1.5, 1]),
        ("pillar-b", "pillar", [4, 0.45, 4], [1, 1.5, 1]),
    ]
    for name, recipe, position, scale in parts:
        client.call(
            "whitebox_create_v1",
            {
                "id": "agent-demo-" + name,
                "name": name,
                "recipe": "prototype." + recipe,
                "position": position,
                "scale": scale,
            },
        )

    # Rejected requests must leave both object count and scene revision intact.
    before = client.call("whitebox_scene_v1")
    invalid_cases = [
        ({"id": "bad", "recipe": "prototype.missing", "position": [0, 0, 0]}, "not_found"),
        (
            {"id": "bad", "recipe": "prototype.cube", "position": [0, 0, 0], "scale": [1, 0, 1]},
            "invalid_argument",
        ),
        (
            {
                "id": "bad",
                "recipe": "prototype.cube",
                "position": [0, 0, 0],
                "params": {"unknown": 1},
            },
            "invalid_argument",
        ),
        (
            {"id": "agent-demo-floor", "recipe": "prototype.cube", "position": [0, 0, 0]},
            "already_exists",
        ),
        (
            {"id": "bad", "recipe": "prototype.cube", "position": [0, 0, 0], "expectedRevision": 0},
            "stale_revision",
        ),
        (
            {"id": "bad", "recipe": "prototype.cube", "position": [0, 0, 0], "typo": 1},
            "invalid_argument",
        ),
    ]
    for payload, expected in invalid_cases:
        result = client.call("whitebox_create_v1", payload, allow_error=True)
        assert result["ok"] is False and result["code"] == expected, result
    after = client.call("whitebox_scene_v1")
    assert before == after, "Rejected inputs changed the authored scene"

    result = client.call(
        "whitebox_transform_v1",
        {
            "object": "agent-demo-ramp",
            "rotation": [0, 3.14159265, 0],
            "expectedRevision": after["revision"],
        },
    )
    assert result["revision"] > after["revision"]
    client.call(
        "whitebox_create_v1",
        {"id": "agent-demo-remove-me", "recipe": "prototype.cube", "position": [25, 0, 0]},
    )
    client.call("whitebox_delete_v1", {"object": "agent-demo-remove-me"})
    assert len(client.call("whitebox_scene_v1")["value"]["objects"]) == len(parts)
    client.call("whitebox_view_v1", {"focus": [0, 1, 0], "distance": 30, "yaw": 0.65, "pitch": 0.8})
    saved = client.call("whitebox_save_v1", {"filename": "agent-whitebox-demo.json"})
    assert saved["value"]["objects"] == len(parts)
    for _ in range(4):
        time.sleep(0.3)
        result = client.call("eve_screenshot", {"path": str(screenshot)}, allow_error=True)
        if result.get("ok"):
            break
        assert result.get("retryable"), result
    else:
        raise RuntimeError("No presented frame after four capture attempts")
    print(
        json.dumps(
            {
                "result": "PASS",
                "recipes": len(recipes),
                "created": len(parts),
                "rejection_cases": len(invalid_cases),
                "save": saved["value"],
                "screenshot": str(screenshot),
            },
            ensure_ascii=False,
            indent=2,
        )
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=7529)
    parser.add_argument("--screenshot", type=Path, default=Path("whitebox-agent-demo.png"))
    args = parser.parse_args()
    run_demo(WhiteboxClient(args.port), args.screenshot.resolve())
