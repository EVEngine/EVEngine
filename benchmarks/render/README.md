# High-poly render benchmark and profiling

## General 3D gameplay workloads

`run_gameplay.py` measures the native scene → animation → palette publication →
render/present path. These are reproducible synthetic workloads for prioritizing
CPU work, not a claim that cubes or a procedural rig reproduce a finished game.

| Workload | Product behavior represented | Work kept active in measured frames |
| --- | --- | --- |
| `3d-gameplay-static` | Exploring a mostly static environment | Moving camera, clean SceneHost, linked renderables, culling and submission |
| `3d-gameplay-dynamic` | Doors/platforms/props attached to moving parents | Every tenth 16-prop group moves and rotates; descendants propagate through real scene links |
| `3d-gameplay-animated` | Several independently animated characters | Looping clips, phase/speed variation, recurring 0.3-second cross-fades, world poses, separate GPU palettes |
| `3d-gameplay-mixed` | Combined environment and characters | Dynamic parents plus the complete animation and render paths |

The default matrix uses 256/1000/5000 props and 8/32/64 characters where applicable.
Props share two mesh types and eight textures/material settings. Each character has
64 bones, 2048 skinned vertices, two influences per vertex, dense rotation tracks
(61 keys over two seconds), sparse translation tracks (1 or 3 keys), and one scale
key. Meshes are separate per character because the engine owns palettes on Mesh.
There is no imported art, texture streaming, script dispatch, physics, animation LOD,
occlusion benchmark, or automatic batching claim. Shadow variants are explicit.
Treat these sizes as workload tiers, not measured requirements from a shipping game;
calibrate them against project captures before setting a frame-budget gate.

Build `render_benchmark` in Release with `scene`, `animation` and `model3d` enabled,
plus `EVENGINE_BUILD_RENDER_BENCHMARK=ON`. For a minimal composition add
`-DEVENGINE_MODULE_scene=ON -DEVENGINE_MODULE_animation=ON -DEVENGINE_MODULE_model3d=ON`
to the normal build configuration. The gameplay source is included only when Scene
and Animation targets exist; the existing high-poly and legacy workloads still work.

```sh
python benchmarks/render/run_gameplay.py <render_benchmark> --output artifacts/gameplay
python benchmarks/render/run_gameplay.py <render_benchmark> --shadows --output artifacts/gameplay-shadows
python benchmarks/render/run_gameplay.py <render_benchmark> --cpu-only --output artifacts/gameplay-cpu
python benchmarks/render/run_gameplay.py <render_benchmark> --profile --output artifacts/gameplay-profile
python benchmarks/render/run_gameplay.py <render_benchmark> --quick --frames 20 --repeats 1 --width 320 --height 180
```

On Linux use the documented Xvfb/Vulkan/null-audio environment. `--cpu-only` still
initializes real mesh/skinning resources but skips render/present during measurement;
it isolates SceneHost and animation CPU demand and is **not** a submission benchmark.
Software-Vulkan frame times cannot establish hardware GPU throughput. Use a real GPU
for the normal matrix and compare active device, resolution, shadows and build flags.

Use `--lighting forwardPlus` / `--lighting hybrid` to compare rendering paths and
pass `--no-msaa` to **both** runs: Hybrid currently disables MSAA. Per-case JSON
records the effective `lightingMode` and `msaaEnabled`, and rejects an unexpected
Hybrid fallback. Direct invocations use `EVENGINE_BENCH_LIGHTING` and
`EVENGINE_BENCH_MSAA=0`. The engine's default remains ForwardPlus.

Use `--skin-influences 1|2|3|4` (default 2) to vary the number of nonzero bone
weights per character vertex. Direct runs use `EVENGINE_BENCH_INFLUENCES`; JSON
records `skinInfluences` and the runner verifies it. Joint indices remain valid
even in zero-weight slots. Compare identical weight counts and shadow settings:
a two-weight fixture alone cannot establish a general skinning optimization.

