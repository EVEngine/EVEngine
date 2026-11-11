# Experimental Vulkan path tracing

Status: draft implementation; not production or performance acceptance.

## Scope

The optional Vulkan KHR ray-tracing provider can render full path samples instead
of the existing screen-color reflection effect. Automatic world capture feeds
`RenderSystem3D` through `IRayTracing`, retaining observable raster fallback when
the provider/device or scene is unsupported. Existing reflection behavior remains
available independently.

Implemented:
- Prepared scene/frame tokens, immutable material/texture snapshots, cached shared
  BLAS geometry and reusable TLAS storage.
- Static mesh instances, diffuse/GGX metallic-roughness materials, emission,
  analytic lights, constant environment, next-event estimation and MIS.
- Base-color, metallic-roughness, emission and normal textures; UV0 transforms,
  wrap/filter semantics, authored/derived tangent frames and stochastic alpha.
- Exact sample batches and opt-in temporal reconstruction plus spatial filtering.
- Runtime `pathTracing` / `pathTemporal` switches and validated C++ per-frame
  sample settings in `RenderControl` (1..16, default 1).
- Native 1080p material gallery and deterministic scale/motion benchmark tooling.

## Ownership and boundaries

Graphics owns capture, render settings and frame selection; its existing optional
`IRayTracing` capability supplies the implementation. No new upward include,
module registration mechanism, global entity root or provider family is added.
Prepared tokens are checked for validity and owning backend. Texture snapshots
retain immutable bytes/resources and content identity; scene/material changes
invalidate reconstruction history. Borrowed scene input is consumed during
preparation, not retained as temporary raw pointers across frames.

Public operations document ownership/lifetime, thread affinity and cost. Fallible
operations return checked `Result`; invalid settings leave state unchanged.
Expensive preparation remains separate from frame recording. The hot renderer
uses typed fields, not per-frame feature-name lookups. No architecture exception.

## Reconstruction

Temporal mode reprojects static-world positions with instance, normal, albedo,
view and position checks. Camera/scene/seed/environment/sample-discontinuity/reset
conditions reject history. Identical-camera accumulation preserves stochastic
coverage instead of rejecting changing first-hit samples. Confidence is bounded
to 32 frames of samples, capped at 16 spp for weighting; it is not an exact
unbounded offline accumulation. Dynamic instance changes currently reset globally.
Two immutable descriptor sets alternate image roles without four full-frame copies.

Multi-sample temporal mode uses complementary uniform subpixel pairs. Only when
samples disagree across a surface/material boundary does a separate pixel-center
visibility ray provide reconstruction guides; it never adds radiance. Camera-axial
depth avoids random ray-length changes in planar filter weights. Multi-sample
spatial reconstruction uses a 7x7 normal/depth/albedo-guided kernel; single-sample
and spatial-only modes retain 5x5. Absolute sample offsets advance by batch size.

## Reproduction

Use the repository Makefile build routes. Run targeted graphics cases through
CTest or the corresponding `make test/<platform>` filter; do not run the full
local Windows suite. Regenerate shader arrays with
`python scripts/compile_graphics_rt_shaders.py`; shader ABI tests live in
`scripts/tests/test_compile_graphics_rt_shaders.py`.

The opt-in `graphics.raytracing.path_scene.gallery_showcase` case creates 40 objects,
47,964 instanced triangles, 10 materials, 512x512 wood/tile textures and three point
lights. It uses native 1920x1080, a continuous camera arc, 2 spp by default and four
bounces. `EVE_PT_GALLERY_SPP` selects the sample count. Set `EVE_PT_GALLERY_DIR` to a
fresh output directory and `EVENGINE_RENDER_PARITY_DIR` to its screenshot folder.
The case writes CSV timings and engine-owned PNGs at camera frames 120/420/719.
Measured frames exclude readback; screenshots use a separate continuous camera
traversal without stationary convergence. Vsync is disabled and the actual present
mode is checked. The test does not include game simulation.

`scripts/benchmark_path_tracing.py --help` documents the scale, geometry, texture,
instance-motion and camera-motion options. Its parser validates sample order,
configuration, finite timings, GPU availability and camera trajectories. GPU values
are completed queue intervals, not a same-row CPU/GPU decomposition. Profiling runs
are explicitly diagnostic rather than acceptance timings.

## Validation provenance and delivery limits

Development validation on Windows ARM64 / RTX Spark N1X passed Debug SHARED and
Release OBJECT builds and 14 focused cases with 2169 assertions per configuration,
without Vulkan validation errors (including synchronization validation). These
runs preceded rebasing this PR onto current dev and included local Release warning
fixes which are deliberately excluded from this feature PR. Five shader ABI tests
passed. The final silhouette regression resets history for 16 batches and preserves
partial coverage with zero measured color jitter; both 1/2-spp disocclusion checks
have zero error over 96 revealed pixels. These synthetic checks do not establish
arbitrary motion quality or eliminate every lighting firefly.

Those native runs used canonical dependency commits:
- `sunxfancy/VKBuilder`: `48978128084d9eb7c875f1fc9f7e37eabd0b18a9`, correcting
  device-local cube-image initial layout.
- `EVEngine/third-party`: `feb902dab93f12ed3aef43b74e1f6c9f5879e28f`, consuming it.

The engine's release pin is intentionally not changed to an unpublished package.
Dependency review/publication and a matching consumer pin are prerequisites for
merge-ready validation; no dependency patch, copied header or include shadow is
introduced. Current-dev native/CI validation must be evaluated separately from the
older development runs. Performance acceptance remains pending; no further
performance runs are part of this delivery.

Remaining limitations include moving-object reprojection, deforming meshes,
advanced transmission/material models, mip footprints, HDR environment sampling,
large real-game quality/scalability and complete provider-absent runtime acceptance.
Some boundary/lighting specks and occasional slow frames remain. The default raster
path and explicit unsupported/fallback diagnostics remain essential.
