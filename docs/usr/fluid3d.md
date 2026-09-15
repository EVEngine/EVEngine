# Fluid3D scripting

Fluid3D is the `fluids` module's bounded three-dimensional particle-fluid API.
It provides liquid, gas, granular, multiphase, emitter, query, foam and
screen-space surface-rendering workflows.

All calls run on the simulation thread. The script owns `VolumeFluid` and
`VolumeFluidEmitter` instances. The emitter retains no solver reference, so either
instance can be destroyed first. Mutation methods return the common Result table:
check `ok` before using `value`. Invalid operations preserve observable state.

```squirrel
function requireFluid(result) {
    if (!result.ok) throw result.status.summary;
    return result.value;
}
local state = requireFluid(fluids.volumeDefaults());
local liquid = requireFluid(fluids.newVolumeSimulator(state));
local emitter = eve.VolumeFluidEmitter();
local nozzle = requireFluid(fluids.volumeEmissionDefaults());
nozzle.description.origin = [0.0, 1.5, 0.0];
nozzle.description.direction = [0.0, -1.0, 0.0];
nozzle.description.prototype.life = 2.0;
// Run from a fixed simulation tick, not an unrestricted presentation loop.
local admitted = requireFluid(emitter.advance(liquid, nozzle, 1.0 / 60.0, 30.0, 8, 0.5));
requireFluid(liquid.step(1.0 / 60.0, 4));
```

Setting `nozzle.description.speed` to zero disables rate and jet controllers, matching
the package input-controller scripts. Disabled calls emit nothing and clear fractional
credit/partial travel, so restoring a positive speed cannot produce catch-up particles.

- `volumeDefaults()` returns schema `eve.volume-fluid`, version 20, with settings,
  owning particles, obstacle samples, height fields, solid attachments and optional simplex topology. `newVolumeSimulator(state)` validates
  the complete input before returning an owned solver.
- `snapshot()` returns an owning Result value. `restore(state)` atomically replaces
  the solver after validation. Unknown fields/schema versions are rejected.
- `emit(particles)` atomically admits an owning particle array. It does not copy or
  serialize the existing particle pool to decode the new batch.
- `getParticleCount()` returns the count without copying particles. `clear()`
  removes particles and contacts.
- Native `particleView()` provides a constant-time, allocation-free borrowed span
  for simulation-thread hot paths. Any non-const solver call invalidates it;
  scripts continue to receive owning snapshots and cannot retain this view. BURST
  actor scans and foam source collection use it instead of copying the full pool.
- `setGravity(x,y,z)` updates world-space acceleration for subsequent fixed
  steps, matching the package `WorldSpaceGravity` component. The native solver
  already stores particles in world coordinates, so no Transform is retained or
  evaluated per particle. Validation is atomic and the O(1) setting remains part
  of the existing solver snapshot.
- `actorMassProperties(actorGroup)` returns `mass`, `centerOfMass` and
  `particleCount` for the selected live actor group. Mass uses rest particle
  volume times density. This allocation-free linear query runs only when called;
  it adds no fixed-step or rendering work. Missing groups return `NotFound`.
- `setActorFilterCategory(actorGroup,category)` implements Fluid3D `SetCategory`.
  Category is an index from 0 through 15; the operation replaces the matching
  particles' low filter bits with `1 << category` and preserves every collision
  mask. It is an explicit allocation-free actor scan. Missing groups fail.
- `setActorCollisionFilter(actorGroup,collisionFilter)` implements the complete
  Fluid3DEmitter `Filter` setter. It replaces both packed 16-bit halves for every
  live particle in the actor group. Both category and mask must be nonzero;
  invalid filters and missing groups fail before mutation. The explicit call is
  one allocation-free linear scan and adds no fixed-step or GPU work.
- `updateActorMaterial(actorGroup,emission)` applies the emitter description's
  prototype material, data channels, radii, packed filter and self-collision flag
  to all existing particles in that actor group. It matches Fluid3DEmitter's explicit
  blueprint refresh while preserving particle position, velocity, orientation,
  color and remaining life. Zero radii resolve to half the solver spacing. The
  call validates the complete update before one allocation-free actor scan.
- `setActorSelfCollisions(actorGroup,enabled)` implements the emitter's direct
  `SetSelfCollisions` control for all currently live actor particles. Missing
  groups fail before mutation. The explicit call performs one allocation-free
  scan and does not register per-frame work.
- `killActorParticles(actorGroup)` implements Fluid3DEmitter `KillAll` without
  clearing other actors sharing the solver. It returns the removed count,
  compacts the dense pool once, and remaps surviving attachments, stitches,
  simplexes, interpolation and grabber state. Solver-owned index scratch avoids
  repeated per-particle erase work; a missing group fails without mutation.
- `applyParticleDrag(index,x,y,z,stiffness,damping,dt)` applies the package
  `Fluid3DParticleDragger` spring response to one ray-picked particle. Call it before
  the matching fixed step. It updates one velocity in O(1), allocates nothing,
  and atomically rejects stale indices, invalid coefficients or speeds above
  1000 m/s. Use `raycast()` to obtain the current dense particle index.
- Enabling the engine `Profiler` records `VolumeFluid::step`,
  `FluidSurfaceRenderer::renderVolume`, `FluidSurfaceRenderer::reconstruct` and
  `FluidSurfaceRenderer::renderToTexture` under the `fluids` module. This matches
  the package profiler's named solver-task visibility while using the shared
  engine timeline. Disabled profiling adds only the profiler's atomic entry test.
- `debugParticleFrames(actorGroup,size,maxParticles)` returns the selected actor's
  current particle origins and local red-X, green-Y and blue-Z ray endpoints. It
  matches the package `DebugParticleFrames` gizmo data while leaving presentation
  to caller tooling. The explicit query scans particles once, enforces its output
  budget, and adds no fixed-step, GPU, upload or surface reconstruction work.
- `debugSdfSlice(colliderLabel,axis,slice,maxDistance,maxSamples)` implements the
  package distance-field cutaway diagnostic. Axis is 0/1/2 for local X/Y/Z;
  `slice` is in `[0,1]`. It returns normalized row-major `values`, `width`, `height`
  and the world-space `origin`, `stepX`, `stepY` sample lattice. It is an explicit
  bounded query and adds no work to ordinary simulation or rendering frames.
- `particleInstances(actorGroup,sx,sy,sz,alpha,maxInstances)` returns stable-order
  instance rows for the package instanced particle renderer. Each row contains the
  current dense `particleIndex`, interpolated `position`, XYZW `orientation`,
  anisotropic `scale` and particle `color`. The caller submits these through its
  Graphics mesh/material batching. The complete-output budget rejects before allocation.
- `particleImpostors(actorGroup,radiusScale,r,g,b,a,alpha,maxInstances)` maps the
  package `Fluid3DParticleRenderer` controls onto the same batched instance row shape.
  It multiplies principal radii by `radiusScale` and particle RGBA by the supplied
  linear tint while preserving interpolation and anisotropic orientation. The caller
  chooses its quad/sphere mesh and material. The explicit query emits one row per
  particle instead of expanding four CPU vertices per particle, and rejects a complete
  output above the caller's budget before allocating it.
  Native render integrations can call `copyParticleImpostors(...)` with a retained
  destination vector; after its capacity is warmed, successful frames allocate no
  instance storage. Invalid input and budget failures leave that vector unchanged.

For targeted stability checks, load `soak-check.nut`, call `beginVolumeFluidSoak()`
once, then call `volumeFluidSoakChunk(1..10)` in bounded control requests. It keeps
the same 640-particle solver, surface renderer and texture, alternates the device-local
fast path with periodic full readback/upload coverage, and rejects larger blocking chunks.
- `contacts()` returns transient contacts from the latest successful step. Each
  item includes `colliderLabel`, point, normal, impulse, current `particleIndex`
  and stable `actorGroup`. The index becomes stale after particle mutation;
  `actorGroup` lets dispatchers deduplicate or route contacts by actor without
  another solver scan.
- `eve.VolumeFluidContactTracker().advance(fluid,distanceThreshold)` returns
  owning `type` 0/1/2 Enter/Stay/Exit events, deduplicated by collider and actor.
  Call it after successful solver steps. It owns no solver reference, invokes no
  callbacks, caps input at 65,536 contacts and atomically preserves prior state
  when validation fails. `reset()` clears the preceding contact set.
- `addRandomVelocity(intensity, seed)` implements the package's
  `AddRandomVelocity` actor helper. One deterministic unit-sphere direction is
  multiplied by the velocity-change intensity and applied uniformly to every
  current particle. The allocation-free linear pass rejects invalid intensity
  or any resulting speed above 100 m/s before changing the pool.
- `teleportActor(currentPose, targetPose)` implements `Fluid3DActorTeleport` with
  explicit `{position,rotation}` world poses because the native solver does not
  own an implicit Unity Transform. It rigidly transforms all particle positions
  and orientations, clears their linear/angular velocity, and preserves particle
  indices. All destinations are preflighted against solver bounds, so failure is
  atomic. The operation is allocation-free and linear in the particle count.
- `killParticle(index)` removes one current dense particle index through a
  structured Result. It returns capacity to the pool, compacts later indices,
  remaps surviving attachments and simplexes, drops affected simplexes, and
  keeps pending per-particle wind aligned. A stale index fails before mutation.
  Use `clear()` for the bounded all-particle operation. Both are explicit
  simulation-thread mutations and invoke no callbacks.
- `volumeEmissionDefaults()` returns schema `eve.volume-fluid-emission`, version 14,
  with a `description`. Unknown/missing fields and unsupported versions fail.
  Domain validation is performed during admission.
- `VolumeFluidEmitter.advanceBurst(solver, emission, count, minimumPoolFraction)`
  implements Fluid3D's `EmissionMethod.BURST`. The emission prototype's `actorGroup`
  identifies the actor: the complete bounded burst is admitted atomically when
  that group has no live particles, waits while any remain, and becomes eligible
  again after all expire or are killed. The actor pool threshold can defer a
  burst without failure. Each call performs one allocation-free linear actor scan
  and adds no GPU work; controller snapshots preserve its deterministic sequence.
- `description.shape`: 0 edge, 1 square, 2 disk, 3 sphere, 4 cube. Shapes are local
  to a nozzle whose +Z axis maps to `direction`; edge is local X, disk/square XY.
  `extent` is half-size; disk/sphere use its X component as radius.
- `speed` and per-axis uniform `jitter` are in m/s; `randomVelocity` in [0,1]
  reproduces Fluid3D's linear blend from the shape direction to a seeded unit vector;
  `seed` selects the reproducible
  emission RNG stream. `prototype` supplies material, color, data and lifetime.
  For precomputed Distribution shapes, `useShapeColor=true` assigns each
  point color exactly, matching Fluid3DEmitter.ResetParticle; set it to false to preserve the
  prototype color. This choice is evaluated only while emitting particles.
  Its position and velocity are replaced by generated values. A prototype with
  `radii=[0,0,0]` resolves to half the destination solver spacing when admitted.
  Explicit ellipsoid orientations are composed with the burst/jet nozzle frame.
