# glTF material extension import contract

The canonical glTF adapter preserves these seven Khronos extensions in
`eve.material/2`, including their image assets and required runtime dependencies.
Vulkan forward rendering now consumes these fields through `PbrSurface` and the
cooked static-prefab loader. The separate Assimp preview has the limits below.
This does not implement cooked skeleton/animation playback.

## Definition fields

- `KHR_materials_specular`: `specularFactor` (default 1, range 0..1),
  `specularColorFactor` (default [1,1,1], nonnegative HDR values allowed),
  `specularTexture` (linear alpha), `specularColorTexture` (sRGB RGB).
- `KHR_materials_anisotropy`: `anisotropyStrength` (default 0, range 0..1),
  `anisotropyRotation` (default 0, finite radians), `anisotropyTexture`
  (linear RG direction remapped from [0,1] to [-1,1], B strength).
- `KHR_materials_clearcoat`: `clearcoatFactor` and `clearcoatRoughnessFactor`
  (default 0, range 0..1), `clearcoatTexture` (linear R),
  `clearcoatRoughnessTexture` (linear G), `clearcoatNormalTexture` (linear RGB),
  `clearcoatNormalScale` (default 1).
- `KHR_materials_emissive_strength`: `emissiveStrength` (default 1,
  nonnegative), multiplying the existing emissive factor and texture.
- `KHR_materials_ior`: `ior` (default 1.5, either 0 or at least 1).
- `KHR_materials_unlit`: `shadingModel="unlit"`. This cannot coexist with
  PBR material extensions on the same material.
- `KHR_texture_transform`: each texture slot may have a separate
  `<slot>Transform` object with `offset` (default [0,0]), `scale` (default [1,1]),
  `rotation` (default 0 radians), and `texCoord` (default parent texture UV set).
  Transform order is offset + rotation * scale * UV. The extension's `texCoord`
  overrides the parent's. Effective UV must exist on every bound primitive.
  Mesh v2 retains additional sets; see [the mesh contract](canonical-mesh-v2.md).
  Without a transform, `<slot>TexCoord` stores the selected channel.

Every texture slot has its own `<slot>Sampler`. Absent wrapS/wrapT default to
REPEAT, including when a partially specified sampler is supplied. Encoded image
bytes are retained; image metadata carries usage and color transfer. Factors
must be finite and satisfy their ranges. Image bounds, indices, and import
budgets are checked before publication. The adapter performs no external IO and
only mutates a private candidate, discarded on failure.

## Versioning and compatibility

The schema identifier remains `eve.material`; version 2 adds optional extension
fields. Missing extensions retain their glTF defaults. Version 1 migration changes
only the version, preserves base fields and unknown definition fields, rehashes
the definition, and updates matching local dependency type versions. Input
archives remain unchanged. Unknown source material or required extensions are
rejected. Unrecognized properties inside a known glTF extension are ignored in
accordance with glTF extensibility; they are not claimed as translated semantics.

The static-prefab reader accepts PBR and unlit materials in v1/v2, including masked
and transparent surfaces. It loads all eleven texture roles and validates their UV
references before publishing a renderer. Additional UV streams are uploaded through
`IMeshResourceFactory`; providers without this capability return `Unsupported` and
the staged mesh is released. Missing fields retain defaults; unknown additive fields
are ignored. Malformed known fields fail, without partially publishing a prefab.

## Import compatibility policy (2026-09-10)

The default `GltfImportMode::Permissive` clamps finite material factors to their
legal range and emits a warning containing the material path, original value and
replacement. For example `materials[5].specularFactor=38406.7422` becomes `1`.
Negative unit factors become `0`; `ior` between 0 and 1 becomes 1, while the defined
special value 0 is retained. This never modifies the source GLB.

`eve asset import input.glb --from gltf --strict ...` selects `GltfImportMode::Strict`
and rejects the same input. The policy participates in the import cache key.
Import findings retain the existing four disposition values; adjustments use
`baked` with additive `severity="warning"`. Readers that do not inspect this
optional report field retain their existing behavior. Archive admission validates
the corrected definition; warnings remain in the source import report.

Neither mode repairs NaN/infinity, invalid indices, missing UV channels, malformed
textures, unknown required extensions, or exceeded resource budgets.

## Runtime ownership and scope

`Material::setPbrSurface` validates and atomically installs an owning parameter
snapshot. Texture pointers remain borrowed graphics-factory resources and must
outlive submitted draws. Shading mode remains owned by Material; returned DTOs
derive the unlit flag. Mutation, bind and rendering are graphics-thread operations,
without scripts/callbacks or locks. `bind` returns checked status. Backends without
the extended path return `Unsupported`; clearing it remains supported.

Vulkan uses an independent descriptor layout with 11 material textures, environment
and shadow sampling (13 samplers, below the Vulkan minimum 16). Per-frame/per-draw
uniforms, UV buffers and joint palettes remain alive through the frame fence.
Descriptor pools, sampler caches and pipelines are renderer-owned and released
before device destruction; swapchain recreation invalidates the cache. UV offsets
are unsigned integers, independent of floating-point precision and channel count.

