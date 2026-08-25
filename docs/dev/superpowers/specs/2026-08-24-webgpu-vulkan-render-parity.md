# WebGPU / Vulkan rendering parity plan

## Goal

The WebGPU backend must run the same public graphics API as Vulkan and produce
semantically equivalent images. Backend-specific shader input languages and
performance paths may differ, but silently ignoring a supported public setting
is not acceptable.

Parity is measured in three layers:

1. API behavior: the same call succeeds, rejects the same invalid input, and
   preserves resource ownership and lifetime rules.
2. Render semantics: geometry coverage, depth ordering, blending, material
   controls and effect enable/disable behavior agree.
3. Image similarity: deterministic scenes rendered by both backends stay within
   the documented pixel tolerances. Byte-identical output is not required
   because rasterization and floating-point behavior vary by driver.

## Current capability matrix

Status values are `supported`, `partial`, `missing`, `intentional difference`,
and `test gap`.

| Area | WebGPU status | Difference from Vulkan | Required work |
| --- | --- | --- | --- |
| Headless device + canvas | supported, verified | Shared semantic tests run on Vulkan and native Dawn | Extend coverage as new features land |
| Dynamic texture update | supported, verified | Uploads the existing mip chain in place and rejects size changes consistently | Add pointer-stability and mip-level coverage |
| Dynamic mesh update | supported, verified | Reuses/grows vertex and index buffers with aligned Uint16 uploads | Add resize, keep-index and Uint16/Uint32 transition coverage |
| 2D solid/textured drawing | supported, verified | Shared Dawn/Vulkan pixel tests lock UV cropping, solid and textured rotation, and opaque/additive/premultiplied/multiply blend equations | Extend the artifact set with rotated-atlas UV orientation |
| 2D lighting | supported, verified | WGSL now matches Vulkan normal-map decoding, logical screen coordinates, directional response, point attenuation and eight-light accumulation | Extend artifacts with moving lights and atlas normals |
| Texture sampling/mipmaps | supported, verified | Shared pixel tests cover nearest/linear filtering, repeat addressing and forced final-LOD sampling; WebGPU now builds stable box-filtered 2D/cubemap chains, including one-pixel NPOT edges, and exposes/clamps anisotropy limits like Vulkan | Extend artifacts with trilinear transitions and cubemap LOD selection |
| Basic 3D/PBR | supported, verified | WebGPU matches Vulkan's derivative-built tangent-space normal mapping, POM UV displacement, texture cell bombing, environment reflection/irradiance, procedural cloud shadows, hemispheric wrap fill and emissive/tonemap order; mesh materials honor the albedo texture's sampler instead of substituting a global sampler; shared pixel tests exercise every feature on both backends | Extend stored artifacts with roughness/metallic charts and animated cloud-shadow frames |
| Surface modes | supported, verified | Vulkan and WebGPU now select the same opaque/alpha/additive/premultiplied/multiply, depth-write and culling variants; transparent surfaces blend with material alpha while opaque outputs retain linear depth in alpha | Extend ordering artifacts to intersecting transparent meshes and MSAA targets |
| Masked materials | supported, verified | Cutoff, screen-space alpha-hash dither and coverage fallback share Vulkan semantics and have backend-neutral visibility-count tests | Add an MSAA alpha-to-coverage path when portable WebGPU sample-count support is enabled |
| Alpha-cutout shadow | supported, verified | Dedicated albedo/UV alpha-discard depth pipelines produce the same masked-caster shadow response on Vulkan and WebGPU | Add direct shadow-depth coverage artifacts when portable depth readback is available |
| Alpha-cutout GBuffer | supported | Dedicated alpha-discard pipeline and backend-neutral pixel test | Keep the Vulkan/Dawn pixel artifact in CI |
| Cascaded shadows | supported, core verified | WebGPU records each caster into the correct array layer, captures receive-shadow per draw, and matches Vulkan cascade selection, slope bias, split cross-fade and strength interpolation; shared scene tests verify receiver gating and 0/0.5/1 strength output | Add a camera-motion artifact crossing both cascade boundaries |
| GBuffer | supported for native consumers | Normal/depth/albedo encoding, clear values and CPU readback match Vulkan exactly in the parity scene; generic SPIR-V post remains unavailable | Enable additional WGSL-native consumers incrementally |
| SSAO | supported, verified | Native SSAO/HBAO/GTAO and blur/overlay pipelines consume filterable linear depth on WebGPU | Extend the artifact set to every AO mode |
| Decals | supported, verified | Native WGSL and Vulkan pipelines share box projection, depth reconstruction, normal rejection, edge feathering and forward composition semantics | Extend coverage to normal/roughness/metal/emissive channels |
| Outline | supported | Native WGSL depth/normal edge pipeline is selected by the public effect API | Add more geometry silhouettes to the image set |
| Anti-aliasing post effects | supported | Native WGSL FXAA/NFAA/SMAA/SSAA pipelines are selected by built-in shader identity | Compare edge metrics rather than exact pixels |
| Global illumination / SSR | supported | Native WGSL SSGI and SSR pipelines consume the parity-locked GBuffer | Extend reflective material/camera-angle artifacts |
| Volumetric effects | supported | Native WGSL cloud/fog/froxel/raymarch/post stages are available | Extend low-resolution parameter coverage |
| Alpha mask | supported, verified | Runtime backend selection now reliably chooses WGSL on WebGPU and SPIR-V on Vulkan; shared pixel tests cover threshold, softness and inversion | Extend coverage to atlas UVs and tinted translucent colors |
| Water / waterfall built-ins | supported, verified | WebGPU uses WGSL ports with the same packed parameter layout and Vulkan retains the packaged SPIR-V shaders; shared pixel tests verify animated ripples/reflection plus waterfall flow/foam response | Extend artifacts to SSR and planar-reflection combinations |
| Custom 2D/mesh WGSL | partial | WGSL is supported only on WebGPU; Vulkan accepts SPIR-V instead | Use backend-specific shader fixtures with identical semantics |
| Runtime GLSL and SPIR-V on browser | intentional difference | Browser WebGPU accepts WGSL, not Vulkan SPIR-V or runtime GLSL | Keep explicit errors; document paired shader assets/toolchain |
| GPGPU dispatch and recorded sequences | supported, verified | Backend-paired GLSL/WGSL tests cover storage upload/readback, ECS dispatch and multiple dependent dispatches recorded into one submission | Extend tensor and GPU-cloth integration coverage to WebGPU |
| Hair/custom hair shaders | supported, explicit language contract | Built-in hair selects the matching WGSL/SPIR-V implementation per backend; WebGPU accepts custom WGSL and explicitly rejects SPIR-V instead of silently substituting the built-in shader | Add paired custom-shader visual artifacts |
| Grass built-in | supported, verified | Both backends use billboard expansion, authored sway atlases, alpha cutout, light/dark layers and the same PBR-style CSM receiver; WebGPU clears empty cascade layers and mirrors shadow-sample Y consistently, preventing blanket false shadow | Keep paired grass lighting artifacts and thresholds in CI |
| Weather precipitation / lightning | supported, API verified | Native WGSL mirrors camera-centred rain/snow recycling, wind billboards, density/fog filtering and dithered lightning; Vulkan retains packaged SPIR-V | Add deterministic rain/snow/lightning image artifacts |
| SpriteStack 3D / batch | supported, verified | WebGPU now uses the stack's own alpha-blended WGSL card shader, including atlas UV rect and alpha cutoff, rather than silently substituting the unrelated built-in hair shader | Extend artifacts to CSM shadow and outline combinations |
| Stylize post / mesh effects | supported, core verified | Cartoon, watercolor, ink and pixel post passes plus cartoon/ink mesh variants have native WGSL implementations with the same packed public parameters; shared creation and rendering tests run on both backends | Compare the full cylinder gallery and tune image tolerances |
| Font rendering | supported | Native and browser WebGPU render supplied font atlases; browser builds do not synthesize atlases with FreeType | Add packaged-atlas browser examples |
| GPU-driven/indirect/visibility buffer | supported, verified | WebGPU compute-compacts 256-byte-aligned per-bucket model ranges, emits portable indirect commands, builds a full previous-frame HZB mip chain and uses eye/FOV/clip inputs for conservative occlusion before the ID/barycentric visibility pass | Extend the parity scene to browser artifacts and mixed opaque/transparent content |
| Virtual geometry GPU path | supported, verified | WebGPU uses compute-written per-cluster indirect commands (`instanceCount=0` for culled clusters), consumes the shared previous-frame HZB and feeds the same ID/bary resolve semantics as Vulkan; both integrated backends currently draw full-detail clusters | Add DAG traversal/SSE selection to both backends and large multi-asset artifacts |
| Standalone `VirtualGeometryRenderer` | supported, verified | WGSL ports of the cluster-DAG cull and atomic software raster passes share the existing build, resolve and LOD-transition tests with Vulkan | Add browser execution and pixel-hash artifacts |
| Native macOS surface | supported, CI verified | SDL Cocoa windows are backed by a retained CAMetalLayer and native Dawn Metal surface | Keep the targeted macOS render tests in CI |
| Browser render tests | supported in CI | Chromium/SwiftShader launches the WASM example, captures a frame and rejects black/white/flat output | Add scene-by-scene browser semantic assertions |
| Voxel vertex AO | supported, verified | Both backends consume the same packed 2-bit-per-corner AO and pass a shared pixel-darkening test | Add AO image artifact to the comparator set |

