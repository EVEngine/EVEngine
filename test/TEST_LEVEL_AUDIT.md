# C++ test reorganization and evidence

Audit date: 2026-10-15. The module manifest owns the 212 declarations;
`test_levels.json` owns test case level, module/task attribution and migration
provenance. `MODULE_TEST_COVERAGE.md` is generated evidence for every module.

## Completed source migration

The 773-source baseline had 6411 literal case identities and 150 mixed sources.
All 747 desktop case-owning sources moved into the canonical layout. The 150
mixed files were split without changing literal names or assertions. Twenty
cohesive responsibility files combine 52 prior source parts (32 fewer compilation
units than an unmerged split). No numeric chunk files or forwarding `.cpp` files
remain. Shared fixtures, standalone entrypoints, compile contracts and the fuzz
entrypoint keep their dedicated roles.

The current inventory has 893 `.cpp` sources: 328 organized unit sources,
539 task integration sources and 26 dedicated helper/runner/probe/contract/fuzz
sources. There are 6413 literal identities: 3290 unit and 3123 integration,
plus six bounded generator-prefix policies. The two added cases exercise real
heightmap/stylize preview admission without a provider and verify structured
failure plus unchanged input state. No desktop source mixes both levels.

CMake resolves legacy profile/standalone source references to actual canonical
files. Canonical paths are never expanded again; a real CMake regression covers
that distinction and duplicate elimination. Source property flags follow both
halves of mixed sources. Source snapshots and Make stale checks discover the
unit/integration trees. Binding/architecture proof paths and manuals follow the
migration. Test-root asset addressing is independent of source nesting.

## Ownership and coverage limits

All 212 modules are inventoried, across 18 task groups. Evidence is deliberately
separated: 87 modules own local-rule cases, 46 own task cases, 72 are referenced
by other test sources, four have API-name evidence through script/shared
interfaces, and three contain only type forwarding plus a link anchor. The
report names all modules without independently owned local-rule cases.

The initial nine modules without direct header references were reviewed. Four
already participate through shared declarations/script names; three are pure
facades with no independent runtime rules. The two uncovered preview adapters
now have failure-admission cases. API/header presence does not establish branch
coverage, and these added negative cases do not prove successful GPU preview
behavior. No empty assertion is used to make a module count appear covered.

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

Full SHARED and OBJECT builds both passed, including discovery and compact impact
index generation for all normal test targets. `make check` passed with 305 Python
script tests. Twenty-two focused OBJECT units and ten reorganized standalone
tilemap units also passed. The negative rendering-fixture admission probe returned
exit code 1 and logged the unit-lane rejection before device startup.
No local full graphics/window suite is run; the repository directs that lane to
CI. Asset downloads are disabled for the link checks, and third-party binaries
are read-only at the matching pinned aggregate revision.

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
