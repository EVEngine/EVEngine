# Road reference alignment lab

The goal is functional alignment with the EasyRoads package audited in
docs/dev/2026-10-13-easyroads-core-comparison.md. This harness starts with
eight **surface-only** fixtures: straight, curve, slope, elevated bridge surface,
T junction, X junction, varying width and 15-degree banking. Each has a perspective and top view.

The second batch adds eight surface fixtures: `s-curve`, `crest`, `valley`,
`curved-uphill`, `variable-bank`, `negative-bank`, `curve-width`, and
`curve-width-bank`. The suite now contains 16 scenes and 32 image pairs.
All use the same 0.98 silhouette IoU threshold. Curved uphill now passes after
the explicit WorldUp road-frame correction; variable banking remains below
threshold. Genuine reference vertices and difference images are retained.

It does not establish terrain, shoulders, sidewalks, bridge structure, tunnels,
traffic, UV, materials or side-object parity. Those require additional fixtures
and non-image contract checks. The Unity adapter is compile-checked against the
actual outer runtime DLL, but requires a licensed Unity editor for runtime validation.

## Reproduce on Windows

Use the repository MSVC wrapper, an initialized dependency checkout and its
Debug zeroerr library. The generator compiles the **current workspace** road
sources; it never obtains road geometry from the renderer SDK.

    cmd /c cmake\with-msvc.cmd cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -DEVE_DEPENDENCY_TREE=C:/path/to/initialized/EVEngine -S scripts/road_parity/native -B build/road-parity/native
    cmd /c cmake\with-msvc.cmd cmake --build build/road-parity/native -j 4
    build/road-parity/native/road_parity_fixture.exe build/road-parity/fixtures
    python -X utf8 scripts/road_parity.py prepare --package 'C:\path\EasyRoads.unitypackage'
    python -X utf8 scripts/road_parity.py render-ev --eve 'C:\path\eve.exe'
    python -X utf8 scripts/road_parity.py render-unity --unity 'C:\path\Editor\Unity.exe'
    python -X utf8 scripts/road_parity.py compare

Unity assets are extracted with their original GUID metadata into
build/road-parity/unity-project/Assets/, keeping the dependency private and
git-ignored. Only the outer package is installed; embedded Beta/SRP overlays
are excluded. Unity creates reference project settings when opening this
Assets directory. Activate an existing Editor license in Unity Hub before running.
No Unity binaries, scripts, textures or scenes from the package belong in Git.

## Input and evidence contract

eve.tests.road-parity version 1 is a test fixture, not a second editable road
document. The native graph/baker produces the authoritative fixture JSON and
candidate surface mesh. Roads have stable test IDs, from/to node references,
world-space Y-up markers, metre widths and directional lane counts.

The Python adapter projects the same marker/width inputs for Unity JsonUtility.
For T/X cases, EasyRoads roads attach to its actual prefab ports while EVEngine
derives sockets from its graph. Thus differences in port radius, tangent and
trim are intentionally visible; the adapter does not substitute EVEngine geometry.
The native input lane width is normalized to 3m, without curb, sidewalk,
markings, navigation or piers. Surface meshes alone are rendered.

Both views use 1280×720, vertical FOV 42°, identical eye/target/up,
near/far 0.1/250m, a magenta unlit road and grey background. Unlit rendering
removes lighting and texture differences from this first geometry comparison.
EVEngine screenshots come from its presented frame readback; Unity screenshots
come from Camera.Render into RenderTexture, not desktop capture.

Each candidate record identifies the fixture SHA-256 and renderer executable
SHA-256. Both sides record image SHA-256 after successful captures.
Changed fixture bytes, missing evidence, changed PNGs, empty masks and incorrect
image dimensions fail validation. Missing reference evidence yields blocked
with no IoU score, and a nonzero comparison exit. No baseline image is fabricated.

The comparison result is build/road-parity/comparison.json.
Per-view differences use grey for shared road pixels, blue for EVEngine-only
and orange for EasyRoads-only pixels. Default silhouette IoU acceptance is 0.98;
this is a geometry calibration threshold, not a functional parity certificate.
The comparison currently measures coverage, not metric edge distance, seam
normal continuity, traversability or material fidelity.

## Functional acceptance sequence

1. Calibrate the six baseline surfaces. Inspect both real renders, silhouette
   differences, port positions and road-width measurements. Fix adapter/camera
   errors before interpreting renderer differences as core defects.
2. Add varying width, roll/rotation pivot, mixed segment controllers, asymmetric
   profiles and IConnector fixtures. Verify split/reverse/undo/snapshot behavior
   alongside cross-section measurements; image matching alone is insufficient.
3. Add graded terrain with separate left/right Indent and Surrounding, explicit
   bridge/tunnel intervals, holes, vegetation and splatmaps. Compare height samples
   and masks; inject failures and verify owner-qualified atomic restore.
4. Add continuous barriers/walls/tunnel assets, first/middle/end pieces, terrain
   rules and junction continuity. Inspect geometry plus deterministic interval
   selection and asset-port compatibility.
5. Add LHT/RHT, typed lanes, turn restrictions/curve overrides, turn speeds and
   traffic controls. Compare lane/turn data and graph invariants; render overlays
   only as supporting evidence.
6. Add roundabout entry-space constraints, custom asset ports, surface UVs,
   materials, LOD/chunk consumers and multi-terrain boundaries.

These are remaining acceptance phases, not completed engine capabilities.
The first width/centerline-roll authoring slice is implemented and covered by
`test/procgen_road_profiles.cpp` and `test/procgen_road_profile_editing.cpp`.
It propagates into mesh, navigation and placements and survives split/reverse,
snapshot migration and authority undo. Run `ctest --test-dir build/road-parity/native`
for its CPU checks. This does not complete phase 2 or establish Unity visual parity.
Architectural rules continue to apply: one authoritative RoadNetwork,
structured results, no upward core dependencies, explicit expensive bake,
deterministic derived outputs, candidate-state commit and stale-safe restoration.
Any new persistent road authoring fields must migrate editing, snapshots,
consumers and failure tests together. No architecture exception is introduced.

## Functional authoring fixtures without Unity

The native target `road_authoring_fixture` generates eight full-mesh EVEngine
scenes: crowned-section, concave-beam, bridge-tunnel, continuous-rail and
custom-connection, prefab-deformation, multi-arm-facilities and terrain-multichannel. They exercise custom sections/caps, explicit structure
intervals, continuous sweeps and a real custom port mesh with facility continuity.
Each scene owns an authoring.json snapshot and generated mesh_data.nut + main.nut.
The terrain scene also owns terrain_state.json, generated after native road-channel
apply, undo, JSON save/restore and redo. It uploads height and splat-derived geometry
from the restored state; the terrain display has a 0.04 metre vertical bias.
These are candidate-only functional scenes, separate from the surface parity lab.

```powershell
build/road-parity/native/road_authoring_fixture.exe build/road-parity/functional-scenes
scripts/capture_example.ps1 -Game build/road-parity/functional-scenes/bridge-tunnel -Eve $env:EVE -Frames 12 -Out build/road-parity/functional-scenes/bridge-tunnel/ev-frame.png
ctest --test-dir build/road-parity/native --output-on-failure
```

Set EVE to the actual renderer executable first. The current-workspace native
core supplies all geometry; a prebuilt renderer only uploads those streams.
The checked-in CTest registration treats assertion warnings as failure even
when the upstream test executable returns zero. Snapshot/mesh/image hashes in
local evidence record the generation and capture boundary.

See [authoring contracts](../../docs/dev/2026-10-13-road-sections-structures-connections.md)
and [extension contracts](../../docs/dev/2026-10-14-road-prefabs-facility-routes-terrain.md)
for bounded source meshes, explicit start/middle/end repetition, compatible welding,
authored one-to-one facility routes and complete-channel terrain history.
The core also writes these channels through TerrainWorldWorkspace.conformRoad;
its native trees retain PointSet identities and attributes and its existing history
records holes with every other domain. Unity visual parity remains deferred.


