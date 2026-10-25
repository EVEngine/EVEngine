# Integration test organization and cost

The starting integration tree contained 164 C++ files, 174762 lines and 3792 literal registered test identities. The complete original case list, original policy classification and current destination are recorded in [integration-audit.md](integration-audit.md). The ledger distinguishes static policy labels from manually reviewed boundaries. An additional appendix freezes 15 expanded registrations from five historical TestScript generator families; these are counted separately from the 3792 literal identities. Literal inventory includes inactive platform branches and excludes unresolved macro-generated names; compiled discovery remains authoritative for an actual build.

Of those 3792 original identities, 184 now belong to unit tests, two belong to the explicit benchmark lane, two redundant smoke registrations are retired, and 3604 remain represented in integration cases or named subcases. No historical identity is unresolved.

## Boundary decisions

The retained task families cover these observable boundaries; the ledger lists the individual scenarios and destinations:

- Asset import and pipelines: real file formats, worker decoding, cache ownership, provider registration and asset-to-renderer conversion.
- Script APIs and gameplay composition: VM exposure, argument/result conversion, callbacks, module/provider availability and consumer transactions.
- Physics, crowds and projectiles: backend contacts, world ownership, worker joins, movement/navigation adapters and real world sweeps.
- Rendering and presentation: pipeline resources, offscreen output, material/animation integration, frame capture and window/backend lifecycle.
- Editing and persistence: document/authority transactions, undo/redo, save/restore and filesystem sidecars.
- UI and platform services: input bindings, HUD composition, audio playback, profiler capture and runtime shutdown.

Exhaustive local input matrices use the owning unit suite when they exercise only a deterministic implementation or injected fake. A retained real adapter test establishes the boundary; multiplying local edge cases does not require multiplying backend startups.

- Unit tests own deterministic local algorithms, codecs, validation, value semantics and transactional updates exercised with direct typed inputs. Merely touching two data types does not establish an integration boundary.
- Integration tests own observable interaction between independently implemented modules/providers, real script bindings and VM registration, actual worker execution, filesystem persistence, backend physics ownership/contact behavior, rendering and other platform services. CPU-only tests can still be integration tests.
- Reviewed moves preserve assertions in unit: 66 crowd-native rule/budget/flow/snapshot/fake-scheduler cases, local index/order/sensing semantics, map-navigation snapshots, carrier-local motion/replay/validation and fake hit probes, and ActionPreviewProfile codec/compatibility analysis. Consult the case ledger for each destination rather than maintaining a parallel list of policies here.
- A second review of all 23 standalone `procgen_road_*.cpp` files moved 36 CPU-only cases into three cohesive unit files: road geometry/control points, sampling/budgets/surface paths, and generated mesh/lane/placement math. Their case bodies are preserved exactly. Editor document undo/redo, EditTarget/authority commands and terrain/provider writeback remain integration. Codecs operating on in-memory values do not alone justify integration.
- Further local matrices moved to their owning unit files: environment-query scoring with fake metrics, editing value/result storage, identity and handle values, in-memory ledger restore, effect-container serialization, PCG configuration/growth and animation rotation/feature-shape math. The PCG real-node lifecycle and animation tensor-provider rewrite remain integration.
- Real CrowdThread/JobSystem joins and rollback, CrowdMotion composition, RTS/crowd/entity/provider interactions, WorldSphereSweep/weapon adaptation, physics force/joint/lifetime behavior and the profile filesystem sidecar remain integration.
- The 1000-body collision workload performs 120 warmup steps, 300 measured steps and 10000 rays without an elapsed-time regression threshold. It belongs in `test/benchmark/physics/physics_collision_benchmark.cpp`. The Make runner already excluded its benchmark label by default; this change also removes the workload from normal compilation/linking and default direct CTest discovery. Enable `EVENGINE_BUILD_TEST_BENCHMARKS=ON` and use `INCLUDE_BENCHMARKS=1 LABEL_FILTER='^benchmark$'` to build and run it explicitly. Its sample count is preserved.
- The 10000-entity animation-motion timing workload also moves to `test/benchmark/animation/animation_motion_benchmark.cpp`, using the same explicit build lane. Its previous environment gate caused a default no-op registration/process; unrelated unused helpers are removed from the benchmark source.

