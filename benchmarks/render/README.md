# High-poly render benchmark and profiling

The existing `render_benchmark` gains opt-in static high-poly workloads. Normal builds and CI
keep `EVENGINE_BUILD_RENDER_BENCHMARK=OFF`; this is a rendering-capacity experiment, not a full
textured/skinned gameplay benchmark.

## Build and run

Use the root Makefile and the dependency setup in `Readme.en.md`. For an interactive Debug smoke:

```powershell
make build/win32-debug CMAKE_EXTRA_ARGS=-DEVENGINE_BUILD_RENDER_BENCHMARK=ON
cmake/with-msvc.cmd cmake --build build/win32-debug --target render_benchmark --parallel 8
python benchmarks/render/run_highpoly.py build/win32-debug/benchmarks/render/render_benchmark.exe --quick --frames 120
```

For performance conclusions, build current-source **Release** with matching Release dependencies;
use `make build/win32 CMAKE_EXTRA_ARGS=-DEVENGINE_BUILD_RENDER_BENCHMARK=ON` and build the
`render_benchmark` target in that configuration. On Linux use the documented Vulkan/Xvfb setup;
software Vulkan results must not be compared with hardware GPU capacity.

```powershell
python benchmarks/render/run_highpoly.py <render_benchmark.exe> --frames 600 --output artifacts/highpoly
python benchmarks/render/run_highpoly.py <render_benchmark.exe> --width 3840 --height 2160 --frames 600
python benchmarks/render/run_highpoly.py <render_benchmark.exe> --model <asset.obj> --frames 600
```

The runner detects the CMake root for DLL lookup; use `--runtime-dir` for another install layout.
Each case runs in a separate process with a timeout. Logs and `results.json` include executable
and engine-DLL hashes, build configuration, adapter identity, setup time, triangle counts,
wall-frame means/percentiles and delayed GPU timings. A failed case exits nonzero. Unknown
report fields may be ignored; unsupported schema versions must be rejected.

The default is 1920x1080, 60 warmup + 600 measured frames, VSync **off**, hidden window. Use
`--show-window` for interactive viewing. This is hidden-window rendering through the production
SDL/Vulkan swapchain, not surface-free rendering: the existing headless path skips `present()`.
The runner confirms actual window visibility. These benchmark settings do not change game defaults.

## Workloads

`3d-highpoly` and `3d-highpoly-shadows` generate an indexed cube projected onto a sphere, with
outward-facing triangles and unit normals. `EVENGINE_BENCH_SUBDIV=1..512` controls resolution:
`12 * subdivisions^2` triangles per mesh. The matrix uses subdivisions 32, 128, 256 and 512,
then 4/16/64 shared instances at 128, and 16 instances with directional shadows. Generation,
import and upload are reported separately from steady-state rendering.

`EVENGINE_BENCH_MODEL` optionally imports an Assimp-supported asset (OBJ/glTF/FBX depends on the
installed dependency build). This extracts static geometry only; textures, skinning and animation
are not benchmarked. Scene triangle counts describe authored geometry before culling and shadow
multipasses. Instances share one mesh and simple material, so these cases do not measure the VRAM
cost of many unique assets. Framing adjusts to keep the grid visible.

## Diagnostics and implementation

```powershell
python benchmarks/render/run_highpoly.py <render_benchmark.exe> --profile --recording parallel --output artifacts/profile-parallel
python benchmarks/render/run_highpoly.py <render_benchmark.exe> --profile --recording serial --output artifacts/profile-serial
python benchmarks/render/run_highpoly.py <render_benchmark.exe> --profile --gpu-timing off --output artifacts/profile-no-timer
```

- The common profiler measures captured CPU span rather than its own aggregation duration,
  groups equivalent zone names by content and thread identity, and labels collection/worker
  threads separately. Automatic 2D frame begin no longer overwrites earlier 3D work.
- GPU queries use independent per-slot ranges and prefix command buffers. The acquired slot
  fence protects reuse; availability is read without WAIT. The start timestamp precedes deferred
  submission, so geometry and shadows are included. The metric is a **delayed graphics-queue
  span**, including possible idle gaps, not exclusive shader execution time.
- `cpuZones` reports per-thread inclusive/self means and calls. Inclusive parent/child costs
  overlap; CPU waits overlap GPU execution. Do not add them. Aggregation overhead is separate,
  and report merging occurs outside timed render calls. Use unprofiled runs for capacity claims.