- `granularRadiusRandomness` is an opt-in percentage in `[0,100]`. For granular
  prototypes it independently reduces each emitted sphere from half the solver
  spacing, with the same 1 mm lower bound used by Fluid3D. Zero consumes no extra RNG
  sample and preserves explicit prototype radii. Non-granular prototypes ignore it.
- Granular material `staticFriction` and `dynamicFriction` are coefficients in
  `[0,1]`. Static friction cancels tangential displacement within its contact
  limit; otherwise dynamic friction clamps sliding. `rollingContacts` adds
  contact-point angular velocity and `rollingFriction` damps relative spin.
  These particle-pair operations reuse the separation-neighbor traversal.
- `emitBurst(nozzle,count)` returns an admitted count. Maximum count is 4096;
  insufficient capacity or an out-of-bounds generated particle rejects the whole
  burst. Zero count does not validate unused prototype material.
- `VolumeFluidEmitter.advance(solver,nozzle,dt,rate,maxPerStep,minimumPoolFraction)`
  returns the count emitted this call. Rate is particles/second. dt is in
  [0,1/30], maxPerStep in [1,4096], threshold in [0,1]. A stopped emitter resumes
  only when free capacity is strictly greater than floor(capacity*threshold).
  Whole-particle demand beyond the work limit is discarded; fractional credit is
  retained. Capacity exhaustion produces a successful zero count.
- `VolumeFluidEmitter.isEmitting()` and `VolumeFluidJetEmitter.isEmitting()`
  expose the package's direct emitter-state query. They read the controller's
  existing state in O(1) without constructing the owning snapshot/Value table;
  snapshot and restore remain available for deterministic checkpoints.
- `emitter.activeParticleCount(solver, description)` exposes Fluid3DActor's live
  particle count for the description's actor group. It returns zero for an empty
  actor and performs one allocation-free read-only scan. Use it for UI and pool
  decisions instead of `actorMassProperties`, which also computes mass and center
  of mass. Invalid descriptions and missing solvers return structured failures.
- `emitter.killParticle(solver, description, actorIndex)` matches Fluid3DEmitter's
  actor-local active index instead of exposing the shared solver's dense index.
  It scans the selected actor once, then delegates to the canonical atomic kill,
  compaction, event and attachment/stitch remapping path. A missing actor, stale
  index or invalid description leaves the complete solver unchanged. This prevents
  an interleaved multi-emitter pool from deleting another emitter's particle.
- `emitter.emitParticle(solver, description, offset, dt)` matches Fluid3DEmitter's
  direct single-particle admission. `offset` is the normalized [0,1] fraction of
  prescribed emission travel within `dt`; the chosen distribution direction and
  speed move the birth position by `direction*speed*dt*offset`. Calls advance the
  controller's deterministic distribution/RNG sequence. Actor and shared capacity
  are checked before constructing the one-particle batch; a full pool returns zero.
- `description.actorCapacity` matches `Fluid3DEmitterBlueprintBase.capacity` and defaults
  to 1000. Each rate, burst or jet emitter stops at its own actor limit even when the
  shared solver still has room. `minimumPoolFraction` is evaluated against that actor
  pool. Capacity is restricted to [1,65536]; legacy emission schemas through version 13
  migrate to 1000. The actor count and both capacity checks happen before any particle
  batch construction, and one actor scan is shared by each controller call.
- `fluids.volumeEmitterBlueprintMetrics(resolution, restDensity, smoothing)` evaluates
  the supplied 3D blueprint formulas exactly: particle size is
  `1/(10*cbrt(resolution))`, mass is `restDensity*particleSize^3`, and support radius
  is `particleSize*smoothing`. Use `particleSize` as the volume solver's spacing when
  reproducing one Fluid3D blueprint so EVEngine's authoritative `spacing^3*density` mass
  agrees with the returned mass. This setup-only query allocates no solver/GPU state.
- `fluids.volumeFluidEmitterBlueprintDefaults()` returns all serialized
  `Fluid3DEmitterBlueprint` fields. After editing that owning value,
  `fluids.prepareVolumeFluidEmitterBlueprint3D(blueprint)` validates it atomically and
  returns `{solver,emission,metrics}`. Capacity and particle size initialize the shared
  solver candidate; capacity also limits the emitter actor. Density, smoothing,
  viscosity, surface tension, buoyancy, atmospheric drag/pressure, vorticity, diffusion
  and diffusion data are copied into the emission prototype. This conversion is a
  setup-time CPU operation and creates no solver, surface renderer or GPU resource.
- `fluids.volumeGranularEmitterBlueprintDefaults()` and
  `fluids.prepareVolumeGranularEmitterBlueprint3D(blueprint)` provide the corresponding
  `Fluid3DGranularEmitterBlueprint` path. Capacity, resolution and rest density initialize
  the solver/emitter candidate; `randomness` in `[0,100]` selects the existing seeded
  granular radius reduction and the prototype phase is Granular. The returned smoothing
  radius is zero, matching Fluid3D's non-fluid material path. Shared solver capacity may be
  increased before construction when several actors use the same solver.
- `configureParticleEvents(capacity)` enables bounded `OnEmitParticle` /
  `OnKillParticle` parity. Zero disables collection; the default is disabled.
  Valid capacities are 0..65536 and storage is reserved at configuration time.
  Emission, explicit removal, actor `KillAll` and lifetime expiry append owning
  snapshots without invoking script callbacks. A full queue drops later events
  and increments `dropped` instead of allocating. `drainParticleEvents()` returns
  `{events,dropped}` and clears both counters. Event `type` is 0 for emitted and
  1 for killed. `particleIndex` is the dense index at event time and may become
  stale; use the included `particle` snapshot and stable `actorGroup` after any
  later pool mutation.

Emitter runtime progress can be captured together with the solver and emission
description using the combined emitter-checkpoint API documented below. A plain
solver snapshot intentionally contains solver state only, while recreating a
controller without that checkpoint starts a new fractional-credit and RNG sequence.
Emission descriptions migrate versions 1-13 to version 14 as documented below. Solver snapshots encode v20; the Value codec migrates v1-v19.

The example starts paused. Press 5 for the jet and Space to advance. It bounds
solver steps and surface updates; discarded wall time is displayed. The rate-driven random controller can produce overlapping particles; the jet
example uses the spacing-driven controller documented below. Moving-jet parity,
including pose interpolation, inherited linear/angular velocity and deterministic
checkpoint continuation, is covered below. Rendering modes that need host-side
foam, refraction or multicolor composition still read GPU data back, so a short
successful run is not evidence of sustained real-time performance.

Press B for the bounded FaucetAndBucket scene. The faucet emits continuously; hold D
to rotate the bucket toward its pouring pose and release D to return it upright. R
rebuilds the solver and emitter. This mode uses three transformed analytic box
colliders, a 64-particle capacity and at most two emissions per fixed step.

## Spacing-driven jets

Use `eve.VolumeFluidJetEmitter()` for a planar nozzle. Its method is
`advance(solver, nozzle, dt, maxPerStep, minimumPoolFraction)`, returning the
common Result with emitted count. Call it immediately **after** `solver.step`
with the same dt. This controller has its own runtime phase; it is independent
of the rate-driven random `VolumeFluidEmitter`.

Edge, square and disk nozzles generate centered lattice points at the solver's
rest spacing. Layers are emitted once the prescribed nozzle travel reaches that
spacing. Newly emitted layers receive their remaining intra-step advection along
the nozzle direction. This advection uses prescribed nozzle velocity, not a full
fluid/gravity integration of each particle's fractional age. A supplied nozzle
sample is held constant for this step; historical moving-nozzle transforms are
not reconstructed.

The work cap must fit a whole nozzle layer. At most 4096 lattice candidates are
visited. Whole layers beyond the work or pool limit are discarded, fractional
travel is retained, and failure preserves phase and solver state. Pool restart
uses the same strict threshold rule as the rate controller. Volume shapes are
rejected by this planar controller; use explicit bursts for sphere/cube domains.
The `5 Jet` example now uses this spacing-driven controller after each fixed tick.


## Precomputed image distributions

Emission descriptions now encode `eve.volume-fluid-emission` version 14 with
`description.distribution`. Version 1 is migrated by supplying an empty point
array, a prototype collision filter, automatic radii and identity orientation.
Versions 2 through 7 receive their later missing fields in sequence; version 5
gains a zero granular-radius-randomness default, version 6 gains smoothing 2,
version 7 gains zero angular velocity with rolling contacts disabled, and
version 8 gains static/dynamic particle friction defaults of 0.2. Version 10
adds a per-distribution-point direction; v1-v9 migrate it to local +Z. Version 12
adds stickiness, stick distance and the two material combine modes; v1-v11 migrate
those fields to zero adhesion with Average combining. Version 13 adds Fluid3DEmitter
`useShapeColor`; v1-v12 migrate it to true so existing distribution colors retain
their previous behavior. Version 14 adds actor-local capacity; v1-v13 migrate to
Fluid3D's default capacity of 1000.
Unknown fields are still rejected. The solver snapshot is v20; emission descriptions remain
independently versioned.
Shape 5 (`Distribution`) emits precomputed local `{position,color,direction}` values.
Color replaces the prototype RGBA when `useShapeColor` is enabled. Explicit bursts cycle through the point list;
the rate-driven stream preserves the cursor across calls. The planar jet
controller emits it as a complete colored nozzle layer and preserves its point order.
Direction is normalized and transformed by the nozzle pose, allowing each sample
to carry its own Fluid3D-style outward or forward emission velocity. Runtime rate
emission indexes borrowed distribution storage directly without copying and rotating
the complete point vector each frame.

The C++ setup function `buildVolumeFluidImageDistribution` accepts a borrowed
linear-RGBA pixel span (bottom row first) and returns owning distribution points.
It preserves aspect ratio under maximum world size, uses a rest-spacing lattice,
clamp-addressed bilinear samples and a strict alpha-greater-than-threshold mask.
It caps candidate lattice work at 65536 and output points at 4096; over-limit
requests fail instead of silently truncating the image. Pixels are never retained.
The ImageData and ModelData adapters described below build these owning
distributions during setup. They can be serialized, burst-emitted, streamed, or
used as complete spacing-driven jet layers.


## ImageData adapter and voxel meshes

`fluids.volumeEmissionFromImage(image,pixelScale,maximumSize,spacing,threshold,srgb)`
returns a Result containing a version-11 emission description. `image` must be a
valid RGBA8 ImageData with at most 16M pixels. The input is borrowed only for this
setup call, never retained. Top-down image rows are converted to the bottom-up
emission plane. Pass `srgb=true` for encoded color textures; RGB is linearized
before interpolation, while alpha remains linear. The returned prototype is
white so the point colors are preserved. Empty masks are successful empty
distributions; rate streams emit zero, while requesting a nonzero explicit burst
from an empty distribution fails.

