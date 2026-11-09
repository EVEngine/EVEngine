# Native procedural PixelFX port

## Scope

Port all244 distinct effects from patch-gg/pixelfx revision
`a84159688af3ca37a4be05a0d422b94451b2e24e` into the existing Stylize module.
The native runtime uses C++ algorithms, existing engine math and the existing dependency
pin. The upstream JavaScript is used by independent reference tooling.

Deliverables:

- Native implementations and source-owned descriptors for244 unique effect IDs.
- Typed configurable options, introspection and atomic validated configuration publication.
- Owning prepared programs, independent simulation/RNG state, checked advance/seek/replay.
- Default and custom actor pixels/masks/anchors, stock rig, targets, body occlusion and12 moves.
- Source Act orchestration, windup/hits/hitstop/recovery, ghosts and shake.
- Runnable searchable gallery with default/variant previews, parameter editing, timeline,
  playback/stop, body modes, targets, color/light layers, backgrounds, mirror and rotation.
- Independent inventory, parameter and color/alpha/light comparison tooling.
- License attribution and user/API documentation.

## Ownership, time and interfaces

Effect instances own their settings, prepared geometry, mutable state and planes. Configuration
and replay candidates publish atomically and do not retain caller arrays or temporary actors.
Prepared frames and pose-cache sessions have explicit owning lifetimes. Public operations use
structured Result diagnostics; expensive preparation is separate from publication and queries.
Callbacks run synchronously with documented reentrancy checks and no held engine lock.

Analytic effects evaluate injected absolute time. Simulations retain fixed1/120-second substeps
and named effect RNG progression. Source Acts retain raw caller-update ordering and source
hitstop/target behavior; replay records successful step/stop commands rather than reconstructing
frame-dependent behavior from wall time. The bounded source pose cache preserves first-winner
rounded-pose history within its owning render session.

The existing Stylize module owns declarations, script bindings and tests. This adds no universal
ECS/gameplay root, damage authority, module boot channel or new external numerical dependency.
The gallery reuses two persistent color/light surface pairs and prepares only the selected pair.

## Reference fidelity and known limits

Inventory coverage must match the exact pinned244 IDs with no placeholder aliases.
Reference tools run the pinned upstream source independently and compare actual RGBA/light
planes, actor visibility and trajectories. Pixel coverage and alpha/light checks remain explicit;
a numerical similarity score alone is not visual parity proof.

The implementation deliberately uses existing engine/platform mathematical functions. Small
floating-point differences can change RGB rounding or the winning sample at raster/depth ties.
Known diagnostic cases include SourceRig overlap/raster edges and several GCC RGB rounding
samples. At an occlusion tie, a different winning material can produce a larger RGB delta at an
individual pixel; this must be distinguished from an incorrect effect algorithm. Full universal
bitwise equality across platform math implementations is not claimed. Canonical reference-frame
expectations and tolerances are not silently rewritten to hide a discrepancy.

Runtime sources, owning test translation units and reference probes share the source-scoped
`pixelfx/NumericContract.cmake` policy: preserve authored binary64 operation order without FMA
contraction. Header-defined arithmetic must not be compiled under a different mode in a test
or probe: a fused inline definition can also be selected by the linker for a runtime call.
The macOS half-plane failure was reproduced on the original Windows/WSL host: contraction
changed the strict `-2.375` vertex to `-2.3749999999999929`. Enabling contraction only in tests
reproduced151 failures,150 overlapping the160 macOS failures, including identical FireballThrow
hashes. This is a compilation-contract repair, not a new numerical tolerance or oracle change.

The remaining macOS Poof failure was captured losslessly at tick24: four color pixels differed
in position/alpha while the entire light plane matched. Moving only the floor-capped speed-line
endpoint across `y=-2` in the pristine upstream painter reproduced the complete macOS color
plane byte-for-byte. Darwin-target Clang output combines adjacent sin/cos calls into
`__sincos_stret`, whereas the radius constructor calls sin alone. Poof now retains and reuses
the exact sine that defined that immutable line's radius; it does not clamp coordinates or
relax alpha/geometry assertions. Local reference comparison covers every tick0..32 for seeds21,
0,123 and4294967295: all132 color/light frame pairs match exactly. The regression also pins
eight full-frame color/light pairs, including seed4294967295 whose source raster would be
changed by simply clamping the endpoint to the floor. Hosted macOS verification remains required.

On 2026-11-09 the user explicitly approved the first three measured RGB quantization boundaries,
then separately approved the additional BlizzardVortex frame exposed by completing the suite.
Each boundary is confined to its frame and the exact pixels/channels below. The pinned upstream
reference export was regenerated and compared with the current GCC native output. Only the
observed directional one-level channel difference is admitted; alpha and every other channel/pixel
remain exact. The original full-frame hashes are retained: after checking the measured pixel,
the comparison hashes its canonical reference value with all other actual pixels unchanged.
Light-plane assertions remain exact, and this approval does not apply to other macOS failures.

- BlizzardVortex, default seed17, tick48 (`0.4s`), 128x116 pixel(71,64): G235/236.
- BlizzardVortex, default seed17, tick120 (`1.0s`), 128x116: pixel(56,89) B254/255,
  pixel(55,90) B255/254, pixel(55,96) G235/236. All three alpha values remain exact.
- ScytheReap, seed0, tick49 (`0.4083333333333333s`), 184x116 pixel(142,60): R215/216, B241/242.
- EarthStomp, default seed1, tick166 (`1.3833333333333333s`), 290x142 pixel(181,124): G105/106;
  alpha remains85.

Counterexamples must reject a two-level difference, changes to an unselected channel,
alpha, or any neighboring pixel, as well as an invalid reference index/channel mask.

Pristine ShadowTendrils seed3 reaches a nonfinite line-width path around tick59 and can loop
indefinitely upstream. The native finite-input guard is documented as PIXELFX-SOURCE-001;
reference evidence before that point and alternate-seed dense coverage are retained locally.

The numerical-library experiment in EVEngine/third-party PR10 was closed at user request and
is not consumed by this change. Source, prebuilt and SDK dependency ownership remain unchanged.

## Validation and review gates

Run against the final PR base/head:

1. `make check` for source quality, binding/example/API and architecture contracts.
2. Windows SHARED and OBJECT builds and focused PixelFX/domain tests.
3. Official `scripts/smoke_examples.sh pixelfx` and actual engine-owned screenshot checks.
4. Hosted platform CI and exact-log repair of relevant failures.
5. Native/reference default/variation, invalid-input atomicity, seeded replay, stop/end,
   actor/move and simulation trajectories; preserve the distinctions above.

Existing local evidence before the final dev refresh includes:

- Actual Windows SHARED and OBJECT builds and917 focused PixelFX cases on each route.
- Official native gallery smoke,72 angle inputs, three exact full-turn screenshot round trips,
  and no spill beyond preview panels.
- Three complete244-effect gallery selection cycles with bounded measured process memory
  after fixing returned-scope retention in the existing CallGraph owner.
- Actual native CPU measurements for24 representative scenarios/8640 frames; separate Debug
  CRT allocation observation and480-frame native texture-update latency observation.
- Read-only native particle-container observation for BigExplosion and ScytheReap with20
  unchanged actual color/light planes. Container occupancy, live records, geometry/light counts,
  allocations and GPU upload timing are reported as different quantities.

These measurements are Debug observations rather than release throughput guarantees. Validation
must be refreshed as required by final source/base changes; queued or running CI is pending.
Generated frames, JSON, probes, metrics, raw work logs and build outputs remain under ignored
`build/` and are not PR artifacts. No architecture exception is taken.
