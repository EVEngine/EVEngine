# Convex Voronoi prefracture

`physics_destruction_cook::cookVoronoi` partitions a closed convex source mesh.
Uniform and clustered sites use an injected seed and named SplitMix64 stream.
For cell i, retain every half-space `(site_j-site_i) dot x <=
(site_j-site_i) dot (site_i+site_j)/2`. Clip polygons, close each cut with an
outward cap, triangulate, and rebase vertices to an interior body origin. Coplanar
source triangles are merged before clipping to avoid slivers from their diagonals.
The result uses actual source planes, not its bounding box. Source winding may be
consistently reversed; open, nonconvex and degenerate inputs reject explicitly.

The source is budgeted to 128 triangles and 254 welded vertices; each source admits
1..64 sites, with at most 254 vertices per output convex collider. Assembly/clip
limits are 4096 fragments and 4 million geometry scalars. Sampling rejection has a
fixed attempt budget. Unsupported inputs return Result diagnostics, never proxy
fallbacks. Geometry validation checks finite coordinates, positive volume, convex
half-spaces and exactly two opposite directed uses of every edge. Geometry/source
scale tolerances are relative; test volume error is 1e-4 for a 2m cube. Stored
float32 geometry survives serialization exactly. Cross-device bitwise physics
simulation is not promised; playback uses the already stored samples.

Cell connections come from nonzero shared Voronoi faces, not center distances.
`cookVoronoiAssembly` fractures authored box components independently and preserves
explicit parent support edges. Across a parent edge, overlapping child bounds with
the configured contactTolerance (default 0.04m) form weld candidates. This assembly weld approximation is separate
from the exact within-component Voronoi graph. No links are invented across parents
that were never connected. Mass is fragment volume times material density.

## Ownership, mechanisms and boundaries

The existing cook satellite owns generation. The runtime destruction module owns
`FractureGeometry`, immutable mesh sharing, world handles, hull creation and tracks.
The existing graphics satellite owns GPU caches. No new top-level module, ECS
system, capability or provider registry is introduced. Four-axis selection is
one caller, offline/once, known consumer, all-or-nothing publication: direct typed
Result calls and owning data are sufficient (R-MECH-1). Draw uses prepared meshes
and typed arrays, with no string lookup (R-MECH-2). Existing manifest fragments own
all sources and the new `DestructionCook` script entry (R-FACE-1/2).

Instances deep-copy validated cook geometry into shared const storage. Clips and
renderers share that immutable storage, so source assets and physics worlds can be
destroyed in either order. Existing generation-qualified PhysicsLinks detect stale
worlds. No raw world/body pointers are persisted. Failed hull creation rolls back
created bodies; failed GPU preparation releases candidates before publishing.
Snapshot restore cannot alter the immutable geometry metadata. Cook and physics
creation invoke no callbacks and hold no locks. GPU submission owns a render-thread
forward-contributor token; it copies poses immediately and unregisters before
renderer destruction. Contributor callbacks never mutate the drawer registry. No runtime dependency points back up to cook or asset.

Bake and upload are explicit stages: cook -> instantiate -> fixed-step capture ->
save; load -> prepareAnimation -> sample/draw (R-COST-1/2). Preparation runs once per
asset/renderer and returns a checked status. A stale prepared asset is rejected
until explicitly prepared again. Box collections and the legacy proxy cook retain
their public behavior, with the latter documented as compatibility-only.

## Persistent format

EVECOLL1 version 2 retains the v1 header (magic, version, bone count, frame count)
then adds uint32 mesh count (0 or bone count). Each mesh stores uint32 vertex and
triangle counts, packed float32 XYZ vertices, uint32 triangle indices, and uint32
material tag per triangle (0 exterior, 1 cut interior). Frame records are unchanged.
All scalars are little endian. Geometry is stored once, independently of frame
count. Version 1 migrates to box playback; other versions, trailing bytes, invalid
indices/tags, nonconvex/open geometry and excessive counts reject before candidate
publication. No separate unknown fields are permitted in this fixed binary schema.
Colors are renderer settings; arbitrary material-resource references are not saved.

## Scope and verification