2026-10-14 后续夹具增加 `prefab-auto-spacing` 和 `tunnel-portals`，共 10 个完整作者场景。
前者保存自动间距和平滑组；后者执行 portal-only 写回、undo、JSON restore、redo，再按真实 holes 跳过地形面。
对应契约见 `docs/dev/2026-10-14-road-spacing-smoothing-portals.md`。Unity 渲染仍延后，不能把 EV 实帧当作 Unity 对齐结果。


## Licensed Unity validation, 2026-10-14

Unity 6000.0.79f1 now runs the real outer-package adapter. All eight cases/two views pass
0.98 silhouette IoU. The adapter reflects world/camera Z for handedness, rotates actual T/X
prefabs to match incident arms, and excludes only the measured surfaceMaterial terrain fill.
The fixture explicitly matches measured 5.5 metre prefab socket radii. Marker width and roll
use real SetWidth/SetMarkerTilting APIs. Reference positions are exported in core coordinates.

T concave returns and cubic width interpolation were corrected from these measurements.
New RoadStyle widths default to CatmullRom; old snapshots without a policy retain Linear.
Read `docs/dev/2026-10-14-road-unity-alignment.md` for scope and remaining advanced paired tests.
Re-run changed EV cases with `render-ev --case tee --case width-profile`; adapter/projection
changes invalidate old Unity evidence. Advanced prefab/terrain fixtures remain candidate-only.

2026-10-14 full-turn calibration adds `full-roll-positive` and `full-roll-negative`
(32 cases total). All EV views explicitly use a retained, single-sided unlit material
and the forward path (`gbuffer` and `gpuDriven` disabled). The reference uses
Unity's `Unlit/Color`. The renderer SDK's default G-buffer path produced visible
back faces despite an unlit/single-sided material; that observed production-path
gap remains open in `docs/dev/2026-10-14-road-full-roll.md`.
These two fixtures author .375 metre parameter-scan sampling rather than fixed
counts, matching reference tessellation. EV evidence validates the render adapter,
capture/startup/hook sources and generated game inputs; changed or missing inputs
require recapture. This remains surface calibration, not full rendering parity.

## Rigid side-object root pose reference

`UnitySideObjectContractProbe.Run` accepts `-sideObjectContractOutput <json>` and
`-sideObjectSourcePose identity|translated|rotated|scaled|combined|child`.
Run each case sequentially in the licensed outer-package project; save the five
independent reports/logs as `build/road-parity/side-pose-<case>.json/.log`.
The probe uses public metadata, public data records/factories and SDK calls only.
It creates a real prefab asset and enables the public edit-mode side-object build
switch. The SDK instance getter returned an empty array despite seven generated
renderers, so measurements traverse the actual ERSideObjectInstance hierarchy.

Run `python scripts/verify_road_side_pose.py --output build/road-parity/side-pose-evidence.json`.
This checks complete executions, nonempty geometry, root pose and child hierarchy,
and records hashes of all reports/logs. Current straight-road reference overrides
source root translation/rotation, preserves its scale and preserves child local pose.
The measured spacing is 5.005 for requested 5, with final Z=30.03 on a 30m road;
the tool records that discrepancy without interpreting its cause. It verifies
reference behavior only, not EVEngine parity or other alignment modes.

## Rigid prefab paired-frame calibration

The independent unit-cube facility fixture uses seven actual EasyRoads instances.
The native probe runs FBX import -> EVA -> evpack -> prefab admission -> standard
PointSet publication -> ECS renderer -> engine-owned readback. It now authors a straight 30m road and calls the standard road recipe with requested
5m Average spacing, Center origin and .02m height. The admitted prefab bounds and
asset/build/root-pose metadata flow through the road producer into the standard
PointSet publication. Seven generated facilities are rendered; there are no
handwritten placement positions. The previous measured-point fixture remains
historical evidence, not the current probe.

Build `road_side_graphics_probe` with the existing MSVC wrapper and host profile.
Copy both `UnitySideObjectContractProbe.cs` and `UnitySideObjectFrameCapture.cs`
into the reference project's `Assets/Editor`, then run the former's `Run` method
with `-sideObjectSourcePose combined`,
`-sideObjectContractOutput <root>/side-render-combined.json`,
`-sideObjectFramePrefix <root>/side-render-combined`, and
`-logFile <root>/side-render-combined.log`. Wait for the logged completion and
exit code; Unity's Windows launcher returning is insufficient.

Run the native capture and compare with:

```powershell
python -X utf8 scripts/compare_road_side_frames.py --run-native build/road-parity/host-profile/test/road_side_graphics_probe.exe
```

The checker binds reference frames to hashes written inside the actual Unity
run, verifies current adapters and the loaded reference assembly identity, and
checks the native executable/source/DLL search-directory inventory before and
after capture. This inventory is not an OS loaded-module trace. Changed frames,
sources or runtime inventory require a fresh capture.

Current 640x480 perspective and top frames both have silhouette IoU 1.0 and
maximum RGB error 0. Unity Gamma Unlit/Color is explicitly calibrated against
native linear HDR input, tone mapping None and sRGB presentation. Default native
ACES output is intentionally retained in `side-render-native-*.png` as evidence
of the default display difference. The calibrated capture uses
`side-render-native-calibrated-*.png`; results are in `side-render-comparison.json`.
The comparison rejects even one channel's 1/255 error despite a perfect mask.
This is one rigid-prefab calibration case, not full road/facility/material parity.

## Measured spacing samples

`UnitySideObjectContractProbe.Run` also accepts `-sideObjectSpacing`,
`-sideObjectRoadLength`, and `-sideObjectAverageDistance true|false`.
It exports requested settings, public road length/distance and centerline points.
Ten recorded fixtures are verified with `python scripts/verify_road_side_spacing.py`.
For these straight/default-offset samples, fixed spacing starts at zero and excludes
an exact end point. Average spacing uses at least one interval, nearest-even interval
count and an extra .005m per interval; requests 2m, 5m and 7m were measured.
The reference verifier remains independent and does not certify the native producer.
Native RoadStyle now exposes Fixed and Average distribution, with Average for new
authoring and LegacyCentered for snapshots missing the field. Native contract tests
compare all ten straight longitudinal samples within .0001m, plus recipe consumption,
budgets, exclusions, presets, save and undo. Average free ends extrapolate their frame;
authored exclusions and junction sockets clip overshooting anchors. New policies use
512 arc probes per span; LegacyCentered retains its historical 32 probes and phase.
Curve/offset/connection reference semantics, full XYZ alignment and the default
renderer display pipeline remain differences. The paired renderer fixture now also verifies one straight 30m/requested-5m
Average Center recipe through actual GPU output. Its nativeRoadSpacingVerified
flag requires both the production-route marker and passing fixed pixel thresholds;
this is one scoped fixture, not all spacing/alignment policies.


## Independent position and offset measurements

Copy `UnitySideObjectOffsetProbe.cs` into `Assets/Editor`, then execute
`UnitySideObjectOffsetProbe.Run -sideOffsetOutput <root>/side-offset-contracts.json`
with a log at `<root>/side-offset-contracts.log`. This independent eight-case
experiment preserves the existing spacing/frame adapter hashes. It requires seven
real renderers per case and records every position and Euler rotation. Run
`python -X utf8 scripts/verify_road_side_offsets.py` after terminal Unity exit 0.

For a flat width-6 road, position 0/1/2 measures center/left asphalt edge/right
asphalt edge; xPosition adds lateral offset, yPosition adds to .02m, and yRotation
adds yaw. Center-relative x2 is indistinguishable from x2 in this fixture. Likewise
align1 cannot establish alignment behavior on curves or grades. The generated
`side-offset-evidence.json` explicitly does not certify native parity.


## Explicit yaw coordinate conversion

