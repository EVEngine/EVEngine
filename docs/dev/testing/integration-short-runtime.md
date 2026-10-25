# Complete integration timing after short-contract review

Windows native MSVC Debug SHARED; individual CTest processes, four jobs, 120-second per-case timeout, `EVENGINE_VIEW_SECONDS=0.3`, `EVENGINE_PERF_FRAMES=30`. Benchmarks are OFF. All 138 integration CPPs contribute 3,453 active cases; 16 actual executables passed compiled-identity preflight. Legacy performance identities still owned by integration sources remain included to match the prior complete-sweep selection.

One complete round took **282.118 seconds**: 3,414 passed, 24 failed, 15 skipped. The failed/skipped identities and statuses are exactly the same as the preceding complete sweep; there are no new nonpassing identities. This is not an all-green suite.

The preceding visible-window round took 300.063 seconds for 3,458 cases. The observed difference is 17.945 seconds (5.98%). This round also changes window placement to a private background desktop. One run under each configuration cannot isolate source simplification from desktop placement, system load and run-to-run variance. The launcher verification is recorded in [hidden windows](integration-hidden-windows.md).

## The four reviewed sources

These are sums of individual process durations from each complete four-job sweep, not elapsed wall time for each file run alone:

- `test/integration/render-presentation/render_presentation_graphics_gpu_driven_tests.cpp`: 91.509 → 54.831 seconds; 51 cases, {'passed': 40, 'failed': 11}.
- `test/integration/layered-cloth/physics_layered_cloth_gpu.cpp`: 64.236 → 19.687 seconds; 12 cases, {'passed': 12}.
- `test/integration/render-presentation/render_presentation_graphics_window_cpp_tests.cpp`: 32.428 → 15.615 seconds; 19 cases, {'passed': 19}.
- `test/integration/card-rts-payment/card_rts_payment_card_tests.cpp`: 36.387 → 1.665 seconds; 11 cases, {'passed': 11}.

The five original long workloads are retained in the opt-in benchmark build; all five passed separately. They do not contribute to this routine run. Short-contract scope and deliberate coverage limits are recorded in [contract review](integration-short-contracts.md).

## Remaining largest source totals

- `test/integration/render-presentation/render_presentation_graphics_RenderSystem3D_tests.cpp`: 81.900 seconds across 76 cases.
- `test/integration/render-presentation/render_presentation_graphics_RenderImageAudit_tests.cpp`: 66.871 seconds across 58 cases.
- `test/integration/render-presentation/render_presentation_graphics_gpu_driven_tests.cpp`: 54.831 seconds across 51 cases.
- `test/integration/render-pipeline/render_pipeline_graphics_graphics_backend_parity_tests.cpp`: 53.414 seconds across 67 cases.
- `test/integration/render-presentation/render_presentation_graphics_ClassicScenes_tests.cpp`: 51.249 seconds across 56 cases.
- `test/integration/render-presentation/render_presentation_graphics_voxel_face_dir_tests.cpp`: 45.803 seconds across 43 cases.
- `test/integration/asset-import/asset_import_graphics_tests.cpp`: 42.667 seconds across 102 cases.
- `test/integration/render-pipeline/render_pipeline_graphics_graphics_vegetation_extras_tests.cpp`: 36.272 seconds across 40 cases.
- `test/integration/render-presentation/render_presentation_graphics_procgen_tests.cpp`: 35.280 seconds across 30 cases.
- `test/integration/file-io/file_io_graphics_tensor_llm_tests.cpp`: 32.571 seconds across 31 cases.

Every source, including small files, is listed in the [138-source CSV](integration-short-runtime.csv). The [JSON](integration-short-runtime.json) preserves all source totals, largest cases, nonpassing cases and executable hashes. Raw CTest/JUnit evidence remains in `.local-debug/integration-full-hidden-short/`.

The native builds with benchmarks ON and OFF passed; OFF was restored. The source-only gate passed against the existing branch baseline `20394d720951b2aa13f035dd9b16104c3de3096e` (527 script tests, three skips). The PR still needs reconciliation with newer dev test-policy changes. No new production API, ECS, persistent schema, module boundary or architecture-rule exception was introduced.
