# Saved collapse animation

Current implementation: [convex Voronoi prefracture](voronoi-prefracture.md).
`CollapseAnimation` owns baked fragment tracks and optionally shares immutable
`FractureGeometry`. `GeometryCollectionInstance` copies validated geometry from a
cooked collection and creates convex-hull colliders. Capture retains immutable
geometry once, then samples typed position/quaternion/state data after fixed steps.
A clip survives the source asset, instance, world, and process that produced it.

Versions 3/4 store embedded meshes and material tags once, followed by frame records
and a 32-byte noncryptographic content checksum. Version 4 additionally stores a
bounded EVECREC1 recipe before the meshes. `beginBake` admits provenance before
capture; `loadForRecipe` rejects absent or mismatching provenance. Versions 1/2 remain readable as
unverified legacy input; version 1 has box geometry. Saving legacy clips upgrades their integrity format to v3 without inventing provenance. Unsupported versions and malformed
geometry/timestamps/counts/trailing bytes reject before publication. Script load
replaces its current clip only on success; save refuses existing paths. See the
linked design for exact byte layout and limits. Recording is single caller, uses
injected time, and performs no wall-clock or global RNG lookup.

Playback clamps endpoints, interpolates positions and shortest-path normalized
quaternions, and switches state at frame boundaries. `CollapsePlayback` owns pause, reverse, seek, speed and rigid placement independently
for each instance. `prepare` freezes an owning snapshot; copies share that snapshot
without sharing mutable clock state. Explicit `preparePlayback` uploads embedded meshes before draws; GPU allocations are not
performed per frame. Source and cap triangles use separate renderer colors.
Material-resource references and dust/audio event tracks are outside this version.

The updated house example prefractures on its first launch, simulates 361 frames,
saves and reloads, destroys its physics world, then plays. Later launches load the
file before any cook/world creation. Source, closed mesh, volume partition,
persistence, source teardown, v1 migration and failure rollback are tested in
`test/integration/house-collapse/physics_voronoi_fracture.cpp` and `test/integration/house-collapse/physics_collapse_animation.cpp`.

Full workspace verification remains separate from focused runtime evidence.
The current implementation and acceptance evidence are tracked in
[the bake/replay checklist](2026-10-21-physics-bake-replay.md). The solver-free
`test/profile/physics_bake` executable validates production loading, sampling and
player behavior; it does not establish successful host rendering or script execution.
