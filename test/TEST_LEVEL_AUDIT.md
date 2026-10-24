# C++ test reorganization and evidence

Audit date: 2026-10-15. The current module manifest owns 193 declarations after
`dev` removed 19 empty editor alias modules from the original 212-module baseline;
Canonical test paths own unit modules and integration tasks; `test_levels.json`
owns classification policy, integration module attribution and migration provenance.
Run `scripts/test_module_coverage.py --output build/reports/module-test-coverage.md`
for current generated evidence for every module.

## Completed source migration

The 773-source baseline had 6411 literal case identities and 150 mixed sources.
All desktop case-owning sources use the canonical layout. The 150 mixed files
were split without changing literal names or assertions, then consolidated by
module and integration task. After merging the latest `dev` tests, the current
inventory has **167** `.cpp` sources: **97** organized unit sources, **44** task
integration sources and **26** dedicated helper/runner/probe/contract/fuzz sources.
That is 726 fewer files than the first 893-source reorganization, and 606 fewer
than the original 773-source baseline.

Of the 89 modules owning local cases, 86 have exactly one unit file. Fluids,
graphics and procgen exceed 5000 total lines and have named responsibility
files; no unit file exceeds 5000 lines. The module audit rejects small-module
fragmentation and oversized unit files. Integration files consolidate the 18 task
goals; large tasks retain bounded responsibility files. Private helper namespaces
preserve helper identities, and embedded shader data keeps its original scope.

There are 6563 literal identities: 3311 unit and 3252 integration,
plus five bounded generator-prefix policies. The latest `dev` additions retain
their assertions and errors in the canonical module/task files; its seven image
script cases now use explicit declarations instead of a generator. The two
preview-admission cases added during reorganization exercise real
heightmap/stylize preview admission without a provider and verify structured
failure plus unchanged input state. No desktop source mixes both levels.
The synthetic image-audit unit now contains only CPU helpers and assertions;
unused GPU scene scaffolding and report/overlay file writes have been removed.

CMake resolves legacy profile/standalone source references to actual canonical
files. Canonical paths are never expanded again; a real CMake regression covers
that distinction, duplicate elimination, and origin selection after consolidation.
The catalogue's provenance generates per-target compile-selection headers, so a trimmed
profile or standalone project enables only its requested original responsibilities;
merging does not silently re-enable filtered tests. Integration composition owners
remain explicitly recorded even when their cases share one task file. Source
property flags follow both
halves of mixed sources. Source snapshots and Make stale checks discover the
unit/integration trees. Binding/architecture proof paths and manuals follow the
migration. Test-root asset addressing is independent of source nesting.

## Ownership and coverage limits

All 193 current modules are inventoried, across 18 task groups. Evidence is deliberately
separated: 89 modules own local-rule cases, 44 own task cases, 57 are referenced
by other test sources, and three have API-name evidence through script/shared
interfaces. The empty alias modules were removed upstream. The
report names all modules without independently owned local-rule cases.

The initial nine modules without direct header references were reviewed. Four
already participate through shared declarations/script names; three are pure
facades with no independent runtime rules. The two uncovered preview adapters
now have failure-admission cases. API/header presence does not establish branch
coverage, and these added negative cases do not prove successful GPU preview
behavior. No empty assertion is used to make a module count appear covered.

## Rendering work

Texture, shadow-scene, rotating-environment and 2D lighting human-preview holds
now require `EVENGINE_TEST_MANUAL_PREVIEW=1`. Automated pixel assertions, required
warm-up frames and readbacks remain in place. The window script no longer runs an
unrelated ten-million-iteration busy loop. CPU-only composition and binding tests
stay separate from rendering tests in the case catalogue.

## Unit runtime boundary

Desktop unit registrations set `EVENGINE_TEST_NO_RENDER=1`. The runner checks SDL
video state before/after cases. Common window/headless rendering fixtures reject
the lane before creating a window or graphics device. The fixture guard exits
the isolated process with failure: zeroerr helper assertions alone can print a
failure without updating a caller TestContext, so they are insufficient here.
The negative admission probe is expected to exit 1 before device creation.

The static gate checks case/fixture/helper reachability for obvious rendering,
VM, filesystem, thread and wall-clock setup. It is not a C++ interpreter; native
runtime verification remains necessary. Desktop main does not bootstrap SDL.
Mobile app bootstrap remains explicit and is outside this desktop headless claim.
Focused CPU projects prove a narrower link closure; the main suite still links
the engine, even when only unit cases run.