A nonconvex building is an assembly of convex walls/slabs. This is not arbitrary
nonconvex triangle CSG, recursive fracture, or a structural engineering solver.
The example loads a content-keyed `.evecollapse` before creating any world or cook
instance. Its structural preset authors 40 components, partitions them into 76
fragments and records 421 frames. After save/reload it destroys the physics world.
Subsequent launches skip both prefracture and simulation. Changing config.nut,
house.json or main.nut derives a new noncryptographic two-word cache identity;
existing bakes are retained. Direct clip.load(path) needs no original recipe.

Tests cover volume conservation, sampled partition coverage without overlaps,
repeatable seeds, tetrahedron and clustered sources, shared-face graph, open and
nonconvex rejection, all 93 house seeds, failed admission without leaked bodies,
immutable snapshot metadata, exact mesh/tag disk round trip, source teardown and
v1 animation migration. Full workspace/source gate results and engine screenshots
are recorded under build/voronoi-*. Only L1 source ownership is claimed until a
trimmed build is run (R-BOUND-1/3); include graphs are not L2 evidence. No deliberate
architecture exception is introduced.

## Parameterized structural damage

`AssemblyFractureSettings` keeps typed group and per-component policies. A component
override wins over its source clusterId group; unconfigured parts use the uniform
FractureRecipe. Granularity (`fragments`), internal strain threshold (`cohesion`)
and authored joint multiplier (`connectionStrength`) are independent. Defaults
retain one piece for foundations, columns and beams; wall panels use three cells;
slabs and roof panels use two. Stronger internal cohesion allows a roof panel to
lose its weaker connections while remaining a large connected chunk. Strain units
are authored gameplay units, not engineering stress in Pa.

The settings are exposed via canonical Result setters. Source groups need no engine
house superclass or fixed role enum. Example config.nut names the structural groups
and independently controls the contact tolerance, fragment budget, seed, fixed Hz,
duration, breaks per step, and each local impact's time, position, radius, strain,
impulse and direction. Actual damage fraction emerges from these parameters and
the graph, rather than deleting a random percentage or breaking all edges globally.

Unbroken graph edges on convex instances now have physical welds. Their wrappers
are uniquely owned by the instance and backend joints belong to World3D; edge
handles are generation-qualified. Breaking an edge releases its weld. Unsupported
connected islands therefore fall as connected chunks instead of immediately
becoming independently moving debris. Legacy box collections retain their prior
behavior. Teardown is safe in either world/instance order. Snapshot restore stages
new welds, rolls back failed staging, then swaps and releases old welds.

The mesh renderer prepares one exterior material batch per authored structural group and one interior batch. Per-frame
sampling transforms vertices and normals into preallocated scratch, updates fitting
ring-buffered meshes, and issues one draw per prepared batch. Separate playback instances
use separate renderers and may share one immutable clip. This bounds draw submission cost; it does not grow the global
pool or alter unrelated Vulkan code. Preparation failure releases candidate GPU
resources before publication. Frame work scales with rendered vertices, so this
is a moderate-size CPU animation cache, not a GPU crowds system.

Runtime verification uses explicit ForwardPlus lighting with this low-light-count
scene. A 30-second validation-enabled run passed with the final ForwardPlus preset;
a separate clustered-disabled comparison also passed, while the clustered path
exhausted descriptors. The existing Vulkan
ensureClusteredBuffers implementation clears descriptor caches on every upload,
even without reallocation; clustered sustained playback remains unverified/blocked
until that independent backend issue is repaired. The isolated executable links
pre-existing backend DLLs because unrelated workspace Result.cpp changes block a
fresh full build. It is not evidence of a clean full-engine build.

Additional tests cover per-group strength/size policies, per-component precedence,
invalid policy rejection without mutation, partial break with retained welds,
falling chunk distance preservation, rebuild on snapshot restore, and world-first
teardown of owned welds.


## Saved fluid dust presentation

The optional host satellite `physics_destruction_dust` depends on fluids, graphics
and the destruction clip. Disabling it leaves the existing fracture/rigid playback
modules independent of fluids. Manifest source ownership is verified (L1); no
trimmed link/runtime claim is made.

`CollapseDust` owns typed gas samples and depth scratch. `capture` borrows a fluid
only for one call; `present` transactionally restores a dedicated presentation fluid,
never a gameplay solver. No source/world/camera/surface pointers survive the call.
Either owner destruction order is safe. There are no ECS systems or structural
mutations, callbacks, event subscriptions or runtime string-keyed service lookups.