The C++ `buildVolumeFluidMeshDistribution(vertices,indices,scale,spacing)` builder
voxelizes indexed triangles and fills enclosed interior cells by excluding the
exterior-connected empty cells. It returns owned distribution points. Open meshes
retain their surface voxels without assuming a closed interior. Voxel size is
at least the requested spacing and longest scaled extent / 32. Budgets are 65536
triangles, 4M candidate intersection checks, and 4096 output points. Excessive
requests fail with a diagnostic; no partial point set is returned. Degenerate
triangles and invalid indices are rejected. This coarse voxelization does not
promise preservation of features thinner than a voxel. ModelData and ImageData
script adapters publish the resulting owning descriptions during setup.

## ModelData adapter

`fluids.volumeEmissionFromModel(model,meshIndex,scaleX,scaleY,scaleZ,spacing)`
returns a Result with an owning version-11 emission description. It borrows a
CPU-decoded `ModelData` during setup, copies the selected triangle mesh, and
retains no resource pointers. It does not read GPU buffers. Invalid mesh slots,
nontriangular faces, indices, geometry and voxelization budgets produce
structured diagnostics. Source positions are mesh-local: node transforms,
skinning and morph poses are not baked by this adapter. The caller supplies
scale and subsequently sets the emission origin/direction.

The fluids module now declares its model3d resource dependency in the module
manifest. The example's `6 Mesh` mode loads the included procedural ring OBJ,
voxelizes it, emits one burst and uses a near-overhead inspection camera.
The fixture has no third-party asset dependency. It is a static shape acceptance
case; it is not evidence for animated mesh emission or arbitrary production meshes.

## Sphere and cube shape distributions

`fluids.volumeEmissionFromSphere(radius,spacing,surface)` and
`fluids.volumeEmissionFromCube(sizeX,sizeY,sizeZ,spacing,surface)` reproduce Fluid3D's
setup-time analytic shape distributions. Volume samples emit along local +Z.
Surface samples use radial sphere normals or normalized incident cube-face normals.
Cube dimensions are full sizes. Both builders reject more than 65536 lattice
candidates or 4096 output points before publishing a description. Their output can
be burst-emitted, rate-streamed, or used as a spacing-driven jet layer.

`fluids.volumeEmissionFromEdge(length,spacing,radialVelocityDegrees)` reproduces
Fluid3D's biased edge lattice. `length` is the full local X extent; consecutive
samples rotate their local emission direction around X by the requested angle.
`fluids.volumeEmissionFromDisk(radius,spacing,edgeEmission)` creates either
concentric filled rings emitting along local +Z or a circumference-only ring
whose particles emit radially. Both return an owning version-11 Distribution
description, reject invalid values before publication, and cap output at 4096
points. These setup calls retain no solver or resource reference.

## Multiple emitter shapes

`checked(fluids.composeVolumeEmitterShapes(emitter, shapes))` reproduces the
ordered distribution used by `Fluid3DEmitter.AddShape`, `RemoveShape` and
`UpdateEmitterDistribution`. `emitter` supplies the shared prototype, speed,
lifetime, filter, capacity, RNG and `useShapeColor` setting. Each item in `shapes`
must be an owning Distribution description returned by one of the image, model or
analytic builders. Its origin/direction transforms local positions and emission
directions into solver space before the distributions are concatenated in array order.

Rebuild the combined value after adding, removing, reordering or moving shapes. An
empty shape contributes one point at its pose; an empty array contributes the emitter
pose, matching Fluid3D's no-shape fallback. Composition accepts at most 64 shapes and 4096
total points. It validates into a new owning description before publication, retains
no script objects and performs no solver, per-frame allocation, rendering or GPU work.
The returned description uses the existing burst/rate/jet paths unchanged.

## Controller snapshots

Both emitter classes expose `snapshot()` and `restore(state)` through the common
Result protocol. State schema is `eve.volume-fluid-emitter-state`, version 1.
`kind` is 0 for the rate controller or 1 for the spacing-driven jet; cross-kind
restore fails. The owning state stores fractional `phase`, RNG `sequence` and
pool-restart `emitting` status. Phase is decimal text with enough precision to
round-trip a double through a float32 VM; do not convert it to a script number.
Unknown fields/versions and invalid phase values are rejected before mutation.

Call `emitter.checkpoint(solver, description)` at a completed simulation-step
boundary to produce schema `eve.volume-fluid-emitter-checkpoint`, version 1. It
owns the solver snapshot, emission description and rate/jet controller state.
`restoreCheckpoint(solver, checkpoint)` validates every nested schema, the full
candidate solver, the emission description and controller kind before publishing
either destination. It returns the restored owning emission description because
the solver and controller cannot mutate a caller's script table. Invalid input
leaves both destinations unchanged. No solver/resource pointers or event queues
are serialized. A newly constructed controller still starts at zero phase unless
restored.

## Moving nozzle poses

The jet method `advanceMoving(solver,description,beginPose,endPose,dt,maxPerStep,
minimumPoolFraction,inheritVelocity)` accepts transient `{position:[x,y,z],
rotation:[x,y,z,w]}` unit-quaternion poses. These override description origin and
direction. The description's other fields are held fixed over the step. Call
once after the solver step. Each layer uses its actual emission-time interpolated
position and shortest-arc quaternion orientation. `inheritVelocity` in [0,1]
scales the sum of nozzle linear velocity and angular surface velocity at the
sample point. It defaults to zero in C++; scripts pass it explicitly.

Input poses are never retained or serialized. The caller supplies both endpoint
samples again after restoring a checkpoint, so no stale scene pointer can survive
object destruction. Invalid poses or out-of-bounds generated particles reject the
operation without advancing phase. Fractional particle age is advected at emission
velocity; fractional external-force integration remains outside this controller.
The `7 Moving` example supplies poses from injected simulation time and enables
full velocity inheritance. Its per-frame work limits match the stationary jet.

## Boundary density support

Container walls now contribute fixed lattice samples to the density constraint
and its position gradient. Queries are local to a particle's support radius:
interior particles skip the boundary path, and boundary queries reject rows
outside the spherical kernel before examining individual samples. These samples
are derived from container settings, not additional simulated particles. They
supply missing rest-volume support at the wall while the existing projection
still enforces geometric nonpenetration. Analytic obstacles follow Fluid3D's
particle-interaction density model and collision projection; they do not inject
native ghost density samples. The container-wall correction remains an explicit
native stability extension.

This fixes the previously failing density-layer inversion case without weakening
its assertion. The stable opposite ordering is tested separately. The
`8 Multiphase` example starts with red heavy fluid above blue light fluid.
The optimized 640-particle probe measured p50 19.082 ms, p95 20.290 ms and
maximum 20.560 ms after boundary-loop pruning (the first implementation was
25.307 ms p50). This is CPU-only evidence and still exceeds a 60 FPS whole-frame
budget. The later resident-presentation path removes the GPU round trip for
uniform display-only liquid; this paragraph measures the CPU solver only.

## Rotating collider samples

Solver snapshots encode `eve.volume-fluid` version 20. The Value codec migrates
version 1 collider poses, adds empty attachments for v1/v2, assigns the default
collision filter to v1/v2/v3 particles, and adds spherical radii plus identity
orientation to v1-v4 particles, then derives v5 attachment-local orientations
from the saved particle and collider poses. Unknown fields and unsupported
versions remain errors. Version 6 gains an empty explicit-simplex array, selecting implicit point topology. Version 7 collider samples gain the default packed filter, version 8 particles gain actor groups plus self-collision state, version 9 materials gain atmospheric pressure, version 10 materials gain smoothing 2, version 11 particles gain zero angular velocity with rolling contacts disabled, version 12 gains static/dynamic particle friction defaults of 0.2, version 14 gains an empty owned SDF-collider array, version 15 gains an empty owned height-field array, version 16 adds `isTrigger=false` to every collider kind, version 17 adds Fluid3D collision-material fields, version 18 adds the attachment orientation switch while migrating old anchors to `true`, version 19 adds dynamic pins, compliance and break thresholds, and version 20 adds particle stitches. Direct C++ restore takes canonical version 20. Collision boxes use local half extents with quaternion `rotation` in
XYZW order. Capsules use their local Y axis, `radius`, and `halfExtent.y` as half
the total height; rotation orients the axis. Sphere geometry is rotation-independent.
All three shapes use world-space `angularVelocity` at the contact point.

`fluids.volumeColliderDefaults()` returns a collider description. Update the
solver's owned samples using `solver.setColliders(array)` before stepping.
The operation validates the whole batch and caches box/capsule rotation matrices once;
no scene pointers survive the call. `solver.contacts()` returns a Result containing
last-step world `point`, `normal`, `impulse` and `colliderLabel`. The impulse is the
opposite impulse for the external body. Its torque about the body center is
`cross(point-center,impulse)`. The caller resolves its own generation-checked body
handle; the numeric label is only correlation data, not a retained runtime handle.

`fluids.volumeSdfColliderDefaults()` returns a small sphere SDF collider suitable
for schema construction and tests. `solver.setSdfColliders(array)` atomically owns
up to 16 fields and rejects more than four million samples or live
particle/collider candidates. Each collider has `position`, XYZW `rotation`,
uniform positive `scale`, linear/angular velocity, friction, packed collision
filter and an `inverted` flag. Negative SDF values are solid normally;
`inverted=true` keeps particles inside a closed field and requires the field
domain to contain the solver bounds.

`fluids.volumeSdfColliderFromModel(model,meshIndex,sx,sy,sz,resolution)` bakes a
closed model mesh during setup and returns an owning collider description.
Resolution is in `[8,128]`; invalid triangles and a bake above four million
voxel/triangle sign checks fail through Result. Do not call mesh baking from a
frame loop. Snapshot v20 persists the samples; v13 migrates an empty SDF array.
Press D in `examples/fluid3d` for a bounded 288-particle SDF-obstacle scene.
It uses the same `FluidSurfaceRenderer` as every liquid mode and keeps collision
sampling on the CPU solver path; no second surface reconstruction pass is added.
`solver.updateSdfColliderPoses(poses)` moves up to 16 existing SDF colliders by
stable label without decoding or copying their distance samples. The batch is
validated atomically, including transformed-domain coverage for inverted fields.
`fluids.volumeSdfPoseDefaults()` returns the strict transient table shape.

`fluids.volumeHeightFieldColliderDefaults()` returns a 2x2 flat regular terrain.
Set `resolution=[x,z]`, positive local `size`, and `heights` in X-fastest row-major
order; normalized samples in `[0,1]` are multiplied by `size.y`, matching Fluid3D's
terrain tracker and `BurstHeightField`. `solver.setHeightFieldColliders(array)`
owns at most 16 grids and four million samples. Each particle reads four samples
from its local cell and tests the same two-triangle split, so runtime work does not
scale with terrain resolution. Rotation, surface velocities, friction, reciprocal
filters, contact labels and granular rolling response use the existing collider
contract. Snapshot v20 owns the samples; v14 migrates an empty height-field array.

