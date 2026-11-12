---
name: eve-scene-search
description: Finds real EVEngine scene objects with semantic MCP search and exact spatial predicates. Use for natural-language object discovery, doors left of a chest, observer-relative directions, HNSW index preparation, semantic query paging, or stale scene-search recovery.
---

# Scene semantic search (EVEngine)

## Entry and discovery

- Inspect the connected server's `tools/list`; use `eve_scene_semantic_query` for natural text.
  Native `eve_scene_search` takes vectors, not query text. Do not invent a tool or embedding.
- Source-checkout setup: start the semantic-scene-search example with the built engine's
  `--mcp-port=7547`, then configure the MCP client to launch:
  `python <repo>/tools/scene-search/scene_search.py --serve --host semantic-demo --port 7547`.
  Use absolute Python/script/cache paths; install that Python's requirements first.
  Add `--offline` only after the model cache exists. Model loading/index build precede tool readiness.
- The repository client is required; do not assume an older SDK contains it. In other games use the
  actual mounted host and port. Native TCP has one active client: stop other bridges/direct clients first.
- If only native tools exist, prepare → encode record.text outside the engine → publish → query
  with the same model identity. With no embedding, native structured filtering needs no model.

## Query workflow

1. **Resolve anchors**: query the reference meaning with several candidates, e.g.
   `{"query":"装金币的宝箱","limit":3}`. Read names/tags/positions; use returned IDs.
   Resolve observer separately when needed. Limit 1 does not prove uniqueness.
2. **Disambiguate**: use conversation context to choose the actual reference and frame.
   If multiple anchors or world/object/observer meanings remain plausible, clarify before claiming an answer.
3. **Split meaning and geometry**: query contains object meaning; tags and spatial carry exact constraints.
   Do not rely on embedding to understand "left of". Do not invent tags not authored in this host.
4. **Inspect evidence**: verify hit IDs, metadata, positions, `spatialEvidence.distance` and
   `offsetRightUpForward`. Similarity score is not a probability or proof of gameplay capability.
5. **Report**: identify host/session, object ID/name, frame/reference/observer and measured basis.
   Say when candidates are ambiguous, truncated, or absent. Search grants no mutation permission.

Example tool arguments AFTER resolving the example's chest ID:

```json
{
  "query":"入口木门",
  "tags":["door","interactable"],
  "spatial":[
    {"relation":"left-of","reference":"chest","frame":"world"},
    {"relation":"near","reference":"chest","distance":5}
  ],
  "limit":5
}
```

The bundled example returns mine-door, offset [-2,0,-3], distance about 3.606.
These IDs are example data; never reuse them in an unrelated host.

## Spatial contract

- Relations: left-of, right-of, above, below, in-front-of, behind, near, inside; filters are AND.
- world: 3D +X right, +Y up, +Z forward. 2D +Y is down; evidence's up component points upward.
- reference: axes of reference node. observer: axes of explicit observer node, same space as reference.
- Observer frame changes the axes, NOT the origin: offsets/distance remain relative to reference.
  To search ahead of the observer itself, set both reference and observer to that resolved ID.
- Node forward is local +Z, not Camera3D -Z or screen space. Do not silently treat an arbitrary camera
  node as an equivalent observer. Degenerate frames fail rather than inventing an orientation.
- near compares origins. inside compares candidate origin to authored reference world AABB;
  missing bounds fails. These are not visibility, whole-object containment, collision or reachability tests.

## Pagination, freshness and errors

- matched counts exact-filter-eligible objects, not semantic relevance. HNSW is approximate.
- candidates defaults to 128. candidateTruncated means the ANN window omits eligible objects;
  increase candidates within 4096 if needed and restart. Larger windows do not prove exact global top-k.
- truncated/pageToken refer to paging inside the current window. Repeat identical query/filters/budgets,
  changing only pageToken. Do not run another successful query between pages; there is one active cursor.
- One process owns one search session/model space. Coordinate callers serially or use separate engines.
- Movement/rotation/visibility/layer/bounds read live and reuse embeddings; old geometry pages become stale.
  Restart paging from the first page after a change; never concatenate different scene projections.
- Name/tag/membership/hierarchy/space/host-lifetime changes require rebuild. Python bridge checks readiness;
  native callers prepare and republish in the new record order. Keep IDs scoped to live host/session.
- isError is failure, not an empty search result. Correct invalid parameters, re-resolve unknown IDs,
  supply required observer, or report unavailable bounds. Do not silently remove a requested constraint.
- No model/provider/cache: report that missing dependency; do not substitute keyword/random embeddings.
  Failed publish keeps the old index. Stop repeated rebuilds if the semantic source keeps changing.

## Verify and handoff

- For "chest's left nearby door", first obtain chest ID, then query the door with spatial predicates.
  Record actual tool request/result and measured offset; successful initialization alone is insufficient.
- Read-only use: compare relevant scene facts when verification is requested; do not mutate for discovery.
  Separate formal actions from search and revalidate the target before acting; do not use eve_eval.
- Limits: 4096 nodes, 1024 vector dimensions, 256 hits/page, 16 spatial predicates. Scene verification
  remains linear; do not claim large-scale speed or recall from this small example.
- Source tutorial: `docs/usr/modules/scene/semantic-search.md`; protocol/model details:
  `tools/scene-search/README.md`. These require the repository, not just the installed skill file.
