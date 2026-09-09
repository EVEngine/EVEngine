# Native shader effect packages

The implemented path is now: authored SPIR-V + parameter defaults → `eve.shader/1`
→ canonical `.eva` → Cook `.evpack` → validated GPU shader lease → existing
MeshVfx curves, envelopes and events → draw. `prepareMeshVfxPackage` bundles the
existing `eve.stylize.mesh-vfx/1` definition and its required shader dependencies.
There is no second archive format or separate playback implementation.

## Compatibility with Unity import

The branch was fast-forwarded to PR #342 head `cb7e4d110fa0fe50be47518f8492475df0003b3c` after its latest two nonoverlapping fixes.
Unity package parsing, prefab translation, sprite animations and their tests are
unchanged. The only shared import integration adds `--from shader`; the cooker
and migration catalogue gain types. The new optional satellite is
`asset/stylize`, declared through the module manifest. The Vulkan offscreen pool
also gains its required command-buffer reset flag, a one-line lifecycle fix
exercised by repeated frames. See the
[Vulkan command-buffer contract](https://docs.vulkan.org/refpages/latest/refpages/source/vkBeginCommandBuffer.html).

## Runtime contract

`EvpackShader` owns one independent shader lease and a copied CPU definition.
`EvpackMeshVfx` owns those leases and the canonical `MeshVfxAssetInstance`.
Graphics must outlive these objects; borrowed meshes and textures are used only
during draw. Package readers can be destroyed after loading. All GPU operations
are graphics-thread affine and invoke no callbacks. Callers choose blend/depth
state and inject finite nonnegative `dt`; no wall clock or RNG is consulted.
The same dt sequence preserves the existing playback determinism contract.

Reload builds a complete candidate before replacing the live instance. Failure
preserves playback and parameters; success reapplies defaults and restarts.
Unknown parameters, invalid time, missing shader references and incompatible
layouts return structured diagnostics. CPU snapshots and archive candidates are
fully validated before publication. There is no ECS or cross-domain Link added.

The schema rejects unknown fields and unsupported versions. Shader parameters
occupy up to 32 packed float slots, with scalar or vec2/3/4 defaults; MeshVfx
curves currently use scalar parameters. Existing MeshVfx v0-to-v1 migration is
retained. Shader v1 has no prior version migration. Archive publication remains
the existing atomic asset store's responsibility.

GPU admission uses SPIRV-Tools plus SPIRV-Cross: full Vulkan 1.2 semantics,
entry points, engine vertex/varying layout, Frame UBO prefix, a single optional
fragment sampler2D and the 128-byte float push-constant ABI. Arbitrary bindings,
storage resources and additional device capabilities are rejected. The existing
Cook profile token `spirv-1.6` is a format-family ceiling; the current Vulkan 1.2
runtime accepts compatible programs through SPIR-V 1.5. The authoring helper
validates against Vulkan 1.2. A missing validation provider yields explicit
`Unsupported` at upload, while CPU packaging remains usable.

## Running the example

See `examples/shader_effect_package/README.md`. The real Vulkan probe writes
an author `.eva`, reopens and cooks it, then loads **only** the consumer `.evpack`.
It renders independent instances and checks actual engine-owned readback for
animation, parameter isolation, failed reload preservation, successful reload
reset and stop. It also checks timeline events and malformed authoring inputs.

CPU suites under `test/asset_shader` cover shader decoding/cooking/import and
GPU admission with provider present and absent, alongside unchanged Unity
source-package, collection, FBX and missing-provider suites. The real probe
is explicit rather than an unconditional GPU test on machines without a device.

## Remaining product work

This delivers the native C++ package/runtime path. It does not yet deliver
editor drag-and-drop import, an asset preview/parameter inspector, script
bindings for these new objects, Unity ShaderLab/Shader Graph translation,
WebGPU shaders, packaged texture resolution, or trail/animation attachment
adapters. Unsupported trail/animation attachments are rejected, not ignored.
Callers may supply a texture to draw through the established graphics API.
