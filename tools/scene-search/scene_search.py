"""Real local embeddings -> native SceneHost search; Python never ranks scene candidates."""

from __future__ import annotations

import argparse
import contextlib
import copy
import json
from pathlib import Path
import socket
import sys

MODEL = "sentence-transformers/paraphrase-multilingual-MiniLM-L12-v2"
REVISION = "e8f8c211226b894fcb81acc59f3b34ba3efd5f42"


class Client:
    """Synchronous local MCP client. Explicit close releases the connection."""

    def __init__(self, port: int):
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=60)
        self.stream = self.socket.makefile("r", encoding="utf-8")
        self.sequence = 0
        self.rpc("initialize", {"protocolVersion": "2025-06-18", "clientInfo": {
            "name": "scene-search", "version": "1"}})
        self.socket.sendall(b'{"jsonrpc":"2.0","method":"notifications/initialized"}\n')

    def close(self):
        self.stream.close()
        self.socket.close()

    def rpc(self, method, params):
        self.sequence += 1
        request = {"jsonrpc": "2.0", "id": self.sequence, "method": method, "params": params}
        self.socket.sendall((json.dumps(request, allow_nan=False) + "\n").encode("utf-8"))
        for line in self.stream:
            response = json.loads(line)
            if response.get("id") == self.sequence:
                if "error" in response:
                    raise RuntimeError(response["error"])
                return response["result"]
        raise RuntimeError("Engine disconnected")

    def tool(self, name, **arguments):
        result = self.rpc("tools/call", {"name": name, "arguments": arguments})
        text = "\n".join(item["text"] for item in result.get("content", []) if item.get("type") == "text")
        try:
            value = json.loads(text)
        except json.JSONDecodeError as error:
            raise RuntimeError(text) from error
        if result.get("isError") or value.get("ok") is False:
            raise RuntimeError(value)
        return value


class Encoder:
    """Pinned multilingual MiniLM, official attention-mask mean pooling, FP32 CPU + L2.

    Runs wholly outside the engine. Does not use remote inference or execute model code.
    Rejects inputs exceeding the trained 128-token sequence budget instead of hiding truncation.
    """

    def __init__(self, cache: Path, offline: bool = False):
        import torch
        from transformers import AutoModel, AutoTokenizer

        self.torch = torch
        self.model_cache = cache
        torch.set_num_threads(4)
        options = {"revision": REVISION, "cache_dir": str(cache), "local_files_only": offline,
                   "trust_remote_code": False}
        self.tokenizer = AutoTokenizer.from_pretrained(MODEL, **options)
        self.model = AutoModel.from_pretrained(MODEL, use_safetensors=True, **options).float().cpu().eval()
        self.identity = f"{MODEL}@{REVISION}:mask-mean:max128:fp32:l2:v1"
        self.cache: dict[str, list[float]] = {}

    def encode(self, texts: list[str]) -> list[list[float]]:
        missing = list(dict.fromkeys(text for text in texts if text not in self.cache))
        for start in range(0, len(missing), 16):
            batch = missing[start:start + 16]
            inputs = self.tokenizer(batch, padding=True, truncation=False, return_tensors="pt")
            if inputs["input_ids"].shape[1] > 128:
                raise ValueError("Embedding text exceeds 128 tokens; shorten scene name/tags or query")
            with self.torch.inference_mode():
                hidden = self.model(**inputs).last_hidden_state
                mask = inputs["attention_mask"].unsqueeze(-1).to(hidden.dtype)
                pooled = (hidden * mask).sum(dim=1) / mask.sum(dim=1).clamp(min=1)
                embeddings = self.torch.nn.functional.normalize(pooled, p=2, dim=1).tolist()
            self.cache.update(zip(batch, embeddings))
        result = [self.cache[text] for text in texts]
        if len(self.cache) > 8192:
            self.cache.clear()
        return result


def build(client: Client, encoder: Encoder, host: str):
    snapshot = client.tool("eve_scene_search_prepare", host=host)
    vectors = encoder.encode([record["text"] for record in snapshot["records"]])
    client.tool("eve_scene_search_publish", snapshot=snapshot["snapshot"], model=encoder.identity, vectors=vectors)
    return snapshot


def ready(client: Client, encoder: Encoder, host: str):
    """Reuse the scene-built native index; rebuild only when semantic membership/text changes."""
    try:
        status = client.tool("eve_scene_search_status")
        if status["host"] == host and status["model"] == encoder.identity:
            return status
    except RuntimeError:
        pass
    build(client, encoder, host)
    return client.tool("eve_scene_search_status")


def semantic_query(client: Client, encoder: Encoder, host: str, request: dict):
    request = dict(request)
    text = request.pop("query")
    if not isinstance(text, str) or not text.strip():
        raise ValueError("query must be non-empty natural language")
    if {"snapshot", "model", "embedding"} & request.keys():
        raise ValueError("snapshot/model/embedding are managed by the semantic query tool")
    status = ready(client, encoder, host)
    return client.tool("eve_scene_search", snapshot=status["snapshot"], model=encoder.identity,
                       embedding=encoder.encode([text])[0], **request)