## Build-derived impact index

Normal host builds generate `test-impact/index.json` after test discovery.
Version 2 stores source/header -> interned consumer-test sets, with include/link
reason metadata and active CTest labels. Include edges and normalized paths are
cached within one indexing run; caches reset between snapshots. Compiler/Ninja
records supplement conditional literal includes. Implementation `.cpp` changes
use real CMake target/link closure, not filename guesses. Unknown changes broaden
selection, and required undiscovered targets fail instead of returning no tests.

The full SHARED build generated an index covering 4225 repository files and 6417
registered nonbundle/nonbenchmark tests. Its serialized size is about 4.6 MB.
This is dependency impact, not behavioral coverage or a call graph; broad DLL
closure can legitimately select many cases. Full cross-platform CI remains the
final platform validation lane.

## Verification

The source inventory and CMake discovery regression account for every literal
identity and all six generator policies. The independent agent and tilemap
projects previously passed 22 CPU cases; the reorganized agent project compiled
and generated its new source index. Focused native gameplay validation passed
490 unit cases across card, tactics/RTS, combat, climbing and RPG domains under
the no-render runtime contract. Both new preview-admission integrations passed.
The negative rendering-fixture probe now exits 1 as required.

Full SHARED and OBJECT builds both passed, including identical discovery of
6425 registrations and compact impact index generation for all normal test
targets. `make check` passed with 311 Python script tests. Twenty-two focused OBJECT units and ten reorganized standalone
tilemap units also passed. The negative rendering-fixture admission probe returned
exit code 1 and logged the unit-lane rejection before device startup.

Consolidation validation additionally passed 477 native unit cases (gameplay plus
one active case from each unit source), 24 focused OBJECT units, and the standalone
terrain project's 148 cases (112 unit, 36 CPU-only integration). Terrain surface,
streaming and terrace CPU implementations move token-equivalently out of the grass
adapter into `TerrainSurface.cpp`, preserving every API and algorithm. This lets
the standalone project link its real logic and bindings without a renderer.
The project now includes the actual CPU dependencies of its current binding table.
The existing procgen core compile boundary also accepts the new CPU source;
its implementation target carries its own link-group export identity in SHARED.
Resource-validation and SCAD-compiler source flags resolve through migration
provenance, restoring eight previously omitted registrations. SCAD fixture build
dependencies follow the executable that actually owns the relocated source.
Nineteen restored/composition contracts and all seven provider-conditioned
resource cases passed. The case-source reverse selector
maps a changed consolidated card unit file to exactly its 22 active unit cases.
Two existing fixture errors exposed by that lane were corrected: the tree-budget
scenario no longer mutates the subsequent removal scenario's east tile, and the
water-flow assertion accounts for the final pooled deposit.

Thirteen of fourteen targeted pixel integrations passed with automatic human
preview holds removed. `Shadow3D.receiveShadowFalseIgnoresMap` fails the same
`maxLumaDelta == 0` assertion using the retained pre-consolidation Shadow3D object
linked to the identical engine DLLs. Its assertion and registration remain active;
this existing rendering issue is not skipped or weakened by this PR.
No local full graphics/window suite is run; the repository directs that lane to
CI. Asset downloads are disabled for the link checks, and third-party binaries
are read-only at the matching pinned aggregate revision.

## Merge verification against dev 10f6faa75

The conflict repair preserves all 6563 expected literal identities and follows
the upstream removal of empty editor aliases and the JSON architecture catalogue.
The final source-only gate passes 360 script tests. Twelve affected Windows
SHARED test domains compile and link under strict warnings; all 281 selected
runtime cases pass, including 280 with `EVENGINE_TEST_NO_RENDER=1` and the separate
window case. Strict assertion evidence follows origin sections, and existing
warning-only baseline identities relocate without adding baseline entries.
Invalid C++ unused annotations inside embedded Squirrel text are removed without
changing C++ annotations or test assertions.

## Architecture rules applied

Module declarations, boot composition and link-group policy remain manifest-owned.
Test grouping preserves public ownership, ECS facts, simulation algorithms and
persistent game schemas. New failure checks consume structured Results and verify
failure atomicity. The newly tested stylize presentation class gains the Windows
export annotation required by its existing link group, with borrowed lifetimes,
thread affinity and GPU/readback cost documented; its API shape is unchanged.
Per-module tests, task composition tests, optional-provider admission and the
source-only architecture gate apply. There is no approved or deliberate rule
exception and no new baseline, allowlist or soft skip.
