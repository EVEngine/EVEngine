"""Verify real MiniLM -> native search on a running semantic-scene-search example."""

import argparse
import json
from pathlib import Path
import sys
import subprocess

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/scene-search"))
from scene_search import Client, Encoder, build, semantic_query  # noqa: E402


def rejected(client, name, **request):
    try:
        client.tool(name, **request)
    except RuntimeError:
        return
    raise AssertionError(f"{name} unexpectedly accepted {list(request)}")


def move(client, x):
    response = client.rpc("tools/call", {"name": "eve_scene_node_set", "arguments": {
        "id": "mine-door", "x": x, "y": 0, "z": 1}})
    assert not response.get("isError"), response
    assert response["content"][0]["text"] == "ok", response


def verify(client, encoder):
    names = {tool["name"] for tool in client.rpc("tools/list", {})["tools"]}
    assert {"eve_scene_search_prepare", "eve_scene_search_status", "eve_scene_search_publish", "eve_scene_search"} <= names
    before = client.tool("eve_scene_node_get", id="mine-door")
    prepared = client.tool("eve_scene_search_prepare", host="semantic-demo")
    token = prepared["snapshot"]
    assert len(prepared["records"]) == 8
    simple = client.tool("eve_scene_search", snapshot=token, tags=["interactable", "door"])
    assert {hit["id"] for hit in simple["hits"]} == {"mine-door", "stone-gate", "far-door"}
    assert simple["indexSnapshot"] == ""
    rejected(client, "eve_scene_search", snapshot=token, model=encoder.identity, embedding=[1, 0])
    prepared = build(client, encoder, "semantic-demo")
    token = prepared["snapshot"]
    request = {"snapshot": token, "model": encoder.identity}
    cases = [("寻找废弃矿井的木制入口", "mine-door"), ("装着金钱的箱子", "chest"),
             ("Find an old oak", "tree"), ("城堡石拱入口", "stone-gate")]
    evidence = []
    for text, expected in cases:
        result = client.tool("eve_scene_search", **request, embedding=encoder.encode([text])[0], limit=3)
        assert result["hits"][0]["id"] == expected, (text, result)
        evidence.append({"query": text, "expected": expected, "hits": [
            {"id": hit["id"], "score": hit["score"]} for hit in result["hits"]]})
    embedding = encoder.encode(["能使用的矿场木门"])[0]
    nearby = client.tool("eve_scene_search", **request, embedding=embedding, tags=["door", "interactable"],
                         center=[0, 0, 0], radius=10, limit=1)
    assert nearby["hits"][0]["id"] == "mine-door"
    assert nearby["matched"] == 2
    assert nearby["indexSnapshot"] == token
    assert nearby["backend"] == "hnswlib-0.8.0" and nearby["approximate"]
    world_left = semantic_query(client, encoder, "semantic-demo", {"query": "木门", "tags": ["door", "interactable"],
        "spatial": [{"relation": "left-of", "reference": "chest", "frame": "world"}]})
    assert [hit["id"] for hit in world_left["hits"]] == ["mine-door"]
    assert world_left["hits"][0]["spatialEvidence"][0]["offsetRightUpForward"][0] == -2
    observer_right = semantic_query(client, encoder, "semantic-demo", {"query": "门", "tags": ["door", "interactable"],
        "spatial": [{"relation": "right-of", "reference": "chest", "frame": "observer", "observer": "observer"},
                    {"relation": "near", "reference": "chest", "distance": 10}]})
    assert {hit["id"] for hit in observer_right["hits"]} == {"mine-door", "stone-gate"}
    assert all(hit["spatialEvidence"][0]["offsetRightUpForward"][0] > 0 for hit in observer_right["hits"])
    assert client.tool("eve_scene_search_status")["snapshot"] == token, "Queries rebuilt the HNSW index"
    rejected(client, "eve_scene_search", **request, embedding=embedding, scanBudget=2)
    rejected(client, "eve_scene_search", snapshot=token, model="wrong-space", embedding=embedding)
    rejected(client, "eve_scene_search", **request, embedding=[0] * len(embedding))
    rejected(client, "eve_scene_search", **request, embedding=embedding, unknown=True)
    ids, page = [], ""
    while True:
        result = client.tool("eve_scene_search", snapshot=token, limit=2, pageToken=page)
        ids.extend(hit["id"] for hit in result["hits"])
        page = result["pageToken"]
        if not page:
            break
        rejected(client, "eve_scene_search", snapshot=token, limit=3, pageToken=page)
    assert len(ids) == len(set(ids)) == 8
    after = client.tool("eve_scene_node_get", id="mine-door")
    assert before == after, "Search changed the authoritative node"
    # Failure injection uses the existing formal scene command, never eval.
    try:
        move(client, 20)
        moved = client.tool("eve_scene_search", **request, embedding=embedding, tags=["door", "interactable"],
                            center=[0, 0, 0], radius=10)
        assert [hit["id"] for hit in moved["hits"]] == ["stone-gate"]
        assert moved["snapshot"] == token, "Movement should reuse the semantic index"
        left_after_move = semantic_query(client, encoder, "semantic-demo", {"query": "木门", "tags": ["door", "interactable"],
            "spatial": [{"relation": "left-of", "reference": "chest"}]})
        assert not left_after_move["hits"]
    finally:
        move(client, before["x"])
    assert client.tool("eve_scene_node_get", id="mine-door") == before
    bridge_requests = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}},
        {"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}},
        {"jsonrpc": "2.0", "id": 3, "method": "tools/call", "params": {"name": "eve_scene_semantic_query", "arguments": {
            "query": "木门", "tags": ["door", "interactable"], "spatial": [{"relation": "left-of", "reference": "chest"}]}}},
    ]
    # Native TCP MCP accepts one client at a time; hand the connection to the bridge.
    port = client.socket.getpeername()[1]
    client.close()
    bridge = subprocess.run([sys.executable, "-X", "utf8", str(ROOT / "tools/scene-search/scene_search.py"),
        "--serve", "--port", str(port), "--offline", "--model-cache", str(encoder.model_cache)],
        input="\n".join(json.dumps(item) for item in bridge_requests) + "\n", capture_output=True, encoding="utf-8", timeout=120)
    assert bridge.returncode == 0, bridge.stderr
    responses = [json.loads(line) for line in bridge.stdout.splitlines()]
    assert responses[1]["result"]["tools"][0]["name"] == "eve_scene_semantic_query"
    bridge_hits = json.loads(responses[2]["result"]["content"][0]["text"])
    assert [hit["id"] for hit in bridge_hits["hits"]] == ["mine-door"]
    return {"schema": "eve.scene-search-verification", "schemaVersion": 1, "ok": True,
            "model": encoder.identity, "recallAt1": 1.0, "cases": evidence,
            "checks": ["real-scene", "real-model", "hnswlib", "structured-without-vectors", "filtered-hnsw",
                       "paging", "read-only", "model-mismatch", "budgets", "live-geometry-without-reembedding",
                       "world-left", "observer-right", "measured-spatial-evidence", "natural-text-stdio-mcp"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=7547)
    parser.add_argument("--model-cache", type=Path, default=ROOT / "build/model-cache")
    parser.add_argument("--offline", action="store_true")
    parser.add_argument("--output", type=Path, default=ROOT / "build/scene-search-verification.json")
    args = parser.parse_args()
    encoder = Encoder(args.model_cache, args.offline)
    client = Client(args.port)
    try:
        report = verify(client, encoder)
    finally:
        client.close()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stderr.reconfigure(encoding="utf-8")
    main()
