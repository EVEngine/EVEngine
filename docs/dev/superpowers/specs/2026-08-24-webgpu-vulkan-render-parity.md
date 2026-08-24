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
| SSAO | partial | Intensity is stored independently and reaches forward/clustered WGSL; GBuffer post remains disabled | Compare disabled/enabled images and enable completed consumers |
| Decals | supported, verified | Native WGSL and Vulkan pipelines share box projection, depth reconstruction, normal rejection, edge feathering and forward composition semantics | Extend coverage to normal/roughness/metal/emissive channels |
| Outline | missing through public effect API | Built-in implementation creates SPIR-V shaders, which WebGPU rejects | Port built-in shader to WGSL and add depth/normal edge tests |
| Anti-aliasing post effects | missing through public effect API | FXAA/NFAA/SMAA/SSAA constructors create SPIR-V shaders | Port built-ins to WGSL; compare edge metrics rather than exact pixels |
| Global illumination / SSR | missing through public effect API | Built-ins create SPIR-V shaders and GBuffer post is disabled | Port after GBuffer conventions are locked by tests |
| Volumetric effects | missing through public effect API | Built-ins create SPIR-V shaders | Port the five shader stages and add low-resolution smoke/parity scenes |
| Alpha mask | supported, unverified | Has a dedicated WGSL implementation | Add mask threshold/softness/inversion tests |
| Custom 2D/mesh WGSL | partial | WGSL is supported only on WebGPU; Vulkan accepts SPIR-V instead | Use backend-specific shader fixtures with identical semantics |
| Runtime GLSL and SPIR-V on browser | intentional difference | Browser WebGPU accepts WGSL, not Vulkan SPIR-V or runtime GLSL | Keep explicit errors; document paired shader assets/toolchain |
| Hair/custom hair shaders | missing | WebGPU rejects the SPIR-V-only hair shader factory | Add built-in WGSL hair shader and a portable custom-shader contract |
| Font rendering | missing | WebGPU implementation throws; the browser profile also removes FreeType/font module | First support native Dawn using FontData, then decide browser atlas packaging |
| GPU-driven/indirect/visibility buffer | missing, fallback available | WebGPU uses the legacy per-draw path | Implement after visual parity; treat as performance parity, not image correctness |
| Virtual geometry GPU path | missing, fallback available | Vulkan-only compute/indirect implementation | Implement after GPU-driven base path |
| Native macOS surface | missing | Native Dawn backend throws instead of creating a Metal-layer surface | Add CAMetalLayer surface creation and a native smoke test |
| Browser render tests | test gap | WASM CI only compiles artifacts; it does not launch a WebGPU browser or inspect pixels | Add Chromium/SwiftShader smoke and parity artifact capture |

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
- [ ] Make SSAO intensity functional and enable GBuffer post capability for
      completed consumers.
- [x] Implement decals, forward composition and cross-backend image coverage.

### P2: built-in effects and content workflows

- [ ] Port anti-aliasing and outline built-ins to WGSL.
- [ ] Port GI and SSR after GBuffer parity passes.
- [ ] Port volumetric built-ins.
- [ ] Add native Dawn font rendering, then browser font-atlas support.
- [ ] Add built-in WGSL hair shading.

### P3: performance and platform completeness

- [ ] GPU-driven opaque submission and compute culling.
- [ ] Visibility-buffer resolve and virtual geometry.
- [ ] Native macOS Dawn surface.
- [ ] Browser WebGPU pixel lane on CI.

## Definition of done for each item

An item is complete only when:

1. the WebGPU implementation no longer throws, silently ignores, or falls back
   for that declared capability;
2. a backend-neutral semantic test passes on Vulkan and native Dawn;
3. a named cross-backend image scene passes its documented tolerance;
4. WebGPU validation reports no errors;
5. the capability matrix and platform README are updated.
