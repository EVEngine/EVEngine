# Road reference alignment: initial evidence

Date: 2026-10-13. Goal: progressively align the 75 audited EasyRoads differences.

The reproducible entry point is
[Road reference alignment lab](../../scripts/road_parity/README.md).
Six current-source CPU fixtures generate twelve real EVEngine captures:
straight, curve, slope, elevated bridge surface, T and X junctions.
All twelve captures have nonempty road masks and matching 1280×720 dimensions.
The native fixture tool compiles and links with MSVC /W4 /WX, using the same
STL DLL-interface warning exclusions as the engine.

The Unity adapter compiles against the installed Unity 6000.0.79f1 modular
assemblies and the supplied outer EasyRoads DLL with zero warnings/errors.
This is a compile check, not Unity execution proof.

The actual Unity reference command fails with exit 198:

    No valid Unity Editor license found. Please activate your license.

Validation completed: all six parity-tool regressions passed (missing reference,
stale fixture, changed image, empty/wrong-size mask, malformed/future schema,
and known identical/different metric oracles). The complete source-only
make check gate passed, including 264 repository script tests. Test oracle
images are isolated synthetic unit-test inputs; they are never reference evidence.

Therefore all six comparisons are **blocked**, not passed or failed by geometry.
There are no Unity reference frames and no reported cross-engine IoU values.
Activate an existing Editor license in Unity Hub, then run render-unity and
compare. The EasyRoads runtime still needs actual execution validation after
license activation, including prefab port attachment, visible-surface selection
and scene persistence. Compilation alone cannot settle those questions.

Candidate surface vertex/triangle counts:

- straight: 368 / 184.
- curve: 372 / 186.
- slope: 372 / 186.
- elevated bridge surface: 368 / 184.
- T junction: 748 / 372.
- X junction: 1068 / 524.

Fresh captures, fixture JSON and hashes live under git-ignored
build/road-parity/fixtures/; package provenance, renderer logs and
comparison.json live under build/road-parity/.
The renderer host is an existing executable whose hash is recorded per fixture;
the uploaded road meshes are freshly generated from this workspace's core
sources. This does not claim the entire renderer host was rebuilt at this HEAD.

This phase intentionally removes shoulders, barriers, markings, navigation,
piers, textures and terrain to calibrate surface geometry. It has not implemented
the remaining 75-item feature alignment. Future implementation must use paired
real reference frames plus native constraint/transaction/data tests as described
in the lab acceptance sequence.

Applied rules: single graph authority, core-only source dependencies,
explicit offline bake and owning snapshots, versioned test interchange,
deterministic fixtures and stale image/fixture rejection. No new public road
API, ECS hierarchy, production persistent format or architecture exception.


## 2026-10-14 update

Unity licensing now works. The refreshed native fixture source, calibrated handedness and
5.5m prefab sockets produce eight real paired scenarios, including width and banking.
See [current Unity alignment](2026-10-14-road-unity-alignment.md). Historical missing-reference
results above remain the initial record, not the current state.

## 2026-10-15 current renderer refresh

Rebuilt-host evidence now supersedes the historical SDK-host limitation above.
All 35 scenarios (70 EVEngine views) were captured again with
`build/road-parity/host-profile/src/engine/eve.exe`, SHA-256
`d4d542cb7e574f54b34201ac229c867a6ad3785fe1039c6e0705a932ca7c3c14`.
Every EV evidence record is version 2 and records the same 12-file executable/DLL
search-root inventory; this is not an operating-system loaded-module trace.
The capture command completed successfully. Comparison against the retained,
hash-validated Unity reference frames reports 33 passed, 2 different, 0 blocked
at the unchanged 0.98 surface-IoU threshold.

Remaining differences are `control-circular-mirror` (perspective 0.971352278,
top 0.988126271) and `control-circular-mirror-zero-bump` (perspective 0.972165424,
top 0.988311800). They remain failures, without threshold adjustment.
This refresh does not rerun Unity or validate facilities, terrain, materials,
traffic, or prefab renderer restoration. It closes the stale EV renderer
evidence gap for these road-surface fixtures only.

Evidence: `build/road-parity/current-runtime-all-render.log`,
`build/road-parity/current-runtime-all-compare.log`, `build/road-parity/comparison.json`,
and each fixture's `ev-evidence.json`. Sixteen targeted asset/road/scene CTest
cases and sixteen Python evidence-tool tests passed in the same verification
round. `git diff --check` passed. The default architecture gate remains blocked
by the previously recorded five findings in IStateProvider.h and RenderSystem.h;
no baseline or exception was added.