- The Result/EditingResult header-isolation contract is the first section of `test/unit/editing/editing_tests.cpp`, before any zeroerr assertion or test-framework include. This editing source remains its own physical translation unit. The contract checks compile isolation and local Result value observation while sharing compilation with the remaining editing unit cases.

## Task structure, macros and includes

Integration compilation now uses real merged `.cpp` files. The intermediate
mechanical extraction into hundreds of per-origin scenario files and its CMake
Unity grouping have been reverted. Related test sections are physically combined
in their workflow translation units, with private helper namespaces retained.
There are no `EVE_TEST_FILTER_ORIGINS` / `EVE_TEST_ORIGIN_*` guards, generated Unity
wrappers, or per-origin selection macros in integration tests. This reduces the
number of physical test files without relying on Unity configuration.

The boundary fixes remain: local algorithms/codecs are in unit, timing workloads
are explicit benchmarks, named subcases retain scenario reporting, and redundant
smoke cases keep their recorded retirement evidence. Physical consolidation does
not undo those coverage decisions. The independent road parity harness uses the existing
`procgen_road_profile_editing.cpp`: its two profile cases and the twenty-four road
editor cases share one physical source. This keeps its real editing/procgen
closure narrow without a new object file, unrelated card/RPG/graphics sections,
or origin-selection macros.
Numeric compile-time, executable-size and runtime changes require measurement of
the final physical organization with the same toolchain and linkage settings;
intermediate split/Unity results do not establish the final performance.

The data-only asset profile has its own physical `asset_pipeline_graphics_file_admission_tests.cpp`
for glTF animation/material admission, canonical mesh UV/attributes, migration,
TIFF and TGA. The remaining two former asset graphics groups are physically
combined in `asset_pipeline_graphics_import_workflows_tests.cpp`, so the regrouping keeps two
objects in the desktop lane. The data profile links EVAsset/EVData/EVCommon and
real importer implementations; the material compatibility target enables its
existing mesh-upload provider contracts and also links the graphics workflow.
No source-origin selection macros are used to simulate a narrow closure. Both source names retain the `graphics` link-domain marker and `_tests` suffix, matching the original desktop ownership. This avoids creating a second importer/renderer closure in the asset test executable; focused profiles still select their physical data/graphics tasks explicitly.

Optional host profile checks compile whole physical workflow sources, so their
compile cost can include more code than their original isolated sections.
Their CTest commands explicitly filter execution back to the skin, PBR, texture
and material contracts rather than running every registered window scenario in
those sources. The data-only admission group remains a complete task batch.
These runtime filters do not select source code or alter the catalogue. Optional
profile compilation cost has not been measured and is separate from default
host build/runtime measurements.

Independent skin/PBR/texture profile checks and the normal test lane share one
`eve_graphics_window_fixtures` library created by an idempotent CMake function.
This owns the real window/headless helpers once, including their dependency and
assertion flags, even when only profile checks are enabled. No duplicate fixture
implementation or unneeded embedded-script library is introduced.

The two projectile script directories (`projectile-behavior/script/` and `projectile-behavior/world-script/`, each with `config.nut` and `main.nut`) remain manual engine fixtures for carrier callbacks and real world sweeps. They are not independent CTest registrations and are not counted as additional automatic tests by this audit.