The offset probe uses source scale (2,3,4). Add
`-sideOffsetFramePrefix <root>/side-yaw-reference` to its Run invocation to capture
the yaw45 sample with the existing engine-owned Unity frame adapter. All eight
position/rotation samples are rerun and hash-bound to the updated adapter.

Run the native production-route comparison and real wrong-sign control with:

```powershell
python -X utf8 scripts/compare_road_side_yaw.py --run-native build/road-parity/host-profile/test/road_side_graphics_probe.exe --run-negative-control
```

Unity yaw +45 maps to native world-Y yaw -45 under the existing Z reflection.
The author field `RoadSidePlacement.yawDegrees` remains the native convention;
conversion belongs at the external coordinate boundary. Both calibrated views
match all RGB pixels. The same recipe with deliberately wrong native +45 fails
with IoU .553875/.383122, without changing thresholds, geometry or display.
Reports are `side-yaw-comparison.json` and `side-yaw-wrong-sign-comparison.json`;
native frame evidence includes the executable, source and runtime search hashes,
command mode, log and frame hashes. This proves one 45-degree straight-road case,
not arbitrary-angle, graded/curved alignment, collider or default-display parity.


## Grade and curved facility alignment

Run `UnitySideObjectOrientationProbe.Run` with
`-sideOrientationOutput <root>/side-orientation-contracts.json`,
`-sideOrientationFramePrefix <root>/side-orientation-reference`, and a log at
`<root>/side-orientation-contracts.log`. It records eight public-SDK cases, actual
markers/strengths, left/right/center points and renderer forward/up vectors.
`python -X utf8 scripts/verify_road_side_orientation.py` verifies source identity,
terminal execution, boundary midpoints, center-polyline length, per-anchor position
and segment orientation. No private assembly implementation is read.

```powershell
python -X utf8 scripts/compare_road_side_orientation.py --run-native build/road-parity/host-profile/test/road_side_graphics_probe.exe
```

This currently returns 1: grade align2/native PathForward passes both calibrated
views exactly; curve differs at fixed .98 IoU / 0 RGB error thresholds. Results
are preserved in `side-orientation-comparison.json`, not skipped. Native placement
CSV files are hash-bound to the runtime evidence. The curve's .07525m maximum
position error and initial heading difference expose a generated-boundary versus
analytic-path consumer gap. Grade0/1 remains upright in the reference; grade2/3
follows the slope. Flat-curve results cannot label all alignment modes generally.
The eight-case reference experiment reconstructs placement from the midpoint
polyline of public left/right road boundaries; it does not certify native parity.


## Isolate the curve boundary correction

`UnitySideObjectOrientationProbe.RunBoundary` accepts
`-sideBoundaryOutput <root>/side-boundary-contracts.json` and a log at the same stem.
Run it in a second fresh Unity process with `side-boundary-repeat.json/.log`, then:

```powershell
python -X utf8 scripts/verify_road_side_boundary.py --repeat-prefix side-boundary-repeat
```

Thirteen controlled cases isolate widths2/6/12, resolutions1/2/4, two seeds, and
source Z scales1/8. Unity Random state is restored after every case. Geometry is
unchanged before/after facility generation, across seeds/source lengths in these
samples, and effectively unchanged across width. Resolution changes the retained
rows and the near-end corrections. Public tValues reproduce middle Catmull points;
excluding the first/last two measured rows is a verifier scope, not a production
endpoint rule. `endpointRuleConfirmed=false` remains explicit. Independent process
geometry hashes must match before the repeat is certified. Native curve parity
remains a failing comparison; no measured points are fed into native production.


## Native retained-surface facility path

Set `RoadStyle.sidePlacement.pathSampling = RoadSidePathSampling::SurfaceSegments`
to follow actual retained road section centers. Missing snapshot fields retain
`Analytic`. The whole placement rule participates in presets, canonical snapshots,
undo/redo and standard recipes; producer identity is v14. Geometry and placement
share one bake-owned frame cache and scan budget. Visible clipped/blended rows
control anchors and conflicts, and actual row vectors/widths control asphalt-edge
positions. No measured Unity points or fitted endpoint corrections enter this path.

Five strong contracts execute in both the host SHARED suite and the independent
STATIC `road_surface_path` test (143 assertions). The CPU boundary list includes
`RoadSurfacePath.cpp`; core OBJECT evidence is L1, standalone STATIC is L2.
The current curve comparison improves to .989005/.982464 IoU but still returns
1 because RGB error remains nonzero. Grade stays pixel-identical.

Full road regression must inspect detailed assertion output: current 169 CTest
zero exit codes conceal 13 cases with warning assertions/exceptions. See
`surface-path-road-warning-audit.json`; do not report the whole suite green.
New SurfacePath helpers return Result so precondition failures are checked in
the test body instead of escaping the test context as helper-only assertions.


## Road editing NoOp contracts

`test/procgen_road_noop.cpp` holds five strong cases, including the two moved
legacy reconnect/connect-turn regressions. Both host SHARED and independent
STATIC `road_noop` execute102 assertions without warnings. Successful NoOp carries
a canonical idempotent payload; registered command plans omit its operation.
Direct idempotent application also returns NoOp without dirtying the target.
Same-endpoint reconnect-and-connect uses a turn batch when links are missing,
with an exact remove inverse. No new serialized operation type is introduced.

The current full road run has172 CTest cases and11 cases with warning assertion
failures; see `road-noop-warning-audit.json`. Zero process exit remains insufficient
for whole-suite acceptance. The new NoOp and SurfacePath scopes are checked
separately with strong assertions and detailed output.


### Junction seam metric and coplanar through fill (2026-10-15)

Producer revision is now `road-core-2026-10-15-junction-seam-metric-v15`.
Junction mouths use the same 24-probe spline metric as trimmed edge geometry;
general-junction through-road fill retains exact mouth coordinates instead of
adding a 0.01m lift. Three strong cases in `procgen_road_junction_seams.cpp`
cover sloped mouths, flat split plane/marking suppression, and mixed profiles.
SHARED and independent STATIC each execute43 assertions without warnings.
See `junction-seam-shared-tests.log` and `junction-seam-static-tests.log`.
This does not prove every two-arm node bypasses junction generation, eliminate
all overlap, validate the axis-cross lift, or establish current render parity.


### Axis-cross apron plane (2026-10-15)

Revision `road-core-2026-10-15-cross-apron-plane-v16` removes the 0.01m
axis-cross asphalt lift and the 0.012m seal-strip lift. Five seam/constraint
cases run55 strong assertions in SHARED and STATIC. The original nonplanar
cross input has a confirmed bounded-construction-plane rejection; equivalence
to EasyRoads is still unconfirmed. `cross-plane-before.log` records the measured
planar-apron failure before the fix; `cross-plane-*-tests.log` records final tests.


### Ring island and authored lane frame contracts (2026-10-15)

`procgen_road_roundabout.cpp` checks both the open central island and covered
one-lane ring for the span48 factory (30 strong assertions). The old mixed
scenario expected island probes to be asphalt. `procgen_road_reference.cpp`
checks asymmetric lanes against the authored side axis (world-up cross forward),
including four horizontal orientations (285 strong assertions). These scopes
execute315 assertions in SHARED and independent STATIC without warnings.
Production geometry and lane algorithms are unchanged. This is not evidence of
EasyRoads LHT/RHT support, exact circular geometry, or complete ring-entry coverage.
See `road-ring-lane-shared-tests.log`, `roundabout-island-static-tests.log`,
and `road-lane-frame-static-tests.log`.


### Marking world-distance metric (2026-10-15)

Revision `road-core-2026-10-15-marking-world-distance-v17` accumulates physical
3D centerline segment length for dashes, retaining the authored trim-start phase.
The rendering interpolation and dash interval now share the emitted-polyline
metric. The hardened marking case covers 40m flat and 50m graded lines, legacy
and 2m metric sampling, distinct coarse/fine legacy meshes, and budget rejection.
SHARED and independent STATIC each execute22 assertions without warnings:
`marking-distance-shared-tests-final.log`, `marking-distance-static-tests-final.log`.
This does not prove reference curve length, endpoint phase, or cross-edge phase
parity. No new author field or persistence migration is introduced.


