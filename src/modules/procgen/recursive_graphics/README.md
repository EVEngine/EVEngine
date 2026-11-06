# Recursive frontier mesh adapter

Default-OFF L6 satellite. Required providers are procgen and procgen_recursive;
the lower mathematical module never includes graphics or MeshBuild. This adapter
produces the existing MeshBuild format for explicit submission by a scene owner.
It changes no generator registration, seeds, cache keys, boot list or camera.

The known owner calls a synchronous builder at refinement/edit/projection changes,
not once per object per frame. These four axes (one owner, rebuild frequency,
known recipient, atomic result) select a direct call. The six boundary faces have
C++ values only: no capability service, ECS System/Link, event, script or MCP API.
There are no retained pointers, locks, callbacks or implicit clocks. The model
remains authoritative; mesh and picking node paths are derived revision-tagged
values. The caller publishes only while that model revision/projection is current,
and discards/rebuilds after restore. No persistent format is introduced.

The input keys must completely cover the model without ancestor/descendant
duplication. Each nonempty node emits its edited axis-aligned bound as 24 vertices
and 12 outward-facing triangles; deleted nodes emit nothing. This is a conservative
coarse representation, not detailed ship/city geometry. Coarse bounds may overlap
after edits and contain empty space, so picking is broad-phase and needs refinement.
Mesh triangle/12 indexes boxNodePaths; no triangle-group strings are generated.

Root-local doubles are translated before exponent scaling and float conversion.
Bounds round outward, with a checked absolute quantization budget. Physical scale
is retained separately even when it cannot fit float. A successful local mesh does
not imply that a legacy camera can display its absolute physical position.
No culling, infinite projection, GPU upload or shader is added here. Quantization
checks are against binary64 projected coordinates, not a certified total arithmetic
error bound. Range, collapsed extents and budgets reject the entire candidate.

Output is owning and cancellation before publication is destruction. Complexity
is frontier validation plus linear mesh output, bounded by maxNodes; potential
capacity is never enumerated. Replay uses stable sorted node order and no RNG;
floating results have tolerance-based, not cross-platform bitwise, parity.
Full trimmed-build and rendered-frame acceptance remains separate from unit proof.