## Re-audit findings (2026-08-25)

Source-level re-audit after the core parity work originally found missing HZB
consumption in both WebGPU GPU-driven paths. Those parity gaps are now closed.

The ordinary GPU-driven path now consumes `eye`, `fovYDeg`, `nearZ` and `farZ`,
builds all HZB mips from the previous G-buffer depth, and has a GPU indirect-count
regression proving that a fully hidden in-frustum instance is removed.

The integrated virtual-geometry path now shares the same HZB semantics and has
the same GPU indirect-count regression. DAG screen-space-error selection is not
a Vulkan/WebGPU gap: the current Vulkan shader also documents full-detail
cluster rendering and leaves DAG LOD as follow-up work for both backends.

Backend shader languages remain an intentional contract difference: Vulkan
accepts SPIR-V/GLSL assets and browser WebGPU accepts WGSL. Stale Vulkan-only
documentation for decals, dynamic updates, SpriteStack and standalone virtual
geometry has been corrected to match the implementations and tests.

## Final re-audit findings (2026-08-25)

A second source audit covered the WebGPU graphics, GPGPU and virtual-geometry
implementations after HZB work landed. It found no remaining silent graphics
fallbacks or public render settings ignored only by WebGPU. Explicit SPIR-V
rejections remain intentional: Vulkan consumes SPIR-V while WebGPU consumes
WGSL. Adapter name, vendor and memory reporting also remain intentionally
unavailable on privacy-preserving browser WebGPU; the capability reports the
backend and readiness without inventing hardware data.