All analytic, SDF and height-field descriptions expose `isTrigger`. A trigger emits
the same filtered contact identity, point, normal and signed distance as a collider,
with zero impulse, while leaving particle position, velocity and material unchanged.
It performs no projection, friction, rolling response, thermal transfer or attachment.
`solidify && isTrigger` is rejected atomically because those behaviors conflict.

All three collider kinds also expose Fluid3D collision-material fields. `friction` is
the dynamic coefficient retained for compatibility; `staticFriction` and
`rollingFriction` are in `[0,1]`, `stickiness` is in `[0,1000]`, and
`stickDistance` is in `[0,10]` metres. `frictionCombine` and `stickinessCombine`
use 0 Average, 1 Minimum, 2 Multiply and 3 Maximum. The higher enum value on the
particle/collider pair selects the operation, matching Fluid3D's priority rule;
stick distance uses the larger value and `rollingContacts` is enabled when either
side requests it. Adhesion and friction run inside the existing contact loop and
reuse its bounded candidate traversal. They add no render pass, GPU dispatch or
per-step allocation. Snapshot v20 owns these fields and strictly migrates v16 to
zero adhesion, Average combining, collider static friction equal to its legacy
dynamic friction, and disabled collider rolling contacts.

`grabContactParticles(colliderLabel, position, rotation, distanceThreshold)` maps
the package `Fluid3DContactGrabber.Grab()` operation to explicit solver state. It captures
the latest contacts whose signed distance is below the threshold, records each
particle in the supplied grabber-local frame, clears its velocity and treats it as
zero inverse mass during later steps. Solid particles are already fixed and are not
captured. `updateGrabbedParticles(...)` applies the authoritative fixed-step pose;
`releaseGrabbedParticles(label)` restores normal dynamics and returns the released
count. One particle has one grabber owner, and conflicting capture fails atomically.

Grab state is transient. Particle death and dense-index compaction remap it, removing
the owning collider releases it, and `clear`, `restore` or actor teleport invalidates
it. The API retains no collider, solver or callback pointer. Its parallel arrays are
reserved to solver capacity; capture is bounded by current contacts plus one particle
scan, while pose update and release are allocation-free linear scans. It adds no
neighbor traversal, GPU operation or rendering work, and liquid presentation remains
on the existing `FluidSurfaceRenderer` path.

This is a sampled kinematic contact interface. The supplied pose is authoritative at
the end of the step. Linear and angular velocities back-extrapolate analytic and SDF
collider poses for every substep, so ordinary translation and rotation do not jump
directly to the final pose. This is discrete substep sampling rather than continuous
time-of-impact collision. Linear speed is limited to 100 m/s, angular speed to
1000 rad/s, and analytic, SDF and height-field batches each accept at most four million live
particle/collider candidates. Moving inverted SDFs must contain the solver bounds at
both ends of the step. Only their lightweight poses are cached; distance samples are
never copied during stepping.

## Rigid-body feedback bridge

`eve.VolumeFluidCoupling()` owns generation-checked PhysicsLink values and local
collider descriptions. `attach(body,localShape)` returns Result; labels must be
unique and at most 1024 links are accepted. The local shape center/rotation are
relative to the body; sampled velocities come from the authoritative rigid body.
`detach(label)` is an idempotent removal. The bridge never owns bodies, worlds or
the fluid solver and does not retain their raw pointers between calls.

`bridge.setImpulseLimits(maxLinearDelta,maxAngularDelta)` bounds the velocity change
that one completed fluid step may apply to each dynamic body. Defaults are 20 m/s
and 100 rad/s; accepted ranges are `[0.01,1000]` and `[0.01,10000]`. Linear impulses
use body mass, while angular impulses use the oriented rigid-body inertia tensor.
`bridge.lastClampedBodyCount()` reports how many bodies were scaled by the last
successful step. This makes extreme mass-ratio protection visible to diagnostics.

Call `bridge.step(world,fluid,dt,substeps)` to resolve every body link, update the
fluid's complete collider set, advance fluid once, and apply opposite contact
impulses to the linked bodies. Linear impulses and torque about body centers of
mass are aggregated before application. The Result value counts contact
contributions. Then advance the rigid world using its existing step API. Do not
also call `fluid.step` for the same tick. This bridge exclusively supplies the
solver's collider set, including static/kinematic bodies that should collide.

Stale or wrong-world links fail before either domain mutates. Solver admission
or numerical-step failures restore previous collider samples and fluid state.
Destroying the bridge leaves bodies/worlds alive. Destroying a body or world
invalidates its link; detach it or rebuild mappings after restore/hot reload.
Runtime handles are not serialized: resolve application persistent identities
when reconstructing the bridge. No script or world callbacks run during bridge
stepping, and concurrent world mutation is unsupported.

This is partitioned feedback, with rigid poses held fixed during the fluid step.
It is not an implicit joint rigid/fluid constraint solver. Attached solid particles
return their prescribed linear inertia, ellipsoid angular inertia, gravity load and neighboring fluid stress at the completed world-space
anchor, so off-center loads also create torque. The bridge aggregates these reactions
once alongside collider contacts and reuses its pose, lookup and impulse buffers after
warmup. A live extreme-mass-ratio probe with a 0.1 m, density-0.1 body and 16
attached particles produced exactly the configured per-step bounds: -0.5 m/s
linear change and -0.25 rad/s angular change, with one clamped body reported.
Swept collisions remain unverified. The bounded FluidMill scene has separate
sustained runtime evidence below; arbitrary mass ratios remain outside that
acceptance.
# Performance display

For contact heating/cooling, create rules with `checked(fluids.volumeThermalRuleDefaults())`
and call `solver.stepWithThermalContacts(dt, substeps, rules)` instead of `step`.
A rule contains colliderLabel, rate, minimumViscosity/maximumViscosity and
minimumCohesion/maximumCohesion. Negative rate reduces user channels x/y (heating),
positive rate increases them (cooling). This is material-channel transfer, not
a physical temperature/energy model. Each touched particle/collider pair receives
`rate * dt` once per outer step, clamped to the rule bounds. Multiple collider
rules apply in ascending label order. Call applyMaterialChannels afterward to
affect the next simulation step.

Rules are transient, simulation-thread inputs and retain no object references.
At most 1024 unique labels are allowed and each must name a current collider.
Rate is in [-1000000,1000000]; viscosity bounds are within [0,100] and cohesion
bounds within [0,10], with minimum <= maximum. Invalid rules fail before stepping.
No rules means no contact-index capture. Active rules require O(contacts) temporary
storage and sorting for deduplication. Expired particles are removed after transfer.
The rigid coupling bridge exposes the same rules through
`coupling.stepWithThermalContacts(world, solver, dt, substeps, rules)` and applies
the resulting contact/attachment reactions to linked rigid bodies once.
`dofile("thermal-check.nut"); verifyVolumeFluidThermalContact();` runs a one-step check.

`solver.applyMaterialChannels()` maps `particle.data[0]` to viscosity and
`particle.data[1]` to cohesion for the entire pool. Initialize those channels in
emission prototypes, enable diffusion, then call this after stepping when you
want transported data to change material behavior. Every x must be in [0,100]
and y in [0,10]; otherwise the Result fails before any material changes.
The operation is explicit, O(particles), simulation-thread-only and retains no
references or callbacks. Channels z/w, positions and phase remain unchanged.
It does not install an automatic per-frame binding or initialize channels for you.
The cohesion parameter is the native solver's force model, not a calibrated
one-to-one physical surface-tension unit conversion from Fluid3D.

Press `0` in the example for opposing red/blue liquids with color diffusion.
Red starts at viscosity 2 and blue at 50. Their viscosity channels diffuse and
are applied to the solver after each step, so the mixture changes material too.
The example starts paused; Space toggles simulation and R resets the current scene.
For a bounded numeric check, evaluate `dofile("mixing-check.nut"); verifyVolumeFluidMixing();`.
It resets to the mixing scene, performs 120 fixed steps without rendering in the
loop, verifies particle count and color transfer, then leaves the result paused.

`solver.overlapBox(x, y, z, hx, hy, hz, qx, qy, qz, qw)` returns owning particle
descriptions whose centers lie inside the oriented box. Half extents are in
[0,10000] meters; the XYZW quaternion must be unit length (squared-norm tolerance
0.001). Identity rotation is `(0,0,0,1)`. Zero half extents are allowed, and boundary
centers are included subject to floating-point precision. Result/ownership/thread
semantics match overlapSphere. The inverse rotation is computed once per query;
the query scans live particles and preserves pool order. No collision state is changed.
These box/sphere helpers select particle centers for simple material brushes. Use
`queryBatch` and `raycast` for geometric surface distances, contact offsets,
category masks, oriented ellipsoid intersections and batched query correlation.

`solver.overlapSphere(x, y, z, radius)` returns the usual Result table whose value
is an owning array of particle descriptions. It includes centers on the sphere
boundary, scans the live particle pool once and does not return stable particle IDs.
Editing the returned array does not edit the solver. Avoid polling large result
sets every frame; query cost is O(live particles), with allocations for the hits.

`solver.paintSphere(x, y, z, radius, material)` atomically replaces the material
of particles whose centers lie inside that sphere. Obtain a complete material
table from a query or emission prototype. Missing/unknown fields, invalid values
and negative radii fail without mutation. Solid phase stops velocity; changing
back to Liquid permits motion on subsequent steps. Empty regions succeed without
changes. Both calls run on the simulation thread, retain no argument references
and invoke no callbacks. Radius is in [0,10000] meters.

`dofile("query-material-check.nut"); verifyVolumeFluidQueryMaterial();` runs the
bounded owning-copy, brush-range, freeze/melt and invalid-input checks in the example.
It resets the example and leaves it paused.

The `9` key selects the hinged waterwheel. For a bounded physics check, evaluate
`dofile("wheel-check.nut"); verifyVolumeFluidWheel();` in the running example.
It replaces the current scene, checks dry and opposite-offset jets, and leaves
the last result paused. It does not render inside the simulation loops.

The interactive wheel uses a 90-particle/second emitter capped at four admissions
per fixed step and 384 total particles. Its four visible paddles map to two rigid
crossbars and two analytic fluid colliders; the segmented rim, axle, supports and
tail race are draw-only geometry. Repeated real-engine runs measured about 4 ms
for simulation, 0-1 ms for the existing fluids SSF reconstruction and 0-1 ms for
the in-place texture upload at 137 live particles on the validation machine.

The Fluid3D example shows the last simulation, surface reconstruction and
texture-upload work in milliseconds. Values remain visible while paused and reset
when switching scenes. Simulation includes emission and rigid coupling; surface
includes copying reconstructed pixels into ImageData. The display uses Squirrel's
host runtime clock and is a coarse Windows diagnostic, not GPU timing or a portable
wall-clock benchmark. Compare the same scene, particle count and build configuration.
# Interactive overload guard

The Fluid3D example starts paused and returns to paused when resetting or
selecting a scene. It stops subsequent fluid work after one measured work sample
exceeds 250 ms, or three consecutive nonempty samples exceed 50 ms. Each sample
includes that update's simulation, surface reconstruction and texture upload.
An inexpensive nonempty sample resets the consecutive counter. The HUD and log
report the stop; reset before resuming. These conservative example limits do not
alter solver APIs or prove real-time performance. They cannot interrupt an
in-flight GPU command, detect every presentation stall, or recover device loss.

