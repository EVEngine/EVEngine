# Saved collapse animation

Current implementation: [convex Voronoi prefracture](voronoi-prefracture.md).
`CollapseAnimation` owns baked fragment tracks and optionally shares immutable
`FractureGeometry`. `GeometryCollectionInstance` copies validated geometry from a
cooked collection and creates convex-hull colliders. Capture retains immutable
geometry once, then samples typed position/quaternion/state data after fixed steps.
A clip survives the source asset, instance, world, and process that produced it.

Version 2 stores embedded meshes and material tags once, followed by frame records.
Version 1 remains readable as box geometry. Unsupported versions and malformed
geometry/timestamps/counts/trailing bytes reject before publication. Script load
replaces its current clip only on success; save refuses existing paths. See the
linked design for exact byte layout and limits. Recording is single caller, uses
injected time, and performs no wall-clock or global RNG lookup.

Playback clamps endpoints, interpolates positions and shortest-path normalized
quaternions, and switches state at frame boundaries. Callers control pause, reverse,
seek and speed. Renderer offsets allow independent instances. Explicit
`prepareAnimation` uploads embedded meshes before draws; GPU allocations are not
performed per frame. Source and cap triangles use separate renderer colors.
Material-resource references and dust/audio event tracks are outside this version.

The updated house example prefractures on its first launch, simulates 361 frames,
saves and reloads, destroys its physics world, then plays. Later launches load the
file before any cook/world creation. Source, closed mesh, volume partition,
persistence, source teardown, v1 migration and failure rollback are tested in
`test/integration/house-collapse/physics_voronoi_fracture.cpp` and `test/integration/house-collapse/physics_collapse_animation.cpp`.

Full workspace verification remains separate from focused runtime evidence. The
normal build currently fails in unrelated common/Result.cpp due to a missing
ResultFailureKind declaration. This workspace uses a focused harness linked to the
existing SHARED libraries for gameplay and engine-MCP screenshot evidence. Source
gate logs, compiler logs, tests and screenshots are under build/voronoi-*; full
SHARED/OBJECT links and trimmed builds are not claimed green.