### Author-section cap area (2026-10-15)

Revision `road-core-2026-10-15-section-cap-area-v18` classifies cap fan
collinearity in width-scaled author (side,up) coordinates using double precision.
The existing 1e-12 squared-area gate is retained; world-coordinate subtraction
no longer creates false area for author-collinear cap points. The generic quad
emitter is unchanged. The ten-scene no-degenerate case is now a strong aggregate
assertion, with diagnostics for every offending triangle. Another case checks
that valid terminal caps survive translation and four horizontal orientations.
SHARED and STATIC each execute101 assertions without warnings. See
`road-sliver-before.log`, `road-sliver-shared-tests.log`, and
`road-sliver-static-tests-final.log`. No new render-parity claim is made.


### Node move geometry preflight (2026-10-15)

`makeMoveNode` now runs native geometry validation on a private candidate before
producing the forward/inverse operation. The degenerate-move case explicitly
uses two controls, avoiding the three-control straight factory's retracing path.
Two cases test atomic rejection and distinct node identities at coincident
positions, with save/restore and undo/redo snapshots. SHARED and STATIC each run34
strong assertions without warnings (`road-node-geometry-*-tests.log`). Retracing
road constraints and reference closed-seam behavior remain unconfirmed; no new
blanket coordinate-coincidence restriction is imposed. Bake revision stays v18.


### Road terrain originY mapping (2026-10-15)

RoadTerrainConformOptions/RoadTerrainTile now map worldY as originY + sample *
heightScale. Workspace ground masks use the same mapping as height writes.
Terrain captures use schema v2 with required originY; v1 histories migrate missing
fields to zero. Mapping changes within retained history and malformed values
reject atomically. Two cases run55 assertions in SHARED and STATIC, including
all-channel masks, JSON, undo/redo, v1 multi-frame migration and receipt restore.
See `road-terrain-origin-*-tests*.log`. Native World still uses zero Y baseline;
whole-World vertical translation, receipt owner qualification and Unity terrain
render parity remain open. Mesh bake revision is unchanged (v18).


### Native World baseline and real VM (2026-10-15)

TerrainWorldCreationSettings.originY owns the world baseline; getTileOriginY is a
checked derived query. Road projection and all six direct/staged tree/object/probe
descriptor paths consume it. setHeightWorldUnits subtracts the baseline before
Clamp01. Explicit rule heightScale and absolute sea/custom modes are retained.
World bindings move from the large TerrainStampScript TU into TerrainWorldScript,
with registration order preserved. Three CPU cases run210 assertions in SHARED
and STATIC; the real Squirrel VM case adds2 framework assertions plus in-script
create/query/set-height/undo checks. See `world-origin-*-tests*.log` and
`world-origin-shared-build.log` (actual new TU compilation). This supersedes the
previous section's zero-baseline-only native World limitation for CPU operations;
render/physics adapter coverage, complete World persistence and Unity comparison
are still open. Road mesh revision stays v18.


### Empty object candidate occupancy (2026-10-15)

Single/multi-tile object placement reserves a collision center only after a child
is emitted. Multi-tile continuous coordinates are rejected before the existing
nearest-even lookup, preventing small negative coordinates from aliasing pixel0.
The public multi-tile function now exports correctly to Windows DLL consumers.
The before-fix negative control records singleX=1.9 / multiX=-0.1; both must be0.9.
A separate interior fixture retains the original eight-child expectation and
budget-failure preservation. Two cases live in `terrain_object_collision.cpp`;
STATIC executes13 strong assertions without warnings. Reference collision parity
and actual prefab collider behavior remain unconfirmed. See
`object-empty-occupancy-before-tests.log` and `object-empty-occupancy-static-tests.log`.

### Unity terrain vertical origin reference (2026-10-15)

`UnityTerrainOriginProbe.Run` creates two independent 65x65 TerrainData scenes,
64x10x64m, normalized initial height0.5, terrain origins Y0/Y100 and a six-meter
wide straight road one meter above the initial surface. It calls the documented
BuildRoadNetwork(false,false,false) and RestoreRoadNetwork public API. The final
JSON/log are `unity-terrain-origin-final.json` / `unity-terrain-origin-final.log`
under build/road-parity. Both cases must actually change terrain, reach center0.6
within1e-4 and restore the full original grid within1e-4; translated full grids
must agree within1e-4. This is height/build/restore evidence, not a rendered frame
or a claim that surrounding interpolation and all-channel writeback match.

### Unity terrain channels reference (2026-10-15)

`UnityTerrainChannelsProbe.Run` / `-terrainChannelsOutput <absolute.json>` runs
six independent scenes: all disabled, each of splat/tree/detail independently,
all enabled, and splat opacity0.5. The diagnostic report/log are
`unity-terrain-channels-diagnostic.json` / `.log` in build/road-parity.
Enabled splats change288 samples (center1, or0.501960814 at opacity0.5);
tree count2 becomes1; detail sum16384 becomes13888. Disabled channels retain
their measured values. Texture weights and every detail sample restore exactly.
The strict final probe intentionally exits1: with tree clearing enabled, restore
reorders trees and restores the removed tree at normalized Y0.6000122 instead of
its original0.5. Full original/restored tree fields are retained in the report.
This is a measured reference difference, not a passed full-channel restore gate.
The synthetic tree has a shader warning; this experiment makes no tree visual
claim. Native independent splat opacity and texel expansion/smoothing remain gaps;
do not reproduce the reference's observed inexact tree restore in our snapshots.

### Native road terrain opacity (2026-10-15)

RoadTerrainWriteOptions appends finite [0,1] splatOpacity (default1 preserves
existing calls and positional aggregate meanings). The canonical all-tile apply
multiplies only splat coverage by opacity and raises the selected weight only
when greater than its current weight; other layers keep relative proportions. It does not quantize to
Unity's byte texture format. Zero still executes independently selected height
and vegetation writes. Invalid input rejects before any publication or history
change. Native TerrainWorldRoad forwards the same option into its private
transaction. Existing schema-v2 terrain history saves resulting complete weights;
the transient build option adds no snapshot field or mesh revision.
The three new tests cover three-layer mixed weights, normalization, untouched
samples, zero/full paint, independent vegetation, JSON/save/load/undo/redo,
NaN/infinite/out-of-range atomic rejection and real native World consumption.
STATIC:3 cases/106 strong assertions, including weaker paint preserving existing weights. Author/editor option persistence, Squirrel
road writeback bindings, texel expand/smooth and full World persistence remain
open; this closes the C++ paint-strength path, not the full authoring requirement.

The final reference expands to nine scenes, including initial target0.3/0.8
and three layers0.2/0.3/0.5. See `unity-terrain-opacity-reference-verified.json`:
opacity0.5 raises initial0.3019608 to0.5019608, preserves initial0.8, and yields
three center weights0.1411765/0.5019608/0.3568628. Native unquantized equivalents
are0.1428571/0.5/0.3571429. This refutes ordinary lerp-toward-one semantics;
the native implementation and mixed-layer tests use the observed raise-only
target rule. Full feather/texel footprint parity remains unproven. The report
separates all-scene write checks from strict tree restore, which still fails.

Final native SHARED regression:222 cases (188 road),268000 passed assertions,
all case/assertion tables audited without warnings/failures/skips. Evidence:
`terrain-opacity-evidence.json` and `terrain-opacity-regression-audit-final.json`.
Full `make check` final run passes format/layering/ten-profile matrix and fails
the same seven architecture findings in IStateProvider, RenderSystem, Sensing,
and Vehicle; see `terrain-opacity-source-check-final.log`. No waiver or baseline
change was added. Road mesh producer remains section-cap-area-v18.

### Script road terrain writeback (2026-10-15)