Colors use their semantic transfer function: source base/emissive/specular-color
textures are decoded from sRGB, data maps remain linear, and already-linear Cook
pixels are never decoded twice. Texture transforms apply scale, rotation, then
offset; every role has its own UV selection and sampler. Direct lighting implements
anisotropic GGX, IOR/specular Fresnel, clearcoat layering, normal/occlusion maps,
emissive strength and unlit. Tangent frames are derived from position/UV derivatives;
source tangent retention and exact UE material-graph parity are not claimed.
Ambient light and environment filtering retain an approximate real-time contract.

The Assimp Model3D preview now binds the properties and maps exposed by its glTF
adapter, restores glTF UV origins/transforms, and uploads all available UV channels.
The pinned Assimp importer does not expose the full anisotropy extension (rotation
and texture); use the canonical material path for those semantics. WebGPU extended
PBR, transformed-alpha shadow/G-buffer parity, and cooked skeletal playback remain
outside this implementation. The extended material uses the forward renderer and
is excluded from the simplified GPU-driven material path.

## Verification

`eve_gltf_animation_check` covers extended factors, HDR specular color, extension
textures through Cook, unlit, per-slot transforms and UV override, malformed and
out-of-range extensions, unknown extensions, import budgets, and material v1
migration. `eve_material_compat_check` covers the existing Unity-native static
prefab import/Cook/load path against material v2 migration.

The nine previously rejected UE fixtures are rerun through import, Windows Vulkan
Cook, EVA validation and EVPACK validation. Local evidence is stored under
`.local-debug/ue-broad-acceptance/packages-material-extensions/`; the earlier
package results remain historical evidence. Morph targets are outside this change.

## Historical Mech UV boundary (superseded by mesh v2)

The Mech fixture uses TEXCOORD_2 for base color, metallic-roughness and occlusion
on `Mech_Paint_Inst_Body_SKM_Mech`, with transform offset [0.5,0] and scale
[2321.14233,1]. Two Dark_Metal materials use TEXCOORD_1 for occlusion. These five
bindings are now retained by mesh v2. The original fixture instead reaches an
out-of-range specularFactor source error; see the mesh contract for evidence.

## Material-only regression outcome (before mesh v2, 2026-09-10)

Eight of the nine formerly extension-rejected fixtures passed import, Cook and
both archive validators. Mech remains blocked by UV1/UV2 as described above.
Across accepted fixtures, 28 materials, 44 explicit extension factors, 145 texture
bindings (including encoded bytes, transfer and dependency roles) and 87 transforms
matched the exported GLBs exactly. All seven extension names are represented.
The full broad suite therefore admits 18/21 packages, with two morph-target
fixtures and Mech's additional UV streams still unsupported.

The import/migration and legacy compatibility suites pass 23 cases with 263
assertions. The renderer-free asset-core-only build also passes the 17-case import
and migration suite. Source architecture contracts, their seven fixtures, module
layering, and whitespace checks pass. Applicable rules were versioned persistent
data with migration, checked Results, atomic candidate publication and module
independence; no architecture exception was taken.

## PBR implementation verification (2026-09-10)

- `eve_pbr_surface_check`: factor changes, all eleven texture roles, UV1 selection,
  transform equivalence, repeat-frame isolation, and a 204-joint palette affect
  actual Vulkan offscreen pixels. Validation-layer errors fail acceptance.
- `eve_material_compat_check`: legacy import/Cook/graphics checks, material-v2 runtime
  decoding, and additional-UV provider present/absent with staged-resource cleanup.
- Original Mech: permissive import, Cook, EVA and EVPACK validation passed with the
  warning retained; strict import rejected `materials[5].specularFactor` and wrote
  no archive. Evidence: `.local-debug/ue-broad-acceptance/packages-pbr-permissive/`.
- Three live GPU-skinned regressions (UE4 mannequin, Mech, UEFN run animation) were
  captured with engine-owned `saveFramePng`, with no Vulkan validation errors after
  fixing G-buffer dependency compatibility and shadow image layout tracking.
  These initial screenshots did not establish visual correctness; the follow-up below
  found UV and final-presentation defects. Dark source AO alone did not explain them.

## Visual regression follow-up (2026-09-10)

The Assimp glTF importer converts UV origin, and its optional FlipUVs postprocess
also changes UV transforms. ModelData now owns that postprocess flag alongside its
scene (including adoption/hot reload). Both file and memory loaders propagate it.
ModelRenderer normalizes each UV stream once and reverses the postprocess changes
before recovering the native glTF texture transform. The actual-loader regression
`model3d.gltfUvOriginAndTransformSurviveLoader` checks both FlipUVs settings against
an embedded glTF fixture; it failed before the correction.

The Vulkan automatic scene resolve also incorrectly selected another FXAA pass,
bypassing its final ACES pipeline. It now tone-maps the already anti-aliased,
exposed linear HDR result, encoding sRGB for UNORM swapchains and leaving the
encoding to sRGB attachments otherwise. This correction covers the automatic
swapchain resolve, not every explicit Canvas composition path or other backend.

Live fixed-exposure gray verification measured RGB (46,46,46) before and
(141,141,141) after for linear 0.18, matching ACES plus sRGB exactly. Evidence:
`.local-debug/ue-broad-acceptance/presentation-gray-pixels.json` and the paired
`presentation-gray-before/` and `presentation-gray-after/` engine screenshots.
The three `runtime-pbr/` captures were regenerated after both corrections.
The misplaced mannequin patches are gone. Mech source base-color maps contain
weathering, but no matched UE reference is available: these captures still do not
certify UE visual parity or completeness of converted UE material graphs.