## Thermal contacts with rigid coupling

`VolumeFluidCoupling.stepWithThermalContacts(world, fluid, dt, substeps, rules)`
uses the same rule array as the solver method. Collider labels refer to attached
local shapes after their current body poses are sampled. Heat transfer and rigid
reaction impulses come from one fluid step; do not additionally call `fluid.step`.
World integration remains caller-owned. Rules are transient borrowed inputs;
rebuild them after restore along with the coupling links. Invalid rules or stale
links fail before either domain changes. Material channel application remains
explicit with `fluid.applyMaterialChannels()`.

C++ callers pass the optional rules span to `VolumeFluidCoupling::step` or
`VolumeFluid::stepWithColliders`. Empty rules retain the ordinary stepping path.
The opt-in `thermal-check.nut` function `verifyVolumeFluidThermalCoupling()` checks
single-step lifetime, heat transfer, rigid feedback and rejected-call atomicity
without rendering inside its check.

## Viscosity-driven color

`fluid.applyMaterialChannelsWithColors(keys)` accepts an array of
`{viscosity = 5.0, color = [1.0, 0.8, 0.1, 1.0]}` keys. Use 2 to 32 keys with
strictly increasing viscosity coordinates in [0,100] and linear RGBA components
in [0,1]. Colors interpolate linearly between keys and clamp to the endpoints.
An empty array preserves colors, as does `applyMaterialChannels()`.

The operation first colors each particle from its existing material viscosity,
then writes data.x/y to viscosity/cohesion. This deliberately follows the local
reference `ColorFromViscosity` script's order: newly transferred channels affect
color on the next call. A bad key or any invalid particle channel rejects the
whole operation without changing colors or material. Data and particle lifetime
are unchanged. C++ callers pass a borrowed `VolumeFluidViscosityColorKey` span to
`applyMaterialChannels`; no keys or callbacks are retained. The transient mapping
has no saved schema; resulting particle colors/materials already live in the
existing snapshot. This is a linear RGBA ramp, not Unity Gradient's full editor
or alternate interpolation-mode serialization.

Press T in the example for 96 particles over separate hot/cold contact regions.
The scene starts paused. `verifyVolumeFluidThermalColors()` in `thermal-check.nut`
advances a bounded 120 steps and verifies material/color divergence, with no
rendering inside its loop. The presentation is a material behavior check;
continuous liquid surfaces and visible scene-depth-composited heater geometry
remain unfinished.

## Spherical surface reconstruction

The SSF renderer now splats spherical-cap depth and chord thickness instead of
flat discs. Normals sample neighboring smoothed depths in view space, with image
Y converted to view-space up. CPU depth output also follows the final smoothing
buffer for odd iteration counts. Resolution, smoothing pass count and buffer
sizes are unchanged. This remains a projected-disc approximation; anisotropy,
exact perspective sphere intersections, scene-depth composition and continuous
surface reconstruction across sparse particles are not complete.

Normals choose the valid adjacent sample with the smaller depth change on each
axis to avoid joining front and rear surfaces at occlusion boundaries. Nearly
equal differences are averaged within depth-quantization tolerance. This uses
the same four samples as central differences; it does not merge disconnected
particle silhouettes into a continuous liquid surface.

## Particle attachments (snapshot v20, introduced in v3)

`checked(solver.bindStaticParticles(indices, colliderLabel,
constrainOrientation))` explicitly binds a nonempty group of unique current
particle indices to an existing analytic collider. Positions are captured in the
collider's local frame. The default `constrainOrientation=false` matches Fluid3D's
static attachment default: the target fixes position while particle orientation
remains independent. Passing `true` captures and drives local orientation too.
The call returns the number installed. It does not change phase, material or actor
group, so attached liquid remains liquid and continues to provide neighbor density.

`checked(solver.bindDynamicParticles(indices, colliderLabel, compliance,
breakThreshold, constrainOrientation))` installs mass-retaining Fluid3D-style pins.
Zero compliance follows the target rigidly; positive compliance permits bounded
lag. A pin detaches after its estimated constraint force exceeds the positive
break threshold. Dynamic pins stay in the liquid solve and report their opposite
constraint impulse through the C++ `attachmentReactions()` view. Work is linear
in the number of active pins and adds no GPU dispatch, particle upload or render
pass.

`checked(solver.unbindStaticParticles(indices))` removes matching static or
dynamic anchors and
returns the number removed. Particle lifetime compaction remaps surviving anchors;
removing the target collider detaches them; `clear` removes all of them. Duplicate,
stale or already-attached bind indices and a missing target fail before mutation.
Snapshot v20 persists attachment kind, compliance and break threshold. Version 18
migrates to static, zero-compliance, effectively unbreakable anchors; v17 migration
selects constrained orientation to preserve older native behavior. These calls are simulation-thread
operations and retain no collider or script pointer.

A collider with `solidify = true` now freezes contacts into solid particles with
owned `{particleIndex, colliderLabel, localPosition}` anchors. Subsequent samples
of that label move the anchor at the next fluid step. Linear interpolation over
substeps supplies kinematic velocity for particle interactions. Targets outside
the solver bounds or displacement speeds above 100 m/s reject the step before
mutation. A new solidification outside bounds rolls the entire step back.

Removing a label detaches its solids at their current world positions. Reusing
the label later does not reconnect them. Setting solidify=false only stops new
attachments; paint particles to a non-solid phase to melt/detach existing ones.
Lifetime compaction remaps attachment indices, and clear removes all anchors.
The solver never stores body/world pointers. With rigid coupling, existing typed
physics links remain the authority for resolving collider poses.

Snapshot schema `eve.volume-fluid` v7 includes an attachments array. The Value
codec migrates older snapshots one version at a time, keeping historical solids
world-fixed; v1 also receives the existing collider-pose migration. Direct C++
restore accepts v7 only.
Unknown fields, duplicate/out-of-range indices, non-solid targets and missing
collider references fail atomically. Local points and labels are saved values;
rebuild rigid bridge links from application identity after restoring.

Press S for a paused 16-particle attached-solid example. The opt-in
`attachment-check.nut` verifies 61 bounded steps and snapshot restore without
rendering in the loop. Attached-particle reaction forces on rigid bodies and complete
SolidifyOnContact scene parity remain pending. Explicit solid coloring is described below.
Contact propagation is described below.

## Contact-solid propagation

At the end of each outer step, solids attached to colliders whose solidify flag
is enabled can freeze movable neighbors within 1.001 times particle spacing.
The existing final-substep spatial grid supplies candidate neighbors. All seeds
are captured before applying the wave, so newly propagated solids cannot cascade
again until the next call. Direct collider contacts created in this step can seed
that wave. This makes propagation layering independent of solver iteration and
substep counts for unchanged geometry. Actual dynamics still depend on timestep.

A particle touching multiple sources joins the smallest collider label. The new
anchor stores its own local point and follows the same removal, restore, lifetime
and melting rules. Unattached world-fixed solids do not propagate. Turning the
collider's solidify flag off disables both direct freezing and propagation;
melting next to an enabled source may freeze the particle again on the next step.
Propagation does not automatically apply a color; use applySolidColor after the
step. On the following step, propagated anchors contribute inertia, gravity and
neighbor stress reactions through the rigid bridge like directly frozen anchors.

`verifyVolumeFluidPropagation()` in `attachment-check.nut` checks actual collider
seeding, three bounded waves and subsequent rigid-pose following without renders.

## Solid color

`fluid.applySolidColor(r, g, b, a)` assigns a finite linear RGBA color in [0,1] to
all current solid particles, including world-fixed and attached solids. Call it
after stepping to color direct-contact and propagated solids. It returns the
normal Result protocol; invalid components leave all colors unchanged. Other
phases, positions, velocities, material channels and lifetime are untouched.

This is an explicit bulk style operation, not a retained callback or per-particle
script loop. It updates all current solids rather than only newly frozen ones.
Melting preserves the last color until another coloring operation changes it.
If also using viscosity gradients, call solid coloring afterwards when solid
color should take precedence. The input color is not retained; resulting colors
serialize in snapshot v20.
The S example colors its frozen particles gold through this operation and renders
through the existing fluids surface reconstructor.

`fluid.applyVelocityColors(sensibility)` reproduces Fluid3D's ColorFromVelocity
mapping: each signed velocity component reaches its RGB extreme at the supplied
positive sensitivity. `fluid.applyActorGroupColors()` reproduces ColorFromPhase
using Fluid3D's 26-entry color alphabet and `actorGroup % 26`. Both are explicit,
allocation-free O(particle count) operations. They do not install a per-frame
callback, so callers choose when the presentation-only scan is worthwhile.

`fluid.applyDataColors(channel, gradient)` covers Fluid3DPropertyColorizer for
any `data[0..3]` component. `fluid.applyRandomColors(gradient, seed)` covers
ColorRandomizer with an injected deterministic stream instead of Unity's global
random state. A gradient contains 2..32 strictly increasing `{coordinate,color}`
keys in `[0,1]`; values outside the key interval clamp to its endpoints. Both
operations validate the complete gradient before changing a color and retain no
configuration between calls.

## Field sampling for diffuse particles

`fluid.sampleField([[x,y,z], ...])` returns an owning Result array in query order.
Each item has velocity, vorticity, density and neighborCount. Queries sample the
current Liquid/Gas particles within twice the rest spacing; Solid and Granular
particles do not contribute. Unsupported space returns zero fields.

Velocity is normalized poly6 interpolation; vorticity is the curl of that
normalized interpolant, evaluated analytically with translation-relative
velocities to avoid cancellation from bulk flow. Density is the SPH mass sum in
kg/m3 without container boundary support. These values are not claimed to be
numerically calibrated to Fluid3D's thresholds or to the solver's spiky-gradient
vorticity-confinement estimate.

At most 65536 finite positions are accepted. A batch exceeding 4000000 candidate
particle visits fails without returning partial samples or advancing state.
The call rebuilds derived spatial scratch to include emission, deletion and
restore. It retains no input references and performs no GPU work. Use bounded
batches for diffuse advection; do not make one call per particle. The transient
query/output does not change the solver snapshot. The secondary pool and automatic
generator are described below; foam rendering is still pending.

## Secondary particle pool

`eve.VolumeFluidDiffuse()` owns an empty pool with default capacity 2048.
`emit([{position=[x,y,z], velocity=[vx,vy,vz], life=seconds}, ...])` admits the
whole batch or returns a failed Result without mutation. Velocity must be finite
with magnitude at most 100 m/s; lifetime must be in (0,86400] seconds.
Call `advance(fluid, dt, minimumNeighbors)` once after stepping the source fluid.
It expires particles first, samples current Liquid/Gas velocity in one batch,
removes particles below the neighbor threshold, and integrates surviving
positions with explicit Euler. dt is in (0,1/30], threshold in [0,1000000].
It neither steps the source fluid nor contributes pressure, collisions or rigid
feedback. Query-budget or invalid-output failure preserves the entire pool.

