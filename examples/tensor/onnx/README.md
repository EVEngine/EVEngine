# Native quantized ONNX inference

Direct ONNX execution in EVEngine's tensor module, without ONNX Runtime, protobuf
or Python at runtime. Original INT8 weights remain packed. The existing block
quantization format is separate from ONNX affine quantization.

## Engine GPU path

After configuring the engine with tensor enabled:

```powershell
cmake/with-msvc.cmd cmake --build build/win32-debug --target onnx_engine_probe --parallel 4
$env:EVE_ONNX_GPU='1'
build/win32-debug/test/onnx_engine_probe.exe model.int8.onnx audio audio.f32 0,73,127,172,88,108,169,4,0 style.f32
```

The probe initializes headless Graphics and calls `createOnnxGpuCompute()`. It also
checks device-unavailable behavior before initialization. `style.f32` contains 256
little-endian floats; providing it also supplies speed=1. The IDs encode “你好。”.

The current minimal host profile also needs font and devtools to link its closure:
`-DEVENGINE_PROFILE=minimal -DEVENGINE_MODULE_tensor=ON -DEVENGINE_MODULE_font=ON
-DEVENGINE_MODULE_devtools=ON`. No manifest or default profile is changed here.

C++ callers check Results from `OnnxModel::load`, `createOnnxGpuCompute` and
`model->runGpu(feeds, *compute, requestedNames)`. The result owns `outputs` and
reports successful `dispatches` plus transfer, submission and allocation/reuse counters. `run(feeds, requestedNames)` selects CPU for
reference execution. Empty requestedNames selects all graph outputs.

GPU kernels cover integer MatMul/Conv and LSTM projections, FP32 MatMul/ConvTranspose,
broadcast arithmetic, activations, Softmax, sum/mean reductions, InstanceNormalization,
Resize and CumSum. Dynamic quantization reduces range on GPU and reads only three
validation scalars; packed activation generation stays on GPU. Shape/index/sequence/control
operations, padding, other quantization variants, RNG and LSTM gates stay on CPU. GPU errors never trigger an automatic CPU retry.
The GLSL adapter explicitly rejects WebGPU.

The adapter reuses the existing `gpgpu::Sequence` recording and `submitAsync`/`wait`
mechanism used by tensor GPU execution. Consecutive GPU nodes share immutable device
buffers. Identity/reshape aliases do not transfer data. CPU reads materialize host bytes;
CPU writes detach shared storage. Packed initializers and recurrent weights are cached
by owning storage identity across runs for immutable model initializers. Dead device allocations return to a size-bounded
reuse pool. A batch flush occurs at a CPU-read boundary or after a bounded recording
backlog, with no download at a backlog flush. The public runGpu call remains synchronous.

The static `GpuProgram` compiler is not an ONNX control-flow executor; ONNX uses the same
Gpgpu buffers/sequences directly while retaining dynamic Loop/If scheduling and affine
integer semantics. Device allocations are bounded to 512 MiB, with a 128 MiB host-input
cache and size-compatible buffer reuse. All referenced buffers survive queued work.

Retain the provider returned by `createOnnxGpuCompute()` outside the dialogue/inference
loop and pass it to every `runGpu` call. It owns a reusable compiled GPU session:
shader pipelines (up to 1024 variants), model initializers (up to 128 MiB) and device
buffer pools (up to 512 MiB) persist across calls. Shader cache keys include the complete
generated source, including shapes, integer interpretation and LSTM direction offsets.
A different shape compiles only missing variants. Dynamic activations remain per-call.
Failed calls discard pending uploads before allowing cache reuse; compiled shaders stay.

The session subscribes to `Graphics::onResourcesRetiring`: resources are freed before
Vulkan device teardown even if the session outlives Graphics. A retired session rejects
GPU work; construct a new one for the new device. Destroying the session first cancels
the subscription. All of this is device-thread-only. Feeds are borrowed during the call;
outputs outlive the model and provider. Use the device's owning thread without
concurrent provider calls. Independent CPU runs are thread-safe. No global RNG,
background tasks, script callbacks or locks around compute calls are introduced.

## Standalone build and tests

```powershell
cmake/with-msvc.cmd cmake -S examples/tensor/onnx -B build/tensor-onnx -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake/with-msvc.cmd cmake --build build/tensor-onnx --parallel 4
ctest --test-dir build/tensor-onnx --output-on-failure
build/tensor-onnx/onnx_probe.exe model.int8.onnx
```

Initialize `external/zeroerr`, or set `ZEROERR_SOURCE` to an initialized checkout.
`EVE_ONNX_TESTS=OFF` removes test linkage. The CPU path needs no Vulkan, SDL or VM.
Tests and their library deliberately use the same assertion policy.

