# RTS crowd implementation and acceptance

Status: implementation in progress; no end-to-end completion claim.

## Scope

Implement the agreed RTS movement chain, including reliable contacts, bounded
transactional spawn placement, yielding/holding policies, predictive avoidance,
congestion recovery, movement groups, dynamic formations, script access and a
real engine demonstration. The complete scope remains required across stages.

## Ownership and contracts

Crowd owns compact simulation state and local contacts. RTS owns orders, selection,
movement-group membership, formation slots and traffic coordination. Navigation
owns paths. The existing RTS CrowdMotionSystem is the projection boundary; do not
introduce a second command queue or a crowd-owned RTS formation manager.

The connection has one simulation provider, per-unit/per-tick frequency, known
consumers and one simulation publication phase. Consequently use the existing
typed ECS view and injected Crowd reference, not per-agent capability discovery.
No new universal entity base or provider registry is needed.

All Crowd access is confined to the simulation thread and is non-reentrant.
Public snapshots own their values; temporary internal arrays never escape a step.
Existing named identities remain the compatibility bridge; new persistent or
cross-frame identity APIs must use owner/generation-qualified handles. Runtime
projections are rebuilt on restore, never serialized as compact slots.

The simulation consumes caller-supplied dt and uses no wall clock. Tests require
repeatability on the same build and fixed step sequence, with stated numerical
tolerances; cross-platform bit-exact lockstep is not claimed. Future avoidance
and contact changes must preserve this distinction.

## Required stages and evidence

1. Solver foundation: frame-snapshot steering, current contact broadphase, radius
   aware queries, wall/boundary constraints, bounded stepping and observed residual
   contacts. Regressions cover reverse insertion order, large agents, translated
   fields, corners, dense contacts and fast movement.
2. Interaction/spawning: independent avoidance priority, pushability, hold state,
   interaction layers, nearest-free/push-neighbors/reject policies, bounded chain
   displacement and atomic batch publication. Failure leaves the world unchanged.
3. Prediction/congestion: stable passing side, crossings, overtaking, stationary
   agents, stalled progress, local repath and RTS narrow-passage coordination.
   Measure arrival ratio, waiting, overlaps and simulation work.
4. Groups/formations: selection versus movement membership, line/grid/wedge/column/
   dispersed layouts, orientation, radius-aware spacing, stable spatial slot
   assignment, loose versus cohesive travel, narrow-passage compression/recovery,
   catch-up, member lifecycle and immediate replacement commands.
5. Integration: C++/Squirrel parity, Result projection, versioned restore/migration
   for any persistent additions, provider presence/absence, composition tests,
   independent link evidence and interactive RTS scenario rendering.

Final acceptance requires `make check`, relevant strict Windows compilation and
tests, trimmed composition checks, example smoke and engine-owned captures of
representative movement. Passing isolated contact tests proves only stage 1's
covered cases, never the entire feature set.

## Current work

The foundation is implemented in separate facade/storage, simulation, stepping
and binding files. `advance(dt)` returns an owning Result report; legacy `step`
delegates to it. Steering uses one frame snapshot, contacts rebuild their grid,
queries include both radii, wall projection uses circular geometry and independent
axis origins, and bounded substeps report unresolved packing.

Verified on native Windows with MSVC Debug and strict warnings enabled:

- `eve_crowd_check` compiled and linked EVCrowd/EVCommon with their required
  support libraries, without graphics/window/RTS libraries (L2 link evidence).
- `ctest --test-dir build/crowd-check/profile/crowd --output-on-failure -j 4`:
  25/25 independent processes passed, including the real Squirrel Result binding.
- `make check` returned success against `origin/dev`: architecture fixtures
  10/10 and repository script tests 180/180 passed. The existing nodiscard compiler
  diagnostic gate explicitly skipped because this host has no GCC/Clang compiler;
  MSVC compilation does not substitute for that fixture gate.

The module-interface catalogue declares the six boundary faces, hot paths,
thread affinity and trim limits. Catalogue validation now takes one filesystem
snapshot per invocation instead of rescanning dependencies for each entry.
This is structural catalogue validation, not complete automatic source-to-face
verification. No approved architecture exceptions.

The interaction-policy portion of stage 2 now has C++ and Squirrel APIs. Policies
are copied into compact storage and survive swap-pop removal; clearing/recreating
agents restores defaults. Hold stops velocity immediately while retaining the
target, and zero pushability leaves locomotion enabled. Bidirectional layer masks
filter steering, contacts and contact reporting. Explicit immobility overrides
the legacy avoidance-priority preference; equal-priority contacts share correction
according to pushability. Terrain constraints still apply to held agents.

