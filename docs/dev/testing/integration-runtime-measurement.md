# Selected integration task runtime measurement

Eight formal Windows x64 Debug SHARED runs measured the tasks changed by the integration-test cleanup. Baseline is `20394d720951b2aa13f035dd9b16104c3de3096e`; final uses the merged physical test organization. The review covers 635 historical scenarios, including all 15 historical TestScript-generated identities. Of these, 628 have comparable default-lane registrations in both versions and map to 585 final parent registrations. All selected cases passed in every warmup and formal round; failures and skips were both zero.

The task set covers crowd/RTS native and script work, carrier motion and continuous collision, capsule grounding, GraphicTest/Joystick shared VM alternatives, sensing native/VM work, all 23 historical road files, both asset-pipeline files, asset-import workflows, and the unit migrations. Nine unchanged generated registrations are included as controls. Real GPU, VM, physics, asset I/O and worker interactions remain in scope. Overlapping membership is deduplicated before execution.

## Formal observations

- 01-baseline: 36.869 seconds; 628 passed, 0 failed, 0 skipped.
- 02-final: 33.024 seconds; 585 passed, 0 failed, 0 skipped.
- 03-final: 32.183 seconds; 585 passed, 0 failed, 0 skipped.
- 04-baseline: 33.684 seconds; 628 passed, 0 failed, 0 skipped.
- 05-final: 31.555 seconds; 585 passed, 0 failed, 0 skipped.
- 06-baseline: 40.298 seconds; 628 passed, 0 failed, 0 skipped.
- 07-baseline: 36.746 seconds; 628 passed, 0 failed, 0 skipped.
- 08-final: 32.245 seconds; 585 passed, 0 failed, 0 skipped.

Across all four samples per version, median wall time was 36.808 → 32.214 seconds (-12.480%); mean was 36.899 → 32.252 seconds (-12.595%). Baseline ranged from 33.684 to 40.298 seconds (span 6.614); final ranged from 31.555 to 33.024 seconds (span 1.469).

The original ABBA rounds were retained exactly, then reverse BAAB rounds were appended because the first two baseline observations varied substantially. No observation was removed or replaced. Four samples per version on one host remain limited evidence: baseline variation is substantial, and these numbers do not establish a universal or cross-platform speedup. The conclusion applies only to this selected task cohort, not all integration tests or the full suite.

## Execution controls

The same CTest installation ran `--tests-from-file` at four jobs, preserving each registration's original command, working directory and other properties. Each selected parent runs in its own process, matching the normal per-case lane. Both versions received `EVENGINE_VIEW_SECONDS=0.3` and `EVENGINE_PERF_FRAMES=30`, the root Makefile's headless test defaults, with a 120-second per-case timeout. Parent/subcase sharing is the optimization being measured; no bundle runner replaced per-case execution.

Before each formal round, matching EXE/DLL/CTest metadata snapshots were restored to their original paths and SHA-verified. Active CTest metadata had to match the saved commands and properties. Every selected executable then ran the real `--list-test-cases --list-format=plain` discovery (with legacy listing parsing supported) to verify DLL loading and exactly one compiled occurrence of every selected identity. The complete set of historical children of each selected final parent was checked so that shared parents do not silently execute unrelated scenarios.

One initial warmup per version was excluded from formal timing: baseline 628 passed in 38.444 seconds; final 585 passed in 33.225 seconds. The BAAB extension added no further warmups. Restoration and discovery preflights are also outside the timed intervals. Only actual CTest process execution is timed; no configure, compiler invocation, source switch or build command runs in the measured interval. The two snapshots each contain 69 binary/CTest files. Test sources stay final throughout runtime measurement; the 22 historical script resources used by the baseline were confirmed byte-identical in final.

JUnit records preserve each testcase's result and duration; a missing, failed or skipped result would invalidate its round. Windows child processes inherit an error mode that suppresses modal loader/crash dialogs. Abnormal termination invokes a watchdog that kills only the launched process tree. All runs completed normally, and final binaries/CTest metadata were restored and SHA-verified afterward.

## Default-lane registration difference

Seven reviewed scenarios were absent from the chosen baseline `PROFILE_CHECKS=OFF` desktop CTest lane but present exactly once in final. Six belong to historical `crowd_avoidance.cpp`, one to `rts_formation.cpp`. The old main-suite basename exclusion removed those sources to avoid duplication with focused crowd checks; the historical optional `PROFILE_CHECKS=ON` lane could register `profile.<name>` versions. These are restored default-lane registrations, not proof that the scenarios had never been tested and not platform skips.

All seven were excluded symmetrically from the performance cohorts rather than altering the baseline. They were then executed once against final: 7 passed, 0 failed, 0 skipped in 1.682 seconds (CTest reported 1.64 seconds). Their execution is not counted as a performance improvement. The two retired smoke registrations and two explicit timing benchmarks are likewise excluded from both versions, with reasons retained in the data.

## Task duration breakdown

The following values are medians of summed JUnit testcase seconds within each task group across four rounds per version. They are accumulated case durations, not elapsed task wall time. Four workers overlap, and groups themselves overlap (for example road/unit migrations); do not add these group figures to infer total runtime. A shared final parent contributes once to each group that owns any of its scenarios.

- `asset_import_all_original_cases` (102 historical scenarios → 102 parents): 39.743 → 38.885 accumulated case-seconds (-2.157%).
- `asset_pipeline_all_original_cases` (189 historical scenarios → 189 parents): 37.011 → 33.953 accumulated case-seconds (-8.264%).
- `all_unit_migrations` (178 historical scenarios → 157 parents): 26.347 → 19.088 accumulated case-seconds (-27.554%).
- `crowd_native_script_and_rts` (139 historical scenarios → 126 parents): 33.459 → 28.764 accumulated case-seconds (-14.034%).
- `road_all_original_files` (101 historical scenarios → 101 parents): 19.110 → 16.671 accumulated case-seconds (-12.760%).
- `capsule_grounding_6` (6 historical scenarios → 1 parent): 0.554 → 0.083 accumulated case-seconds (-85.043%).
- `carrier_23` (23 historical scenarios → 2 parents): 3.104 → 0.267 accumulated case-seconds (-91.411%).
- `sensing_native_vm` (29 historical scenarios → 29 parents): 3.839 → 2.884 accumulated case-seconds (-24.882%).
- `shared_vm_graphic_joystick` (6 historical scenarios → 2 parents): 1.163 → 0.481 accumulated case-seconds (-58.645%).
- `unchanged_generated_controls` (9 historical scenarios → 9 parents): 1.345 → 1.065 accumulated case-seconds (-20.781%).

The nine unchanged generated controls also show lower accumulated case durations (-20.781%). Their scenario bodies were not the shared-setup optimization, so aggregate savings cannot be attributed entirely to individual case-body changes; process startup, changed runner composition and host variation can contribute.

The [sanitized runtime data](integration-runtime-measurement.json) contains all eight round values, explicit counts, cohort medians/means/ranges, binary snapshot manifest hashes, the 628 historical-to-final mappings, and each default-lane comparability exclusion. Machine-specific paths, executable listings, full per-round JUnit and raw process logs remain ignored local evidence. No full-suite or rendered-image correctness claim follows from this measurement. The [compile/link and artifact-size experiment](integration-build-measurement.md) is a separate measurement with separate samples and scope.