`snapshot()` returns an owning Result value with schema `eve.volume-fluid-diffuse`,
version 1, capacity and particles. `restore(state)` validates every field before
replacement; capacity is in [1,65536], unknown fields/versions are rejected, and
there are no prior versions to migrate. `getParticleCount()` and
`getAvailableCapacity()` return integers directly. `clear()` keeps capacity.
Only particle state/capacity persist; no source fluid pointer or identity is
retained. A restored pool can sample any caller-supplied fluid. All calls are
simulation-thread affine, invoke no callbacks and require no concurrent calls.
Fixed input/dt repeats on the same build; cross-platform comparison uses float
tolerance. No RNG or automatic emission is part of this pool.

Advance retains bounded survivor, query-position and field-sample scratch at their
peak capacity; stable-size frames allocate none of these working arrays. It shares the
sampler's 4-million candidate-visit limit. Empty and completely expired pools skip sampling.
One Release CPU measurement with 640 source particles averaged 0.384 ms for 640
secondary particles and 2.542 ms for 4096, over 100 calls. This includes sampling
and allocations, excludes script projection and rendering, and does not establish
dense-pool or total frame performance. The opt-in `diffuse-check.nut` verifies
script admission, advection, lifetime recycling and strict restoration. Automatic
vorticity/density-based foam emission is described below; its visual renderer
remains pending.

## Automatic foam generation

`eve.VolumeFluidFoam()` owns emission settings, fractional credit and an isolated
RNG stream. Call `foam.advance(fluid, pool, dt)` after stepping the fluid and
advecting the existing pool. It returns a Result with the admitted count. A source
site qualifies when its sampled curl magnitude is strictly above
`vorticityThreshold` and its density is strictly below `densityThreshold`.
`advance` draws eligible sites from all current Liquid/Gas particles of the
supplied solver. `advanceFromActor(fluid, pool, dt, actorGroup)` matches the
package component's owning-emitter behavior by accepting only particles with
that explicit actor group, corresponding to `Fluid3DEmitter.solverIndices`. The
comparison is fused into the existing source collection pass, adds no traversal
or allocation, and the transient group is not retained in the controller
snapshot. Both paths use current simulation positions; render-interpolated
source positions are available through
`advanceFromActorInterpolated(fluid,pool,dt,actorGroup,alpha)`. Alpha zero uses
the previous successful fixed-step endpoint and one uses the current endpoint,
matching the package's `solver.renderablePositions` timing. The solver keeps one
capacity-reserved transient position array: successful steps replace it, failed
steps preserve it, particle removal compacts it, and teleport resets both
endpoints to prevent trails. It is omitted from snapshots; restore initializes
both endpoints to restored positions.

The generator distributes bounded emissions across qualifying sites with an
integer stride and a random starting offset, adding a uniform spherical position
offset of radius `randomness`. New velocity comes from the sampled fluid and
`lifetime` supplies the initial life. Unlike the reference's stride loop, it never
exceeds whole-particle rate credit or `maxPerStep`. Excess whole credit when the
pool is full, the candidate set is small or the cap is reached is dropped; only
fractional credit carries forward. Empty eligibility consumes no random draws.
Source-position, field-sample and emission-batch scratch retain peak capacity;
stable-size productive updates allocate none of these three working arrays.
Zero budget/full pools skip particle copying and field queries. At most 65536
source particles and 4 million query candidate visits are accepted; failure
preserves the pool, credit and RNG together. No solver stepping or GPU work occurs.

Use `checked(foam.snapshot())` to get schema `eve.volume-fluid-foam`, version 1,
settings, decimal-text `credit` in [0,1), and nonzero uint32 `randomState`.
Modify and pass this owning value to `foam.restore(state)`; every field is
validated before replacement and unknown fields/versions fail. There is no older
schema to migrate. To reproduce a checkpoint, restore the source, diffuse pool
and generator at the same step boundary; no source/pool pointer is retained.
Restore `randomState` to inject the named foam RNG seed. All calls are
simulation-thread affine with no callbacks/concurrent calls. Identical input
ordering repeats on one build; cross-platform float results require tolerance.

Settings defaults are rate 500/s, randomness 0.01 m, vorticity threshold 10/s,
density threshold 2000 kg/m3, lifetime 2 s and maxPerStep 256. Valid ranges are
rate [0,1000000], randomness [0,10], vorticityThreshold [0,1000000],
densityThreshold [0,1000000000], lifetime (0,86400], maxPerStep [1,4096]. dt is
[0,1/30]. Native field definitions still require scene calibration to Fluid3D values.
The opt-in `foam-check.nut` verifies generation, filtering, replay and advection
composition in the real scripting VM. A 640-source Release measurement averaged
0.421454 ms for generation over 100 calls, emitting 1999 particles in total;
it excludes pool advection, script projection and rendering. This is not a full
FluidFoam scene or total-frame performance result.

## Surface reconstruction

`surface.renderVolume(fluid)` reuses the `fluids` screen-space surface renderer.
`surface.renderVolumeInterpolated(fluid, alpha)` reconstructs the same surface
from fixed-step interpolated positions. Alpha is in `[0,1]`; invalid input fails
before replacing the prior frame. Position/color copying is fused into one
allocation-reusing particle pass, including the anisotropic shape path. The SSF
dispatch, smoothing, normal, shading and readback counts are unchanged.
Use `surface.configureProjection(true, verticalHalfSize)` for an orthographic
camera. Spherical, anisotropic, multicolor-liquid and gas splats then keep the
same screen size across depth while retaining linear view depth for occlusion.
Passing `false` restores perspective projection; invalid half sizes preserve the
previous configuration. The switch reuses renderer buffers and does not add a
GPU allocation, descriptor, or dispatch.
For display-only frames, `surface.renderVolumeColorOnly(fluid)` keeps uniform and
multicolor shading on the GPU and reads back only RGBA. Its auxiliary depth, normal, and
thickness arrays remain unchanged, so use `renderVolume` before CPU diffuse-foam
composition or auxiliary-buffer inspection. Explicit particle blends automatically
fall back to the complete readback path.
When the result goes directly to an existing display texture, prefer
`checked(surface.renderVolumeColorToTexture(fluid, gfx, texture))`. The common
uniform or multicolor Vulkan path keeps RGBA in the SSF storage buffer and performs a
device-local copy into the matching single-mip texture, avoiding both host readback
and upload. Custom lighting, smoothness, metalness, ambient, reflection, opacity,
reflection-color and thickness-cutoff settings stay on this path: the shade dispatch
reuses push-constant slots that are no longer needed for its camera matrix. Explicit
per-particle blend configuration remains on the host-visible path because overlap
changes the color spatially. Multicolor tint uses a bounded fixed-point weighted
accumulator. Its upload buffer, accumulator and two shaders are created transactionally
on the first multicolor frame; uniform renderers retain only a valid 20-byte placeholder
for the optional shade binding. With anisotropy enabled,
the established ellipsoid reconstruction supplies depth while the established spherical
particle-color projection remains device-local. CPU, WebGPU and other unsupported cases
use the existing host upload. Use the two-step `renderVolume` plus
`checked(surface.copyToTexture(gfx, texture))` when foam, refraction or another CPU
color operation must run between reconstruction and presentation.
Renderer dimensions are clamped to 8–1024 pixels per axis so accidental settings
cannot create unbounded CPU scratch storage, Vulkan buffers or synchronous readback.
The CPU fallback reuses its bilateral-filter buffers between frames.
Call `checked(surface.prepare())` during initialization so one-time Vulkan shader
and buffer creation finishes before frame timing starts. `surface.usingGpu()` then
reports whether the compute path is active or the renderer retained its CPU fallback.
Preparation is render-thread affine, changes no simulation/output state and is
idempotent.

For smoke and other gas materials, call
`checked(surface.renderGasVolume(fluid, absorption))`. It selects only Gas
particles and renders blurred screen-space thickness with Beer-Lambert opacity,
using the same volume kernel as Fluid3D's surface-disabled renderer. To reproduce
the renderer-level `generateSurface = false` setting for Liquid, Gas, Granular,
and Solid particles together, call
`checked(surface.renderVolumeWithoutSurface(fluid, absorption))`. `absorption` is finite and
within [0.01,30]. This path intentionally submits no GPU work: it avoids the
particle-atomic-write/readback pattern that can stall or reset a graphics driver,
uses a fixed 4x downsample and two separable seven-tap blur passes. Both calls
reuse their frame scratch after warmup.

The gas frame accepts at most 65536 particles and four million projected
bounding-box pixel visits. It preflights both limits and returns a structured
failure without modifying the previous frame. Using Fluid3D's supported
`thicknessDownsample = 4` performance setting, work after splatting is two fixed
7-tap separable blur passes over a quarter-resolution image, followed by one bounded expansion. It reuses owned
scratch buffers after warmup, leaves normals zero, and exposes nearest gas depth
and blurred thickness for inspection. Liquid still uses `renderVolume` or
`renderVolumeColorOnly` and the existing SSF surface reconstruction.

Native presentation adapters can call
`surface.occludeWithSceneDepth(sceneDepth, depthBias)` after full liquid/gas
reconstruction and optional diffuse compositing. The borrowed array contains one
finite, nonnegative linear view depth per output pixel; a large finite value means
no scene geometry. The call validates the complete input before mutation, then
clears hidden RGBA pixels in one allocation-free linear pass. It preserves fluid
depth, normals and thickness. `depthBias` is a view-space tolerance in `[0,1]` m.
The uniform-color GPU-only path rejects this operation because it deliberately did
not read depth back; a future graphics-owned composition path must consume GPU
depth directly instead of forcing that readback back into the fast path.

For transparent liquid over an existing color frame, call
`checked(surface.compositeSceneRefraction(sceneRgba8, distortionPixels, absorption))`
after a full `render` or `renderVolume` and before `copyToImage`. The scene image must
match the renderer dimensions and use RGBA8. Normals offset the sampled scene by up
to 64 pixels, thickness controls both bend strength and Beer-Lambert transmission,
and reconstructed alpha supplies coverage. The call borrows the scene only during
one allocation-free pixel pass, preserves depth/normals/thickness, and submits no
GPU work. Distortion must be in `[0,64]` and absorption in `[0,30]`; invalid input or
the auxiliary-free color-only GPU path fails before changing the prior frame.

For the package renderer contract, configure the four independent Fluid3D controls with
`checked(surface.configureRefraction(transparency, absorption, coefficient,
downsample))`, then call
`checked(surface.compositeConfiguredSceneRefraction(sceneRgba8))`. Transparency is
in `[0,1]`, absorption in `[0,30]`, the signed normalized-screen coefficient in
`[-0.1,0.1]`, and target downsampling in `[1,4]`. The background target is bilinearly
reduced and sampled, while absorption follows Fluid3D's per-channel
`exp(-absorption * (1-liquidColor) * thickness)` response. Scratch capacity is reused,
the input is not retained, and invalid settings or dimensions preserve the frame.