TerrainWorldScript registers VM-owned RoadTerrainWriteOptions, all eight channel
properties, a checked positive-integer setMaximumWork and its query, plus
TerrainWorldWorkspace.conformRoad and copyHoles. Both new consumers inspect
ssq objects and the existing stableTypeHash before taking synchronous typed
borrowed references; null, primitives and unrelated instances return standard
Result diagnostics rather than unsafe third-party pointer casts. copyHoles
publishes its owning raster into the caller's output only after validation.
The integration test uses full ModuleManager registration, a real RoadNetwork
bake and the production ProcgenMeshBuild class in a real Squirrel VM, covering
nonzero origin, opacity, holes, undo/redo, null/wrong types and budget failures.
`terrain-road-vm-test-final.log`:1 case/12 framework assertions plus in-script
assertions; no warning/failure/skip. No substitute test-only class registration.
Author/editor option persistence, complete World save and GPU frames remain
open. This supersedes the earlier missing script-writeback limitation; it does
not add a persistent options owner or new terrain/mesh schema revision.

Final evidence: `terrain-road-vm-evidence.json`. The full223-case SHARED run
passed268010 framework assertions with no warnings/failures/skips; a later
test-only extension validates the owned script mesh producer and raises that
VM case from10 to12 framework assertions, independently rebuilt/rerun. It is
not represented as part of the earlier full log. Final make check retains the
known seven architecture findings, with format/layering/ten profiles passing
and no Git command errors. Initial lock-error gate remains historical only.

### Road script native proxy lifetime (2026-10-15)

Road argument borrowing now recognizes the existing NativeProxyReleases record
without dereferencing the native pointer, then re-resolves its original typed
generation/ownerEpoch through the live Procgen owner. Released MeshBuild inputs
and Heightmap outputs return StaleHandle before any World/output mutation.
No second registry, release path or mutable owner was introduced. Explicit
standalone borrowed values retain their caller-owned lifetime requirement.
The real VM test releases an owned road mesh, rejects the old VM instance,
builds and consumes a fresh owned mesh, and releases a generated Heightmap
before rejecting it as copyHoles output. Existing undo history remains valid.
See `terrain-road-lifetime-test.log`:1 case/12 framework assertions plus the
expanded in-script lifecycle assertions. No full module teardown or allocator
address-reuse guarantee is inferred from this focused lifecycle experiment.

Final `terrain-road-lifetime-evidence.json`:223 cases/268012 passed framework
assertions, no warnings/failures/skips. A test-only precise stale_handle-code
supplement then passes1 case/12 framework assertions plus script assertions;
the production implementation is unchanged. Full make check passes format,
layering and ten profiles, retaining the known seven architecture findings;
no Git command/lock error. No exception, skip or baseline was added.

### Per-road terrain policies and atomic batch (2026-10-15)

`UnityTerrainRoadPolicyProbe.Run` / `-terrainPolicyOutput <absolute.json>`
uses two disjoint real roads with target layers1/2 and opacity0.4/0.8.
One all-road build changes576 texels with center weights(0.6,0.4,0) and
(0.2,0,0.8). Each selected-road build changes288 texels and leaves the other
center(1,0,0). Full texture restore error is0. Three cases pass with Unity
exit0; see `unity-terrain-road-policy-evidence.json`.

Native RoadTerrainWrite contains a synchronous const mesh reference and copied
options. RoadTerrainWorkspace.applyBatch and TerrainWorldWorkspace.conformRoadBatch
publish all ordered policies in one complete-channel history frame. Single
writes delegate to this canonical batch path. The shared work cap includes all
roads/tiles; each item retains its own cap. Late missing-layer and budget errors
preserve every channel and redo branch. Summed changed-element counts reject
overflow before publication. Existing terrain schema-v2 saves the resulting
frame/history, not the transient borrowed requests or author policy inputs.
Three STATIC cases/76 strong assertions cover independent policy centers,
selected subset, one-step undo, JSON/save/restore/redo, late failure, independent
and shared caps, and real native World consumption. The final SHARED selected
regression is226 cases (192 road)/268088 assertions, all tables clean.
See `terrain-batch-regression-audit.json`.

These fixtures use different extents/resolutions: only policy/selection and
transaction behavior are compared. Full raster footprints, overlapping-road
priority, author preset persistence and a script batch entry remain open.
There are no new GPU frames or road mesh revision. The schema/budget/ownership
contracts are updated without a new provider, ECS type or mutable policy owner.

Final evidence: `terrain-batch-evidence.json`. Full source gate retry passes
format/layering/ten profiles and retains the known seven architecture findings;
the final log contains no Git command/lock errors. The first lock-error run is
historical only. No baseline, exception or skip was added.

### Script batch production consumer (2026-10-15)

TerrainWorldWorkspace.conformRoadBatch(meshes, policies, maximumWork) admits
two bounded equal-length arrays, each mesh paired with its owning options value.
The input arrays root all VM instances until the synchronous native transaction
returns; typed mesh borrows never escape, and options are copied. Budget kind
must be INTEGER, positive and within native size_t range: no bool/float/string
coercion or 32-bit narrowing. Every source/policy type and owned mesh generation
validates before World dispatch. Diagnostics identify meshes[i]/policies[i].
No table property callbacks or new registry/author state were introduced.

`terrain_road_batch_vm.cpp` uses full module registration and two independent
networkSnapshot→buildMesh(mesh.roadNetwork) owned production meshes. The real
VM verifies layers1/2 with opacity0.4/0.8, one history frame/undo, redo retaining
old weights after options mutate, malformed/empty/oversized/mismatched arrays,
strict budget kinds (including a valid2147483649), per-item/global exhaustion,
and a released second mesh returning exact stale_handle at meshes[1].
The focused test passes1 case/4 framework assertions plus its script assertions.
This closes the script batch consumption gap. Author preset policy persistence,
geometry attribution, overlap reference semantics and rendered-frame parity
remain open. Native terrain/mesh schema revisions are unchanged.

Final evidence: `terrain-batch-vm-evidence.json`. SHARED selected regression:
227 cases (193 road)/268092 passed framework assertions, no warnings/failures/
skips. Full make check passes format/layering/ten profiles and retains the same
seven architecture findings, without Git command/lock errors. Reference DLL
hash still matches the previously measured three per-road policy cases.


2026-10-15 geometry sources: producer v19 adds bake-local Edge/Junction/NetworkDerived intervals on the full road mesh. PreparedRoadGeometrySources validates before returning an owning snapshot; chunks and historical unattributed meshes require regeneration. Artifact-store mesh snapshots now preserve RGBA; missing historical colors remain empty and malformed known colors reject restore atomically. Five STATIC focused cases / 101 assertions currently pass. This is not author terrain-policy persistence, policy projection or new rendered parity evidence. See the comparison log for remaining scope and SHARED/gate status.

Final verification: SHARED combined 244 cases / 268325 assertions clean, including 198 road cases; STATIC focused 5 / 101 and artifact regression 3 / 207 clean. Full make check still fails on the same seven existing architecture findings; changed-line format, layering and ten-profile matrix pass, with no Git command errors. Evidence: build/road-parity/geometry-sources-evidence.json.

## 2026-10-15 shared material render comparison

The current 35 surface fixtures were recaptured in both Unity and EVEngine:
35 scenes x 2 views x 2 renderers = 140 actual frames. Run:

```powershell
python -X utf8 scripts/road_material_parity.py prepare --root build/road-parity/material-35 --project build/road-parity/unity-project --glslc C:/VulkanSDK/1.4.357.0/Bin/glslc.exe
```

Then run Unity `-executeMethod RoadParityCapture.RunMaterial`, with
`-roadParityRoot build/road-parity/material-35/fixtures`;
capture every generated native game with `scripts/capture_example.ps1`.
Only after both capture jobs finish, audit:

```powershell
python -X utf8 scripts/road_material_parity.py audit --root build/road-parity/material-35 --project build/road-parity/unity-project --unity-log build/road-parity/material-35-unity-final.log --eve build/road-parity/host-profile/src/engine/eve.exe
```