The audit did expose two reference-path defects while warning-only assertions
were converted to fatal conformance checks. Vulkan failed to reject an
out-of-range dynamic-mesh index, and VKBuilder discarded custom color-blend
attachments while constructing Vulkan pipelines. Both are fixed. The latter
made Vulkan additive, premultiplied and multiply pipelines use the requested
blend equations instead of an accidental default.

The subsequent full-suite pass exposed one WebGPU-only window-path defect that
the headless artifact matrix did not exercise: `begin3DFrame()` cleared cascade
draws that `RenderSystem3D` had just recorded, leaving every shadow layer empty.
WebGPU mesh bind-group caching also omitted the in-flight UBO buffer identity,
so a group could retain another frame slot's uniform arena. Both are fixed;
all four `Shadow3D.*` cases now pass on native Dawn, including caster enable,
per-receiver gating, strength interpolation and masked casters.

All assertions in `graphics_backend_parity.cpp` are now fatal. The complete
suite passes 13/13 isolated cases on both native Vulkan and native Dawn. A fresh
pairwise comparison of 31 named 64x64 artifacts produced byte-identical RGB for
29 scenes. Texture cell bombing has mean linear-RGB error 0.000115 (p99 0), and
parallax mapping has mean 0.001434 with p99 0.068842; both remain below the
documented lit-3D limits. Artifact manifests now identify 3D scenes as `lit3d`
instead of applying the stricter `flat2d` profile indiscriminately.

