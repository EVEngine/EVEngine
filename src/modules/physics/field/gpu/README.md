# physics_field_gpu boundary

This default-OFF L5 adapter depends on `physics_field`, `gpgpu` and
`graphics_multiscale`.
It owns source/target GPU storage, compute pipelines, a command sequence and
one admission staging word. The existing CPU `GravityField`, `GravitySimulation`
and physics loop do not query or invoke it. Only an explicit C++ caller can
create a session. The GPU session is not a rigid-body owner.

The boundary's six faces are: direct C++ creation/prepare/commit/snapshot and
explicit projection into a borrowed multiscale sphere batch;
no capability in/out, ECS/Link, events, script bindings or MCP tools. One
known caller owns one session, calls are frequent, and publication is atomic,
so a typed direct call is appropriate. No registry lookup occurs per frame.

`GpuGravitySession` owns the only published source and target state. Each
`prepareStep` writes alternate device buffers, constructs eight mass/first-
moment summaries, evaluates targets and reads one admission word. A rejected
frame does not advance tick, revision, source or target buffer index. A
`PreparedGpuGravityStep` grants one publication right. Dropping it cancels;
`commit` checks owner, generation and base stamp before swapping indices.
Caller-provided simulation time is exact in nanoseconds; shader arithmetic is
binary32 and backend parity is by tolerance, not bitwise identity. There is
no RNG or callback. Snapshot explicitly reads back all rows.

`updateSphereBatch` consumes the published target device buffer, projects one
GPU-resident sphere per target in a finite view-relative binary32 camera, writes
device-local sphere packets and copies them into a preallocated batch image.
It reads back only one admission word. Near-plane or non-finite projection
rejects the complete update before the batch changes. The packet buffer is
session-owned; the batch image is renderer-owned. This is an explicit caller
path, not an automatic graphics or physics loop hook. It does not provide
global far-distance coordinate conversion, visibility culling or a general
hierarchical field solver.

`setSphereAppearances` uploads one radius, depth exponent and color per target
when those values change. A complete validated replacement publishes together;
an invalid replacement leaves the previous device buffer selected. Without
one, the projection call's uniform appearance remains the default. Target
motion and appearance share target order, but appearance is not integrated by
the field solver or persisted by this session.

`editSphereAppearances` applies strictly increasing target-index edits. It
validates the entire batch, copies the published appearance buffer to a spare
device buffer, records only edited rows as uploads, and swaps publication after
the GPU submission succeeds. This keeps CPU upload proportional to the number
of edits; the device copy remains linear in total targets. Rejected edits leave
the published appearance selected. The first sparse edit allocates the spare
buffer; later edits reuse it.

The admitted model is Newtonian G=1, eight sign-of-coordinate source clusters,
positive finite masses, bounded opposite x steps and finite target states.
Initial per-cell radius plus twice the maximum cumulative source displacement
must fit the configured radius. Sources cannot cross their x sign boundary.
Target distance and the analytic monopole force error bound gate every frame;
non-finite acceleration, integrated velocity or integrated position also reject
the whole candidate before publication, even when all initial values were finite.
The bound excludes binary32 roundoff and physical-model error. A near target
rejects the complete frame. This is a limited backend, not the general FMM
or the full million-object performance path. It has no soft fallback to the CPU
solver; the caller can choose that solver explicitly on a structured failure.

The caller destroys the session before the Graphics/GPGPU providers. The
session checks the Graphics lifetime before operations, but existing GPGPU
resource destructors require their Vulkan device to be alive. This ownership
precondition is part of the public API. The session retains no temporary input
span, scene pointer, ECS handle or persistent identity. Its runtime state is
not a saved format; restore creates a new session from authoritative values.

`GpuMortonKeys` is a separate, opt-in source spatial-index stage. It borrows a
resident vec4 source buffer for one synchronous update, writes one 30-bit Morton
key and original row index per source, checks one admission word, and publishes
one of three owned device buffers only after the entire batch succeeds. It accepts
an inclusive finite view-relative domain and rejects any out-of-domain source.
Its stable 30-pass binary radix sort stays on the device; the explicit snapshot
performs a full readback for diagnostics. This stage does not yet construct
compressed nodes or replace the session's eight-cluster field.
It has no capability, ECS, event, script or MCP face, and uses the same direct
owner-thread C++ mechanism as the field session.

The source files are the shader authority; `scripts/compile_gpu_field_shaders.py`
regenerates checked-in SPIR-V arrays. The direct sequence integration test and
session contract test run under `test/integration/multiscale-world/`. The latter
is compiled only when this module is enabled; provider absence is the default
configuration and must still link the legacy engine.
