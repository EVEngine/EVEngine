# PCG AAA GPU execution contract

## Goal

Move `PointGraph` from isolated GPU transforms to production graph execution comparable with
modern UE/Unity PCG workflows: large point sets stay resident on the active Vulkan/WebGPU
backend, compatible unary nodes fuse into one segment, editor metrics explain every barrier and
fallback, and CPU execution remains a deterministic reference path.

This document is an acceptance contract. A backend is not considered complete because one
kernel dispatches successfully.

## Execution model

The compiler lowers an evaluated graph into ordered segments. A GPU segment begins at a CPU to
GPU upload and may contain consecutive supported unary operations. It ends at a topology-changing
operation, a branch whose other consumer needs a host value, an editor readback, an unsupported
attribute type, cancellation, or graph output. One upload and one final readback are allowed per
segment; intermediate nodes must not round-trip through host memory.

Initial fusible operations are:

- `transform`
- `density.remap`
- `attribute.math.float` for built-in numeric point fields
- `attribute.set.float` for built-in numeric point fields

`merge`, `copy.points`, spatial queries, biome/grammar generation, string attributes and nested
graph boundaries are explicit barriers until they have dedicated kernels. Branches preserve each
logical node cache: the compiler may retain a GPU snapshot or materialize once, but must never
silently recompute a random or external-data node.

## Required semantics

1. `cpu`, `gpu`, and `auto` policies produce the same point count, seeds, string metadata and
   topology. Numeric GPU fields match the CPU reference within `1e-4` absolute error.
2. Stable seeds make independent graph instances bit-stable on the CPU path. GPU results are
   stable on repeated execution of the same backend and device.
3. Incremental invalidation recompiles only segments downstream of the changed node. Cached
   upstream segments and unrelated branches remain valid.
4. Cancellation is checked before upload and between segments. A cancelled or failed segment
   publishes no partial node cache.
5. Shader compilation, allocation, submission, device loss and readback errors retry the whole
   segment on CPU. Metrics retain the backend error and identify the barrier/fallback node.
6. Node output budgets are checked before GPU allocation. Byte-size arithmetic uses checked
   64-bit calculations and rejects values outside backend buffer limits.

## Observability

Every execution report must expose:

- compiled segment count and fused logical-node count;
- GPU dispatch, upload and readback counts and bytes;
- backend (`cpu`, `vulkan`, `webgpu`) per logical node;
- compilation and execution milliseconds per segment;
- barrier reason and CPU fallback reason;
- peak resident GPU bytes and buffer reuse count.

Editor preview requests may force a readback and must label it `editor-preview`, so profiling does
not mistake debug observation for normal runtime cost.

## Performance acceptance

CI records results rather than enforcing wall-clock thresholds on shared runners. A dedicated
reference machine enforces these budgets after a baseline is checked in:

| Workload | Required result |
| --- | --- |
| 1,000,000 points, four compatible unary nodes | one upload, one dispatch segment, one readback |
| Repeat unchanged graph | no dispatch; all requested outputs served from cache |
| Change final node parameter | upstream buffers reused; only final affected segment executes |
| Forced GPU allocation/submit failure | complete CPU result, no partial cache, reason reported |
| 64-cell streaming window | resident point and byte budgets never exceeded |

For the million-point chain, the fused GPU path must be at least 2x faster than the CPU reference
and at least 1.5x faster than four isolated GPU round-trips on the reference discrete GPU. Median
of 20 measured runs is used after 5 warmups; shader compilation is reported separately.

## Cross-platform gates

- Vulkan Lavapipe: correctness, validation-layer cleanliness and forced-failure recovery.
- Windows Vulkan plus native Dawn: CPU/Vulkan/WebGPU parity and resource-reuse execution.
- macOS native Dawn/Metal: compilation, dispatch and repeated-run stability.
- WebGPU/WASM: shader compilation and artifact build; browser execution parity where an adapter
  is available.
- ASan/UBSan: graph compilation, invalidation, cancellation and fallback ownership paths.

## Completion evidence

Completion requires all of the following in the same PR SHA:

- graph compiler tests proving barrier placement and incremental recompilation;
- backend parity tests covering every fusible operation and a four-node fused chain;
- failure-injection tests for allocation, compilation, dispatch and readback;
- a checked-in benchmark fixture and machine-readable baseline;
- green full platform CI, including ASan/UBSan and native Dawn parity;
- documentation and script bindings for all public metrics and policy controls.

## Acceptance commands

The regular `gpgpu.procgen` cases enforce graph-segment transfer counts, lazy intermediate
preview, cache reuse and atomic CPU recovery from forced `compile`, `allocation`, `submit` and
`readback` failures. The failure selector is the process-local
`EVENGINE_POINT_COMPUTE_FAIL` environment variable and is intended only for validation.

Run the dedicated reference-machine benchmark with:

```sh
EVENGINE_PCG_MILLION_BENCHMARK=1 make test/linux-debug \
  FILTER=gpgpu.procgen.millionPointAcceptanceBenchmark
```

It performs five GPU warmups and twenty measured CPU, fused-GPU and four-round-trip GPU samples,
then prints one `PCG_BENCHMARK_JSON=...` record. Required structure and speedups are versioned in
`docs/dev/baselines/pcg-million-point.json`; shared CI records correctness and transfer counts but
does not enforce wall-clock ratios.