The [2026-10-31 sparse-weight experiment](results/2026-10-31-skin-zero-weight.json)
compares Vulkan vertex shaders before/after skipping zero-weight matrix additions
in forward, G-buffer and shadow passes. On the same software-Vulkan environment,
default ForwardPlus/MSAA, 1000 props and 32 actors, the three-pair two-weight screen
improved median frame mean from 112.093 to 107.285 ms (4.3%). Two-pair follow-ups
measured 110.928 → 101.391 ms for one weight, 110.964 → 101.539 ms for four weights,
and 135.953 → 126.280 ms for two weights with shadows. All paired captures were
byte-identical. Four-weight gains may involve driver code generation; no zero-weight
fetch saving exists in that case. The 256-prop static control was 25.721 → 26.185 ms
(1.8% slower): no static-scene benefit or universal speedup is claimed. Run ranges,
tail timings and short/shared-VM limitations are retained in the result. WebGPU's
skinning shaders are unchanged; hardware GPU performance remains unmeasured.

Vulkan's deferred graph now caches two plans per in-flight slot: G-buffer-only
when there are no pending shadow updates, and shadow layers plus G-buffer when
updates are present. The former avoids seven idle shadow clear passes. Shadow
images are still initialized at allocation; both plans are retired before target
resize/destruction. Empty submitted shadow updates retain their clear operation,
so removing the last caster cannot leave a stale shadow. This does not skip
individual idle layers within a frame that has other pending shadow updates.

The [2026-10-31 idle-shadow-pass comparison](results/2026-10-31-idle-shadow-passes.json)
records final-binary alternating controls at 320×180, default ForwardPlus/MSAA:
256 static props fall from 25.487 to 16.493 ms (35.3%), and 1000 props plus 32
actors from 100.186 to 91.104 ms (9.1%), using medians of three per-run frame means.
The mixed workload includes one slower run and a higher median p99; it is not a
universal latency improvement. With shadows active, two-pair means are essentially
unchanged (125.824 → 124.451 ms). All captures are byte-identical. Five isolated
runtime cases (331 assertions), Vulkan validation of shadow transitions/resize,
and `make check CI_BASE=HEAD` pass. Validation emits unused-attribute performance
warnings but no validation errors. Results remain software-Vulkan measurements.
The same record includes four longer static controls for the prior skinning change:
pair directions reverse and median time is 1.1% lower, so the earlier 1.8% slowdown
did not reproduce as a stable regression; no static-skin speedup is claimed.

Hybrid core opaque lighting shares the Forward BRDF, hemisphere fill and point-light
attenuation; point-only clustered scenes no longer acquire an extra default sun.
The image regression covers unshadowed directional/point lights, metallic and
dielectric surfaces at two roughness levels. This is not full visual equivalence:
G-buffer quantization, shadow filtering and advanced material/environment features
still require separate evaluation before selecting Hybrid for a project.

The [2026-10-31 core-lighting comparison](results/2026-10-31-hybrid-core-lighting.json)
records three alternating pairs per scene on llvmpipe, 320×180, no shadows/MSAA,
60 warmup + 90 measured frames. Median frame means are 24.983 → 21.881 ms for
256 static props (12.4% lower), and 85.400 → 50.877 ms for 1000 props plus 32
animated characters (40.4% lower). These compare two modes on the same changed
binary; they are not a default-mode speedup or hardware-GPU result. Independent
state hashes match. Forward's default-mode capture remains byte-identical to the
pre-change capture. Hybrid/Forward mean RGB error is below 0.11/255 per channel,
but the mixed capture contains 99/57600 pixels with a channel error above 8/255
(maximum 126/255); average agreement is not pixel equivalence.