Remaining work is feature growth shared by both backends rather than a current
WebGPU/Vulkan parity defect: DAG/SSE selection for integrated virtual geometry,
broader animated/effect artifact coverage, and browser execution of the full
native parity matrix.

## Test architecture

### Backend-neutral conformance tests

Tests must call `Graphics::create()` and must not assert a hard-coded backend
name. The same source is compiled once with Vulkan and once with native Dawn.
Each test renders to an RGBA8 `Canvas`, reads it back, and asserts semantic
properties such as coverage, channel dominance, depth ordering and blend
equations.

The first suite is `graphics.backendParity.*`:

- clear and solid rectangles;
- every 2D blend mode;
- UV crop and rotated UV;
- nearest/linear and repeat sampling;
- in-place texture update with stable `Texture*`;
- mesh vertex update, growth, index preservation and Uint16/Uint32 transition;
- opaque 3D depth ordering and basic PBR lighting.

### Cross-backend image comparison

Both backends write named PNGs plus a JSON manifest containing backend, adapter,
scene, dimensions and feature flags. `scripts/compare_render_backends.py`
compares matching images after converting them to linear RGB.

Default acceptance thresholds:

- identical dimensions and alpha-coverage difference no greater than 0.5% of
  pixels;
- mean absolute RGB error no greater than 2/255 for flat 2D scenes;
- mean absolute RGB error no greater than 6/255 and 99th-percentile error no
  greater than 20/255 for lit 3D scenes;
- edge-aware effects use a structural/edge metric and feature-specific
  thresholds instead of exact pixels.

Threshold changes must be justified per scene; a global tolerance increase is
not an acceptable fix.

### CI lanes

1. Existing Vulkan graphics tests remain the reference lane.
2. Add native Dawn Debug with `BUILD_PLATFORM=webgpu` and
   `BUILD_TESTING=ON`, filtered initially to `graphics.backendParity.*`.
3. Upload the two parity artifact directories and run the comparator.
4. Add a browser Chromium/SwiftShader lane for WASM smoke scenes. Browser
   results are compared separately because SwiftShader and native Vulkan can
   have different precision characteristics.

## Implementation order

### P0: make parity measurable and fix incorrect core behavior

- [x] Replace the unreachable Dawn revision with a verified fixed commit.
- [x] Implement WebGPU in-place texture updates.
- [x] Implement WebGPU dynamic mesh updates and aligned Uint16 uploads.
- [x] Implement WebGPU headless device/canvas initialization.
- [x] Add `graphics.backendParity` 2D/resource tests.
- [x] Add the PNG manifest/comparison script and native Dawn CI lane.
- [x] Split material surface mode from SSAO strength.
- [x] Honor transparent blend, depth-write and double-sided settings per draw.

### P1: geometry visibility and deferred data

- [x] Implement alpha-cutout shadow rendering.
- [x] Implement alpha-cutout GBuffer rendering.
- [x] Lock GBuffer normal/depth/albedo conventions with tests.
- [x] Make SSAO intensity functional and enable GBuffer post capability for
      completed consumers.
- [x] Implement decals, forward composition and cross-backend image coverage.

### P2: built-in effects and content workflows

- [x] Port anti-aliasing and outline built-ins to WGSL.
- [x] Port GI and SSR after GBuffer parity passes.
- [x] Port volumetric built-ins.
- [x] Add native Dawn font rendering and browser font-atlas support.
- [x] Add built-in WGSL hair shading.

### P3: performance and platform completeness

- [x] GPU-driven opaque submission, compute compaction and indexed-indirect draws.
- [x] Visibility-buffer attachments, barycentric reconstruction and fullscreen PBR resolve.
- [x] Virtual geometry compute cull, indirect visibility raster and material resolve.
- [x] Standalone `VirtualGeometryRenderer` WGSL compute/raster/readback backend.
- [x] Native macOS Dawn surface.
- [x] Browser WebGPU pixel lane on CI.

## Definition of done for each item

An item is complete only when:

1. the WebGPU implementation no longer throws, silently ignores, or falls back
   for that declared capability;
2. a backend-neutral semantic test passes on Vulkan and native Dawn;
3. a named cross-backend image scene passes its documented tolerance;
4. WebGPU validation reports no errors;
5. the capability matrix and platform README are updated.