Registration macros that hide case identities or stack many registrations onto one source line should be replaced by explicit fixture construction and named cases/subcases. Keep the test framework's `TEST_CASE`, `SUB_CASE`, assertion macros and required ECS declaration macros. Necessary platform/backend feature guards remain real configuration constraints. A repeated header is removable only after its compilation scope is understood: the old origin guards made some otherwise duplicate includes necessary. `PrivateTestScope.h` intentionally handles Windows `near`/`far` aliases and must not be discarded as an ordinary redundant header.

Includes should name types actually used by a task, avoiding copied graphics umbrella blocks on headless cases. This reduces parsing and prevents unrelated backend dependencies from obscuring the scenario. Include removal still needs compilation on the affected platform; token searches alone do not prove a header unnecessary.

## Subcases and shared setup

This repository's zeroerr `SUB_CASE` executes immediately with a child TestContext; it does not replay the outer fixture. Its lambda captures by value. A failed REQUIRE is caught and contributes failure to the parent while sibling subcases can continue. Capture a reference wrapper when sharing a noncopyable VM, and never rely on a mutation of an ordinary value capture updating the parent.

- Fourteen crowd script alternatives share one VM and binding registration instead of fourteen. Script-local variables isolate their scenario state.
- GraphicTest's two read-only binding alternatives and JoystickScript's four read-only alternatives share one fixture per task. This removes four process/VM setups. Mouse mutation, reload transactions and filesystem identity mutation retain separate fixtures.
- Twenty-three carrier motion/solver alternatives become two unit parent tests. Six capsule grounding/filtering alternatives become one integration parent test. Each subcase still constructs its own mutable runtime/world: process startup is shared, backend state is isolated.
- Do not merge unrelated cases just to reduce the registration count. Shared fixture state must be immutable or explicitly reset after every alternative, including failed ones. Independent production ownership and failure isolation take precedence over maximal reuse.

## Mock and cost strategy

Use injected deterministic providers for exhaustive local boundary values, invalid inputs, budgets, seeded replay, callback failure and rollback. Carrier hit probes and fake schedulers make these tests independent of a real world or worker pool. Keep a smaller real adapter/backend contract alongside those local matrices. Replacing the real world sweep, worker joins or VM registration with a mock would remove the very boundary the integration test exists to check.

Keep timed benchmarks explicit, use deterministic simulation ticks instead of sleeps, and share expensive setup only across alternatives for the same task. Avoid repeatedly loading assets or creating graphics contexts for validation that accepts direct in-memory inputs. Assert behavior and resource ownership; a faster test that silently omits those assertions is not an optimization.

## Verification and measurement limits

`scripts/audit_integration_tests.py` reuses the C++ lexer and literal inventory from `scripts/test_levels.py`. It reconciles every frozen original name against current registered cases and named subcases, reports all unresolved identities, and exits nonzero if any are missing. It regenerates paths from source; the historical Markdown is evidence, not a second live policy catalogue. Run it after source moves:

```sh
python -X utf8 scripts/audit_integration_tests.py
```

Initially two redundant registrations were explicitly retired: `MedialoaderModel.linkHeaders` only checked nonzero sizeof values, and `virtualgeometry.module.create` repeated module construction/name checks exercised by retained renderer workflows. A later user-directed [hex-level contract reduction](hex-level-contract-reduction.md) deliberately retired 34 scenario variants and kept one representative integration workflow; this does not claim equivalent scenario coverage. Exact retirement evidence lives beside the original names in the historical ledger. The audit accepts only recorded retirement entries; an unexpected missing case still fails.

The first import expanded the original TestScript calls from Git HEAD and imported explicit retirement evidence. Subsequent runs read both frozen generated identities and retirement reasons from the Markdown ledger; they require no Git history or external deletion allowlist. Comments and string literals containing SUB_CASE text cannot satisfy coverage reconciliation.

Name preservation does not prove equivalent assertions, fixture cleanup or backend behavior. The carrier/grounding transformation additionally checked preserved assertion counts; the crowd work checked all 150 names in its reviewed scope. Required source-quality checks and focused compiled tests remain necessary. Dynamic generated cases and platform-specific branches need the compiled runner's discovery and CI lanes.