Strict compilation and the independent CTest suite now pass 30/30 cases, including
five interaction regressions and extended script Result coverage. RTS order
projection and authoring/persistence integration for these policies remain open.

Stages 2–5 remain open. In particular this evidence does not prove spawn
transactions, prediction, traffic recovery, formations, provider-absent startup,
full RTS composition, or real rendered behavior. Next integrate these policies and spawn transactions with RTS ownership,
then implement predictive avoidance and dynamic formations.


## Spawn transaction progress

The caller-built SpawnBatch API supports reject, deterministic sampled nearest-free,
and anchored-new-agent push-neighbors modes. A private simulation snapshot is
published by swap only after the whole batch passes placement, geometry, identity,
capacity and explicit work/displacement limits. Contact activation is local to the
connected region; unrelated overlaps are untouched. Existing simulation time,
velocities and targets are retained. Slot-order ties are repeatable for identical
input order; arbitrary insertion-order invariance is not claimed.

The current implementation copies the whole simulation/field and scans agent
candidates under a check budget. Large-scale spawn cost remains to be measured;
this is not evidence for a ten-thousand-agent performance target. C++ and Squirrel
share canonical Result semantics. Strict MSVC build and 40/40 independent Crowd
cases passed, including ten spawn regressions and real script parsing/projection.
RTS production/order integration, persistence, prediction and formations remain open.


## Predictive avoidance progress

Implemented opt-in, acceleration-limited velocity sampling on a simultaneous
position/velocity snapshot. Candidate costs combine circle time-to-contact,
preferred/current velocity deviation and a right-hand passing preference.
Nearest-neighbor selection is bounded and stable by named identity on distance
ties. Query truncation and candidate-neighbor checks are reported by advance.
Scratch neighbor storage is reused. Agent masks and hold state remain authoritative.