Use `checked(surface.configureRefractionEnabled(enabled))` for the package's
independent `generateRefraction` switch. When disabled,
`compositeConfiguredSceneRefraction` succeeds immediately without validating,
copying or sampling the scene and leaves the current liquid frame unchanged.
Re-enabling restores the transparency, absorption, signed coefficient and target
divisor last supplied to `configureRefraction`. Toggling allocates no memory and
submits no GPU work.

`checked(surface.configureReflection(enabled))` maps the package's independent
`generateReflection` switch. Disabling it removes the Fresnel reflection contribution
without overwriting the coefficient supplied to `configureMaterial`; enabling it later
restores that coefficient. The call allocates no resources and only affects subsequent
frames. The default enabled state retains the resident Vulkan presentation fast path.

Press W in `examples/fluid3d` for the WhiskeyBottle checkpoint. The open-neck
glass shell is baked into one 24-cubed model SDF at rebuild time, while holding D
tilts it using `updateSdfColliderPoses` only. The scene uses 24 particles with a
capacity of 48 and reuses the established SSF reconstruction. Its sparse background
grid makes refraction visible without building or uploading a new scene image per frame.

Press K for the bounded FluidKarmanVortex checkpoint. It sends 180 particles through a
labelled spherical obstacle and periodically samples a fixed downstream lattice through
`sampleField`. The HUD reports peak curl and contact count. The opt-in
`karman-check.nut` acceptance requires both positive and negative downstream curl,
collider contacts, and a downstream magnitude above the upstream sample. Vorticity
confinement is limited to 0.2 so the visual wake remains stable without accumulating
unnecessary kinetic energy.

## Surface and material controls

The existing SSF renderer exposes package-level material controls through
configureSurface, configureMaterial and configureColors. The first call controls
thickness scale/cutoff and bilateral depth falloff/pass count. The second controls
lighting, smoothness, metalness, ambient contribution, Fresnel reflection and opacity.
The third sets linear base and reflection RGB values. Every call validates the full
batch before mutation.

Default settings retain the uniform-color GPU fast path. A nondefault material needs
host-visible normals and thickness, so it disables that fast path and applies one
bounded linear CPU shading pass after the existing GPU readback. It allocates no frame
scratch and submits no extra GPU commands. Returning every control to its default
automatically restores the fast path. Mode W demonstrates customized amber lighting
and reflection.

Call `checked(surface.configureAnisotropy(true))` to project each volume particle as its
oriented ellipsoid instead of the default sphere. The implementation copies radii
and XYZW orientations in the same renderer handoff, precomputes a 2D covariance and
conditional depth section once per particle, then replaces the normal spherical
splat dispatch with an elliptical dispatch. The shader keeps the existing 128-byte
push-constant layout. Its three-vec4 particle upload is sent only on enabled frames;
the GPU resources are created on first use and retained for reuse. Disabled frames
retain the original upload and shader.

Granular mode in `examples/fluid3d` enables this path so seeded per-particle
radius variation is visible and matches the radii used by contacts. It also selects
`surfaceDownsample=2`: the anisotropic upload replaces the spherical upload/dispatch,
while the reduced target cuts reconstruction pixels to one quarter. A measured
640-particle Debug Vulkan frame reported about 2 ms surface work and 0 ms upload.

`checked(surface.configureSurfaceDownsample(factor))` accepts factors 1 through 4,
matching Fluid3D's surface target range. Factors above one run particle splatting,
bilateral smoothing, normal reconstruction and shading in an owned
`ceil(width/factor)` by `ceil(height/factor)` SSF target. One bounded host pass then
expands color, depth, thickness and normals into the renderer's unchanged public
dimensions. Configure it before `prepare()` so only the reduced GPU target is created.
Factor 1 retains the original path. This controls the complete surface target;
use the independent thickness control below when the two resolutions differ.

`checked(surface.configureSurfaceEnabled(enabled))` maps Fluid3D's `generateSurface`
switch. Call `checked(surface.renderConfiguredVolume(fluid))` to apply it: enabled
uses the existing SSF reconstruction, while disabled uses the existing bounded
all-phase thickness-volume path and skips surface smoothing, normal reconstruction,
lighting, reflection, refraction and GPU work. Toggling changes no current frame and
allocates nothing. The disabled render can still return its explicit particle or
covered-pixel budget error without replacing the previous output.

For an atomic package-level setup, start with
`local settings = checked(fluids.fluidRendererSettingsDefaults())`, edit its fields,
then call `checked(surface.configureRendererSettings(settings))`. The owning value
contains all 23 fields from Fluid3D 6.4 `FluidRendererSettings`, including both blend
pairs, depth write, all three downsample factors, surface/lighting/reflection/
refraction/foam switches and their scalar controls. `checked(surface.rendererSettings())`
returns a detached snapshot. The complete value is decoded and validated before any
renderer state changes, then committed with one reduced-target reset; an invalid or
incomplete table preserves the prior configuration. Applying it allocates no render
resource and submits no GPU command. Fluid3D-only settings do not replace native controls
such as anisotropy, projection, colors or iteration count.

`configureSurface(thicknessScale, thicknessCutoff, depthFalloff, iterations)`
uses Fluid3D's cutoff units: a pixel is discarded when reconstructed world thickness
times 10 is below `thicknessCutoff`. The accepted cutoff range is `[0,5]`, matching
`FluidRenderingUtils.ThicknessClip`; zero disables clipping. This comparison is
performed inside the existing normal/shading loops and adds no pass or allocation.

`checked(surface.configureSurfaceBlurRadius(radius))` maps Fluid3D's world-space
`blurRadius`; the package default is 0.02 m and the accepted range is `[0,0.1]` m.
Each existing bilateral pass converts the radius to screen pixels from the current
linear depth (or orthographic scale), capped at four pixels. Radius zero performs no
neighbor reads. This changes the current kernel footprint without adding a pass,
buffer, descriptor or GPU submission. Renderers that never call the method retain
the established fixed 5x5 native kernel for compatibility.

`checked(surface.configureSurfaceBlend(source, destination))` accepts Unity's
integer `BlendMode` values exactly: Zero=0, One=1, DstColor=2, SrcColor=3,
OneMinusDstColor=4, SrcAlpha=5, OneMinusSrcColor=6, DstAlpha=7,
OneMinusDstAlpha=8, SrcAlphaSaturate=9 and OneMinusSrcAlpha=10. The package
default is `(5,10)`. A normal texture draw already implements that default at no
extra cost. For a custom pair, call
`checked(surface.compositeConfiguredSurfaceBlend(sceneImage))` after rendering;
the matching RGBA8 image is the destination. This performs one bounded CPU pixel
pass and supports every pair without creating a parameterized graphics pipeline.
Invalid settings or image dimensions preserve the current surface frame.

`checked(surface.configureParticleBlend(source, destination, depthWrite))` maps
`particleBlendSource`, `particleBlendDestination` and `particleZWrite`. It uses
the same 0-10 Unity factor values. Fluid3D's default `(2,0,false)` starts its RGB
target at white and multiplies every overlapping particle color; the pass keeps
alpha unchanged because the reference shader declares `ColorMask RGB`. With
depth writes enabled, the nearest particle-color fragment at a pixel replaces
farther contributions. The implementation reuses the existing tint/weight
scratch and adds no render pass or GPU resource. Explicit particle blending
disables the uniform-color shortcut because overlap is then observable; leave it
unconfigured when package color blending is not required. On this host, enabling
the package blend in the 640-particle Mixing scene raised Debug surface work to
about 24 ms, so the interactive sample leaves this opt-in control disabled.

`checked(surface.configureThicknessDownsample(factor))` independently selects the
liquid thickness target at factors 1 through 4. `renderVolume` reconstructs that
target, expands only its thickness channel, and then performs final material/color
shading, so absorption and alpha use the configured thickness resolution rather
than the surface target's incidental thickness. Surface and thickness factors may
be combined in either order. `renderVolumeColorOnly` retains its original direct
GPU fast path and intentionally ignores both target divisors because its contract
leaves host depth, normals and thickness unchanged.

## Foam surface compositing

Call `checked(surface.configureFoam(enabled, downsample))` to map Fluid3D's
`generateFoam` and `foamDownsample` settings. The divisor is an integer in `[1,4]`.
Disabled foam makes `compositeDiffuse` a constant-time no-op. At factor one the
existing full-resolution path is unchanged. Larger factors project and depth-test
particles in a reusable reduced coverage target, reducing candidate splat visits by
roughly the square of the divisor, then perform one linear nearest expansion while
compositing white foam over the liquid. Configuration allocates and submits no GPU work.

After `surface.renderVolume(fluid)`, call
`checked(surface.compositeDiffuse(pool, radius, opacity, fadeSeconds))` before
`surface.copyToImage(image)`. This overlays white soft particle splats onto the
existing RGBA output and reuses the reconstructed liquid depth for occlusion.
It leaves liquid depth, normals and thickness unchanged and submits no additional
GPU command or readback. Follow it with `occludeWithSceneDepth` when a matching
linear scene-depth buffer is available. Repeated
composite calls accumulate until the next `render` or `renderVolume` replaces the
frame.

The call is render-thread affine, borrows the pool for the call only, invokes no
callbacks, and advances no simulation state. Radius is a world-space value in
(0,1], opacity is [0,1], and `fadeSeconds` is in (0,86400]. Remaining lifetime
below `fadeSeconds` fades alpha linearly. Before changing output, it projects and
counts clipped particle bounding boxes; more than 4 million candidate pixel visits
fails atomically. Fully off-screen pools and zero opacity avoid pixel work. This
CPU composition reuses renderer-owned position/life and projected-splat buffers,
so capacity-stable frames perform no per-call heap allocation. It still scales
with projected area; keep the pool, radius and render frequency bounded.

The sample's `F` mode uses 192 source particles, a 512-particle diffuse capacity,
at most 8 new particles per simulation step, and the existing 30 Hz reconstruction
cap. Its native density range was measured at roughly 520-1010 kg/m3, so the sample
uses a 700 kg/m3 threshold to prefer its surface instead of copying the Fluid3D default
blindly. A real engine-owned frame showed visible foam on the outer surface with
liquid occlusion. With 168 live particles and radius 0.035 m, 100 repeated
composites measured 1.28-1.31 ms per call across five consecutive 100-call
batches in the current Debug build. This excludes SSF, image copying and texture
upload and is not a GPU/driver or full-frame guarantee.

## Bounded ray queries

`checked(fluid.raycast(ox,oy,oz, dx,dy,dz, maxDistance, maxHits, phaseMask))`
returns owning hit objects sorted by distance and then current pool index. Each hit
contains `particleIndex`, `distance`, world-space `point`, outward `normal`, and an
owning `particle` copy. Direction is normalized internally. Bits 0, 1, 2 and 3 of
`phaseMask` select Liquid, Gas, Granular and Solid respectively; use 15 for all.

