# WebGPU and Vulkan implementation gap (2026-10-30)

This inventory describes the current source tree. It separates implemented paths
from runtime evidence; a successful build or a single browser frame is not a
claim of feature or performance parity.

The [WebGPU specification](https://gpuweb.github.io/gpuweb/#limits) sets the
baseline `maxColorAttachmentBytesPerSample` to 32. Its
[plain-color-format table](https://gpuweb.github.io/gpuweb/#plain-color-formats)
assigns RGBA8 render targets a cost of eight bytes each, explaining the
five-target GBuffer's 40-byte requirement.

| Priority | Gap in the current implementation | Improvement and acceptance evidence |
| --- | --- | --- |
| P0 | The five-target GBuffer needs 40 color-attachment bytes per sample, above the WebGPU baseline of 32. | Device creation now requests the available budget up to 40; low-limit devices compile forward passes and skip GBuffer, decals and GPU-driven rendering. Direct GBuffer use throws a descriptive unsupported-limit exception. `RenderControl::requestFeature` returns structured `Unsupported` when a caller explicitly requests a GBuffer-dependent effect, while the old `enable/disable` methods remain compatibility facades. Dawn tests now request and receive a 32-byte device limit, initialize the engine with that device, and read back 2D and forward 3D pixels. A real adapter whose physical limit is 32 and browser presentation remain to be verified. |
| P1 | Browser `web` profile omits model/animation, audio, network, particles, voxel and other modules because of third-party or module dependencies (`platform/webgpu/README.md`). | Port individual dependencies behind their module boundaries, starting with assets needed by actual browser games. For each module, verify browser build, runtime use, provider-present/absent behavior and a representative visual or audio result. |
| P1 | The WebGPU mip-chain, half-float array and 3D texture upload implementations were excluded from Emscripten even though the resource-factory interface declares them. | The implementations now compile for Emscripten and wait for upload validation callbacks using browser event processing. Three existing native Dawn cases pass, covering mip-level pixel readback and array/volume upload. A browser build and runtime calls are still required before claiming browser support. |
| P1 | Browser shaders require WGSL. WebGPU rejects the public SPIR-V and runtime GLSL creation/replacement paths (`graphics/webgpu/Graphics.cpp`, `GraphicsShaderReload.cpp`). | The offline GLSL-to-WGSL step now fails the build and prints compiler diagnostics when an available GLSL compiler or Tint fails; previously it returned success and could reuse stale WGSL includes. WebGPU WGSL replacement and creation now wait for device validation before publishing pipelines; Dawn tests prove invalid 2D/Mesh/Hair WGSL creation is rejected and invalid replacement preserves the last good rendered shader. Mesh surface reconfiguration likewise commits only a validated candidate. The remaining work is a validated dual-backend asset pipeline from one authored source and browser runtime parity. |
| P1 | `updateTextureRegions` validates a batch but issues one `Queue::WriteTexture` per region; the Vulkan implementation packs the batch into one staging allocation and submission. | A native Dawn trial with 64 separate 7×9 RGBA regions per batch measured 2.33 ms/batch for the current path and 5.13 ms/batch for a fresh aligned staging buffer plus one command submission (100 batches). The trial path was reverted. Revisit only with representative workloads and a reusable staging design; verify pixels, overlapping updates, row strides, GPU lifetime and browser performance. |
| P2 | Vulkan has a device-gated hardware ray-tracing path; WebGPU has no corresponding implementation. | Keep the feature capability explicit. Evaluate a raster or compute substitute only for a specific effect with measured quality and cost targets. |

## Existing evidence

- `test/integration/render-pipeline/render_pipeline_graphics_graphics_backend_parity_tests.cpp`
  contains shared render scenes. Native Dawn and Vulkan artifacts are compared
  by `scripts/compare_render_backends.py` in Windows CI.
- WebGPU WASM CI now serves the artifact with COOP/COEP, waits for engine and
  adapter readiness, and checks canvas-only captures of the default 3D sphere,
  2D sprites and textured 3D demos. This replaces the previous whole-page
  screenshot, which could pass on Playground UI pixels even if the GPU canvas
  stayed blank. The expanded check has not yet run in CI for these changes;
  it does not exercise the full backend-parity scene set in a browser.
- Full and partial texture updates, plus PBR-surface validation, now reject
  textures not owned by the active WebGPU `Graphics` instance before using
  their backend handles.
  WebGPU also rejects a row stride that cannot be represented by its 32-bit
  `bytesPerRow` field.
- Device creation requests 16 or 17 sampled textures per shader stage according
  to adapter support. Extended PBR variants still return `Unsupported` when
  their actual resource count exceeds the captured device limit.
- Window initialization creates its WebGPU surface before adapter selection,
  then passes that surface as the adapter compatibility requirement. Headless
  initialization continues to request an adapter without a surface.
- Native WebGPU now resolves its default home and appdata paths to absolute
  paths before PhysFS creates a write directory. The former relative `.` and
  `appdata` values stopped the existing window smoke test before rendering;
  the browser `/` and `/persist` paths are unchanged.
- WebGPU's two-argument `newCubemap` now matches Vulkan's default GGX mip-chain
  generation and reports six layers plus pixel dimensions. An explicit
  `TextureCreateInfo::defaults()` still creates one mip. `present()` now rejects
  an active Canvas in windowed mode, matching the Vulkan contract.
- Native Windows `OBJECT` builds now link the actual SDL2 static-only archive
  name (`SDL2d.lib` in Debug), allowing the full Dawn test target to link.

## Native Dawn verification before porting to `dev` (2026-10-30)

The following runs used the original local `main` checkout. The `dev` PR
worktree needs its own build and test results; its source-quality gate passed
after the port. Test sources were moved to the current `test/integration/`
layout.

- A temporary native Dawn benchmark submitted 100 batches of 64 small atlas
  patches to one 512×512 texture. The direct `WriteTexture` path averaged
  2.33 ms per batch; a trial using one new 256-byte-row-aligned staging buffer,
  64 `CopyBufferToTexture` commands and one submit averaged 5.13 ms per batch.
  These are CPU call timings on this host, not GPU completion or browser
  measurements. The slower trial implementation and benchmark test were
  removed; the current upload implementation is unchanged by this experiment.
- `graphics.textureRegionBatch.strideOverlapAndReadback` now checks a positive
  two-region update through rendered pixel readback. It covers a 3-pixel-wide
  source with row padding, an overlapping later region, and an untouched pixel;
  the native Dawn case passed. The shared case still needs a Vulkan run.

- The full-profile `unit_test.exe` linked on Windows x64 under Dawn. The local
  MSVC installation needed `/Z7` to avoid C1902, and shader generation reused
  checked-in WGSL includes because `glslc`/Tint were unavailable on PATH.
- `GraphicsSmoke.clearAndPresentWindow` now completes its 60-frame native Dawn
  render loop with ten passing assertions, exercising surface initialization,
  texture loading and presentation. A deliberately invalid appdata path makes
  the same case fail in CTest; the filesystem assertion was moved into the test
  body because an assertion in its helper previously printed an error while
  the case still reported success. This verifies Windows native presentation
  on the tested device, not browser or other native surfaces.
- Native Dawn window and texture smoke cases now pass without ZeroErr warnings:
  `Canvas.offscreenGetPixelAndSampleOnScreen` (14 assertions),
  `GraphicsSmoke.cubemapGeneratesMipChain` (8),
  `GraphicsSmoke.textureMipmapsAndAnisotropy` (7), and
  `GraphicsSmoke.setTextureSamplerValidatesStrings` (5). The Canvas fixture
  explicitly disables 2D lighting for its raw-color pixel expectation; the
  default sprite otherwise receives 0.15 ambient light. The cubemap and Canvas
  checks now use hard assertions so regressions fail CTest instead of only
  emitting warnings.
- Before the WGSL creation fix, invalid WGSL returned a Shader and emitted
  repeated uncaptured Dawn validation errors. After the fix, invalid 2D, Mesh
  and Hair creation throws without publishing a Shader; two native Dawn cases
  for creation and hot-reload rollback pass without warnings. The browser event
  loop and error-scope behavior still need a browser runtime check.
- All 23 selected native Dawn CTest cases passed: 14 shared backend-parity
  scenes, two primitive readback cases and seven PBR-variant cases, including
  the new foreign-texture rejection test. This does not verify Vulkan output
  equivalence or browser behavior.
- After the GBuffer capability fallback and `requestFeature` changes, a separate
  22-case Dawn selection passed: 14 shared parity scenes, two primitive readback
  cases, five RenderControl cases and one synthetic 32-byte capability case.
  The pass-planning case verifies fallback and rejection. An additional Dawn
  case requested a device limit of exactly 32 bytes/sample, confirmed the
  granted device limit and observed that five-target GBuffer support is false.
  A third case moves that limited Dawn device into WebGPU Graphics, renders a
  2D rectangle and a forward 3D mesh to engine canvases, and checks readback
  pixels. This proves the restricted-device engine path on the tested Dawn
  adapter, not browser presentation or a physically 32-byte adapter.
- The complete `scripts/check_source_quality.sh HEAD` gate passed on the
  original checkout after the browser texture implementation change, including
  276 Python source tests. It also passed on the `dev` PR worktree after the
  test-policy port. On this host the installed formatter is LLVM 23;
  the required `make check` wrapper cannot launch Git Bash through MSYS Make's
  path-with-spaces handling. The CI script itself ran with the same `HEAD`
  change base and a short-path shell override for its nested Make fixture.

## Verification still needed for the current fixes

Run the Vulkan `graphics.backendParity.textureUpdateAndAlphaBlend` case on a
matching Vulkan build. Repeat the source-quality gate with the repository's
required clang-format 18 and native Windows GNU Make. The browser path needs a
separate runtime check;
native Dawn results alone do not prove it. Verify window startup and presentation
on each supported native surface type after the adapter-selection reorder.
