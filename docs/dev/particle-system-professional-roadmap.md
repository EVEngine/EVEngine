# Professional particle-system roadmap

This roadmap treats a production VFX system as more than a long list of
emitter setters. The target is a predictable asset/runtime contract, scalable
simulation, multiple renderers, authorable composition, and measurable visual
output.

## Capability baseline

EVEngine already has CPU sprite simulation, lifetime curves and gradients,
flipbooks, bursts, prewarm, local/world space, force fields, collisions,
sub-emitters, skinned-surface emission, bone attachment, hot reload, lights,
and a script/JSON authoring surface. The original synchronous compute/readback
prototype has been removed; Vulkan now owns resident per-emitter state and
delayed counters.

## Delivery phases

### P0 — deterministic playback and stable emission (implemented)

- Explicit or automatic random seeds, with fixed-seed rewind on `start()`.
- Playback speed, bounded fixed stepping, and finite looping timelines.
- Rate-over-distance emission with segment interpolation.
- JSON and Squirrel parity, focused unit tests, and user documentation.

These controls are prerequisites for replays, capture, slow motion, networked
effects, stable trails, and trustworthy visual regression tests.

### P1 — GPU-resident simulation and renderer scalability (core implemented)

Scalability contract implemented: priority-ordered soft particle/emitter
budgets, per-emitter spawn caps, quality tiers, distance/visibility simulation
policies, typed frame counters, and a rendered budget stress scene. These
controls are backend-independent and will also govern the GPU path.

GPU-resident execution implemented:

- Per-frame-slot state, spawn, metadata, and indirect buffers are backend-owned.
- A compute pass updates, kills, atomically compacts, and writes the complete
  indirect command; rendering consumes the compacted SSBO without state readback.
- Each emitter records into the shared frame command buffer, so there is no
  per-emitter submit or queue wait. Reused-slot metadata provides delayed,
  non-blocking counters.
- Unsupported or gameplay-coupled features remain on the deterministic CPU
  backend, and scripts can query requested versus active GPU execution.
- A 16,384-particle regression measures median CPU main-thread simulation plus
  render-submission time. The Windows debug reference run measured 16.424 ms
  on CPU versus 0.125 ms for resident GPU submission.

Remaining P1 profiling work: backend timestamp queries for fine-grained GPU
spawn/update/compaction/draw timings. Collision and sorting timestamps arrive
with their GPU implementations rather than reporting synthetic zeroes.

Core exit evidence: measured crossover, no per-emitter queue stall, focused CPU
fallback coverage, validation-layer comparison against the startup baseline,
and a six-emitter/150-frame rendered stress capture.

### P2 — production renderers and data interfaces

- Camera-facing, velocity-aligned, and axis-aligned sprite modes now share one
  renderer contract. Stable oldest/youngest/distance transparency policies run
  on CPU (index reorder) and on the GPU resident path (bitonic index sort +
  vertex indirection); distance without a camera stays unordered.
- A CPU ribbon renderer connects stable birth-order control points, rejects
  undersized segments, and shares sprite material/color/size handling. GPU
  ribbon scan/index generation remains pending. Mesh and light renderers remain.
- Depth sorting policies and a real G-buffer linear-depth soft-particle path are
  implemented with explicit runtime availability. Lit/unlit particle materials
  and tangent-space normal maps now reuse the 2D lighting pipeline, with an
  explicit CPU-simulation fallback while GPU-resident lighting data remains
  pending. Distortion is implemented on CPU; motion-vector policy is authored
  (`none` / `velocity` / `spawn_delta`) with `isMotionVectorActive()` reporting
  false until a graphics velocity-buffer path lands.
- Mesh/skeleton, scene depth, and collision interfaces now have explicit
  CPU/GPU availability. Named gameplay float parameters can drive emission,
  speed, size, and playback on both simulation paths without rebuilding an
  emitter. A borrowed `IParticleSdfField` interface samples 2D signed distance
  for CPU collision (`setSdfField`); GPU resident sim reports `sdf` fallback.
- Reference effect pack under `examples/particle-effects/` (fire, smoke, impact,
  trail, weather) plus screenshot coverage in `test/particles_p2_p3.cpp`.

Remaining P2 work: GPU ribbon/lit data, mesh particle renderer inside the
particles module, and active motion-vector buffer writes.

### P3 — composable effect assets and tooling

- Versioned effect assets support schema v1 and v2. v2 adds authored
  `eventRoutes` (compiled to sub-emitters) and `timeline` cues/duration/looping.
  Unknown fields on a supported version are ignored; unsupported versions fail
  closed. `tryFromText` / `tryFromFile` return structured `Result`.
- Effect-local timeline clocks auto-advance from `Particles::update` /
  `advance` when playing. Transactional `reloadFromText` / `reloadFromFile`
  swap layers only after a successful parse and preserve world transform.
- `ParticleEmitterPool` provides acquire/recycle for short-lived gameplay VFX.
- ParticleGraph remains a fixed emission→motion→collision→renderer→output chain
  that compiles to emitter config (not an open spawn/update/render plugin graph).
  Compact inspector / curve preview UI and dependency-graph tracking remain.

Exit criteria (partial): artists can assemble multi-emitter effects with
parameters, routes, and timeline cues without a native code change; Squirrel
exposes the same effect/pool APIs. Remaining: inspector preview widgets,
dependency tracking, and broader runtime/editor command parity tests.

## Architectural constraints

- Cross-module services stay behind capability interfaces; public particle
  headers must not leak graphics, physics, animation, or editor implementation
  headers.
- GPU support is enabled only when state remains resident through rendering.
- Simulation determinism is a declared per-effect policy, not an accidental
  property of a particular frame rate or device.
- Every rendering milestone requires a real captured frame plus focused tests;
  startup logs or successful shader compilation are insufficient.