This is an independently implemented local heuristic informed by Detour's
[documented sampling parameters](https://recastnav.com/structdtObstacleAvoidanceParams.html).
It is not ORCA and does not inherit [ORCA's formal reciprocal constraints](https://gamma-web.iacs.umd.edu/ORCA/).
Eight focused tests pass with positional contact repair disabled: opposing and
perpendicular crossings, an eight-way crossing, overtaking, held obstacles,
reverse insertion order with acceleration bounds, masks, and settings/budget reports.
Strict MSVC compilation and the complete independent Crowd suite pass 48/48 cases.
These small scenarios do not establish large-crowd throughput or deadlock recovery.
Global navigation, stalled progress/repath, narrow-passage coordination, formations,
RTS projection and real engine rendering remain required.


## RTS projection integration in progress

CrowdMotionSystem now lives in RTSCrowdMotion.cpp. Existing movement helpers
were moved into one private shared header so the split does not introduce a
second definition of order classification, target resolution or current-order reads.
The existing ECS catalogue includes this system: it reads the same Unit view,
writes motion/heading and injected Crowd state, makes no ECS structural mutation,
and runs on the simulation thread before presentation.

The active HoldPosition order now controls Crowd hold state; replacing the order
releases it on the next step. Other explicitly configured Crowd interaction fields
are preserved. Arrival uses the existing gameplay tolerance without snapping the
collision-resolved position to the goal. advance failures propagate through Result.
No public RTS API, component schema or persistent format was changed in this slice.
Production spawn integration, provider lifecycle, runtime defaults and full RTS
module-interface inventory remain open; the standalone spawn API is not yet the
RTS production path. The new composition target links the configured host closure
and is not independent-RTS trim evidence.


Verified RTS composition evidence: strict MSVC Debug compilation and linking in
minimal + rts + font; eve_rts_crowd_check uses configured host libraries without
unrelated CLI command implementations, plus the real command registry required
by asset import. This is composition evidence, not a claim that the minimal host
CLI or provider-absent startup links. The full CTest directory passes 65/65 cases:
48 Crowd cases and 17 RTS order/crowd cases (four new projection regressions).
Checks cover preserving separation on arrival, HoldPosition replacement, predictive
crossing through ECS projection and Result propagation of step-budget rejection.
No architecture exception was introduced.


## Production placement and verification correction

Production settlement now lives in RTSBuildingProduction.cpp. ProductionSpawn
receives the requested WorldPosition, and the factory owns the final placement.
All in-tree callback consumers have been updated. The script factory currently
initializes at that position; Crowd spawn transactions, rollback and displaced
peer projection still need integration.

The production ECS View is now destroyed before factory callbacks. The previous
scope deferred entity creation until function return, invalidating factory-returned
raw pointers when ECS committed temporary entities. The new regression verifies
both usable returned units and preservation of factory-adjusted coordinates.
Spawn-cleared notifications collected during traversal are also delivered after
closing the View. No persistent schema or module ownership change was introduced.

The isolated runner now installs Windows crash handling and CTest treats all
Assertion Failed output as failure, with a 60-second per-case timeout. IMPORTANT:
earlier 65/65 CTest exit-status evidence did not detect zeroerr CHECK warnings
and therefore does not prove every assertion passed. Strict rerun currently gives
74/77, including all 48 Crowd cases, four Crowd/RTS projection cases and the new
production placement regression. Three existing RTS test cases report assertions:
commandFanOutReplacesDirectCommandsAndAppendsQueuedWaypoints,
radarCreatesQuantizedUntargetableContactsAndJammingStopsRefresh, and
factionResourceFloorsProtectHighPriorityProductionAcrossFactories.
Their causes remain to be investigated; they are not waived or classified as
baseline without comparison evidence. The full suite is not green.


Follow-up verification: the production resource-floor fixture credited its ledger
inside REQUIRE_EQ, whose upstream macro evaluates each operand twice (comparison
and formatting). Moving those two mutations before the assertions fixes the
fixture without changing resource behavior. Latest strict run is 75/77: all Crowd
and production cases pass. Command fan-out and radar assertions remain open.
Build: build/rts-production-final-build.log; tests:
build/rts-production-final-tests.log. No architecture exception was taken.


## Corrected RTS fixtures and containment projection

The remaining strict assertion failures were fixture errors confirmed against
canonical owners: orders::CommandQueue retains cancelled history, and radar
jamming checks the target's current distance to the jammer. The order regression
now also completes the replacement and queued waypoint and proves the old order
never resumes. The radar fixture keeps the moved target inside the jammer radius,
then moves it outside and proves refresh resumes. Strict rerun passed 77/77 before
the containment change.

CrowdMotionSystem removes a contained unit's named projection instead of leaving
an invisible world-space obstacle. On disembark the normal missing-agent path
rebuilds the projection from authoritative ECS motion. The new test covers both
transitions. This changes no public API/schema and preserves the existing
Containment owner. Immediate transitions occurring after CrowdMotionSystem in the
RTS frame still require phase coordination for same-frame production placement.
Production push integration and the original remaining stages are still open.


Containment verification: strict MSVC compilation and assertion-sensitive CTest
passed 78/78 (build/rts-containment-build.log and build/rts-containment-tests.log).
Production rollback investigation found RTS::removeUnitRoot also erases the
matching paidProduction record. It must not be reused unchanged for failed spawn
placement: the completed paid task and pending subject reservation must remain
retryable without a second debit. This is still an implementation requirement,
not a completed transaction guarantee.


## Script production now uses Crowd placement

ProductionSpawn returns the explicit Created/Blocked outcome. The script factory
materializes the reserved unit, applies a bounded PushNeighbors batch, and publishes
displaced peer motion back to ECS immediately. A failed placement releases the
new ECS unit and its weapon, with a distinct rollback reason that preserves paid
production, pending identity and untouched provider projections. Completed but
unsettled tasks retain payment bookkeeping until settlement. Root creation and
new production requests reject identities reserved by pending tasks; only the
active production factory can consume its reservation.

A zero-duration CrowdMotionSystem pass before production reconciles same-frame
containment and order changes without advancing time. Machine-readable contracts
now record this phase and the production projection view/read/write sets. This
adds a second projection traversal and is not a performance claim. No persistent
schema changed and no architecture exception was introduced.

Strict MSVC and assertion-sensitive CTest pass 80/80 before the contract metadata
refresh. Two new script-profile composition tests verify actual neighbor pushes,
ECS publication surviving the next frame, repeated blocked placement with no leaked
units/faction members or duplicate debit, reserved identity exclusion, and success
on the original paid task when the blocker moves. Native custom factories must
adopt the explicit outcome contract. Production exits must fit the full agent
radius inside walkable terrain; there is no silent nearest-free fallback.

Remaining integration work includes blocked-task cancellation/refund and checkpoint
round trips, automatic transport boarding/disembark placement, runtime avoidance
defaults, provider lifecycle and all original formation/congestion/visual stages.
The factory's tentative ECS generations are consumed on rollback, not rewound.


Final verification of this production slice: strict MSVC build and all 80 CTest
cases pass after the metadata refresh; architecture contract validation passes.
make check passes (180 repository script tests); the existing GCC/Clang nodiscard
compiler-diagnostic fixture still skips on this MSVC-only host. Evidence lives in
build/rts-spawn-final-build.log, build/rts-spawn-final-tests.log,
build/rts-spawn-contracts.log and build/rts-spawn-quality.log. The full RTS crowd
objective remains incomplete; in particular there is no rendered runtime or
large-scale performance proof yet.


## Blocked production checkpoint evidence

The blocked-production regression now captures the script checkpoint while the
paid task waits, permits it to produce, then restores the checkpoint. It reacquires
all roots by stable subject (no pointers retained across restore), verifies the
produced root is absent, verifies balance and identity reservation, steps again
while blocked, and releases the blocker to produce exactly once at the original
cost. Strict MSVC compilation and the targeted assertion-sensitive CTest pass
(build/rts-spawn-checkpoint-build.log and build/rts-spawn-checkpoint-tests.log).
This proves the existing in-memory script checkpoint path for this scenario;
it does not establish cross-process persistence or every Crowd interaction policy.
No API/schema or production code changed in this verification slice.


## Radius-aware formation command groundwork

FormationSpec validation, pure planning and command fan-out now live in
RTSFormation.cpp. Stateless identity/position helpers have a single private
shared implementation. RTS still owns layout/selection and the canonical Orders
queue still owns commands; there is no Crowd-owned formation manager or second
queue. No public API or persistent format changed in this slice.

Fan-out validates live selection uniqueness and finite positive agent geometry
before any order mutation. It uses at least the sum of the two largest radii as
uniform center spacing for multi-unit layouts, preserving larger requested spacing.
This conservative spacing accommodates line/grid/wedge pairs without claiming
optimal dense packing. Nonfinite generated targets fail before queue changes.

Two regression cases cover mixed-radius pair clearance for all existing layouts,
stable assignment after reversing selection order, and unchanged command snapshots
on duplicate/invalid selection. Strict build and 82/82 assertion-sensitive tests
passed before final formatting; final logs are build/rts-formation-final-build.log,
build/rts-formation-final-tests.log and build/rts-formation-quality.log.
Orientation, column/dispersed layouts, movement-group lifetime, dynamic compression,
recovery and congestion coordination remain required. Adding orientation must also
update the versioned command replay format and script entry points; it has not
been added only to the planner while leaving replay silently incomplete.


## Oriented native layouts and command-log migration

FormationSpec now includes finite rotationRadians with zero as the legacy default.
Column and deterministic Dispersed layouts are added; all five layouts use the
same radius-aware fan-out spacing. Column is centered on Y before rotation.
Dispersed uses concentric rings at 1.5 times resolved spacing, with no RNG state.
The planner rejects nonfinite generated targets instead of publishing them.

EVERTS_COMMANDS version 4 carries orientation and the new enum kinds. Existing
legacy inputs still export their original v1/v2/v3 format; imports of those formats
retain zero rotation and their old enum limits. All replay operations validate the
formation fields, including fields unused by that operation, to preserve valid
serialization. Unsupported versions/malformed fields fail before mutating history.
The persistence catalogue records schema/version/migration/unknown-field policy.

Strict MSVC build and 84/84 assertion-sensitive tests pass, including all five
layouts with mixed radii, rotation around the anchor, actual application of a v4
replay to two units, malformed-import atomicity, and v1-v3 round trips. Evidence:
build/rts-orientation-final-build.log and build/rts-orientation-final-tests.log.
The script convenience commands still expose only their existing grid settings;
full script formation input remains required. These are static command targets,
not completed dynamic movement groups. No architecture exception was introduced.


## Squirrel formation command boundary

RTSFormationBindings.cpp now owns existing moveUnits/attackMoveUnits registration
and the explicit moveFormationUnits / queueScriptFormationMove entry points.
Shared subject parsing and fan-out receipt projection moved to one private helper
header, preserving canonical validation and Result projection. All four moved/new
movement entry points explicitly read ssq::Array: the previous std::vector<string>
binding expected a native instance and rejected ordinary Squirrel arrays. Array
element failures now become structured diagnostics before command mutation.

A real VM composition test verifies immediate rotated targets, malformed kind,
spacing and subject element rejection, tick-delayed column commands and v4 export,
as well as the existing movement convenience bindings with script arrays. This is
native VM execution, not a parser-only check or rendered gameplay proof. Full
formation input is available for Move; attackMoveUnits retains its existing grid
layout. Dynamic groups, narrow-passage recovery and interactive engine proof remain
required. No public C++ layout/state ownership or persistent schema changed here.


Script-boundary verification: strict MSVC build, all 85 assertion-sensitive CTest
cases and make check pass (180 repository script tests). Logs:
build/rts-formation-script-final-build.log,
build/rts-formation-script-final-tests.log,
build/rts-formation-script-quality.log. The source gate retains its explicit
GCC/Clang nodiscard-fixture skip on this MSVC host. The overall objective is still
open; the script API proof does not establish dynamic groups or runtime visuals.

## RTS profile scale and convoy waiting

The owned script Crowd now enables predictive avoidance, uses one navigation cell
as its arrival slowdown distance, and disables legacy long-range Boids separation.
The original pixel-scale separation radius and slowdown distance made two RTS
walkers move away from their goals despite valid avoidance velocities. The new
full script-world regression issues opposing orders and checks both destination
arrival and pair clearance over 240 simulation steps; it failed before this fix.
External Crowd providers retain ownership of their own settings.

CrowdMotion now honors convoy waiting along with traffic waiting and retreat cover.
Waiting units remain unarrived, preserving the active order until movement resumes.
This is braking/waiting intent, not an implicit immovable-body policy.
Strict MSVC build and all 87 assertion-sensitive tests pass; evidence:
build/rts-profile-scale-build.log and build/rts-profile-scale-tests.log.
Dynamic movement groups, traffic recovery and real rendered RTS proof remain open.
Source-quality verification also passed: build/rts-profile-scale-quality.log
(make check, 180 script tests; GCC/Clang nodiscard fixture remains explicitly
skipped on this MSVC host). No architecture exception was introduced; the change
keeps RTS-owned profile policy separate from external Crowd provider settings.

## Formation slot travel relaxation

Inspection before dynamic movement-group work found a greedy-assignment defect:
a unit far from the destination could consume the slot immediately beside a
second unit, sending the second unit across the layout. Command admission now
runs at most four deterministic pair-exchange sweeps. Every exchange strictly
reduces summed Euclidean travel distance beyond relative numerical tolerance;
position and slot index move together. Work remains quadratic in selection size,
with no retained pointers, new state owner, API, ECS layout or persistent schema.
It does not claim globally optimal matching or collision-free terrain routes.
The two-unit regression verifies the shorter assignment and matching slot ids;
existing five-layout selection-reversal checks cover stable iteration order.
Movement groups and dynamic formation recovery remain required and unimplemented.
Verification passed: strict MSVC build, 88/88 assertion-sensitive cases, and
make check (180 repository script tests). Logs: build/rts-slot-relax-build.log,
build/rts-slot-relax-tests.log, build/rts-slot-relax-quality.log. GCC/Clang
nodiscard fixture remains an explicit host skip. No architecture exception.

## Coordinated movement-group implementation contract

RTS owns movement-group membership separately from Player selection. A caller-built
MovementGroupBatch admits an immediate Move group. Each runtime member uses a
checked ECS handle and the exact order id, while snapshots use SubjectRef/order id.
Replacing/completing an order, death, containment or stale generation detaches that
member; fewer than two members dissolve the group. No raw pointer crosses a step.
Native and Crowd motion consume one derived Navigation formationSpeedFactor. The
coordinator runs after navigation and convoy planning and before both motion paths.
It compares remaining route lengths and slows leaders beyond leadDistance, without
increasing any unit's authoritative maximum speed. Slot-following anchors, dynamic
compression/recovery, appended group commands and AttackMove groups remain open.

RTSStateSnapshot v2 persists groups and includes them in canonical state hashing.
Version 1 migrates with no groups. Restore stages references/order validation before
publishing; speed factors are not persistent authority and rebuild after restore.
RTSSnapshot restoration and command admission moved into their own translation
units before extending the original oversized source files. The hot coordinator
resolves generation handles and compares order ids, with no new string-keyed lookup.
It consumes existing order projections, whose legacy JSON projection cost remains
part of the existing RTS order boundary and requires separate performance work.

Movement-group verification now covers both native and injected Crowd pacing,
completion/dissolution, command replacement, queue-clear id reuse, ECS pool growth,
member removal, v2 restore/rebuild, v1 migration, malformed-group atomic rejection,
and actual Squirrel v5 replay at its requested tick. Runtime membership includes a
non-persistent queue epoch so cleared/restored/copied queues cannot accidentally
reuse an old textual order id. Whole-RTS restore rebinds that epoch after restoring
orders; fan-out rollback preserves it. Attribute-provider revisions intentionally
advance during restore, so tests compare restored gameplay/group/queue data rather
than asserting identical pre/post-restore provider revisions.

Strict MSVC build and 104/104 assertion-sensitive CTest cases pass. Logs:
build/rts-groups-final-build.log and build/rts-groups-final-tests.log.
Adding the existing restore test file exposed two fixture assumptions: canonical
orders retain terminal history, and a single 500 ms ballistic integration step
can miss a small 3D target. Fixtures now check terminal-history semantics plus no
pending predecessor, and restored ballistic flight at 10 ms steps respectively;
no projectile production behavior was changed.

The current mandatory default make check is NOT green. Its preceding format,
layering, binding, manifest, examples, version, quality and profile stages pass,
but architecture reports unchanged Object.h lines 39/94 as new relative to the
advanced shared origin/dev. HEAD is 0182af046d0e0d63d7478e5a444c0f87047136bf;
observed origin/dev is 759c3bb6baacd73d4df92a004355ab33a04aa93f. Upstream commit
90b9b2687 deleted Object.h; this worktree has not modified that file. Evidence:
build/rts-groups-quality.log. Baseline alignment remains required; no allowlist,
metadata exception or replacement gate baseline was introduced. A separate
--base HEAD diagnostic confirms this worktree's architecture changes pass
(build/rts-groups-worktree-architecture.log), without overriding the failed gate.
Ruff passes. Script tests pass in combination: 171 cases in the first run, with
two import-failed modules retried for their nine cases after including build/tooling
(Pillow) and scripts (utf8_stdio) on PYTHONPATH. Logs:
build/rts-groups-script-tests.log, build/rts-groups-script-tests-retry.log,
build/rts-groups-ruff.log. Full moving-anchor formations, corridor recovery,
advanced congestion, queued/attack groups, runtime visuals and complete trim proof
remain open. The overall goal is not achieved.

## Upstream alignment and current verification

Fast-forwarded the worktree to observed origin/dev
759c3bb6baacd73d4df92a004355ab33a04aa93f, retaining all local feature work.
Recovery artifacts remain in build/crowd-baseline-backup-20260922 (file archive,
binary diff, SHA256 manifest and exact retained stash id). Forty-three unaffected
feature files match their backups after line-ending normalization. The production
extraction conflict was resolved by integrating upstream settlement into the
extracted implementation, without restoring duplicate systems.

Production now consumes ReadyToSettle tasks as well as legacy automatically
completed tasks. Blocked placement retains the task and payment; successful unit
publication is followed by a stable rts.unit:<task-id> settlement receipt. A
published task retried after a failed acknowledgment settles without another
spawn. The existing blocked-production regression checks the pending state and
empty receipt before success, then Completed and the exact receipt afterward.

The previous default-gate failure is resolved by actual baseline alignment, not
by overriding CI_BASE or adding an exception. make check passes against origin/dev,
including all 180 repository script tests (build/rts-aligned-quality.log).
An entirely new build/crowd-aligned-check directory avoids stale objects across
upstream resource/Squirrel header changes. Strict MSVC compilation passes
(build/rts-aligned-build.log), and all 104 assertion-sensitive Crowd/RTS CTest
cases pass (build/rts-aligned-tests.log). Existing matching WebP 1.6.0, utf8proc
2.11.3 and zstd 1.5.7 source caches were used read-only after the fresh WebP
download failed in the restricted network environment. git diff --check passes.

Applied rules include single authoritative production settlement, typed Results,
closed ECS iteration before spawn callbacks, preserved module boundaries and
the mandatory default source-quality gate. No architecture exception was taken.
These checks validate the implemented slice; moving-anchor formations, corridor
compression/recovery, congestion coordination, queued/attack groups, rendered
runtime demonstration, performance measurements and complete trim evidence remain
required before the overall RTS crowd goal can be marked complete.

## Pacing and traffic dependency

A yielding group member previously contributed its remaining distance to the
leader's pacing limit. This could stop a traffic winner while the yielding member
waited for that same winner to clear the passage. The coordinator now excludes
traffic, convoy and retreat-cover waits from that limit, without detaching their
order-bound membership. Pacing resumes from current routes on the next ordinary
movement step. No persistent state, clock or extra provider is introduced.
The existing native/Crowd pacing regression now checks winner progress during a
traffic wait, preserved membership and resumed leader slowdown afterward.
Strict MSVC build and all four movement-group cases pass:
build/rts-group-yield-build.log and build/rts-group-yield-tests.log.
The mandatory default make check also passes, including all 180 script tests
(build/rts-group-yield-quality.log). The ECS read-set catalogue records the
waiting-state inputs. No public API or persistence version changed and no
architecture exception was required. This resolves a pacing/traffic dependency;
it does not establish complete corridor traffic recovery or dynamic formations.

## Open-travel moving slots

The group coordinator now derives a translated slot layout from the mean of
member position minus assigned destination. The shared anchor advances by at
most leadDistance toward the final anchor; each member steers toward its own
translated slot. This projection is rebuilt each simulation step, requires no
leader identity or new persistent state, and is reset on capture/restore.
Members retain radius-aware final assignments and authoritative speed limits.
Native and Crowd motion consume the same optional Navigation formationTarget.
Native motion was extracted into RTSMotion.cpp before extending the oversized
systems translation unit. Reaching a temporary slot cannot complete the order;
arrival remains measured against its authoritative final destination.

Scope is live order-bound group members; reads include Motion, Orders, Navigation,
Durability, Containment and existing wait states. Writes are derived Navigation
speed/target projections and pruned RTS membership, with no ECS structural change,
events or new service. Execution remains after navigation/convoy, before motion.
Snapshot v2 remains unchanged: runtime targets never become persistent authority.
Waiting members disable shared steering for that step, preserving passage exit.
The pathfinder-present path deliberately still uses individual navigation routes;
shared-path turns and corridor compression remain unfinished, not trim proof.

Regression coverage adds widely separated units that must assemble before reaching
the destination in native and Crowd motion, non-completion at intermediate slots,
derived-state reset/rebuild on restore and removal on command replacement.
It also checks that attaching a pathfinder removes shared steering while producing
ordinary paths. Restore reconstructs the same target at a zero-duration step
within 1e-5 world units. All 105 assertion-sensitive CTest cases now pass
(build/rts-moving-slots-final-tests.log), with strict MSVC compilation recorded in
build/rts-moving-slots-build.log and build/rts-moving-slots-final-build.log.
All selected engine/test translation units were touched before rebuilding because
the derived Navigation field changes layout and this build uses unscanned headers.

Two fixture assumptions were corrected during validation: leaders now need less
slowdown when moving slots hold them closer to their group, so the pacing test
checks slowdown plus bounded separation rather than an arbitrary factor below
0.5; and the injected Crowd provider must outlive RTS cleanup. The initial new
test declared those objects in the reverse order, producing an access violation
in RTS::setCrowdProvider during teardown and a CTest timeout. The final fixture
uses the documented provider lifetime; no timeout was enlarged or test skipped.
The default make check passed with all 180 script tests
(build/rts-moving-slots-quality.log). No architecture exception was introduced.
After the fixture corrections, the final default gate also passes with all 180
script tests (build/rts-moving-slots-final-quality.log).

## Pathfinder-aware formation release and recovery

Shared moving slots now use the canonical map grid when a Pathfinder is injected.
Every member must have a clear swept-radius segment to both its final slot and its
moving slot. Blocked cells are expanded by radius and tested with segment slabs;
this conservative box expansion can reject safe corner grazes but cannot approve
a circle sweep through the blocked cell. Grid origin/scale and grid boundaries
are included. Invalid or more-than-4096-cell query regions return not-clear,
retaining ordinary path steering; there is no allocation or A* per query.

The group publishes shared steering only after all members pass. A constrained
group releases both slots and pacing so passage winners can leave while others
follow existing paths/reservations. It automatically reforms when clearance is
available. A validated direct final route advances the cached waypoint cursor to
its end, preventing stale-path backtracking and premature waypoint-based order
completion. Navigation remains the owner of route planning; the coordinator reads
the injected Pathfinder and advances only paths superseded by checked direct routes.
No schema, script input, owner lifetime or simulation clock changed.

Tests cover native/Crowd passage traversal and recovery, per-frame wall clearance,
both members completing, provider attachment, temporary blockage removal, different
radii, translated/scaled grids, out-of-grid footprints and bounded query rejection.
This implements loose passage release/recovery, not ordered column compression or
rotating slots along a shared route. Those remain part of the full goal.

The passage regression initially reproduced a Crowd deadlock at a shared
intermediate waypoint: contact separation held both centers outside the small
waypoint arrival radius. Group members now advance one intermediate waypoint
when within radius plus a grid-relative margin, only after the next segment is
swept-radius clear. Final destination tolerances are unchanged. Both native and
Crowd now traverse the single-cell gap and recover their slots; seven group tests
pass (build/rts-passage-recovery-tests.log), as does strict MSVC compilation
(build/rts-passage-recovery-build.log). No timeout adjustment or skipped case.
The stronger passage assertion checks the complete per-frame movement segment,
not just each endpoint. All 107 cases pass after the final predicate naming fix
(build/rts-passage-final-tests.log), with strict MSVC build evidence in
build/rts-passage-final-build.log. The source gate initially required the query
name to express a predicate; it is now isFormationSegmentClear. No operation-result
bool, allowlist or gate override was introduced.
Final default make check passes against origin/dev, including all 180 repository
script tests (build/rts-passage-final-quality.log). Applicable contracts are the
single map authority, bounded hot-path queries, derived nonpersistent steering,
explicit ECS read/write sets and provider-present/absent behavior; no exception.

## Corridor direction reservations

The six-member mixed-speed passage regression passes with native and Crowd
movement. Opposing traffic requires a broader reservation than one next cell:
units can otherwise occupy different cells of the same single-width corridor
while walking toward each other. TrafficReservationSystem now discovers connected
walkable cells with at most two orthogonal exits, caching discovered membership
within the step. Current occupants precede outside entrants; priority and stable
subject break ties. Candidates sharing the selected exit direction retain ordinary
next-cell arbitration, while opposite entrants wait outside. Approaches inspect
at most three pending route waypoints; corridor discovery rejects components over
1024 cells with a structured Unsupported diagnostic rather than publishing a
partial corridor as if it were complete.

Implementation moved out of the oversized RTSSystems.cpp into RTSTraffic.cpp.
The system's View includes Identity, Motion, Navigation, Orders and Containment;
it reads the injected map and writes only derived trafficWaiting. No ECS mutation,
new persistent reservation state, event, clock or provider lookup is introduced.
The existing tiny-map test now retains the losing direction's wait while the
winner occupies another cell of the same corridor, and proves release when that
winner is removed. This intentionally replaces the old next-cell-only behavior.
The new end-to-end opposing-squad scenario checks exclusive corridor direction
and completion for both native and Crowd movement. Initially opposing occupants
already inside a corridor still require evacuation/backtracking; this algorithm
coordinates admission and does not claim to solve that separate recovery case.
Dynamic entity targets and patrol legs use Navigation plannedGoal for exit choice,
so the original order's static point does not override the actual planned leg.
The relevant 11 group/traffic cases pass, including both opposing squads reaching
their goals with no simultaneous opposite-direction corridor occupancy
(build/rts-corridor-tests.log). Strict MSVC build passes (build/rts-corridor-build.log).
All 110 Crowd/RTS cases pass (build/rts-corridor-final-tests.log). The budget
regression additionally verifies DiagnosticCode::Unsupported after the final
strict rebuild (build/rts-corridor-budget-build.log and
build/rts-corridor-budget-tests.log). Default make check passes against origin/dev,
including all 180 script tests (build/rts-corridor-quality.log); git diff --check
passes. Applicable rules include typed failure, explicit bounded simulation work,
single map authority, scoped ECS component borrows, no new persistent authority,
and source-quality validation. No architecture exception. Ordered column slots,
shared-path rotation, trapped-opponent evacuation and runtime/performance proof
remain open; the overall goal is not complete.

## Temporary corridor evacuation

Traffic now projects an optional evacuation target for the losing direction when
a corridor has a walkable outside exit and a radius-clear side bay. It chooses
distinct candidate bays in stable arbitration order, and traverses corridor cells
toward that exit using bounded reverse adjacency rather than invoking A* each
frame. Every published segment is checked against the canonical grid with the
member's radius. If no safe bay/segment exists, the member continues waiting.

The existing original order is unchanged. Native and Crowd movement give the
temporary traffic target precedence over formation/route targets; arrival at an
evacuation step never completes the order. Supply and retreat-cover holds still
apply. When evacuation ends, the route id is invalidated and one movement step
waits for NavigationSystem to replan from the new location. Removing/replacing
the navigation provider clears the owned units' evacuation projections.

This adds a derived Navigation field, excluded from snapshots just like moving
formation targets. A snapshot taken during evacuation clears the saved planned
order id so restoring cannot reuse a path from before the retreat. Restore drops
any supplied evacuation projection, and normal traffic arbitration reconstructs
it. No schema version, order identity, queue or persistent traffic lease changes.
ECS reads additionally include Crowd radius; writes include trafficRecoveryTarget
and plannedOrderId. All component borrows remain scoped to the synchronous View;
no entity creation, callbacks or cross-frame pointers are introduced.

Regression scope includes opposing squads starting inside the corridor, both
movement backends, snapshot/restore during evacuation and eventual completion of
the original orders. Collision-sweep checks prove each evacuation step stays
radius-clear against the navigation grid, and provider removal clears temporary
evacuation projections without preserving stale route ids.
