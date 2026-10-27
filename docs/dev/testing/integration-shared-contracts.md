# Shared cloth, window and resource contracts

This follow-up keeps the same three merged integration CPPs and introduces no
Unity source, selector macro, production API or benchmark workload. Card/RTS
already runs a bounded short contract; its distinct configured-resource/dropoff
observation is retained unchanged.

## Cloth

Five 2×2 GPU contact cases become two parents: `contactNarrowPhaseContract`
(point/face CCD, edge CCD, friction/filtering) and `sweptContactSolverContract`
(hash/scan fallback and balanced-mass swept edges). All five original identities
remain named subcases, with their original geometric and conservation assertions.
Each parent creates one window, shader, sequence and buffer set. Before every
subcase, parameters are restored and all eight buffers are cleared; every run
uploads its own current/previous poses. Dense and folded geometries retain their
own differently sized fixtures. Twelve parent cases become nine.

The rendering case replaces two PNG writes/reads with two owned in-memory frame
captures. Each pose is drawn/presented once, rather than three times. Both images
must contain nonuniform RGB geometry and their pixels must differ after reset;
dimensions and byte lengths use actual framebuffer pixels for HiDPI hosts.
The existing twelve `1/60` simulation updates and particle-deformation assertion
remain. File stream/filesystem/iterator includes are removed; test presentation
uses vsync zero. No large dt substitutes for deleted work.

## Windows

Seven overlapping parents become three contracts: settings/size, desktop
fullscreen, and close/recreate. All seven old identities remain named subcases.
Fullscreen makes one real enter/leave transition; the second historical identity
checks settings before that transition, rather than repeating it. Close/recreate
shares one ordered lifecycle while checking open state, handle clearing, rejected
fullscreen operations after close, recreated settings/dimensions and final close.
Assertions are fatal and helpers use the current context; RAII closes the window
even after an assertion failure. Nineteen parent cases become fifteen.

Desktop visibility, native maximize and application background/foreground
contracts stay distinct. Event injection now stores `SDL_PushEvent`'s return value
before comparison: zeroerr's comparison macro evaluates operands for both result
and diagnostics, so embedding the call had posted every event twice. Readback
validation uses image/framebuffer dimensions rather than logical window stride.

## Resource observations

`graphics.lighting.frameSlotResourceReuse` is renamed
`graphics.lighting.boundedFrameSlotSubmission`: the public observations establish
bounded submission/output, not private allocator reuse. Its two eight-frame
alternatives retain no per-frame readback. At the end, deferred output checks the
cleared depth attachment and clustered output checks rendered centre pixels.
The original 800/1000-frame endurance identities remain in the opt-in benchmark.

Resident-cache repeated lookups and identical sampler setters each run three
times instead of twenty/fifty. The resource identity, deferred batch cancellation,
release/reload failure, recreation, sampler sharing, groom publication count,
mesh identity, LOD and rejected-update invariants remain and now fail the case via
`REQUIRE`. Empty backend conditional blocks are removed; actual Vulkan guards stay.

The historical audit still accounts for all 3,792 original identities with zero
unresolved destinations. No production module boundary, ECS, persistent format or
lifecycle API changed, and no architecture-rule exception is needed.

## Verification and matched runtime

The [measurement JSON](integration-shared-contracts-runtime.json) records three
before rounds followed by three after rounds on the same Windows host, with a
private background desktop, four CTest workers, 120-second case timeout,
`EVENGINE_VIEW_SECONDS=0.3` and `EVENGINE_PERF_FRAMES=30`. Compilation and source
checks were finished before the final timed rounds. Compiled identity preflight
verified each final selected case exactly once.

Before: 35 parents, wall times 11.200 / 12.378 / 10.403 seconds.
After: 28 parents, wall times 8.972 / 8.289 / 8.706 seconds; all 28 passed each
round with no warning-level failed assertions. Median wall time falls from
11.200 to 8.706 seconds, an observed 22.26% reduction (2.494 seconds).
This is a focused cohort, not a new full 138-source suite measurement, and the
rounds were not interleaved ABBA trials.

Median cumulative process times are 22.128 → 15.902 seconds for cloth,
14.559 → 10.220 for windows, and 3.430 → 2.913 for the four selected
lighting/resource cases. These are process sums under concurrency, not wall
times for each source in isolation.

Strengthening resource checks exposed a baseline false green: removing the file
did not remove its decoded image from ResourceManager, so explicit reload still
succeeded and changed the GPU handle. The old `CHECK` assertions only warned.
The fixture now explicitly unloads that path's decoded cache before asserting
reload failure, while preserving successful resident-texture cache access and GPU
identity. All baseline rounds contained those two warning-level failed assertions;
the corrected final rounds contain none. This is a fixture precondition fix,
not a weakened assertion or a production reload-policy change.

The same three merged OBJ files total 35,312,916 → 35,133,690 bytes
(179,226 bytes / 0.51% smaller). One window OBJ grows slightly while the combined
total falls. The graphics PDB is 500,748,288 → 484,077,568 bytes. Its history was
reset/relinked during verification and then linked incrementally again; this
storage comparison does not isolate source-change savings from PDB cleanup.
No controlled cold-compilation timing comparison was performed this round.

Native MSVC Debug SHARED compilation/link passed. Final
`make check CI_BASE=76bd8dd53` passed, including 527 script tests (three skips),
formatting and architecture gates. This base is the start of this follow-up;
the preceding branch-wide checks against `20394d720` remain separately recorded.
No Linux/macOS native run or new full-suite-green claim is made.