EVEDUST1 v1 stores little-endian float32 sample time, solver spacing and XYZ/RGBA
records. The budget is 2048 frames, 1024 particles/frame and 524288 total samples;
nonfinite data, out-of-range colour, invalid times, unknown versions and trailing
bytes reject the entire candidate. No old schema exists to migrate. Save refuses
existing output and removes incomplete writes. Load failure preserves prior samples.
Capture fades gas during its last two remaining seconds. Fixed input ordering and
injected timesteps are repeatable on the same fluid build; cross-platform bake floats
are tolerance bounded. Replaying a saved track performs no simulation or RNG.

Expensive preparation is explicit: `prepareDepth` allocates at most 1024x1024
linear depths and RGBA8 upload scratch. `updateDepthTexture` rasterizes immutable
embedded fragment triangles with sampled transforms and perspective-correct depth.
It borrows the caller's Graphics-owned output texture for one call and retains no
GPU observer pointers. The fog compositor expects normalized linear RGBA8 depth;
quantization follows the selected camera clip range. Near-crossing triangles are
skipped; the overview camera stays outside the building.

Presentation reuses existing FogVolume/AtmosphereVolume/Volumetric froxel rendering.
`injectFog` projects each saved gas particle's support sphere to a bounded grid
footprint and evaluates the existing seeded FogVolume density/edge falloff there.
It matches full-frustum FogVolume injection in CPU reference tests. The clip owns no
fog grid: the caller clears, injects, integrates and uploads it. Existing shader
interpolation reconstructs XY and logarithmic Z slices without the discarded gas
image overlay. Uniform incident lighting is used; volumetric shadow scattering is
not claimed. The example updates at 12Hz and reuses uploads while paused.

Tests cover gas rising, saved replay after source destruction, endpoint/seek behavior,
overwrite refusal, unknown/truncated/trailing/nonfinite/over-budget file rejection,
failure rollback, bounded depth allocation and exact fog-injection reference parity.
Actual engine-owned frames compare fog enabled/disabled and paused-frame identity.


Runtime reconstruction is bounded to one CPU worker. `prepareFog` copies the immutable
rigid clip once outside playback, so source teardown cannot stale the worker. Jobs
capture owning gas/camera values, shared immutable clip storage and dimensions only.
They perform no ECS access, GPU calls, script callbacks or simulation. `requestFog`
returns Started/Busy/Unchanged; `takeFogFrame` polls without waiting and returns owning
pixels. `updateFog` is the render-thread adapter: it uploads completed media atlas pixels only, rejects old camera views and admits only the latest time when the slot is free.
There is no accumulating queue. Destruction joins and checks an unpublished Result
before member destruction. Cache mutation and re-preparation reject outstanding work.
Tests cover nonblocking collection, source-first teardown, in-flight owner teardown,
no duplicate work, admission validation and no task backlog.

The owning worker also reconstructs each integrated atlas slice with a separable
five-tap binomial filter, clamped inside that slice's tile. This smooths sparse dust
footprints without interpolating across unrelated Z tiles. It adds no main-thread
reconstruction. The configured update rate is a target; busy work is never queued.


### Same-frame fog occlusion

Runtime fog compositing uses the live GPU GBuffer, not a completed worker's depth
export. Submitted fragment meshes now contribute to the GBuffer pass with that
pass's camera, current vertex poses and clipping range. Forward drawing consumes
the submission after the GBuffer. Background completion uploads only fog media.
CPU depth remains an owning, timestamped offline export; it cannot become the
runtime scene-depth authority through updateFog's interface.

The legacy GBuffer callback API has process lifetime. The destruction graphics
satellite installs one dispatcher and keeps only weak callback registrations.
Each renderer owns its registration and expires it before mesh release; expired
entries are pruned on both admission and dispatch. All registration/dispatch/destruction
is render-thread affine and forbids reentrant renderer teardown during drawing.
No public graphics API or registry is duplicated; the existing engine pass owns
the current scene-depth attachment. This fixes stale house-shaped holes in dust
without returning CPU rasterization to the main thread.