Material source and settings are in `scripts/road_parity/material/`. Both
backends share the same shading math, raw textures with explicit albedo decode,
0 EV, no tone mapping, camera, lighting, background, depth write and back-face
culling. Unity image V is reflected to match native texture orientation.
The material samples the interior of the road texture, removing its embedded
white border so geometric markings remain the sole marking source. Mirrored
world triplanar projection and tapered normal detail avoid material seams;
this deliberately does not modify or establish correctness of authored UVs.
This is an explicit forward material, not proof of default deferred/G-buffer,
GPU-driven, terrain, facility or all production-material parity.

Result: 31/35 pass unchanged thresholds (both views: surface IoU >= .98,
interior RGB MAE <= 2/255, P95 <= 5/255, background error <= 1/255).
Remaining differences: circular mirror and circular mirror zero-bump have
perspective IoU .970314/.972165; positive/negative full-roll now pass silhouette
IoU but retain RGB MAE 4.05-6.63/255, P95 11-16/255. Their shading is unresolved.
No threshold was relaxed. Report: `build/road-parity/material-35/material-comparison.json`.
Gallery: `build/road-parity/gallery-35/material-35.html`; five material overview
sheets and four full native before/after previews are beside it.

GLSL compilation, Unity batch compilation/capture and all 70 native engine
captures succeeded; the report checks dimensions, geometry SHA, capture markers,
nonempty surfaces, image/source/runtime hashes and computes fresh metrics.
Full `make check` still fails on the same seven existing architecture findings
(IStateProvider x3, RenderSystem lifetime x2, sensing catalogue, vehicle runtime
lookup). Changed-line formatting, layering and ten profile configurations pass.
No public C++ API, persistence schema or module boundary changed in this material
work; no architecture exception or baseline was introduced.

## Optional-component material previews (2026-10-15)

`python -X utf8 scripts/road_component_preview.py prepare` runs the current
`road_authoring_fixture` generator and creates 11 full native authoring scenes,
each in perspective and top views. Capture the 22 generated games using
`scripts/capture_example.ps1 -Frames 24`, writing `frame.png` and `capture.log`
inside each game directory. Then run:

```powershell
python -X utf8 scripts/road_component_preview.py publish
```

Output: `build/road-parity/gallery-35/components.html`,
`components-overview.png`, and `build/road-parity/component-preview/evidence.json`.
This contains existing crowned/closed sections, bridge/tunnel intervals,
continuous rails, custom connections, prefab bending/spacing/smoothing,
multi-arm facility routes, terrain channels/portal holes, and shared presets.
The source generator exercises recipe/direct-bake equivalence and existing
save/undo/restore paths before producing geometry; frames alone do not prove
those behavior contracts or terrain-channel correctness.

All groups use the shared explicit forward rough-dielectric shader, with a
reference asphalt texture and solid category colors for other materials.
No new production metal/concrete/vegetation texture assets or shadows are
claimed. Tone mapping/exposure/raster state match the road material experiment.
The component gallery is native-only: it does not establish Unity component
parity and must not inherit the 31/35 surface-only score. The renderer retains
mesh, material and texture lifetimes until scene teardown. No engine public
API, persistent schema, module boundary or lifecycle contract is changed.

## Optional-component consistency comparison: current limitations

The native-only 11-scene preview is not reference parity evidence. The canonical
`components.html` now points to a real comparison produced by
`scripts/road_component_parity.py`; the previous native gallery is retained as
`components-native-preview.html`.

Reproduce: `road_component_parity.py prepare`, capture the 22 native games with
`capture_example.ps1`, run Unity `-executeMethod UnityComponentCapture.Run
-componentRoot <absolute component-parity/source>`, append the actual process
`returncode=0` to the reference log only after successful termination. Then run
`road_component_parity.py diagnostics`, capture its twelve native games, and
`road_component_parity.py publish`.

The independently generated Unity meshes currently support six actual pairs:
crowned section, closed concave beam, continuous rail, bent prefab,
automatic-spacing prefab and shared presets.
Full-scene color differences also contain unmapped marking/material assignments;
the additional diagnostic images remove markings and use identical materials on
sections, or isolate fences with identical category material. No engine shading
conclusion can be inferred from the full-scene difference alone.

Five remaining entries are explicitly unpaired: bridge/tunnel, custom connection,
multi-arm facilities, terrain channels and portal terrain lack a complete
independent reference adapter. Missing references have no score. They are
adapter gaps, not evidence that EVEngine lacks all five
features. Prefab fixed-copy count, end caps, smoothing and automatic-spacing
settings still require further reference calibration; current geometry mismatches
must not be attributed entirely to production-engine behavior.

Six paired complete scenes are all outside the established thresholds. The
isolated crown has perspective/top IoU .988568/.999730, RGB MAE 2.0329/.4557;
the closed beam has .969623/1.0, RGB MAE 1.9328/.4506. Isolated prefab fences have
perspective IoU .40805/.47043 and top .14248/.15263. Thin top-view fences have no
remaining common area after two-pixel erosion: report RGB error as null/N/A,
never zero or passed. These are measured render differences with explicit
projection limits, not a complete functional acceptance score.

Public SDK metadata/API and genuine generated geometry only; no IL/private
method-body inspection and no native generated mesh used as the reference.
The public ERMesh constructor prepares prefab source data; activation is checked
using SDK getters because setter false can indicate unchanged state. Source
world Z and prefab-local side are reflected explicitly; the already calibrated
roll convention preserves the angle sign. Shared forward shader and 0 EV/light/
camera match the material experiment; shadows remain disabled.

Group-index metadata is an ephemeral fixture record with schema
`eve.tests.road-component-groups` v1. Unknown fields are ignored, unsupported
schema/version rejected. It is not a production authoring/persistence format.
The 31-step forced fixture rebuild and final two-step adapter rebuild succeeded;
recipe/direct-bake equivalence still executes for all 11. Four comparison
rejection/measurement tests and ten UTF-8 tests pass. No public engine API,
production persistence schema or module boundary changed; no architecture
exception is introduced.

Reference documentation consulted:
https://easyroads3d.com/v3/html/side_objects.html
https://easyroads3d.com/v3/html/ERRoad.html
https://easyroads3d.com/v3/html/ERRoadNetwork.html

The shape cache prerequisite was resolved experimentally through the public
ERMesh constructor. A hidden Unity cube supplies its required Mesh argument;
its renderer is disabled, and generated rail bounds independently confirm the
authored .36m x 1.1m profile, not cube geometry. Continuous rails now have
isolated perspective/top IoU .96852/1.0, RGB MAE .4865/.03798. Shared presets
remain different (.55481/.09517); the full preset/marker material/width/tilt
projection is not complete. Reference passes and failed isolated probes are
kept separately; no failed probe frame is used as accepted reference evidence.
Full make check completed with the same seven existing architecture findings;
changed-line formatting, layering and profile matrix pass. No exceptions or
baseline additions.

## Unity junction reference scenes (2026-10-15)

`python -X utf8 scripts/road_junction_parity.py prepare` generates five fresh
candidate scenes: junction-tee, junction-cross, junction-roundabout,
tee-facilities and multi-arm-facilities. It uses the current native author
fixture executable, which also checks recipe/direct-bake geometry equivalence.
Run Unity with `-executeMethod UnityJunctionCapture.Run -junctionRoot
<absolute build/road-parity/junction-parity/source>`, capture all ten native games
using the engine-owned capture script, then run a separate Unity process with
`-executeMethod UnityJunctionCapture.VerifySaved` and the same root argument.
Record each actual process exit code in its own log. Finally run
`python -X utf8 scripts/road_junction_parity.py publish`.

