"""Unit tests for the Creative Brain pipeline (no network / no LLM required)."""

import json
import os
import socket
import sys
import threading

import pytest

sys_path = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, sys_path)

from brain import intent, layout, mcp, resource_proto
from brain.assets import Catalog
from brain.plan import build_plan
from brain.schema import GenerationPlan

CATALOG = os.path.join(sys_path, "catalogs", "assets.example.json")


def _unset_openai():
    os.environ.pop("OPENAI_API_KEY", None)


def test_intent_deterministic_cave_night():
    _unset_openai()
    cfg = intent.parse_intent_deterministic("a moonlit dark cave dungeon with treasure corners", seed=7)
    assert cfg.terrain.algorithm == "cave.cellular"
    assert cfg.lighting.timeOfDay == "night"
    assert cfg.lighting.atmosphere == "moonlit"
    assert any(r.asset == "chest" and r.placement == "corners" for r in cfg.exploration.propRules)
    assert cfg.terrain.seed == 7


def test_intent_parse_entrypoint_falls_back():
    _unset_openai()
    cfg = intent.parse_intent("fantasy forest at dusk with winding roads", seed=1)
    assert cfg.style == "fantasy"
    assert cfg.vegetation.density == pytest.approx(0.45)
    assert cfg.roads.network == "organic"
    assert cfg.lighting.timeOfDay == "dusk"


def test_assets_match_whitelist_and_missing():
    catalog = Catalog.load(CATALOG)
    cfg = intent.parse_intent_deterministic("a desert dungeon with treasure", seed=3)
    whitelist, missing = catalog.match_to_config(cfg)
    assert whitelist, "expected a non-empty whitelist"
    assert all(a.id for a in whitelist)
    # material pbr.stone should resolve for desert/dungeon
    assert any(a.id == "pbr.stone" for a in whitelist)


def test_layout_rules_four_kinds():
    cfg = intent.parse_intent_deterministic("large snowy tundra with scattered rocks", seed=5)
    catalog = Catalog.load(CATALOG)
    whitelist, missing = catalog.match_to_config(cfg)
    rules = layout.generate_layout(cfg, whitelist)
    kinds = {r.kind for r in rules}
    assert {"road_buffer", "layer", "scatter", "corner"} <= kinds


def test_plan_build_and_serialize():
    _unset_openai()
    plan = build_plan("a cyberpunk city at night with neon corners", seed=9)
    assert isinstance(plan, GenerationPlan)
    d = plan.to_dict()
    assert "config" in d and "whitelist" in d and "layout_rules" in d and "steps" in d
    assert any(s["action"] == "terrain" for s in d["steps"])
    assert any(s["action"] == "place" for s in d["steps"])
    json.loads(plan.to_json())  # round-trips


def test_layout_steps_respect_road_buffer():
    cfg = intent.parse_intent_deterministic("grid city", seed=2)
    catalog = Catalog.load(CATALOG)
    whitelist, _ = catalog.match_to_config(cfg)
    steps = layout.build_placement_steps(cfg, whitelist, seed=2)
    band = set(layout._road_band(cfg))
    for s in steps:
        if s.action == "place":
            x, y = s.params["x"], s.params["y"]
            assert (x, y) not in band, f"place step inside road no-build band: {(x, y)}"


def test_resource_broker_roundtrip(tmp_path):
    q = tmp_path / "requests.json"
    broker = resource_proto.ResourceBroker(queue_path=q)
    req = broker.request("model.ruin_arch", style="ruins")
    assert broker.status(req.request_id) == "queued"
    # simulate PR6 response ingestion
    rid = broker.import_json({"request_id": req.request_id, "ok": True})
    assert rid == req.request_id
    assert broker.status(req.request_id) == "ready"
    # reload persists
    broker2 = resource_proto.ResourceBroker(queue_path=q)
    assert broker2.status(req.request_id) == "ready"


def test_snippet_generation():
    plan = build_plan("cave dungeon", seed=1)
    src = mcp.snippet_install_plan(plan)
    assert "::_cb_plan_json" in src
    # Plan steps now dispatch through the scene_director kit (real actions).
    step_src = mcp.snippet_for_step("terrain", "cave.cellular", {"seed": 1})
    assert "scene_director.modify" in step_src
    assert "add_object" in step_src
    assert "terrain" in step_src
    place_src = mcp.snippet_for_step(
        "place", "rock", {"x": 4, "y": 5, "mapWidth": 32, "mapHeight": 32, "seed": 3})
    assert "scene_director.modify" in place_src
    assert "add_object" in place_src
    assert "rock" in place_src
    lit_src = mcp.snippet_for_step("set_lighting", "scene", {"timeOfDay": "night"})
    assert '"lighting"' in lit_src


def _fake_engine_server(result_text):
    """A minimal MCP server for testing the client framing."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("127.0.0.1", 0))
    sock.listen(1)
    port = sock.getsockname()[1]

    def serve():
        conn, _ = sock.accept()
        with conn:
            buf = b""
            while True:
                while b"\n" not in buf:
                    chunk = conn.recv(4096)
                    if not chunk:
                        return
                    buf += chunk
                line, buf = buf.split(b"\n", 1)
                req = json.loads(line)
                rid = req.get("id")
                if req.get("method") == "initialize":
                    conn.sendall(
                        (json.dumps({"jsonrpc": "2.0", "id": rid,
                                     "result": {"serverInfo": {"name": "fake"},
                                                "capabilities": {}}}) + "\n").encode()
                    )
                elif req.get("method") == "tools/call":
                    conn.sendall(
                        (json.dumps({"jsonrpc": "2.0", "id": rid, "result": {
                            "content": [{"type": "text", "text": result_text}]}}) + "\n").encode()
                    )
        sock.close()

    threading.Thread(target=serve, daemon=True).start()
    return port


def test_mcp_client_call():
    port = _fake_engine_server("fake-ok")
    with mcp.McpClient("127.0.0.1", port) as c:
        assert c.status() == "fake-ok"


def test_run_batch_no_engine_missing():
    # Running a batch against a fake server should return ok results for steps.
    port = _fake_engine_server("ok")
    plan = build_plan("forest", seed=1)
    with mcp.McpClient("127.0.0.1", port) as c:
        results = mcp.run_batch(plan, c)
    assert results[0]["action"] == "install_scene_director"
    assert results[0]["ok"] is True
    assert results[1]["action"] == "install_plan"
    assert results[1]["ok"] is True
    assert len(results) == 2 + len(plan.steps)
