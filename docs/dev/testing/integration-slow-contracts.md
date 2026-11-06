# Follow-up review of the slow integration contracts

The 2026-10-26 baseline uses the resolved PR #684 merge, MSVC Debug SHARED,
real Vulkan rendering on a private background desktop, individual CTest
processes and four workers. Benchmarks are excluded. View/performance defaults
are `EVENGINE_VIEW_SECONDS=0.3` and `EVENGINE_PERF_FRAMES=30`; each case has a
120-second timeout. All 148 integration CPPs contributed 3,482 actual cases;
compiled-identity preflight checked all 17 executables before timing.

Baseline wall time was 265.316 seconds: 3,443 passed, 24 failed, 15 skipped.
This is a current baseline, not a controlled comparison with the older
138-source runs. Per-source numbers are sums of process time under concurrency,
not elapsed time with each source running alone.

## Changes and retained observations

- `RenderSystem3D`: five smoke/Canvas/UI display loops submit three frames each,
  preserving real rendering, canvas pixels, rejected operations, host lifetime
  and repeated remount observations. The former fixed waits totalled 3.84 seconds.
- `RenderImageAudit`: retain the original warmup frames and two-frame flicker
  comparison. Remove the extra 8ms delay in `warmPresent`; successful PNGs are
  manual-only, while failed audit reports and overlays remain. Pixel occupancy,
  severe-defect and colour assertions are unchanged.
- `ClassicScenes`: five rendering configurations retain their independent pixel
  audits and lighting-response assertions. The routine camera path uses three
  fixed samples per phase; manual preview retains timed motion. The final
  FullLit/path-start readback remains because callers sample it. Unobserved
  hero PNG encoding and post-assertion rotating-light preview are manual-only.
- Modified audit helpers receive the invoking zeroerr `TestContext` explicitly;
  their assertions must be attributed to the actual case.
- The 100,000-record asset-index task moves intact to an editor benchmark.
  The existing import/query contract shares its three-record database to also
  check type filtering, URI lookup and exact publication generation.
- The 8,000-particle, 120-step, 24-iteration cloth stability workload moves intact
  into the existing cloth benchmark CPP. Small GPU cloth pin, interaction,
  separation and bounds contracts remain in integration.
- The original 96-unit, 120-step overlap/replay scenario moves to an RTS
  benchmark with its original identity and distance threshold. The integration
  contract uses four explicitly overlapping units, eight steps at `1/120`, and
  two independent worlds: every unit is published, pair distances become
  nonzero, net target-directed displacement is positive, and positions replay
  bit exactly. It establishes short-path composition, not the original crowd
  scale or long-distance arrival.

The existing cohesive integration CPPs remain. No Unity rule, origin-selection
macro, production API, lifecycle, ECS model or persistence change is introduced.
Benchmark sources are opt-in; migrating a scale workload is not equivalent to
proving its original scale in routine integration.

## Deferred candidates

LLM forward-parity cases could share weights and reference FP32 output, but the
model/reference files are absent on this host. Their implementation and
24-token generation workload remain unchanged; skipped work cannot establish
inference speed or correctness. Project-index scans, real backend comparisons,
sky-wisps preparation and distinct imported assets retain independent observable
contracts. Further sharing requires targeted measurements and isolation review.

No architecture-rule exception is required. All changes are test-only.

## Migrated workload verification

With benchmarks enabled, the original 100,000-record asset task and
8,000-particle cloth task pass. The short asset-projection and overlap contracts
also pass. The original large RTS workload does not satisfy its existing
`progressed > 80` threshold on this host: only 57 units progress beyond five
metres. The pre-change full-run JUnit already records precisely `57 > 80` as a
warning, even though CTest counts the case as passed. The benchmark now uses
fatal assertions and exposes that existing failure. The workload, step size,
duration and threshold are retained; no engine correctness fix is claimed.

The local ECS and zeroerr submodules retain pre-existing edits. Those edits are
not part of this change; native verification uses that same local dependency
state before and after. Hosted CI on pinned dependencies remains necessary.

Explicit audit context also exposes two previous CTest passes:
`graphics.imageAudit.gbufferViews` has zero normal-buffer occupancy in both the
baseline JUnit and the new run; `ClassicScenes.cornell.flythroughConfigs` fails
its existing lighting-response predicate. Re-running Cornell with the retained
original timed flythrough and PNG branch also fails that predicate with the same
phase luminances. These assertions are retained and now fail the parent case.
Any complete-suite time comparison therefore includes changed early-failure
behaviour; it must not be presented as an isolated all-passing speedup.

## Complete rerun and matched comparison

The complete optimized run takes **289.385 seconds**, compared with
**265.316 seconds** before: **9.07% slower**, not a demonstrated suite speedup.
All 148 CPPs and 17 executables are checked again; 3,480 regular cases run:
3,439 pass, 26 fail, 15 skip. Three original scale identities move to benchmark
and one short overlap parent is added. The original 24 failed identities and all
15 skipped identities remain; the only two status changes are the newly fatal
Cornell and GBuffer observations described above.

Unchanged sources also cost more in the second run: gpu_driven 47.822→54.909
cumulative process seconds, backend_parity 44.340→52.823, and asset_import
39.175→43.709. One full run per revision cannot separate system drift from
the implementation changes. No general driver, hardware or engine cause is
inferred from these measurements.

To measure the modified render work more closely, old and new graphics binaries
were retained and run against the same current DLLs and identical named cases,
settings, working directory and four-worker hidden-desktop launcher. The order
was **old→new→new→old**, with no builds/source checks during timing. Compiled
identity preflight verifies all 89 selected parents once in each executable.

- Old: 36.448 / 30.132 seconds; median **33.290**.
- New: 32.858 / 28.844 seconds; median **30.851**, **7.33% lower**.

All 89 have CTest pass status in every round. Four warning cases visible in the
original truncated JUnit were excluded from this cohort. Larger output capture
reveals two further warning-only cases, PBR chart and the same-frame UI overlay;
both persist in every old/new round. They are not claimed correct or newly
fixed. Excluding those two from process-time sums leaves the same 87 identities:
old 131.432 / 111.581 process seconds; new 121.906 / 108.510. Those are process
sums under concurrency, not a separate measured 87-case wall time.

The cohort supports an observed local cost reduction, with only two rounds per
version and substantial variation. It does not overturn the slower full rerun
or establish equivalent long-duration/large-scale coverage.

The ten render-presentation OBJ files remain ten; their total is
153,194,710→153,121,284 bytes (73,426 bytes smaller, 0.048%). No controlled PDB
or cold-compilation timing improvement is claimed.

The [148-source CSV](integration-slow-runtime.csv) records every source's case
counts, status counts and cumulative times before/after. The
[measurement JSON](integration-slow-runtime.json) records selection changes,
slow cases, executable hashes and ABBA rounds. Raw CTest/JUnit evidence is in
`.local-debug/integration-20261026-before/`,
`.local-debug/integration-20261026-after/` and `.local-debug/slow-ab/`.

Native benchmark-ON and benchmark-OFF compilation/link checks pass. The final
configuration is OFF. `make check CI_BASE=010c77ce2` completes its source checks,
then its 562-test script stage reports the same three failures and one error
in `test_ci_third_party_cache`, caused by Windows/Bash path handling; five tests
skip. The gate is not green. The historical audit still resolves all 3,792
identities with zero unexplained gaps.