Output: junction-parity/comparison.json and gallery-35/junctions.html,
junction-overview.png. Five real .unity scenes and persistent mesh/material
snapshots are under the local reference project's Assets/RoadJunctionParity/.
They are generated, private build artifacts; no packaged assets are committed.
The separate reopen process confirmed all five scene triangle counts and
byte-identical perspective PNGs. Every T/X/roundabout instance has three/four
successful SDK road attachments; final terminal markers match final ports within
.001m. SDK connection updates can move port positions, so the adapter re-reads
both after connection/build rather than checking an earlier port snapshot.
The final cases retained their authored marker counts (no insertion observed).
Requested and actual road widths, marker counts and port coordinates are stored.
The road-network raise offset is explicitly zero.

This is a uniform-material diagnostic: geometric markings are excluded on the
native side, curbs/sidewalks share asphalt material, rails use matching category
color. Exposure, shader math/textures, light, background, raster state and both
cameras are the same. It is not final production-material or traffic acceptance.
T/X factory candidates include native sidewalk/curb geometry that the default
reference road/prefab setup does not fully map. Ring width/radius/port geometry
are also not fully mapped (native radius12 versus reference prefab defaults).
Native explicit facility-route pairs and reference continueOnConnections are
not equivalent configurations yet. These differences must not be attributed
entirely to the engine or promoted to a complete parity score.

Measured perspective/top IoU: tee .69096/.68960, cross .68866/.69040,
roundabout .39731/.39598, tee facilities .97993/.98622, four-arm facilities
.96350/.95553. The retained image thresholds are IoU .98, interior RGB MAE2/255,
P95 5/255; no scene passes both views under the present mappings. Geometry and
reference-setting gaps are shown openly in the gallery. In particular the new
four-arm reference replaces the old claim that no independent reference exists;
its explicit facility-routing correspondence is still open. Custom prefab
connections, arbitrary-arm junctions, bridge/tunnel and terrain combination
adapters remain separate work, not claimed by these five scenes.

Unity input and evidence records have test-specific schema identifiers/version1;
unknown fields are ignored, unsupported input versions rejected. No production
engine API, persistent scene format or module boundary changed. No architecture
exception is requested. MSVC fixture build, Unity capture/reopen, ten engine
captures, four existing image-comparison contract tests and ten UTF-8 tests pass.

Final source gate for junction references: make check completed with the same
seven existing architecture findings (IStateProvider bool operations x3,
RenderSystem lifetime docs x2, sensing catalogue, vehicle runtime lookup).
Changed-line format, module layering and all ten source profiles pass; no new
baseline or waiver. Log: build/road-parity/junction-parity-source-check.log.


#### 2026-10-16 junction author bands

`junction-alignment/` preserves a second genuine five-scene capture alongside
`junction-parity/`. Prepare/publish with `--root build/road-parity/junction-alignment`
and use `junction-alignment-unity.log` / `junction-alignment-reopen.log` for the
corresponding Unity run and separate-process reopen. The gallery now shows this
second run. The reference yaw search includes 45-degree rotations and no longer
exempts roundabouts from its direction check. Other adapter limitations remain.

The native cross fast path now respects authored curb/walk widths, independently
skips disabled junction bands and uses the general per-mouth interpolation for
nonuniform profiles. Explicit bare asphalt no longer gains artificial curb/walk
triangles. Producer identity v20 invalidates old derived artifacts. Five focused
tests cover dimensions, disabled bands, mixed arms, transactional editing,
save/reload and undo (29 assertions). Production SHARED road tests: 208 cases,
267758 assertions, no failures or warnings. Static road tests: 149/11285 clean.
Static artifact tests: 3/207 clean.

Four-arm facility IoU improves .96350/.95553 to .97944/.97920. Correcting the
reference ring orientation changes its IoU to .49675/.49581. No scene passes
both views at unchanged thresholds. Public sidewalk setters reported success
without generated sidewalk instances; public ring radius/width assignment and
ResetData did not rebuild the measured point arrays. Those exploratory probes
are recorded locally, not promoted into the canonical reference adapter.

This iteration completed make check; the same seven pre-existing architecture
findings remain. Format, layering, bindings, manifest and ten source profiles
pass. No baseline/waiver was added. See junction-alignment-source-check.log.


#### Authored Unity sidewalks, ring dimensions and explicit route references

Latest run: `build/road-parity/junction-authored/`; prepare/publish with that root
and `junction-authored-unity.log` / `junction-authored-reopen.log`. Test input and
reference evidence are version2. Production scene/network schema is unchanged.
SDK, primary and helper adapter hashes are verified. The public SDK mesh entry
requires a registered road preset; inspecting its early-return guard located the
missing rt. No SDK geometry algorithm was copied. Obfuscated public generator
names are version-specific and pinned by DLL SHA.

Sidewalks use the SDK factory/instance/generator with authored profile, centerline
samples and half-width. Middle-section referenced vertices verify road-edge and
outer widths, not junction flares. Ring center/inner/outer radii and final width
are checked after connected road builds. Radius12, width3.25; four socket indices
are rebuilt for the new72-segment ring. SDK prefab auto facility continuation is
still unproven. Explicit authored routes instead use hidden-road SDK carriers,
straight adjacent constraints, projected-chord strength conversion and one-meter
facility sampling; only the middle marker segment is active. These are genuine
independent SDK meshes, not a claim of built-in automatic intersection routing.

Both facility scenes pass the unchanged full-scene image thresholds: tee IoU
.99239/.99084; four-arm .98583/.98120. Full-scene coverage includes asphalt and is
not an isolated facility or topology acceptance. T/X corner sidewalks, ring
island/entrance geometry and shading, facility-specific masks and exact seam/cap
contracts remain open. No native production code changed in this iteration.
Five saved reference scenes reopen in a separate process with identical meshes
and pixel-identical perspective PNGs. New generation contract tests5, comparator
contracts4 and UTF-8 tests10 pass. Full source gate retains the same seven existing
architecture findings; no new baseline or waiver.


#### Outer guardrail correction (supersedes unsafe authored route layouts)

Latest evidence root: junction-outer. Right approach direction is reversed; the
four-arm south approach is also reversed. Tee route is right-to-north; four-arm
routes are right-to-north and west-to-south. These pairs stay outside traffic.
Old authored fixture image passes did not prove physical validity. The same
geometric checker detects94/88 native overlapping pairs in the old tee/cross.

Publish now requires SHA-bound actual Unity mesh footprints and triangle-clipped
XZ guardrail/asphalt intersections <=0.0001 square meters per pair. Current two
facility scenes have exactly zero overlaps on both engines. Tests cover touching,
separated, reversed winding and edge crossings with no contained vertices.
Inactive SDK endpoint sections caused zero-position rows and road-center spikes;
short0.05m active guides fix this without deleting bad triangles after generation.
Only joined route caps are disabled. Explicit SDK carriers remain a diagnostic,
not proof of built-in automatic junction routing. All20 frames are recaptured and
all five scenes independently reopened with identical perspective frame bytes.

#### Optional facilities and graded bridges (8 additional cases)

`road_options_parity.py prepare` prepares four optional-component combinations and
four independent crossing bridge scenes (30/60/90/45 degrees; 4/8/12/8 percent;
last case additionally banks 5 degrees). Run UnityRoadOptionsCapture.Run with
-optionsRoot <absolute options-parity/source>, capture all 16 games with
scripts/capture_example.ps1, then UnityRoadOptionsCapture.VerifySaved in a separate
Unity process. Finally `python -X utf8 scripts/road_options_parity.py publish`.
Default evidence root is build/road-parity/options-parity; options-unity.log and
options-reopen.log record process exits. Saved scenes: Assets/RoadOptionsParity.
The default gallery page is options.html, alongside the existing junction pages.

The SDK receives author profiles and source prefabs, never native baked geometry.
Register the whole facility extension list: single-element replacements silently
removed preceding options. Every expected nonempty facility renderer is checked.
Closed bridge decks explicitly enable SDK custom-road flags. The publisher checks
source/adapter/SDK/mesh/scene/frame hashes and saved-scene replay, validates actual
median footprints and samples actual bridge grade and center clearance. Test input,
SDK evidence and comparison report are separately named v1 schemas (unknown fields
ignored, unsupported versions rejected); production author schemas are unchanged.