The final source-quality gate passed against `origin/dev`: 511 repository script tests ran with three skips and no failures. It covers architecture contracts, module/test ownership, source/case discovery, assertion policy, formatting and script regressions. A stale lock from an interrupted formatter invalidated the initial formatting pass; after its confirmed orphan process and empty lock were cleaned, a separate complete changed-lines formatting check passed without Git errors. No assertion baseline was broadened. Strict evidence checking resolves each original responsibility's retained namespace, including helpers, and fails if that scope is missing.

The final Windows Debug OBJECT route passed the full build and 22 individually filtered CTest registrations. These cover the retained focused binding/header/backend contracts and the local unit migrations. All five optional focused profiles passed real compile, link and CTest execution before the final asset filename adjustment. That adjustment preserves their existing graphics domain and case bodies; the final filenames and path references were subsequently covered by the script gate and OBJECT build. These checks do not claim a local execution of the entire registered suite, rendered-image correctness or hosted cross-platform CI success.

Earlier focused native checks remain additional behavior evidence: Crowd's 113 parent cases passed 31,729 assertions, including fourteen shared-VM subcases; Carrier's two parent cases passed 2,852 assertions across 23 subcases. The Carrier check used exact source snapshots before merging those bodies into their unit owner. A separate zeroerr probe verified one shared setup, sibling continuation after an injected child failure, and failure propagation. These focused results are distinct from the final full build and source gate.

The completed [Windows Debug SHARED build measurement](integration-build-measurement.md) compares all 25 real test targets in four cold-artifact ABBA builds, including migrated unit coverage. Two-sample median compile/link time decreased by 4.789%, test OBJ bytes by 1.674%, and main linker PDB bytes by 0.540%. Main EXE bytes increased by 0.306% (889,856 bytes); the change does not shrink every artifact. The report records raw rounds, snapshot hashes, fixed settings and the generated-launcher cache bypass. These are local build measurements. A separate [selected-task runtime experiment](integration-runtime-measurement.md) reviewed 635 historical scenarios, compared 628 of them through 585 final parents over eight formal rounds, and observed median elapsed time of 36.808 → 32.214 seconds (-12.480%). Every round passed without failures or skips. Seven registrations restored to the default desktop lane were run separately and excluded from the comparison. Four samples per version and substantial baseline variation limit this local result; it does not establish full-integration or cross-platform runtime performance. Optional profile build cost remains unmeasured.

After the benchmark, all 387 frozen-input hashes matched the restored final sources and build inputs. The standard `make build/win32-debug JOBS=4` completed successfully through 2,506 build steps, linking the engine DLLs, all 25 domain test executables and native plugins. Compiled discovery indexed 7,275 cases without claiming full-suite execution. All 22 focused registrations passed against the final SHARED artifacts in 2.91 seconds; the full OBJECT build and its 22 focused registrations also passed (2.86 seconds). These restoration checks are separate from the timed ABBA experiment.

This work changes test placement/fixtures rather than production APIs, ECS ownership, persistence schemas or module lifecycle. Existing backend/composition assertions remain subject to their original contracts. No production architecture exception is introduced by these test-only moves.

Subsequent contract-driven reductions are recorded separately: [hex levels](hex-level-contract-reduction.md) retain one representative scenario, and [voxel rendering](voxel-contract-reduction.md) replaces 63 GPU parent cases with 10 contracts/21 subcases and removes four presentation-only previews. Their deliberate retirements are recorded in the historical ledger rather than described as equivalent scenario coverage. Both retain the existing physical compilation units; subsequent runtime measurements do not overwrite the earlier build or full-file timing snapshots.

脚本场景后续按契约精简、共享初始化、明确退役范围与实际运行耗时见 [脚本场景契约精简](script-scene-contract-reduction.md)。