Optional `-DEVE_ONNX_VULKAN_PROBE=ON` enables a standalone hardware parity provider
using Vulkan SDK and shaderc. It executes the same tensor-generated kernels on a
physical GPU. Production uses the engine's Gpgpu adapter instead.

## Reproduce Chinese speech

Download `model.int8.onnx`, `voices.bin` and `tokens.txt` from the
[Kokoro INT8 v1.1 package](https://huggingface.co/csukuangfj/kokoro-int8-multi-lang-v1_1/tree/main).
Tested model: 114299010 bytes, SHA256
`bda15858163726a492d02a9a727bc263551b86ac77f90812c4b30ff41d380e26`.

```powershell
python -m pip install numpy onnx onnxruntime
python scripts/run_kokoro_onnx.py --model model.int8.onnx --voices voices.bin --tokens tokens.txt --probe build/win32-debug/test/onnx_engine_probe.exe --work build/kokoro-gpu --gpu
```

The utility prepares explicit phonemes and zf_001 voice data, invokes native inference,
and writes `audio.wav`, `native.log` and `result.json`. It does not transform weights.
Style selection follows [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx/blob/master/sherpa-onnx/csrc/offline-tts-kokoro-model.cc):
index by phoneme length excluding boundary zeros. This is not yet a text normalizer,
phonemizer or dialogue playback integration.

## Import and numerical contract

- IR 3–10, default opset 13–17, Microsoft opset 1; FP32, int8/uint8, int32/int64,
  bool and internal sequences. External/sparse tensor data and functions are rejected.
- Bounded parsing checks truncation, sizes, duplicates and lexical captures. Nested
  Loop/If bodies are parsed; scheduling includes implicit captured values, including
  Kokoro captures appearing later in the serialized main graph.
- Unknown operators are reported and required unknown nodes fail before dispatch.
  This bounded executor is not a complete ONNX schema validator.
- Affine APIs reject nonfinite values. The ONNX DynamicQuantizeLinear bridge matches
  ORT CPU for NaN: exclude it from range estimation and emit byte code zero. Kokoro's
  exported STFT phase graph produces 0/0 at zero-amplitude bins. Infinity remains an
  error in quantization. Rounding is ties-to-even.
- GPU integer accumulation is exact int32, with a conservative term-count overflow
  bound. CPU checks actual int64 sums before narrowing. Per-axis dequantization and
  forward/reverse/bidirectional quantized LSTM are supported.
- `OnnxRunOptions.seed` controls per-node RNG streams. Floating CPU/GPU results use
  tolerances. `requireFinite` is a strict diagnostic that rejects intentional
  intermediate infinity/NaN; keep it false for Kokoro.
- Specialized boundaries: integer MatMul uses rank>=2 A/rank-2 B and scalar zeros;
  Conv is 1D/2D NOTSET padding; ConvTranspose is 1D; nearest Resize uses
  asymmetric/floor coordinates; linear Resize interpolates the final axis; Loop
  supports carried tensors/sequences without scan outputs. Other variants fail.
- Limits: 512 MiB model, 100000 recursive nodes, depth 16, rank 6, 128M elements per
  tensor, 10000 loop iterations, 1M execution steps. Live owned intermediates and
  copied outputs are bounded to 512 MiB; model/feed data and temporary workspaces
  are additional. GPU work is bounded to 65535 groups of 64.

## Validation and performance

```powershell
python scripts/test_tensor_onnx.py --probe build/tensor-onnx/onnx_probe.exe --work build/onnx-cpu-fixtures --kokoro model.int8.onnx
python scripts/test_tensor_onnx.py --probe build/tensor-onnx/onnx_probe.exe --work build/onnx-gpu-fixtures --kokoro model.int8.onnx --gpu
```

Reference packages are test-only. Coverage includes exact integer results, floating
kernels, shape operations, lexical If captures, Loop/Sequence, neural resampling,
and all six original packed Kokoro LSTMs. Contract tests include malformed inputs,
owning outputs, unsupported preflight, injected GPU failure and scope cleanup.

The original model generates 26400 finite 24 kHz samples (1.1 s of audio).
The GPU implementation remains synchronous at `runGpu`; CPU boundaries include LSTM
gates, data-dependent shape/index work and quantization validation scalars.

The hardware residency contract verifies that a 16-node Add chain with aliases uses
only two input uploads, one download and one submission, on the first run. The second run performs zero uploads and zero shader compilations.
Separate hardware tests cover staging-pool growth/reordering and descriptor retirement
when unsubmitted sequences are canceled. Run them with:

```powershell
cmake/with-msvc.cmd cmake --build build/win32-debug --target onnx_gpu_contracts --parallel 4
$env:EVENGINE_VULKAN_VALIDATION='1'
$cases = 'tensor.onnx.gpu.residentChain', 'gpgpu.sequence.stagingSizeReuse', 'gpgpu.sequence.cancelRecordingRetiresDescriptors', 'tensor.onnx.gpu.sessionShapesAndLifetime', 'tensor.onnx.gpu.parallelCompilation'
foreach ($case in $cases) {
    build/win32-debug/test/onnx_gpu_contracts.exe "--testcase=$case"
    if ($LASTEXITCODE -ne 0) { throw "Failed: $case" }
}
```


The session now compiles different GLSL sources on a bounded CPU queue and defers
pipeline creation/dispatch recording until a host-read or segment-budget boundary.
Dispatches retain their buffers and preserve graph order. Identical pending sources
share one future; completed pipelines persist across calls. All Vulkan access stays
on the device thread. Error cleanup drains admitted CPU jobs and discards unsubmitted
commands; compiler workers never capture a device or call the render tracer.

`createOnnxGpuCompute(compilerWorkers)` accepts 1..8 workers (default 4), with at most
48 waiting compilation jobs. The probe exposes this as `EVE_ONNX_COMPILER_WORKERS`.
This is an upper bound, not a promise of a speedup: dynamic CPU boundaries limit how
many independent sources are available. Shaderc retains a compiler/options context
per thread until that thread exits. Session workers are joined at destruction.
Thread-safety follows the [shaderc C API contract](https://github.com/google/shaderc/blob/main/libshaderc/include/shaderc/shaderc.h).

Windows CMake now defines `EVE_HAS_SHADERC` on EVGpgpu itself. Previously its
INTERFACE-only definition linked the library but left the implementation launching
`glslc.exe`. A shaderc-enabled build now reports compilation failures directly and
never requires that executable. The build without the shaderc library uses an external compiler path that retains unique
temporary names through output consumption and explicitly restricts child handle
inheritance so concurrent jobs cannot lock each other's temporary files.

Measured on the same native Windows Debug build and original model/input, with a
fresh process/session (application shader cache empty; driver caches were not cleared):

- Previous synchronous compilation: first `runGpu` 35.72-36.99 s; repeat 2.60-2.64 s.
- Four external compiler workers: first 19.89 s; repeat 2.69 s.
- Four in-process workers with retained contexts: first 8.23-9.88 s; repeat 2.74-3.26 s.
- One in-process worker with retained context: first 6.81-9.82 s; repeat 2.69-3.22 s.

Each new session compiled 370 variants; every repeat compiled zero (2608 cache hits).
Audio was byte-identical across the old implementation, external/in-process compilers,
and one/four-worker runs. Thus compiler wiring/context reuse supplies the main benefit;
on this Debug workload, four workers did not consistently beat one. The final paired
run measured 9.82 s cold / 3.22 s warm with one worker, and 9.88 s / 3.26 s with four.
Timing varied between runs, so the ranges above are retained rather than selecting only the best result. Release performance is not
established. Even the warm call exceeds the 1.1 s audio duration, so this is not real-time.

Set `EVE_ONNX_PROFILE=1` for wall-time buckets. `compile_task_ms` sums durations of
worker tasks (overlapping durations, not elapsed time); `compile_wait_ms` is device-thread
blocking on futures; `pipeline_ms` is Vulkan pipeline creation on that thread.
`shader_ms` comprises the latter two. `compiler_peak_workers` reports observed concurrency.
Allocation, upload/dispatch recording, submit/wait and host readback are also reported;
these are not GPU timestamp-query kernel timings. An earlier four-worker cold measurement reported
7.75 s aggregate compile tasks, 2.07 s compile waits and 2.86 s pipeline creation.
The one-worker control measured 3.07 s aggregate tasks, 1.22 s waits and 2.65 s pipelines.

Reproduce both configurations with the same model/session in each process:

```powershell
$env:EVE_ONNX_PROFILE='1'
$env:EVE_ONNX_COMPILER_WORKERS='4' # use 1 for the single-worker control
python scripts/run_kokoro_onnx.py --model model.int8.onnx --voices voices.bin --tokens tokens.txt --probe build/win32-debug/test/onnx_engine_probe.exe --work build/kokoro-repeat --gpu --repeat 2
```

`result.json` includes per-iteration latency, compilation/cache-hit counts, peak workers
and exact repeat verification. Hardware contracts cover serial/parallel dependent chains
across submission boundaries, changed shapes/constants, cold cancellation/retry,
compilation failure/recovery, optional-device absence and both device/session destruction orders.

Architecture rules applied: checked Results, ownership and device-thread contracts,
transactional publication, optional-device present/absent tests, failure injection,
deterministic RNG and foreign-format bounds. No ECS, Link, module boundary or engine
persistence changes are introduced.