Every case runs in its own process with 60 advancing warmup frames, followed by 300
measured frames by default. Three repetitions alternate case order. `phases` reports
actual single-frame distributions; `summary` is the median of per-run p50 values.
Animation includes sampling, blending, world propagation and palette publication;
its three child metrics break those steps down and must not be added to their parent.
Render/present timings include their CPU waits, and delayed GPU queue spans overlap
those waits: do not sum GPU timing with CPU phases. Profiler aggregation and output
are outside the measured frame. Profiled runs are diagnostic, not timing controls.
Setup includes scene/rig/resource creation, while `ecsTeardownMs` includes actors and
scene entities but excludes Graphics-owned resource retirement and window shutdown.
Window/device initialization precedes `setupMs` and is reported by startup logs.

Validation checks bind-palette identity, independent analytic scene world positions,
exact linked-matrix publication, finite bone/palette values, and continuing playback.
The final state hash covers all linked world matrices, bone worlds and published
palettes; repeated processes must match on the same build/machine. A hash is not a
cross-platform replay guarantee or pixel-correctness test. To inspect actual output,
run one case with `EVENGINE_BENCH_CAPTURE=<path.ppm>`; readback uses the engine's
presented-frame path after timing. Xvfb framebuffer captures are not used.

The runner records executable hashes, build configuration, source revision, individual
runs and validation failures in `results.json`; only `status=passed` is a complete
matrix. Check matching workload fields and state hashes before comparing timings.

The [2026-10-31 CPU baseline and comparison](results/2026-10-31-gameplay-cpu.json)
records 36 before and 36 after runs on a shared AMD EPYC VM, GCC 14.2 Release,
render/present disabled during timing. Phase timers and sampling identified clip
sampling and scene propagation as the main CPU paths. Root-motion extraction now
samples only its selected bone instead of redundantly evaluating every bone; displayed
poses still use the full sampler. A separate three-pair alternating control at 1000
props/32 characters, 600 measured frames, reduced median sampling p50 from 0.122932
to 0.065648 ms (46.6%), and CPU-update p50 from 0.348186 to 0.282689 ms (18.8%).
These are CPU workload results, not full-game FPS gains. Static/dynamic control cases
have no intended animation benefit, and shared-VM variability remains visible in the
individual runs. All 72 matrix runs matched their before/after world/bone/palette
hashes. Thirty focused animation cases passed in isolated processes; the engine-owned
640x360 mixed-scene captures before/after were byte-identical. Hardware-GPU throughput
and the complete engine runtime test suite were not validated in this environment.

The [follow-up storage and scene-link comparison](results/2026-10-31-gameplay-storage-links.json)
keeps the root-motion optimization in its baseline. It compares that baseline,
palette-capacity reuse alone, and capacity reuse plus one component resolution per
3D scene-link update. Three alternating runs per variant/scenario (27 processes,
60 warmup + 1200 measured frames) matched all world/bone/palette hashes. Median
CPU-update p50 decreased from 0.124334 to 0.093649 ms for 1000 dynamic props (24.7%),
0.310270 to 0.259926 ms for 1000 props/32 actors (16.2%), and 1.115630 to 0.949239 ms
for 5000 props/64 actors (14.9%). Palette reuse alone showed little total-time
benefit; its purpose is to remove recurring allocations. Capacity is retained at
the mesh's high-water mark until destruction. These shared-VM CPU results do not
measure GPU throughput or full-game FPS.

The [scene dirty-scan follow-up](results/2026-10-31-gameplay-scene-scan.json) merges
legacy and subtree dirty detection after pull callbacks, without skipping liveness
checks or changing callback order. Across three alternating runs per case (24
processes), static scene-update p50 changed from 0.041251 to 0.040220 ms for 1000
props and 0.326003 to 0.319704 ms for 5000 props: a small roughly 2% reduction.
Dynamic/mixed measurements remain noisy; no general mixed-scene gain is claimed.
All state hashes matched and 16 focused scene cases passed in separate processes.
Animation pose-buffer swapping was not applied because world-state observations
and borrowed-reference semantics require a separate contract review.

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
