# Professional particle-system roadmap

This roadmap treats a production VFX system as more than a long list of
emitter setters. The target is a predictable asset/runtime contract, scalable
simulation, multiple renderers, authorable composition, and measurable visual
output.

## Capability baseline

EVEngine already has CPU sprite simulation, lifetime curves and gradients,
flipbooks, bursts, prewarm, local/world space, force fields, collisions,
sub-emitters, skinned-surface emission, bone attachment, hot reload, lights,
and a script/JSON authoring surface. The experimental compute kernel is not a
production GPU path because it synchronously reads particle state back every
frame.

## Delivery phases

### P0 — deterministic playback and stable emission (implemented)

- Explicit or automatic random seeds, with fixed-seed rewind on `start()`.
- Playback speed, bounded fixed stepping, and finite looping timelines.
- Rate-over-distance emission with segment interpolation.
- JSON and Squirrel parity, focused unit tests, and user documentation.

These controls are prerequisites for replays, capture, slow motion, networked
effects, stable trails, and trustworthy visual regression tests.

### P1 — GPU-resident simulation and renderer scalability

Scalability contract implemented: priority-ordered soft particle/emitter
budgets, per-emitter spawn caps, quality tiers, distance/visibility simulation
policies, typed frame counters, and a rendered budget stress scene. These
controls are backend-independent and will also govern the GPU path.

GPU-resident execution remains in progress:

- Keep state, alive/dead lists, spawn commands, and compaction on the GPU.
- Render directly from GPU buffers using indirect draws; no frame readback.
- Extend the current aggregate profiling counters with GPU timestamps for
  spawn, update, collision, sort, and draw passes.
- Preserve a CPU deterministic backend for gameplay-coupled and replay-critical
  effects.

Exit criteria: measured crossover against CPU simulation, no per-emitter queue
stall, stable fallback behavior, and rendered stress-scene evidence.

### P2 — production renderers and data interfaces

- Camera-facing, velocity-aligned, axis-aligned, ribbon/trail, mesh, and light
  renderers behind a shared renderer interface.
- Depth sorting policies, soft particles, depth fade, distortion, normal maps,
  lit/unlit materials, and motion-vector policy.
- Mesh/skeleton, scene depth, collision, signed-distance-field, and gameplay
  parameter data interfaces with explicit CPU/GPU availability.

Exit criteria: composited reference effects (fire, smoke, impact, projectile
trail, weather volume) at multiple camera angles with automated screenshots.

### P3 — composable effect assets and tooling

- A versioned effect asset containing multiple reusable emitters, exposed user
  parameters, event routes, and timeline controls.
- Code-first/scriptable modules for spawn/update/render stages, with a compact
  inspector and curve/gradient/flipbook preview rather than a mandatory graph.
- Transactional hot reload, migration tests, dependency tracking, pooling, and
  runtime/editor command parity.

Exit criteria: artists can assemble and tune a multi-emitter effect without a
native code change, while shipped games can expose the same parameters through
Squirrel.

## Architectural constraints

- Cross-module services stay behind capability interfaces; public particle
  headers must not leak graphics, physics, animation, or editor implementation
  headers.
- GPU support is enabled only when state remains resident through rendering.
- Simulation determinism is a declared per-effect policy, not an accidental
  property of a particular frame rate or device.
- Every rendering milestone requires a real captured frame plus focused tests;
  startup logs or successful shader compilation are insufficient.
