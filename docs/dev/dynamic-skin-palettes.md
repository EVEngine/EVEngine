# Dynamic GPU skin palettes

The 128-matrix per-mesh uniform array is removed. Mesh owns a dynamically sized,
column-major CPU palette. Vulkan and WebGPU upload the entire palette to a
read-only storage buffer; forward, shadow and G-buffer shaders use that buffer.
No palette is truncated to fit a uniform block.

`Mesh::setSkinPalette` now returns `Result<void>`; callers must observe it. It
copies input, rejects empty/negative/non-finite input with `InvalidArgument`, and
preserves the previous palette on failure. Allocation exceptions propagate
without publishing partial state. The existing script-facing
`AnimSkin::updateGpuMesh` boolean compatibility entry observes that Result.
Calls belong to the render thread and invoke no callbacks.

Each frame slot owns its GPU buffers. Each draw gets a distinct palette snapshot;
buffer storage can be reused or grown only after that frame slot is available.
Descriptors include palette identity, so multiple meshes/poses cannot overwrite
one another's bound palette. Buffers outlive queued draws independently of Mesh.
Mesh owns its CPU copy; backend shutdown releases GPU copies. This introduces no
ECS type, cross-module dependency, persistent schema, or restore/hot-reload state.

Device storage-buffer limits remain authoritative: Vulkan checks
`maxStorageBufferRange`, WebGPU checks `maxStorageBufferBindingSize`, and an
oversized draw reports an exception before upload. The vertex stream still uses
16-bit joint indices, representing indices 0 through 65535. `AnimSkin` rejects
larger bindings before narrowing. This is a representation boundary, not a
preallocated matrix array. Memory use is 64 bytes per matrix per retained draw
slot; capacity is reused on shrink.

The bundled uniform layouts and compiled shader artifacts migrate together.
Custom shaders that reproduce the old uniform tail must be recompiled: the
fixed array between skinInfo and reflection probes no longer exists. Vulkan
storage bindings are 21 (forward) and 2 (skin passes); WebGPU uses 21 and 3.
Static indirect/clustered shaders do not consume a skin palette; skinned draws
use the skin-capable forward path instead.

Verification in this workspace:

- `eve_skin_palette_check`: ownership/rollback plus real GPU pixel movement
  using the final joint of 129, 204, 1024 and 4096-matrix palettes, then shrink.
- The existing UE sample set: all 21 GPU runs captured two frames, including
  the seven bindings formerly rejected at 128 joints. CPU/GPU image comparison
  covers the same animation times; this is not UE material fidelity evidence.
- Architecture contract gate and its seven fixtures; module dependency gate.
- Vulkan is built and run locally. WebGPU code and WGSL are updated together;
  a native Dawn/browser build and runtime have not been verified in this checkout.
- The initial scene validation errors (render-pass compatibility, depth layout,
  and Canvas command-buffer reset VUID 00050) were corrected in the subsequent
  PBR work in this branch. See `gltf-material-extensions.md` for the later clean
  Vulkan runs and the UV/presentation visual corrections.

Local evidence is in `.local-debug/ue-broad-acceptance/gpu-dynamic-results.json`,
`cpu-gpu-dynamic-comparison.json`, `runtime-gpu-dynamic/`, and
`.local-debug/dynamic-skin-palette-test.log`.