The query intersects each selected particle as an oriented ellipsoid. Its ray is
inverse-rotated and divided by the principal radii before a unit-sphere test. An
origin inside a particle reports distance zero and a
stable opposite-ray normal when it lies at the center. `maxDistance` is [0,10000]
meters and `maxHits` is [1,4096]. The nearest hits are retained with a bounded
heap rather than allocating all matches. To keep an accidental large query from
stalling a frame, live pools above 65536 particles return a failed Result before
scanning; invalid directions, masks and limits also fail without simulation
mutation. Returned indices become stale after emission, removal, restore or step.

This is simulation-thread affine and invokes no callbacks or GPU work. A Release
CPU measurement over 100 calls averaged 0.006074 ms for 640 particles and
0.376790 ms for the 65536-particle limit while retaining 64 coincident hits.
This measures the earlier equal-radius query implementation only; the current
ellipsoid measurements are recorded in the parity notes.
`query-material-check.nut` exercises
the binding, sorting, phase selection, owning results and invalid inputs.

### Sphere surface-distance query

`checked(fluid.querySphere(x,y,z, radius, contactOffset, maxDistance,
maxHits, phaseMask))` follows the reference query-result convention more closely
than the center-only `overlapSphere`. Each owning result contains
`particleIndex`, signed `distance`, `queryPoint`, `normal` and a captured
`particle`. Signed distance is center separation minus query radius, contact
offset and the oriented particle radius along the result normal; negative values
mean overlapping surfaces.
The query point lies on the contact-offset-expanded query sphere. At coincident
centers, +Y is the deterministic normal.

Radius, contact offset and maximum positive separation are finite values in
[0,10000]. Overlaps always qualify; separated particles qualify up to
`maxDistance`. The phase mask and output/source bounds match `raycast`, and hits
are sorted by signed distance then index. A Release 100-call measurement retaining
64 coincident hits averaged 0.004318 ms at 640 particles and 0.178107 ms at the
65536-particle ceiling. The real script probe verifies signed overlap, contact
offset, owning results and invalid inputs. Oriented box signed-distance and batch
query descriptions remain pending.

### Oriented-box surface-distance query

`checked(fluid.queryBox(x,y,z, hx,hy,hz, qx,qy,qz,qw,
contactOffset,maxDistance,maxHits,phaseMask))` returns the same owning signed-hit
shape as `querySphere`. Half extents are local nonnegative values up to 10000 m;
rotation is a unit XYZW quaternion with squared-norm tolerance 0.001. For an
outside particle, the query point is the nearest clamped box point moved outward
by contact offset. For an inside particle, it lies on the nearest face; exact ties
choose X, then Y, then Z, making normals repeatable. The oriented ellipsoid radius
along that world normal is subtracted from signed distance.

The source/output limits, phase mask, sorting, ownership and failure semantics
match the sphere query. Release 100-call means retaining 64 coincident hits were
0.008243 ms at 640 particles and 0.494404 ms at 65536. The real script probe
verifies a rotated box, inside signed distance, contact offset, owning output and
invalid geometry.

### Mixed query batches

`checked(fluid.queryBatch(queries,maxHitsPerQuery))` accepts up to 256 strict
query objects and returns owning hits grouped by input `queryIndex`, then nearest
distance and particle index. Types 0, 1 and 2 mean sphere, box and ray. `center`
is the sphere/box center or ray start. `size` stores `[radius,0,0]`, the full box
size, or the ray segment endpoint. Every object also supplies `rotation` in XYZW,
`contactOffset`, `maxDistance`, `phaseMask` and `collisionFilter`; rotation affects boxes only, while
ray endpoint length defines the segment.

`checked(fluid.applyQueryColors(queries, colors, baseR, baseG, baseB, baseA,
maxHitsPerQuery))` performs the observable coloring step used by Fluid3D's overlap-query
sample. `colors` must contain one linear RGBA array per query. All particles first
receive the base color; negative-distance hits receive their query color, and a later
query wins when a particle overlaps several shapes. The returned owning integer array
contains overlap counts in query order. Query/color validation and the complete bounded
query finish before colors change, so failure leaves the solver untouched. The method
is simulation-thread affine, invokes no callbacks, retains no input and performs no GPU
work. Keep `maxHitsPerQuery` within 1..4096; excess overlaps outside that retained bound
are intentionally not colored.

`checked(fluid.applyQueryColorsPreservingOutside(queries, colors,
maxHitsPerQuery))` uses the same validation, bounds and later-query priority, but
changes only negative-distance hits. Colors outside all triggers remain untouched,
so contact colorizers such as FluidMaze can persist contamination after a particle
leaves a trigger. The full query succeeds before any color mutation.

`collisionFilter` follows Fluid3D's packed layout: the low 16 bits are the query
category and the high 16 bits are its mask. A particle is considered only when
its category is in the query mask and the query category is in the particle mask.
Zero categories or masks are rejected. Existing snapshots migrate particles to
category 1 with all categories in their mask (`0xffff0001`).

The same reciprocal test gates particle-particle simulation interactions,
including density constraints, separation, viscosity, cohesion, vorticity,
diffusion and contact-solid propagation. Filtering happens inside the existing
spatial-grid neighbor traversal before distance and kernel calculations, so
rejected pairs become cheaper and no second broadphase or pair list is allocated.

Particles also expose `actorGroup` (0 through `0x00ffffff`) and `selfCollide`.
These are the native representation of Fluid3D's phase-group bits and SelfCollide
flag; they are independent from `material.phase`. Particles in the same actor
group interact only when both have `selfCollide = true`, and same-group pairs do
not consult their packed filters. Particles in different groups use the reciprocal
category/mask test. Defaults are group 0 with self-collision enabled, preserving
ordinary fluid behavior. The group test shares the existing grid loop and runs
before displacement or kernel evaluation.

`material.drag` and `material.atmosphericPressure` follow Fluid3D's free-surface
atmosphere pass. Drag damps velocity in proportion to `max(0, 1-densityRatio)`,
so well-supported interior particles are unaffected. Atmospheric pressure adds
acceleration along the inward SPH color-field normal. Pressure is accepted in
`[0,1000]`; drag remains `[0,100]`. The normal calculation is fused into the
existing viscosity/cohesion neighbor traversal and is skipped entirely unless
at least one live particle enables either property. Scratch vectors are retained
by the solver, and the feature performs no GPU work.

`solver.setParticleWinds([[vx,vy,vz], ...])` copies exactly one wind velocity per
live particle. Atmospheric drag uses `particleVelocity - wind`, matching Fluid3D's
force-zone input. Speeds are finite and at most 1000 m/s. The array is transient:
it survives a failed step for an exact retry, then clears after a successful whole
step. Pass an empty array to clear it explicitly. Admission and `clear()` also
discard pending wind because particle indexing changed. Wind is not serialized.

`checked(solver.accumulateWindZones(zones, fixedTimeSeconds))` adds Fluid3D-style
ambient and spherical force zones to that same pending wind array. Start from
`checked(fluids.volumeWindZoneDefaults())`; type 0 is ambient and type 1 is
spherical. Ambient zones add `direction * (intensity + turbulence)` uniformly.
Spherical zones use `clamp((radius^2-distance^2)/radius^2,0,1)` and either the
outward radial direction or their configured direction. Turbulence is coherent,
deterministic `[0,1]` noise sampled from the explicitly supplied fixed time,
frequency and seed, then multiplied by the signed turbulence amplitude.

At most 64 zones and four million particle/spherical-zone candidates are accepted.
Ambient contributions are precombined, while all spherical zones share one
particle traversal. Every field and final speed is validated before pending wind
is replaced, so failure preserves previous wind for retry. Zones are borrowed for
the call only, invoke no callbacks, perform no GPU work and are absent from snapshots.

`setParticleExternalForces(forces)` and
`accumulateExternalForceZones(zones,fixedTimeSeconds)` cover the ordinary-force
path used when an Fluid3D actor does not request custom wind. Values are newtons and
the solver applies `force / restMass` during each substep. Pending forces survive
a failed step for exact retry and clear after success. Invalid input preserves the
previous pending values. When empty, a template specialization removes force-array
access from integration. The inputs are transient and add no GPU or surface work.

Collider samples also own the packed filter and apply the reciprocal test before
any sphere/box geometry work. Rejected pairs create no projection, friction,
contact, thermal transfer or solid attachment. The check adds no allocation or
second broadphase; each existing particle-collider candidate pays one integer
mask comparison.

The batch is rejected before scanning if any description is invalid, the source
exceeds 65536 particles, or query count times particle count exceeds four million.
Each query retains at most 1-4096 hits. No partial result, simulation mutation,
callback or GPU work occurs on failure. Sphere/box distances use the oriented
ellipsoid radius along the result normal. Rays use an exact ray/unit-sphere
intersection after inverse particle rotation and scale.

### Simplex topology and barycentric queries

`checked(fluid.setSimplexes(simplexes))` installs up to 65536 point, edge or
triangle entries. Each entry contains `particleIndices=[a,b,c]` and `size` 1, 2
or 3; only the first `size` indices participate and they must be distinct and in
range. An empty array selects the fluid default: one implicit point simplex per
particle, without allocating another per-particle topology array.

`checked(fluid.querySimplexes(queries,maxHitsPerQuery))` accepts the same strict
sphere, oriented-box and finite-ray descriptions as `queryBatch`. Results match
Fluid3D's query shape: `simplexBary`, `queryPoint`, `normal`, signed `distance`,
`simplexIndex` and `queryIndex`. Edge and triangle barycentrics identify the
nearest point and sum to one. Particle ellipsoid support radii are interpolated
with those weights. A simplex participates when any of its particles passes the
phase and reciprocal collision-filter test, matching Fluid3D's spatial query job.

The operation uses a bounded nearest-hit heap and eight fixed local projection
iterations for box/ray simplexes. It rejects more than four million
query-simplex candidates before scanning and performs no GPU work or simulation
mutation. Explicit topology is part of snapshot v20 and was introduced in v7. Lifetime compaction remaps
surviving indices and drops simplexes that reference an expired particle; failed
topology replacement leaves the previous topology intact.

### Occupied particle-grid diagnostics

`checked(fluid.debugParticleGrid(maxCells))` returns only occupied uniform-grid
cells in stable linear-grid order. Each result contains `center`, scalar `size`
and `particleCount`. The default budget is 65536 cells; zero and values above one
million are rejected, and exceeding the caller's budget returns an error without
changing solver state.

This is an explicit diagnostic query. It sorts one capacity-reserved cell index
per live particle when called, rather than scanning every possible cell or
tracking debug data during normal steps. It performs no GPU work and adds no
branch, allocation, upload or render pass to ordinary simulation and surface
reconstruction frames.

The sample's `wave-support.nut` mirrors Fluid3D's `WaveGenerator` X-axis motion using
caller-supplied fixed simulation time. `fluidWavePosition(originalPosition,
amplitude, frequency, fixedTime)` is a pure constant-cost helper, so pausing and
restoring the sample reproduces the same collider pose without wall-clock state.