def serve(client: Client, encoder: Encoder, host: str):
    """Local stdio MCP entry point: the calling language model supplies explicit spatial predicates."""
    ready(client, encoder, host)
    native = next(tool for tool in client.rpc("tools/list", {})["tools"] if tool["name"] == "eve_scene_search")
    schema = copy.deepcopy(native["inputSchema"])
    for field in ("snapshot", "model", "embedding"):
        schema["properties"].pop(field)
    schema["properties"]["query"] = {"type": "string", "minLength": 1,
                                      "description": "Object meaning only, e.g. wooden door; put geometric intent in spatial"}
    schema["required"] = ["query"]
    tool = {"name": "eve_scene_semantic_query", "inputSchema": schema,
            "description": "Search real scene objects using a fixed local embedding model and native hnswlib. "
            "For 'door left of the chest', first resolve 'chest' to a unique ID with this tool, then query 'door' "
            "with spatial=[{relation:'left-of',reference:<ID>,frame:'world'}]. For the player's view use "
            "frame:'observer',observer:<player ID>. Ask for clarification if anchor or frame is ambiguous. "
            "3D: +X right,+Y up,+Z forward; 2D:+Y down. Node frames use local +Z forward, not camera -Z. "
            "Read offsetRightUpForward and distance in spatialEvidence; never infer geometry from cosine. "
            "inside means origin within authored AABB, not collision/visibility. " + native["description"]}
    for line in sys.stdin:
        message = None
        try:
            message = json.loads(line)
            if "id" not in message:
                continue
            method = message.get("method")
            if method == "initialize":
                result = {"protocolVersion": "2025-06-18", "capabilities": {"tools": {}},
                          "serverInfo": {"name": "eve-scene-semantic-search", "version": "1"}}
            elif method == "tools/list":
                result = {"tools": [tool]}
            elif method == "ping":
                result = {}
            elif method == "tools/call":
                params = message.get("params", {})
                if params.get("name") != tool["name"]:
                    raise ValueError("unknown tool")
                with contextlib.redirect_stdout(sys.stderr):
                    value = semantic_query(client, encoder, host, params.get("arguments", {}))
                result = {"content": [{"type": "text", "text": json.dumps(value, ensure_ascii=False)}]}
            else:
                raise ValueError("unsupported MCP method")
            response = {"jsonrpc": "2.0", "id": message["id"], "result": result}
        except Exception as error:
            response = {"jsonrpc": "2.0", "id": message.get("id") if isinstance(message, dict) else None}
            if isinstance(message, dict) and message.get("method") == "tools/call":
                response["result"] = {"isError": True, "content": [{"type": "text", "text": str(error)}]}
            else:
                response["error"] = {"code": -32700 if message is None else -32601, "message": str(error)}
        print(json.dumps(response, ensure_ascii=False, allow_nan=False), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("query", nargs="?", help="Object meaning; pass geometry explicitly with --spatial")
    parser.add_argument("--serve", action="store_true", help="Expose natural-text semantic query as a stdio MCP tool")
    parser.add_argument("--build", action="store_true", help="Build the scene HNSW index once, before queries")
    parser.add_argument("--spatial", type=json.loads, default=[], help="JSON array of exact spatial predicates")
    parser.add_argument("--host", default="semantic-demo")
    parser.add_argument("--port", type=int, default=7547)
    parser.add_argument("--tag", action="append", default=[])
    parser.add_argument("--kind", default="")
    parser.add_argument("--center", nargs=3, type=float)
    parser.add_argument("--radius", type=float)
    parser.add_argument("--limit", type=int, default=10)
    parser.add_argument("--offline", action="store_true")
    parser.add_argument("--model-cache", type=Path, default=Path(__file__).resolve().parents[2] / "build/model-cache")
    args = parser.parse_args()
    if (args.center is None) != (args.radius is None):
        parser.error("--center and --radius must be supplied together")
    if not args.query and not args.serve and not args.build:
        parser.error("a query, --build or --serve is required")
    with contextlib.redirect_stdout(sys.stderr):
        encoder = Encoder(args.model_cache, args.offline)
    client = Client(args.port)
    try:
        if args.serve:
            serve(client, encoder, args.host)
            return
        if args.build:
            build(client, encoder, args.host)
            if not args.query:
                print(json.dumps(client.tool("eve_scene_search_status"), ensure_ascii=False, indent=2))
                return
        request = {"query": args.query, "tags": args.tag, "kind": args.kind,
                   "limit": args.limit, "spatial": args.spatial}
        if args.center is not None:
            request.update(center=args.center, radius=args.radius)
        result = semantic_query(client, encoder, args.host, request)
        print(json.dumps(result, ensure_ascii=False, indent=2))
    finally:
        client.close()


if __name__ == "__main__":
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdin.reconfigure(encoding="utf-8")
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stderr.reconfigure(encoding="utf-8")
    main()