32 true frames plus 8 separate-process reopen frames. Only the median case passes
both full-scene views; all other failures remain visible. Median footprints have
zero asphalt overlap on both engines. Actual crossing-center deck clearance is
approximately 9.2m and grades match 4/8/12/8 percent. This is not minimum clearance
over the entire bridge. Repetition phase, ends, rail offsets, support deformation
and deck-end differences remain. Props are diagnostic shapes/materials; lamps do
not prove nighttime illumination. These bridges have no interchange ramps. They
exercise prefab deformation, not rigid upright-placement parity. No production API
or persistence format change, architecture waiver or original35 pass-count change.

#### Canonical reference policy: bundled examples first

User requested original EasyRoads examples as the baseline. Do not treat the
custom optional-fixture props as canonical quality or parity acceptance. Open
bundled scenes directly with UnityBundledExamplesCapture.Run -bundledRoot <root>.
This preserves meshes/materials/settings and does not save source scenes. Publish
with python -X utf8 scripts/road_bundled_examples.py --package <original package>.
The publisher verifies original package scene bytes, imported scene/evidence SHA,
all dependency hashes, adapter and frame hashes. Page: bundled-examples.html.

Three supplied scenes inventoried: main (33 roads plus connections/ring/terrain),
runtime-existing (one prebuilt road), runtime-new (requires its official play
workflow, not validated yet). Main/existing each have overview/detail true frames.
No EVEngine correspondence or parity score is claimed yet. Original shaders and
materials are retained; only an evidence camera is added in memory. Main package
contains no standalone lamp/bench/bridge source-model prefabs found in inventory;
do not invent official assets to fill that coverage gap.

#### Original examples: native correspondence (first verified iteration)

The current comparison page is `bundled-alignment.html`, evidence root
`build/road-parity/bundled-alignment`. Three original scenes yield four states:
main33 roads, existing-network edit1, existing-network Play2, new-network Play1.
Do not call those37 independent examples. Both runtime scenes run the original
runtimeScript via real Play mode (terrain data/collider cloned before entering).
Use UnityBundledRuntimeCapture.Run with -exampleName and -exportRoot, without
-quit: the callback exports after frame30 and exits. Use
UnityBundledSceneExport.Run -exportRoot for main/existing editor states.

Build `road_bundled_fixture` in native-verified, then run
`python -X utf8 scripts/road_bundled_alignment.py`. It restores/bakes production
RoadNetwork v1 records, validates full mesh equality after save/restore, and
creates GLB games. Original SDK mesh rendering is calibration only. Independent
native road strips are composed with shared SDK junction/ring meshes and shared
terrain; this does not prove native junction graph or terrain generation parity.
UV atlas resource binding is currently in the fixture adapter, not production.

Capture every game with scripts/capture_example.ps1 and preserve capture.log.
Before launch save hashes of scene.glb/main.nut/config.nut/textures/SPVs to
capture-inputs.json (schema eve.tests.bundled-capture-inputs v1), then bind the
final frame hash. `python -X utf8 scripts/road_bundled_report.py` rejects stale or
changed inputs, source scene/SDK changes and incomplete identity mappings. It
also verifies40 original source dependencies against the provided unitypackage,
including TerrainData. The three scene files and source assets remain unchanged.

48 true frames include RGB and road-only masks, two cameras, Unity and both native
paths. No new state passes all unchanged image thresholds. Report masks separately
so background/shared geometry cannot hide errors. One-pixel boundary statistics
are diagnostic, not a relaxed pass criterion. Original mesh tangents use COLOR_0
only as a test shader transport. Original road shaders ignore vertex color.

Correct ports include original raster depth bias, repeat/mipmap sampling, original
2x MSAA without extra FXAA, normal/tangent handedness, and actual
Nature/Terrain/Diffuse Lambert terrain (not Standard specular). Unity Gamma SH
requires the shader's LinearToGamma conversion: raw .033 SH is not directly .2
material ambient. The temporary lower-ambient attempt was rejected; tests pin
this conversion. Standard BRDF/environment reflection/shadow filters, sky/terrain
LOD, native Circular/connection constraints and a production section-UV contract
remain open. The isolated0045 SDK input reproduces the original circular bounds;
current native maximum bounds delta is13.84m, not a surface-distance metric.

New author/capture contracts14, comparator4, UTF-8 tests10 and MSVC fixture build
pass;37 full save/restore mesh checks pass. No production API/schema/module change,
architecture waiver, original35 score change or full-source-gate rerun this round.

The subsequent v21 iteration fixes Circular's nonnegative straight lead and
neighboring exit-tangent constraint in the production spline. Public SDK probes
vary exit direction, predecessor distance and spline strength; no DLL algorithm
is copied. Missing/singular neighbors or a negative lead retain the previous
single-tangent circle contract and are not claimed as SDK-verified parity.
Original author inputs, resources, lighting and cameras remain unchanged.
`bundled-before-circular-v21/` preserves the previous meshes, captures and report;
the report now shows Unity / before / after and checks baseline hashes and inputs.
Main-scene road-mask IoU improves from .79008/.87910 to .81564/.94945
(overview/detail). Roads0045/0056 AABB delta drops from13.84169/5.17993m to
.42349/.05145m, not a claim about pointwise surface distance. Road0034 remains
2.35211m, and material/shadow, junction graph and UV-contract gaps remain open.
STATIC and SHARED closures each pass168 cases, artifact tests pass3, and the
actual eve target links. Publishing now requires matching executable/DLL hashes
in every capture manifest; source/input/frame hashes alone cannot certify a frame
from the current build. Author/capture tests15, comparator4 and UTF-8 tests10 pass.
The full source gate is rerun, with its actual outcome recorded in
`circular-v21-make-check.log`; no exception or new baseline is used.

Eight controlled derivatives now live in `build/road-parity/bundled-variants/`:
east/north/south exit, X reflection, half scale, uphill, downhill and banking.
These edit the original0045 instance using public SDK methods, retaining its
actual section/materials, and are explicitly not eight additional bundled demos.
Open the original scene afresh for each case, hide the other roads/connections,
clone TerrainData and never save source assets. `derivation.json` preserves both
original and edited marker inputs; publication checks the original author export
and generator hash. Material or section changes fail reference generation.

Run Unity with `-batchmode -quit -projectPath <project>` and
`-executeMethod UnityBundledVariants.Run -exportRoot <bundled-variants>`.
Then run:

```powershell
python -X utf8 scripts/road_bundled_alignment.py --root build/road-parity/bundled-variants
& scripts/road_parity/capture_bundled.ps1 -Root build/road-parity/bundled-variants
python -X utf8 scripts/road_bundled_report.py --root build/road-parity/bundled-variants --gallery build/road-parity/gallery-35/variants-v22
```

v22 matches the SDK's chord output for the negative-lead south-exit span and
preserves the neighboring incoming spline direction under reversal. Existing
invalid exact-reversal geometry still fails atomically. South-exit detail mask
IoU improves .46692 to .98931; AABB delta drops6.8833m to.3596m, not surface
distance. Baseline meshes and four failed-case frames remain in
`bundled-variants-before-v22/`. The gallery includes before/after images with
unchanged material/camera script hashes. Final evidence is32 Unity plus64 native
1280x720 frames, all current native runtime manifests verified;40 package assets
remain identical. Six slope/banking views still fail the unchanged IoU threshold,
and all eight cases fail combined color thresholds. Height interpolation, banking
frames, shading, native junction graphs and production section UVs remain open.
STATIC/SHARED169 each, artifacts3, author/capture/provenance18, comparator4,
UTF-8 tests10 and actual eve build pass. Full source gate still reports the same
seven unrelated architecture findings; no waiver or new baseline was introduced.