- Default `--recording auto` / `EVENGINE_FRAMEGRAPH_RECORDING=auto` selects inline recording
  when the graph's measured serial CPU cost is below 500 microseconds. More expensive graphs use
  the existing parallel executor, with a serial reprobe after 60 parallel frames. The render-thread
  owner resets history when graph topology rebuilds. Explicit serial/parallel modes remain controls.
- Canonical FXAA and exposure-composite outputs own one HDR canvas per acquired frame slot.
  Independent images/framebuffers/descriptors and slot-owned vertices are protected by the slot
  fence. Resize drains consumers and invalidates descriptor caches before retiring old targets.
  With two slots these two RGBA16F outputs add about **31.6 MiB** of color-image payload at 1080p
  (allocator overhead excluded). Ordinary canvases, standalone Exposure outputs, TAA and adaptation
  histories retain persistent/shared-image behavior. Synchronous backends use the shared-target default.
- Present-depth writes, including late fragment tests, have an external GPU dependency. This
  addresses the hazard exposed when removing the CPU all-frame drain without reintroducing that drain.

Game `config.nut` now accepts boolean `vsync`, defaulting to `true` even when the project replaces
its config table. `vsync = false` requests unsynchronized presentation before ordinary or delayed
editor window creation; non-booleans are rejected. Restart after editing config. Runtime bindings
`gfx.setVSyncCount(0|1)` / `gfx.getVSyncCount()` use the existing presentation interface. Vulkan
prefers Immediate when disabled and Mailbox/FIFO when enabled; unsupported modes retain backend
fallback behavior. WebGPU performance was not measured here.

## Recorded measurements

[Portable measurement summary](results/r9700-1080p.json) retains source snapshot, binary/DLL
hashes and run ranges without machine-specific paths. This is the pre-PR development snapshot,
**not a measurement of the rebased PR head**. AMD Radeon AI PRO R9700, Vulkan Release, 1080p,
VSync off, hidden window, profiling/validation off; three independent runs per case, 60 warmup +
600 measured frames each. Values are medians across runs; listed p95 values are medians of per-run
p95, not pooled-frame percentiles.

- 196,608 scene triangles with shadows: **480.3 -> 1005.6 FPS (+109.4%)**; frame mean
  **2.082 -> 0.994 ms**, p95 **2.545 -> 1.169 ms**. FPS ranges: 479.1-483.9 / 983.2-1173.9.
- 3,145,728 triangles with shadows: **71.3 -> 70.3 FPS (-1.4%)**; frame mean
  **14.025 -> 14.226 ms**, p95 **14.410 -> 15.619 ms**. Ranges: 69.7-75.3 / 67.3-77.0.
- 12,582,912 triangles without shadows: **68.4 -> 67.7 FPS (-1.1%)**; frame mean
  **14.613 -> 14.770 ms**, p95 **15.232 -> 16.018 ms**. Ranges: 68.3-71.5 / 67.1-71.3.

The small workload improves substantially; the high-poly cases show **no established throughput
or tail-latency improvement** and remain GPU-bound. No statistical significance or engine-to-engine
ranking is claimed. Separate profiling found small-workload record/join falling from 1.289 ms to
0.059 ms (inline recording itself 0.050 ms). Default postprocessing no longer drains all frames,
but high-load slot acquisition still waits for the GPU. That waiting was moved to safe slot reuse,
not eliminated. Reduced distant/shadow geometry is a separate optimization.

## Validation scope

The development snapshot passed current-source Release compile/link, profiler composition tests,
frame-resource regression tests, VSync config tests, Vulkan synchronization validation across
legacy workloads, and `make check`. Config tests observed FIFO for default/true and Immediate for
false on the tested device. Engine-owned presented shadow captures were byte-identical before/after
(SHA-256 `4973cf772af7bc83c7a8cc1245576922a22cd4f5db162459920d7d0708794a16`). Local logs,
SDK binaries and captures are deliberately not versioned. PR-head validation is reported separately
in the pull request and hosted checks; the historical results must not be interpreted as hosted CI.

All new native tests are process-isolated and discovered from their paths/source policies; generated
module evidence comes from `scripts/test_module_coverage.py`. Resource ownership/lifetime, costs,
resize and teardown are documented; no module boundary, persistent format or ECS lifecycle changes
or architecture-rule exceptions are introduced.
