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
| 2D solid/textured drawing | partial | Basic paths exist, including blend pipeline variants, but have no cross-backend image comparison | Add deterministic blend/UV/rotation tests |
| 2D lighting | partial | WGSL path exists but is not compared against Vulkan | Add normal-map and multi-light parity scenes |
| Texture sampling/mipmaps | partial | Sampler recreation, mip generation and anisotropy exist; limits and LOD output are unverified | Add nearest/linear/repeat/mipmap scenes and capability assertions |
| Basic 3D/PBR | partial | WGSL forward path exists; lighting, normal, parallax, environment and cloud-shadow output are unverified | Add isolated material feature scenes |
| Surface modes | supported, test gap | Blend mode, depth-write and double-sided state are captured per draw and select dedicated pipelines | Add ordering and culling image scenes |
| Masked materials | partial | Surface mode, cutoff and SSAO strength now have independent UBO fields | Test cutoff, dither and coverage |
| Alpha-cutout shadow | supported | Dedicated albedo/UV alpha-discard depth pipeline | Add a shadow-depth coverage artifact when depth readback is available |
| Alpha-cutout GBuffer | supported | Dedicated alpha-discard pipeline and backend-neutral pixel test | Keep the Vulkan/Dawn pixel artifact in CI |
| Cascaded shadows | partial | Opaque CSM path exists; cascade selection/bias/output have no Vulkan comparison | Add cascade boundary and receiver tests |
| GBuffer | supported for native consumers | Normal/depth/albedo encoding, clear values and CPU readback match Vulkan exactly in the parity scene; generic SPIR-V post remains unavailable | Enable additional WGSL-native consumers incrementally |
| SSAO | supported, verified | Native SSAO/HBAO/GTAO and blur/overlay pipelines consume filterable linear depth on WebGPU | Extend the artifact set to every AO mode |
| Decals | supported, verified | Native WGSL and Vulkan pipelines share box projection, depth reconstruction, normal rejection, edge feathering and forward composition semantics | Extend coverage to normal/roughness/metal/emissive channels |
| Outline | supported | Native WGSL depth/normal edge pipeline is selected by the public effect API | Add more geometry silhouettes to the image set |
| Anti-aliasing post effects | supported | Native WGSL FXAA/NFAA/SMAA/SSAA pipelines are selected by built-in shader identity | Compare edge metrics rather than exact pixels |
| Global illumination / SSR | supported | Native WGSL SSGI and SSR pipelines consume the parity-locked GBuffer | Extend reflective material/camera-angle artifacts |
| Volumetric effects | supported | Native WGSL cloud/fog/froxel/raymarch/post stages are available | Extend low-resolution parameter coverage |
| Alpha mask | supported, unverified | Has a dedicated WGSL implementation | Add mask threshold/softness/inversion tests |
| Custom 2D/mesh WGSL | partial | WGSL is supported only on WebGPU; Vulkan accepts SPIR-V instead | Use backend-specific shader fixtures with identical semantics |
| Runtime GLSL and SPIR-V on browser | intentional difference | Browser WebGPU accepts WGSL, not Vulkan SPIR-V or runtime GLSL | Keep explicit errors; document paired shader assets/toolchain |
| Hair/custom hair shaders | supported for built-in hair | Anisotropic built-in WGSL shading is available; arbitrary SPIR-V hair remains an intentional input-language difference | Define a portable custom WGSL/SPIR-V shader-pair contract |
| Font rendering | supported | Native and browser WebGPU render supplied font atlases; browser builds do not synthesize atlases with FreeType | Add packaged-atlas browser examples |
| GPU-driven/indirect/visibility buffer | partial | WebGPU compute-compacts visible transforms per mesh/material bucket and emits indexed-indirect commands; visibility-buffer resolve remains | Implement visibility attachments and fullscreen material resolve |
| Virtual geometry GPU path | missing, fallback available | Vulkan-only compute/indirect implementation | Implement after GPU-driven base path |
| Native macOS surface | supported, CI verified | SDL Cocoa windows are backed by a retained CAMetalLayer and native Dawn Metal surface | Keep the targeted macOS render tests in CI |
| Browser render tests | supported in CI | Chromium/SwiftShader launches the WASM example, captures a frame and rejects black/white/flat output | Add scene-by-scene browser semantic assertions |
| Voxel vertex AO | supported, verified | Both backends consume the same packed 2-bit-per-corner AO and pass a shared pixel-darkening test | Add AO image artifact to the comparator set |

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
- [ ] Visibility-buffer resolve and virtual geometry.
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
