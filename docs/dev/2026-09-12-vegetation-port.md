# Vegetation Engine 12.6.0 source investigation and native implementation

Status: partial implementation; **not a complete TVE port or a claim of visual parity**.

## Reference inspected

Input: `The Vegetation Engine [12.6.0].unitypackage`, 279876763 bytes.
SHA-256: `46b2958ec430425b9eb303fde454d4049a4f0a9cee32352985037fb00df8d8dc`.
The external reference is retained only under the ignored `build/tve-reference`
directory for local investigation. It is not a runtime dependency.

The outer package has 784 path records, including 92 C# scripts, 52 shaders,
121 conversion presets, 58 prefabs, 27 FBX sources, 11 scenes and 7 nested
pipeline packages. Nested packages cover Built-in plus URP/HDRP for 2021.3,
2022.3 and 6000.0. Outer-package inspection alone misses these adapters.
Unity pathname records in this archive have a newline followed by `00`; the
pathname must be read up to the newline rather than used as a literal filename.

## Source design

Paths below are relative to `Assets/BOXOPHOBIC/The Vegetation Engine/Core`.

- `Runtime/TVEManager.cs`: creates and coordinates global motion, details,
  control and volume components; enable/disable publishes shader state.
- `Runtime/TVEGlobalVolume.cs`: maintains Colors, Extras, Motion and Vertex
  render data. Elements write selected array layers through command buffers.
  It supports global/custom volumes, per-layer usage, instanced draws, sorting,
  texture dimensions, global defaults and world-to-volume coordinate projection.
  The world X/Z projection origin is snapped to a texel grid.
- `Runtime/TVEElement.cs`: registers/removes element contributions, interprets
  material tags and layer masks, computes visibility/fading and handles terrain
  and renderer contributions. Elements are spatial material effects, not plants.
- `Runtime/TVEGlobalControl.cs`: seasons run winter → spring → summer → autumn
  → winter over [0,4], with smooth interpolation. Extras pack emission, wetness,
  overlay and alpha. Other defaults cover global tint, size, height, overlay
  textures, proximity fading, subsurface and sun parameters.
- `Runtime/TVEGlobalMotion.cs`: direction, wind power, noise texture/tiling,
  bending/branch/flutter amplitudes and flutter distance fading.
- `Shaders/Geometry/Plant Standard Lit.shader`: uses per-plant pivots and
  variation, advected noise sampling, nonlinear wind strength, bending,
  branch squash/rolling, flutter, interaction, field layers and surface effects.
  Generated vertex logic is repeated across the relevant rendering passes.
  Replacing it with one sinusoidal translation is not visual equivalence.
- `Runtime/TVEUtils.cs`, `CreatePackedMesh`: vertex color RGBA stores variation,
  occlusion, detail and height masks. UV0.z packs two motion masks; UV0.w packs
  height/radius divided by 100; UV4 stores pivot X/Z/Y. Two [0,1] values use
  floor-to-2047 quantization and base-2048 packing into one float.
  Import must preserve these channels before projecting into native data.
- `Runtime/TVEUtils.cs` and `Editor/TVEAssetsConverter.cs`: material/mesh
  conversion, texture packing, mesh merging/splitting, source reimport settings
  and preset-driven conversion are substantial authoring functions.
- `Runtime/TVETerrain.cs`, `TVEGlobalDetails.cs`: terrain texture/layer data,
  detail motion, layer selection and perspective controls. These are distinct
  from the core field evaluator.

`_RenderCoverage` is synchronized by `TVEUtils.SetMaterialSettings` into the
actual `AlphaToMask` render state only while alpha clipping is active. Canonical
material v15 preserves that behavior as `alphaToCoverage`; Vulkan selects a
separate masked multisample pipeline. `_RenderDirect`, `_RenderAmbient`, and
`_RenderShadow` are declarations without calculation references in the four
admitted Plant/Prop Lit shaders and remain Vertex Lit-only editor controls.

## EVEngine integration implemented

`VegetationField` owns admitted CPU field definitions. It implements four
channels, nine selectable layers, stable priority composition, five blend modes,
rotated box/ellipsoid influences, smooth edges, bilinear masks, seasons and
texel-snapped CPU atlas baking. Native signed direction/tilt values deliberately
avoid TVE's texture encoding bias; this is a native contract, not binary TVE
material compatibility.

`VegetationMotion` implements the inspected Plant Standard vertex formulas:
advected repeating noise, nonlinear wind power, pivot bending, branch squash
then rolling, flutter, fourth-power interaction blending, rigidity/facing,
ordinary-object and batched paths, size/distance fading and perspective push.
Time override, authored variation, object transforms and floating origin are
explicit inputs. Rest positions are object-space immutable inputs; output is
world-space and animation does not accumulate. Normals use a local deformation
Jacobian with masks held fixed followed by inverse-transpose transformation.
Very large local coordinates retain float precision limits. This CPU path and
its numerical fixtures do not establish full shader or GPU visual equivalence.

`VegetationDetailSettings` and `configureVegetationDetails` implement the
runtime publication performed by `TVEGlobalDetails` without mutable process
globals. One validated call produces detached PBR and CPU-motion snapshots,
selects Colors/Extras/Motion layers, carries the two independent mask remaps
and the source `alphaThreshold - 0.5` offset, and applies global color, alpha,
overlay, wetness, motion highlight, bending, flutter, interaction and
perspective controls. Invalid publication leaves both input snapshots intact.

Canonical `eve.terrain-material/3` now preserves the data copied by
`TVETerrain`: holes, four control/splat textures, per-layer albedo/normal/mask,
RGBA mask remap and specular, metallic, smoothness, signed normal scale,
independent XY tiling/offset, and positive patch-bounds multiplier. Unity
TerrainData imports these fields and the strict runtime loader publishes them
atomically. Version 1 migrates with explicit neutral defaults. The procedural
terrain path resolves referenced EVIMG payloads, applies normal convention and
mask remaps, and renders up to sixteen source layers in one draw through 4x4
albedo, tangent-normal and mask atlases. Four control maps plus holes share one
control atlas; a lossless parameter texture carries every layer's tiling,
metallic, normal scale and smoothness. Vulkan and WebGPU use the same weight
normalization and all-zero fallback contract.

`decodeTveVegetationVertices` decodes original color masks, base-2048 packed
motion masks/bounds, main/secondary/detail UVs and X/Z/Y pivot ordering. It
rejects malformed packed integers before publishing an owning native stream.
`asset_graphics::VegetationAsset` now connects canonical runtime mesh loading to
the motion and GPU upload path. It owns decoded rest vertices, render UVs and
indices; no archive or graphics resource pointer is retained. Explicit admission
requires complete TVE float4 authoring streams. The raw X/Z/Y pivot is decoded
then reflected into the canonical Z basis, while render UVs keep their canonical
origin. `createMesh` and `updateMesh` use the same deformation implementation;
`evaluate` remains a worker-safe CPU projection. Mesh ownership stays with Graphics.
Unity native Mesh v9/v11 now retains complete float authoring channels through
the canonical importer, Cook and runtime loader; see [the v3 binary contract](canonical-mesh-v3.md).
The desktop FBX path now exports every Assimp UV and vertex-color set plus a
float4 tangent stream into that same canonical contract. Authored tangent space
is retained; when an otherwise valid static FBX omits it, Assimp calculates it
from imported normals and UV0 before conversion. The importer reflects tangent
axes with the mesh and derives its handedness from the source normal, tangent and
bitangent. A generated FBX fixture verifies `TEXCOORD_1`, `TANGENT` and `COLOR_0`
values after canonical decode. Generation-1 metadata now recovers indexed Unity
mesh IDs and accepts a unique explicit Prefab mesh reference when an old importer
published a non-indexed 64-bit ID. Multiple Assimp meshes attached to one Unity
mesh-bearing node are retained as ordered material submeshes. The original
`TreeIt.fbx` consequently restores its `1243444849563829797` identity and both
Bark/Leaves submeshes without inventing an ID.

`VegetationRender` applies deformed positions through the active Graphics mesh
update API and recomputes bounds. Color/depth/shadow consumers consequently use
the same updated mesh. Its surface adapter publishes masked, double-sided PBR
surface values plus field-sampled emission, wetness and overlay. Vulkan evaluates
overlay variation, normal projection, luminance and vertex occlusion per fragment,
then applies the source overlay/wetness color, normal, metallic, smoothness and
overlay-subsurface equations.

`packVegetationOrm` implements the main/detail texture-mask projection used by
the source shader: output R is `lerp(1, source.G, occlusion)`, output G is
`1 - source.A * smoothness`, output B is zero metallic and output A retains
source B for later color-mask interpretation. Inputs and outputs are linear
owning float textures; no sRGB conversion occurs. The bounded conversion accepts
up to 16,777,216 texels, including 4096-square masks. Original texture values
remain authoritative. Runtime PBR consumes the projected texture through both
metallic/roughness and occlusion bindings with sRGB decode disabled. This covers
these channel equations, not the complete material, detail blend or subsurface
shader. The render fixture compares the same geometry with and without the
converted mask to verify actual GPU consumption.

The Vulkan extended PBR path now evaluates optional mask-driven RGB tinting in
the fragment shader. `PbrSurface::colorMaskEnabled` samples projected ORM alpha
after filtering, clamps it to [0.0001,0.9999], applies the source remap denominator
`max - min + 0.0001`, then interpolates secondary RGB to Material's primary tint.
Inverted remap intervals are admitted; an exactly zero denominator is rejected.
The mask binding must explicitly be linear. `albedoTextureStrength` interpolates
texture RGB from white without altering texture alpha. Default parameters preserve
existing PBR behavior. This is RGB main-color support only: secondary color alpha,
detail blending, complete material import and WebGPU equivalence remain unfinished.
The base backend continues returning Unsupported for extended PBR, rather than
silently ignoring these parameters. GLSL, embedded SPIR-V and the host uniform
layout were updated together; both stages share the 1120-byte block contract.

The runtime is in the existing graphics module. Existing `GrassField` sampling,
procgen foliage generation, material PBR and backend mesh upload remain their
respective authoritative implementations. No new module, ECS root or capability
registry is introduced.

## Contracts

- `replace` and version-one `restore` validate candidates before swapping.
  Invalid definitions retain the previous field. Samples, snapshots, atlases
  and deformed geometry are detached owning projections.
- Persistence uses `eve.graphics.vegetation-field`, version 1. Exact field sets
  are required. Unknown fields, other schema IDs and unsupported versions are
  rejected; no historical version exists to migrate yet.
- No callbacks, global singleton, wall clock, external resource pointers or
  retained cross-domain references are introduced into the field/motion core.
  Const evaluations permit concurrent readers only when there is no writer.
- GPU/material adapters borrow existing graphics resources only for the call;
  render-thread affinity and existing resource ownership still apply.
  There are no new Links or ECS Systems, so destruction-order and deferred ECS
  mutation contracts do not apply to this slice.
- Time and per-instance variation are explicit. Floating-point/transcendental
  evaluation is tolerance-bounded, not cross-platform bit-exact replay.
- Script factory `eve.Graphics().newVegetationField()` returns the common
  checked Result with a VM-owned object. `restore`, `snapshot` and `sample`
  use the same native data validation and common Result projection.
- No deliberate architecture exception is taken. GPU backend parity, profile
  trimming, allocation-failure injection and full authoring integration are
  not established by the current focused tests.

## Canonical material v6 and original-source runtime evidence

Canonical material v6 owns the material-local TVE overlay/wetness multipliers,
variation, projection, vertex-occlusion influence and inversion. The runtime
reader combines these with the same definition's albedo, emission, roughness and
alpha cutoff into a complete `VegetationSurface` snapshot so applying a dynamic
field cannot replace imported values with defaults.

The selected original-source project imports 32 TVE materials. Two referenced
serialized `Texture2D.asset` sources were recovered directly from the supplied
outer unitypackage by GUID; two genuinely absent GUIDs remain explicit
admission failures. The v6 EVA contains 32 strict vegetation objects, and the
Grass Meadow values match the source exactly, including HDR motion highlight
`[2.1185474, 1.4974027, 0]`, overlay 0.3, wetness 1, variation 0.1,
projection 0.6 and occlusion alpha 0.34117648.

The 735,510,972-byte Vulkan EVPACK contains 376 chunks. An independent decoder
verified every material definition hash, schema version and decoded value
against the EVA. A headless Graphics Canvas probe loaded Grass Meadow's four
cooked textures, applied its owning vegetation snapshot and mesh factor streams,
then captured pixels through `Canvas::newImageData`. Disabling only
translucency changed 202 pixels with a maximum linear-channel delta of 0.568627;
the Vulkan validation layer reported no VUID errors. The original 128x64 capture
and a nearest-neighbour inspection copy are retained under
`build/tve-reference/grass-meadow-v6-comparison.*`.

Canonical material v7 moves the TVE global Colors stage from the earlier CPU
pre-tint approximation into the PBR fragment shader. The importer now retains
the material-local Colors coverage, intensity, ORM-mask use, mesh variation,
vertex-occlusion range, and independent inversion. The render adapter passes the
sampled field RGBA without modifying the base tint. Vulkan follows the original
linear-space formula, including the alpha influence curve, `4.594794` color
multiplier, filtered ORM alpha, textured luminance, and final `0.1..0.2` remap.
Ordinary glTF and Unity material producers also emit v7 directly; v6 remains the
only migration window.

The original 12.6.0 material project was re-imported and cooked with the v7
binary. `build/tve-materials-hdr-v7.eva` contains 32 v7 vegetation materials;
`build/tve-materials-hdr-v7.evpack` contains 376 chunks. Independent TOC,
decompression, SHA-256, and definition comparison found 32/32 definitions equal,
including 15 subsurface materials and the exact Grass Meadow Colors controls.
The Vulkan validation probe loaded four original textures and measured 271
Colors-changed pixels (maximum delta 0.07451) plus 75 translucency-changed pixels
(maximum delta 0.17647). Its engine-owned comparison frame is
`build/tve-reference/grass-meadow-v7-comparison-8x.png`.

## Remaining requirements for the requested complete port

The package's 121 `.tvepreset` files now import as strict
`eve.vegetation-conversion-preset/1` command trees. Commands, ordered arguments,
nested and negated conditions, and Include directives survive EVA and EVPACK
publication. The execution API expands Includes with cycle/missing detection and
evaluates all predicate families present in the supplied package against an
immutable conversion context. The one source-authored `f SHADER_NAME_CONTAINS`
typo is recovered with an explicit report finding. See
[the preset v1 contract](canonical-vegetation-conversion-preset-v1.md). Include
expansion feeds a typed, transactional command application stage for every
Material, Mesh, Texture, Shader, Utility, Output and Collect operation spelling
present in the package. Material copies, component projection, shader selection,
keywords, locking and instancing mutate an unpublished owning candidate. The CPU
conversion entry now decodes Unity text Material floats, vectors/colors, texture
GUIDs and ST, valid/legacy keywords, instancing and shader identity into that
same candidate. Predicate facts are projected from the candidate so evaluation
cannot drift from the properties later mutated by execution. A public owning
request now performs decode, predicate evaluation, include expansion, command
application, mesh conversion, and texture packing as one detached transaction.
OutputMeshes, OutputMaterials, OutputTextures and OutputTransforms are validated
into strong publication modes; disabling materials also suppresses orphan packed
textures, matching the source conversion sequence. World-space output applies an
explicit column-major affine transform to positions and a separately supplied
proper rotation to normals/tangents, reproducing Unity TransformPoint and
scale-ignoring TransformDirection while retaining tangent handedness. The CPU
texture executor performs bounded RGBA8 resampling, channel selection, max/gray,
one-minus and gamma/linear conversion. Missing source textures skip publication,
matching the source converter. Object/tangent normal repacking requires a mesh
execution context. The canonical mesh executor writes the
source-compatible color masks, packed motion/bounds, detail coordinates and
element pivots, including procedural and texture-sampled masks, CTI/SpeedTree
adapters and normal overrides. Normal overrides rebuild canonical tangent space;
meshes without UV0 discard stale tangents. Source `NONE` pivot rules preserve the
authored stream, and the legacy ignored third `COPY_FLOAT` token is accepted with
the same TVE 12.6 semantics. The generated mesh passes canonical v3 encode,
decode and runtime `VegetationAsset` admission. One transaction now applies commands and returns detached material,
mesh and packed-texture outputs only after every stage succeeds. Procedural bounds
and CPU-readability publication metadata are retained in that result. Converted objects can be combined through
the batch transaction using unique stable output keys; asset, entry and entrypoint collisions abort the whole
candidate, mappings are object-namespaced, and one final report covers the package. The source
object/tangent normal recipes are executed by triangle-bounded rasterization of canonical UV0 with
interpolated normal/tangent/handedness and applying the inspected Packer shader
direction transforms; uncovered texels retain the packed source value and later
triangles win deterministic UV overlaps, matching source draw order. Rasterization now bins triangle bounds into
fixed 16x16 pixel tiles with explicit aggregate work and bin-reference budgets. Each tile retains source triangle
order, so the cache-local path preserves overlap semantics across tile boundaries; a 33x17 regression exercises
two overlapping frames on the second tile. The repeatable MSVC Debug scalability fixture converts an 8,192-triangle
64x64 grid over a 512x512 normal image in 5,652 ms on the current Windows host, including three-channel bilinear
packing and object-to-tangent conversion. This is a local regression baseline rather than a release-build target.

The [material source inventory](vegetation-material-source-contract.json) records
active defaults and differences across the four outer geometry shaders. Counts
are Plant Standard 204, Plant Subsurface 210, Prop Standard 167 and Prop
Subsurface 175; commented declarations are recorded separately and excluded.
The outer package contains 86 materials referring to 14 shader GUID groups.
Four explicitly enable detail blending, two dual main colors, and 22 alpha
clipping. Counts describe explicitly serialized values, not omitted defaults.
The Unity importer now admits the four inspected TVE 12.6 Plant/Prop shader
identities and emits strict canonical material v6 definitions. Main albedo,
main mask, authored/height normals, dual color, UV/sampler state, visible
subsurface controls, motion-highlight color and material-local overlay/wetness
controls survive import, cook and runtime reading. The original-source validation
set contains 32 imported materials; missing external GUIDs remain explicit rather
than receiving fabricated pixels. Detail blending and the remaining per-pixel
semantics still require their actual shader paths. The inventory is source
evidence only and does not certify feature completion.

- Terrain detail-mesh/grass consumers, height blend, and per-pixel terrain
  features beyond the inspected `TVETerrain` layer publication.
- Runtime instancing and explicit scalability measurements for dense source scenes.
- Editor document/property schemas, gizmos, previews, undo/redo, material batch
  editing, converter tools and hot-reload lifecycle integration.
- An authored representative tree/shrub/grass scene with real interaction,
  before/after frame captures, Vulkan/WebGPU contracts and trimmed builds.

Completing the field evaluator or passing its tests must not be reported as
completing these remaining requirements.

## Unity terrain Detail prototype and runtime instance ownership

The public-API Unity exporter now records both exact `ComputeDetailInstanceTransforms` output and the owning
`DetailPrototype` metadata. Unity mesh and texture prototypes retain separate stable GUID-prefixed identities.
The Unity importer validates the complete detached sidecar before canonical mutation: prototype IDs are unique,
instances reference declared prototypes, modes and mesh/texture identity agree, and width, height, density,
noise, alignment and jitter values remain bounded. A real Unity 6000.0.79f1 batch-mode fixture exported six
instances from two populated density cells, including Unity's jitter, scale and yaw results.

Canonical `eve.instance-set/5` owns this prototype table beside the EVINST records. Each prototype also owns a
validated `resourceAsset`: texture Details point at the imported `eve.image`, while mesh Details point at the
imported prefab scene. Its runtime loader selects v3 first and retains direct v2/v1 read compatibility. The N-1
migration preserves unknown fields and writes an empty resource reference to expose unresolved legacy data without
fabricating a target.
The terrain realization stage merges these baked instances with PointGraph output, performs deterministic terrain
weight filtering, groups prototypes, resolves all GPU table slots before submission and reports generation budgets.
Vulkan and native WebGPU configurations pass the sidecar import, v2 loader, v1 migration and four realization/GPU
contract cases. Automatic texture-grass card/material construction and a representative source terrain render still
remain; prototype persistence alone is not a complete Detail renderer.

Terrain realization now retains the owning prototype table and passes a call-scoped metadata view to the GPU
resolver together with each stable prototype ID. Resolvers can therefore select authored meshes, crossed grass or
camera-facing grass without consulting a second registry; duplicate metadata fails before any resource resolution.
`buildTerrainDetailCard` creates bottom-anchored unit geometry matching the exact per-instance width/height transform:
`GrassBillboard` yields one quad marked camera-facing and `Grass` yields two orthogonal quads. Mesh-backed and
`VertexLit` prototypes return `Unsupported` instead of receiving substitute cards. `TerrainDetailGpuResolver` now
loads each v3 texture AssetRef, uploads the card, builds a double-sided masked material, retains the GPU leases, and
caches valid mesh/material table slots for Vulkan and WebGPU. Mesh Detail now loads its prefab scene and expands
every enabled renderer into a GPU part while preserving the renderer node's local transform; one terrain instance
can therefore submit a multi-renderer plant without flattening its authored hierarchy. Cleanup failures remain owned
for explicit retry. A representative imported Terrain Detail fixture now passes the full Unity sidecar import,
EVA build, target cook, EVPACK decode, Detail card construction, and real screen readback path on Vulkan and
WebGPU. Its saved backend images must contain more than 40 green foreground samples, so a uniform clear frame can
no longer pass. Runtime collector submission remains a separate GPU lifecycle gate.

The dedicated Terrain import path now merges the runtime dependency closure for every sidecar Detail prototype
from the indexed Unity collection. Texture Details therefore publish their canonical `eve.image` definitions and
pixel chunks into the same EVA/EVPACK, while mesh Details retain the referenced prefab dependency closure. A
backend-neutral regression imports a schema-3 sidecar with three grass instances, cooks it, loads instance-set v5,
and decodes the referenced 2x2 image from the resulting EVPACK on both Vulkan and WebGPU builds. Previously the
prototype contained a valid-looking AssetRef whose image asset was absent from the package.

The native material editor now exposes the complete TVE alpha-fade authoring group: enable, global and variation
weights, glancing/camera/constant effects, camera range, 3D noise asset and tiling, and detail-fade selection.
Every field uses the shared property validator, participates in the existing transaction service, survives the
versioned material-document snapshot, and has focused undo/redo coverage. `MaterialPublishingTarget::reloadSnapshot`
loads persisted content into a detached candidate and invokes the runtime sink only after parsing succeeds; sink
failure preserves both the authoring revision and live state. The complete emission-remap and height-gradient groups
are also authorable and publish into the same validated `PbrSurface`; disabling any of these three groups clears its
previous runtime state. The full translucency group and its linear ORM mask binding, plus the emissive texture role,
are exposed through the same path; a requested translucency mask without ORM data is rejected before live mutation.
The Colors, Overlay, Wetness and vertex-occlusion controls are exposed as one editor stage matching their shared
`PbrVegetationColorStages` owner; disabling the group restores the neutral stage instead of leaving stale tint,
normal, smoothness or occlusion state. The secondary Detail group exposes its three textures, dual colors, UV
projection, blend selectors, contributions and remap ranges, and publishes the complete `PbrVegetationDetail`
snapshot only after every referenced texture resolves. Disabling it clears the runtime stage; a missing texture
leaves both the material document revision and bound runtime material unchanged. Colors and Extras field sampling
now expose their array texture, layer, all nine usage weights, fallback, projection coordinates and pivot selection;
disabled groups clear stale runtime bindings without resolving retained asset paths. The Vertex field group adds
the array texture, layer, nine usage weights, fallback/coordinates, global size, distance fade and an explicit
RestMesh/CpuDeformed/GpuFields deformation owner. Missing GPU field textures reject the detached candidate without
changing the document or runtime surface. The Motion group completes material-side field publication with its array
and noise textures, layer/usage/fallback projection, explicit direction, world origin and injected time, plus every
object bending, branch, rolling, flutter, interaction, fade and perspective control represented by
`PbrVegetationMotion`. It requires the existing GpuFields vertex owner and clears retained resource paths without
resolving them when disabled. Material editor snapshots are now schema v9 and migrate v1 through v8 documents by
filling new vegetation fields from declared defaults. All native TVE `PbrSurface` groups are now represented in the
material document/runtime sink. `VegetationFieldGizmoBuilder` now takes an owning, priority-ordered element snapshot
from the authoritative field and emits a bounded, revision-tagged overlay. Rotated boxes remain oriented boxes;
non-uniform ellipsoids retain all three extents, channels have stable viewport colors, and masked volumes use dashed
lines. `PrimitiveGizmoRenderer` submits both new wire shapes through the existing frame-local primitive path. Vulkan
and WebGPU engine readbacks produced byte-identical 96x96 PNGs for the representative two-volume fixture. The
remaining editor requirement is narrowed to converter UI, concrete host batch-sink bindings, and hot-reload
lifecycle integration.
Resources resolve before mutation, `Material::setPbrSurface` validates and atomically replaces the extension, masked
mode is admitted for vegetation alpha, and a missing bound Material returns Unsupported without advancing the live
document.

## Validation performed

- Native MSVC Debug `eve` and `unit_test` built against the repository-pinned
  third-party aggregate `54725d0ae7c4a3092dc16841e0b0064f03365a45`.
- All 24 `graphics.vegetation.*` CTest cases passed, including script ownership,
  failed restore retention, CPU field/motion contracts and actual Vulkan rendering.
  Numerical fixtures cover pivot rotation, separate global/local directions,
  fourth-power interaction, squash-before-rolling, texture-driven flutter and
  fade, batching, time override, object transforms, packed channels and overflow.
- Summer and autumn frames were captured through `Graphics::newImageData` and
  visually inspected at `build/win32-debug/test/out/vegetation.png` and
  `vegetation-autumn.png`. They are minimal render fixtures, not quality/parity
  demonstrations of the supplied art.
- Validation initially reported `VUID-vkCmdDraw-None-09600`: deferred shadows
  finished in shader-read-only layout while their recorded layout was changed to
  depth-stencil-read-only. `GraphicsDeferredGraph.cpp` now uses one layout
  constant for both the graph declaration and post-submit state tracking.
  This follows the [Vulkan descriptor image layout contract](https://docs.vulkan.org/refpages/latest/refpages/source/VkDescriptorImageInfo.html).
  Re-running the 24 tests with `EVENGINE_VULKAN_VALIDATION=1` passed with no
  validation errors; driver-layer/performance warnings remain.
- The source architecture contract gate, its 9 fixture tests, dependency graph,
  test source discovery, graphics binding documentation, quality metadata and
  whitespace checks passed. No baseline or allowlist was changed.
- WebGPU vegetation rendering, renderer trim configurations and complete TVE
  equivalence remain unverified. The renderer-free asset-core-only profile has
  passed 25 cases for canonical import/codec compatibility. No PR or commit was created.

The latest selected asset/import/graphics, PBR and vegetation regression run passed
93 cases. Mask conversion has hand-calculated numeric and malformed-input tests.
The actual render compares enabled/disabled ORM bindings on fixed geometry and
requires more than 100 sampled pixels to change; the check passed, with no
Vulkan validation errors. This is narrow channel-conversion evidence and does
not complete material import or source shader parity.

The RGB mask test samples a filtered value of 0.3, then checks the expected
approximately 0.1 blend factor after remapping. It also checks the reversed
interval, texture intensity endpoints and repeatability, plus atomic invalid
configuration rejection and explicit provider-absent failure. The dual-color
scene is captured at `build/win32-debug/test/out/vegetation-dual-color.png`.
Default PBR extension/UV tests passed with the updated uniform and SPIR-V layout.
The PbrSurface layout change invalidated 138 transitive source translation units
before rebuilding; the dependency list is in `build/vegetation-pbr-rebuild.json`.

## Material persistence preparation

Unity Standard materials now emit the existing `eve.material/2` definition and
manifest version, with matching prefab dependency types. The import/cook/runtime
fixture checks both material dependencies and successfully loads the resulting
prefab. The 14 selected Unity native mesh, FBX, nested prefab and package cases
passed after rebuilding. This change does not yet translate TVE materials.

Material persistence now uses `eve.material/3`; see
[the versioned contract](canonical-material-v3.md). The reader accepts v2/v3,
rejects mismatched definition/chunk versions and malformed nested color settings,
and distinguishes logical image identity validation from resolved GPU binding
validation. V2 migrates explicitly to v3; v1 is outside the N/N-1 window and
requires the preceding migration tool. Root additive fields retain their existing
policy. No architecture exception was introduced.

This does not implement TVE source material translation. The native parameters can
now be authored in an asset definition and are retained through cooking; the
source-specific importer is still required.

The material-v3 validation run passed 102 selected asset/migration/import/graphics,
PBR and vegetation cases with Vulkan validation enabled and no validation errors.
It includes authored-v3 cook/read with a real cooked image dependency, malformed
fields, unversioned parameters, chunk/definition version mismatch, v2 compatibility,
and migration identity/dependency preservation. The renderer-free asset-core profile
still passes. The architecture gate and its 9 fixtures pass without exceptions.

## Original TIFF texture admission

The source package uses 82 TIFFs and 9 TGAs, so PNG/JPEG-only image admission was
insufficient for its actual materials. Native TIFF import/cook now preserves RGB
and the fourth data channel for the package's raw/LZW/Deflate, 8/16-bit strip
layouts. See [TIFF admission](tiff-source-admission.md) for the exact scope and
quantization contract. All 82 actual TIFF sources passed admission. Ten original
encoding combinations were cooked and compared against Pillow/libtiff, changing
only the in-memory ExtraSamples metadata to expose the fourth channel. The six
8-bit combinations match every output byte; the four 16-bit combinations differ
by at most 3 RGB steps after transfer and 1 alpha step because of downconversion
rounding. This is not a lossless 16-bit claim.

The final selected regression run passed 104 cases with no Vulkan validation
errors. Asset-core passed 28 cases / 490 assertions; architecture fixtures passed
9 cases. TGA admission and complete TVE source material translation remain open.

## Original TGA texture admission

TGA admission is now implemented for the source package's 24/32-bit raw and RLE
files. All nine original TGAs pass import/cook and their RGBA pixel bytes match
Pillow exactly under the explicit linear-transfer comparison. See
[TGA admission](tga-source-admission.md) for the decoder's supported layouts and
ownership/budget contract. The selected full regression run passes 106 cases;
asset-core passes 30 cases / 804 assertions. These results do not claim TVE shader
or source material conversion; that remains the next integration step.

## Primary TVE material conversion

Primary material translation is now connected to Unity collection import and
material-v3 cook/read. It converts main albedo binding, linear colors, primary UVs,
AO/smoothness channel packing and RGB mixing. Prefab dependencies use the actual
material schema version. Unsupported shader features remain explicit findings.
See [the import contract](unity-vegetation-material-import.md) for exact scope,
original-package evidence and the remaining source/shader gaps.

Twenty-eight original materials now produce validated primary definitions and
derived ORM textures; 109 selected regression cases pass. The source-complete
diagnostic set excludes both genuinely missing references and currently unsupported
serialized Texture2D sources; these must not be conflated. Full material parity
and original-material rendered-scene acceptance remain incomplete.


## Native Texture2D BC3 admission

The two original Pine albedo Texture2D assets (GUIDs 86017fa9aef64ed4d9209b3d2f1e126f and 87f02e48c3ef43fa9381d4083d85c410) now import as native image/2 assets instead of being misrouted to Mesh conversion. Their complete serialized source is retained. Base-level RGBA8 decoding was compared with Pillow DXT5 decoding over all pixels: both 1024x1024 and 2048x2048 images match exactly in all four channels after the required row flip. Evidence: build/tve-reference/native-texture-pixel-comparison.json; output: build/tve-native-textures.eva.

Focused decoder and collection validation cover alpha endpoint modes, color selectors, partial blocks, allocation limits, truncated data and duplicate fields. The full related regression suite passes 111/111 with no Vulkan validation errors; architecture contracts and 9 fixtures pass, with no module back-edges. See unity-native-texture-import.md for transfer semantics and runtime mip/sampler limitations. The full TVE port remains incomplete.

The expanded original collection now imports 32 primary TVE materials and 144 images (112 original images plus 32 derived ORM images). All material texture references resolve within the emitted package. Four Pine materials were enabled by native Texture2D decoding and support for native type-2 texture references. Evidence: build/tve-materials-native-textures.eva and build/tve-reference/native-texture-material-validation.json. This validates source conversion and dependency closure, not a rendered original-asset scene. No architecture exception was taken.

Native BC3 admission now retains the complete mip chain rather than publishing only its base level.
The two supplied Pine sources preserve 11 and 12 levels respectively; their decoded RGBA byte
counts exactly match the sum of all source level dimensions. The isolated EVA and four-chunk
Windows Vulkan EVPACK both pass full validation. See `unity-native-texture-import.md`.


## Filtered Vulkan normal reconstruction

Added native PBR normal modes for TVE RG, R*A/G and A/G source shader branches, evaluated after filtering with signed strength and fixed Z=1. Material/3 persists normalEncoding and rejects unknown, missing-image or invalid-strength inputs. Material/2 rejects the new semantics rather than silently ignoring them. Twelve real Vulkan/CPU directional-normal comparisons pass within 0.015 RGB per-channel tolerance. The selected full regression suite passes 115 cases, with architecture contracts, nine fixtures and the renderer-free asset profile passing. CPU PbrSurface consumers were refreshed for the changed layout; the GPU uniform remains 960 bytes. No architecture exception was taken.

This stage does not yet automatically process original Unity normal import settings, reproduce source tangent frames, generate height normals, or implement source detail-mask normal consumers. Those remain part of the full port.

## Secondary detail material stage

Canonical material v8 preserves TVE's optional second albedo, normal and mask
layer as a named vegetation detail stage. The importer retains UV selection and
scale mode, dual colors, albedo/normal/material strengths, texture and mesh mask
selectors, and all three remap ranges. Runtime vegetation geometry carries the
authored blue detail mask and detail UV beside variation and occlusion without
adding a Vulkan vertex attribute. Vulkan samples the three dedicated bindings
and follows the source albedo, alpha, additive-normal, metallic, smoothness and
occlusion formulas, including world-projected normal conversion.

The original-package evidence is `build/tve-materials-detail-v8.eva`, the
735,514,156-byte `build/tve-materials-detail-v8-final.evpack`, and
`build/tve-reference/runtime-detail-v8-validation.json`. The EVPACK build
identity is `602325b6-64f6-566b-aafe-4efc566c40ba`; all 376 chunks pass archive
validation. The independent check finds 32 admitted materials and exactly four
enabled detail definitions: `Pine_Big_Base`, `Pine_Big_Bark`, `Rock_03`, and
`Rock_Ground`; their source parameters and runtime-required image dependencies
match the original `.mat` files. A focused Vulkan readback checks the replace
blend against an independent sRGB-to-linear reference.

The original Rock Ground material was then loaded from that EVPACK with all
seven cooked images and rendered through a real Vulkan canvas on a synthetic
quad whose detail UVs span the source texture. Disabling only the detail layer
changed 2,140 inspected pixels with a maximum channel delta of 0.968627; the
validation layer reported no VUID errors. The engine-owned comparison frame is
`build/tve-reference/rock-ground-v8-accepted-8x.png` (detail disabled on the
left, enabled on the right). This proves the imported secondary layer reaches
the renderer and remains spatially textured; it is not mesh-level visual parity
for the original Rock Ground scene.

## Global extras field source contract

Source inspection confirms that TVE's global overlay is one channel of a
nine-slice `RGBAHalf` field, rather than a standalone overlay image. The other
channels are emissive, wetness and alpha. Geometry shaders select one slice with
`_LayerExtrasValue`, project world or object X/Z through `TVE_ExtrasCoords`, and
fall back to `TVE_ExtrasParams` when that slice is unused. The exact resource,
sampling and alpha equations are recorded in
`docs/dev/vegetation-extras-field-source-contract.json`.

EVEngine's `VegetationField` remains the sole mutable authority. The native GPU
projection now bakes all nine Extras layers directly from it, converts finite
values to IEEE binary16, and uploads one linear RGBA16F 2D array. Publication is
atomic: invalid geometry, the 256 MiB upload budget, composition failure,
half-range overflow, a revision change during baking, or backend upload failure
returns a structured error before a texture is exposed. The detached snapshot
records the source revision and snapped center/extent; its texture remains owned
by the resource factory and must be released through that factory.

The Vulkan PBR path binds the array at descriptor binding 8. It reproduces the
source usage/fallback branch, `coords.zw + coords.xy * position.xz`, and the
world-position versus model-pivot switch. Extras R/G/B drive emissive, wetness
and overlay per fragment. A Canvas readback test renders one mesh across two
world-space field texels and proves their overlay results differ under Vulkan
validation. Extras alpha now survives Unity import and canonical material v9
cooking. Masked PBR materials apply the TVE detail-alpha, mesh-variation,
detail-fade-mask, global-alpha and internal `0.01` clip order per fragment. The
remaining glancing, camera-distance, constant and noise-dither fade stages are
tracked separately.

The same revisioned baker now projects all nine Colors layers to a second
RGBA16F array. PBR binding 9 reproduces `TVE_ColorsUsage`, fallback parameters,
layer selection, world/pivot coordinates and LOD-zero sampling before the
existing TVE grayscale/tint blend. Canonical material v9 keeps Colors and Extras
layer/pivot choices in `vegetationFields`; runtime arrays remain derived field
state. A Vulkan Canvas test spans two field texels and reads back independent red
and blue tint results from a single mesh. The source contract is recorded in
`docs/dev/vegetation-colors-field-source-contract.json`.

Motion and Vertex now use the same bounded, revision-checked nine-layer
RGBA16F projection API and pass real Vulkan allocation/release validation.
The GPU vertex path consumes both arrays when selected as the deformation owner;
CPU-deformed draws reject that selection so field displacement is not applied twice.
## GPU global Vertex field

The Vulkan PBR vertex path now consumes the revisioned nine-layer RGBA16F
Vertex atlas. It reproduces TVE Plant Standard 12.6 object-pivot sampling,
usage/fallback selection, A-channel global size, and camera distance size fade.
`PbrVegetationDeformationSource` makes the geometry owner explicit so a draw
cannot silently apply the field to CPU-deformed vertices. The GPU path rejects
skinning and covers object mode around the local origin; authored per-instance
pivots and batched vegetation continue through `deformVegetation`.

Canonical material v10 persists motion/vertex layer choices and size/fade
controls while leaving atlas ownership and activation at runtime. A Canvas
readback test draws one rest mesh at two world pivots and verifies independent
25% and 100% silhouettes under Vulkan validation.

## GPU object motion and material persistence

The Vulkan PBR vertex path now applies TVE 12.6 object motion to the immutable
rest stream. It consumes the nine-layer Motion atlas and a repeat noise texture,
uses explicit time, wind direction and world origin, and reproduces bending,
branch, rolling, flutter, interaction, distance fade and perspective stages.
The native field stores signed X/Z direction directly, so it is not decoded as
Unity's unsigned render texture. A validation-layer Canvas readback test proves
that a fully weighted rest mesh moves by more than four pixels at a fixed time.

Canonical material v11 persists the 21 material-local motion controls and the
v10 field layer. Global wind, atlas, noise, time and global motion multipliers
remain runtime-owned. Cooked material loading restores parameters without
enabling deformation; activation requires explicit runtime bindings. See
`canonical-material-v11.md` and
`vegetation-motion-field-source-contract.json`.

## Coherent GPU field publication

`uploadVegetationGpuFieldSet` publishes the Colors, Extras, Motion and Vertex
RGBA16F arrays as one detached result. If a later channel fails, every earlier
factory-owned texture is released before failure returns. The successful set
records one revision. The scene-manager overload accepts four independently
sized nine-layer groups, matching TVE's separate Colors, Extras, Motion and
Vertex render-target resolutions. Each group is internally coherent, and the
complete publication has a combined 256 MiB RGBA16F upload budget. The original
four-channel atlas overload remains available for same-geometry fields.

`bindVegetationGpuFields` validates that coherence against the authoritative
`VegetationField`, computes each channel's world-XZ atlas transform, and applies all four
bindings plus explicit motion globals to a candidate `PbrSurface`. Stale,
mismatched or invalid inputs leave the caller's surface unchanged. Texture and
noise lifetimes remain with Graphics and are documented as covering all draws;
`releaseVegetationGpuFieldSet` provides structured best-effort teardown after
the material has stopped referencing the set. A real Vulkan test covers upload,
binding, independent dimensions and UV derivation, stale rejection, atomic
surface retention and release on both Vulkan and WebGPU.

## Volumetric alpha fade

The resource factory now admits validated RGBA8 3D textures. Vulkan uploads a
true `e3D` image and exposes a repeating trilinear sampler; unsupported backends
return a structured status. The PBR fragment path samples that volume in world
space after TVE's glancing, camera, constant, detail and global-alpha controls.
A validation-layer Canvas test proves the threshold ordering with a controlled
50% volume sample: a 0.25 constant fade remains visible and 0.75 is discarded.

Canonical material v12 persists the three material-local fade controls while
the coherent field runtime owns the camera range, tiling and borrowed volume
texture. Strict v11 migration expands only a valid legacy alpha object and is
atomic. See `canonical-material-v12.md`.

The package's `Internal NoiseTex3D.asset` is a 16x16x16 embedded scalar volume
with one mip and 4096 source bytes. `eve.volume-texture/1` now preserves that
source as R8, cooks it to bounded RGBA8 `EVVOL`, uploads a true Vulkan 3D image,
and can bind the loaded texture directly as the PBR vegetation fade source. See
`canonical-volume-texture-v1.md`.

Unity 6000.0.79f1 oracle confirms the serialized source is R8_UNorm with
bilinear filtering and Repeat on all three axes. The actual 12.6 source bytes
survive EVA exactly, cook to the specified EVVOL RGBA expansion, and upload
through the Vulkan production loader with validation enabled.

## Emissive texture stage

Canonical material v13 translates TVE's opt-in emissive stage. It preserves the
HDR color, Unity's baked Nits/EV100 intensity, emissive UV transform and sampler,
texture remap endpoints, phase, and global Extras influence. The Vulkan fragment
path follows the source order: texture decode, epsilon remap, global-field phase,
saturation, then HDR color and intensity multiplication. A validation-layer
Canvas test compares both halves of a controlled texture against a separately
evaluated reference texture, including a zero result and a nonzero HDR result.

The Extras atlas remains revisioned runtime state, while all local controls and
the emissive image dependency are canonical material data. Missing textures,
invalid modes, ranges, UV scale, colors, and intensities fail before publishing
an import. See `canonical-material-v13.md` and
`vegetation-emission-source-contract.json`.

## Height-gradient stage

Canonical material v14 translates TVE's Plant Standard height gradient. It
preserves both linear HDR colors and the min/max endpoints, consumes the mesh
height already retained by the canonical vegetation stream, and uses the blue
mask after the same main/detail blend as the albedo layers. The Vulkan fragment
path follows the source order: saturating epsilon height remap, color-two to
color-one interpolation, white-to-gradient mask blend, then multiplication
before global Colors-field tint and overlay.

Unity import, canonical cook, runtime loading, and v13-to-v14 migration reject
malformed colors, endpoints, and unversioned fields before publication. A real
Vulkan Canvas test exercises the low and high mesh-height endpoints. See
`canonical-material-v14.md` and `vegetation-gradient-source-contract.json`.

The same schema revision completes TVE vertex occlusion. The mesh green channel
is clamped and epsilon-remapped once; its RGB path interpolates from
`_VertexOcclusionColor.rgb` to white before global Colors, while its alpha and
independent inversion switches gate Colors and Overlay. This also corrects the
Overlay path to consume the remapped mask rather than the raw vertex channel.

The final source-backed v14 import is
`build/tve-materials-gradient-v14.eva` (625312572 bytes, SHA-256
`1633f6246e4c401a180d090053fec816b5966c80a26300a5d9a0ffde37cef0a0`).
Its 205 assets include 32 schema-v14 materials. Independent source comparison
passes for all 32 Gradient definitions and all 32 vertex-occlusion RGB values;
10 materials use a non-white occlusion color. The source collection's Gradient
values are all default white, so the non-default Gradient formula is separately
covered by the validation-layer Vulkan Canvas test.

TVE render controls now preserve `_RenderSpecular` and the complete
`_RenderNormals` Flip/Mirror/Same facing branch. Three source materials disable
specular, while the two meadow-grass materials select Same normals. A reversed-
winding Vulkan Canvas probe confirms that Flip darkens the back face and that
Mirror/Same retain the positive geometric Z response when no normal texture is
bound. The hidden shader state shows Direct, Ambient and Shadow at one,
AlphaToCoverage at zero, and actual ZWrite at one for the complete source
collection; those paths already match EVEngine's current material behavior.
`_RenderCull` is preserved independently as `none`, `back`, or `front` rather
than being collapsed into the old `doubleSided` flag. A validation-layer Vulkan
Canvas probe renders the same winding through all three pipeline variants and
confirms that TVE's value 2 selects front-face culling. The closed source set
contains 17 no-cull and 15 front-cull materials; 13 of the latter use admitted
TVE shaders and two are generic scene materials.
The resolved state is also retained on cascaded-shadow draws in both Vulkan and
WebGPU, with separate static/skinned and opaque/alpha-cutout pipeline variants;
legacy shadow drawers keep their original no-cull default.
GBuffer draw collection carries the same resolved state. Both backends select
none/back/front variants for opaque and alpha-cutout fills, and Vulkan applies
them to skinned fills as well. A single-pass Vulkan attachment readback confirms
the three variants on side-by-side triangles without stale frame-slot ambiguity.

## Terrain material rendering

Canonical terrain material v3 reaches the portable runtime material path. The
asset layer resolves EVIMG references into bounded CPU groups, then packs up to
sixteen layers into 4x4 albedo, tangent-normal and mask atlases. The draw texture
contains four control maps and holes; the fourth auxiliary slot stores lossless
per-layer values without expanding the portable 32-float push contract. All
groups are normalized together and shaded once. Legacy callers retain the
previous procedural colors until atlas sampling is explicitly enabled.

The shader ships as embedded SPIR-V and native WebGPU WGSL, fixing the old API's
inability to create its runtime GLSL program on Windows. A headless Canvas test
renders through the public upload/bind APIs, then selects only layer five through
the second control map. Vulkan and WebGPU both read back the expected green center
pixel. See `procgen-terrain-material-shader.md`.
## Global Details publication and mask consumption

TVEGlobalDetails.cs publishes the three layer selectors, global Color/Alpha/Overlay/Wetness
multipliers, independent Colors and Overlay mask endpoints, and alphaTreshold - 0.5.
EVEngine now represents that state with VegetationDetailSettings, validates and projects it
transactionally into detached PBR/motion snapshots, and persists it as the exact
eve.graphics.vegetation-details/1 schema. Graphics.newVegetationDetails() provides the
VM-owned snapshot/restore interface.

The Vulkan PBR path consumes the independent Colors and Overlay endpoints using TVE's shared
clamped remap with the 0.0001 denominator guard. The alpha offset is added to the material cutoff
and clamped before upload. Default PBR endpoint values preserve the previously established
0.1-to-0.2 stage response when no Global Details publication is applied. A real Canvas readback
proves that changing the global Colors endpoints changes the fragment-stage result; projection,
persistence and script ownership have focused tests.
## Stable terrain image references

Terrain material v3 supplements every retained source identity with an optional strong image
AssetRef for diffuse, normal, weight, mask, holes and all four control maps. Unity derives these
references deterministically from the package identity and texture GUID. Runtime load parses all
non-empty references transactionally. The v2 migration emits explicit empty bindings because old
standalone definitions cannot recover package-relative asset identities.

EVIMG validation and owning CPU mip decode now live in `asset::decodeEvpackImage`; the existing GPU
image loader calls the same function. Focused tests prove canonical cook-to-pack CPU decode, GPU
mip upload, Unity terrain v3 cook/load and v2 migration. Atlas assembly performs bounded resampling,
normal-convention conversion, mask remap baking, neutral fallback creation and multi-control packing.
Transactional upload/bind/release tests cover the public resource path, and a real Canvas readback
proves that the second control map selects layer five in one draw on Vulkan and WebGPU.

## Current material v15 package evidence

The current importer produced `build/tve-materials-v15.eva` from the closed 32-material source
collection. It contains 204 canonical assets; all 32 material manifest records and definitions use
`eve.material/15`. The 624828129-byte EVA has SHA-256
`bf09b8acd432fb342c6380788d8e704dee86155afd16b00d53ca15fef04d1093` and passes full archive
validation. Cooking it for `windows-x86_64-vulkan` produced a 735527884-byte, 376-chunk EVPACK
with build identity `7841b06a-c3f6-5f58-a8dc-98323f24f17f` and SHA-256
`69d91fe2843cd96a6751d70e41ea0aa72b35743ac755b14f73ff508cd267987e`; the runtime pack also
passes full validation.

## Preset material publication bridge

An applied conversion candidate can now be encoded as a bounded, detached Unity text Material and passed back
through the existing TVE material importer. The bridge accepts exactly the four shader families declared by the
12.6 presets (Plant or Prop, Standard or Subsurface), maps them to the four inspected shader GUIDs, preserves
floats, colors/vectors, texture GUIDs and transforms, keywords, and instancing, and rejects conflicting or
non-finite state before publication. This keeps canonical material v15 construction in
`prepareUnityVegetationMaterial` as the single source of truth. Focused testing decodes the generated document
again and compares every represented property.

The Vulkan GPU-driven material table now carries each masked material's authored alpha cutoff through both the
indirect forward and visibility-buffer paths. WebGPU's per-bucket frame record carries the same surface code and
cutoff through direct and resident indirect draws. Real pixel-readback regressions use alpha 0.6 with cutoffs 0.25
and 0.75, so a legacy hard-coded 0.5 threshold cannot pass on either backend. Transparent materials and materials that enable the full
`PbrSurface` extension are rejected by GPU-driven eligibility until that path represents their blend ordering,
extra texture bindings, vegetation vertex streams and deformation inputs. This contract is enforced by a shared
Vulkan/WebGPU eligibility test; the WebGPU path previously accepted opaque extended surfaces even though its
indirect shader could not consume them. These materials therefore retain the complete renderer rather than
silently dropping TVE behavior. Focused Vulkan and Dawn/WebGPU eligibility, opaque indirect, masked cutoff and
camera-facing card regressions pass together.

Unity Terrain Detail sidecar v2 also retains `healthyColor`, `dryColor`, `bendFactor`, `holeEdgePadding`, and
`useDensityScaling`. These values were absent from sidecar v1 even though they affect classic grass coloration,
bending, hole-edge distribution and global density scaling. Sidecar v3 additionally owns TerrainData's global
`wavingGrassAmount`, `wavingGrassSpeed`, `wavingGrassStrength`, and `wavingGrassTint`; these inputs are required
to interpret a prototype's bend factor. Canonical `eve.instance-set/5` is their single persistent owner; v3 and
v4 migrations supply neutral appearance and disabled waving defaults. The runtime loader accepts v1-v4 directly
for compatibility and validates every v5 scalar and normalized color.
The supplied TVE archive itself contains no serialized TerrainData/detail-prototype asset, so placement evidence
must come from a Unity project through `ComputeDetailInstanceTransforms`; it is not inferred from the package.
Classic non-instanced details derive their per-instance linear RGBA multiplier from the exported height scale and
the prototype's dry-to-healthy range; instanced detail prototypes retain Unity's neutral-white behavior. The
multiplier is part of the shared `GpuInstance` record and is consumed by Vulkan forward/visibility resolve plus
WebGPU direct, compute-compacted, and resident submissions. The WebGPU visible stream mirrors the complete 208-byte
`GpuInstance` layout, including color and terrain-wave inputs; 16-record bucket alignment keeps every dynamic
storage offset aligned to 256 bytes without allowing culling to discard appearance or motion state.

TerrainData's global waving speed, amount, strength and tint now combine with each prototype's `bendFactor` and an
explicit frame time in `buildTerrainVegetationGpuPlan`. Vulkan forward, visibility raster and visibility resolve
deform the same world-space vertices, while WebGPU direct, compute-compacted and resident submissions use the same
Unity-compatible fast-sine core. Tree-prefab parts retain disabled grass waving. Non-finite frame time fails before
any resolver or graphics mutation.

`TerrainVegetationRenderer` owns the detached realization and registers a tokenized GPU-opaque collector with
`RenderSystem3D`. The graphics thread invokes it before the normal GPU-driven cull, visibility/direct draw, and
resolve stages, so ordinary renderables and terrain vegetation share one correctly ordered instance batch.
Destruction removes the token, so terrain unload and hot replacement cannot leave a callback retaining loader
storage. Animation time is an explicit frame input and asynchronous submission failures remain observable through
`lastSubmissionError()`.

Its system scope is one renderer-owned terrain realization. It reads immutable instances and prototypes plus the
explicit frame clock, writes backend GPU-driven submission buffers, performs no ECS structural mutation, emits no
events, uses the borrowed prototype resolver service, and runs in the GPU-opaque collection phase. Identical
realization, resolver records, and frame time produce identical GPU instance records. The resolver must outlive the
renderer; neither its calls nor the collector callback may re-enter registration or terrain submission.

`TerrainDetailRuntime::load` is the owning high-level entry point for cooked Unity Terrain Detail. It loads the
instance set, realizes deterministic buckets, resolves every prototype before a render frame begins, and registers
the renderer only after preparation succeeds. Its explicit `release` first unregisters the collector and then
releases material, image, mesh, and prefab leases. Callback lifecycle and release order have focused tests; the
current visible backend proof composes the imported instance set, decoded texture, and generated Detail card without
claiming collector submission as renderer evidence.

Vulkan stage-one and resident draws bind each prototype mesh's private buffers and therefore emit zero-based
indirect index/vertex offsets; pooled offsets remain exclusive to cull/visibility resolve. WebGPU provides separate
one-sample RGBA8 and RGBA16F pipelines for direct and resident Canvas submission, so HDR preview or capture does
not reuse an attachment-incompatible pipeline.

The WebGPU extended-PBR path cannot use one unconditional translation of the Vulkan descriptor set.
The Vulkan fragment stage declares 19 sampled textures and 19 independently configured samplers, while the
native Dawn adapter used by the regression suite rejects a device request at those limits; the established
backend contract is 17 sampled textures and the WebGPU default sampler budget is 16. Tint converts the complete
vertex stage directly, and converts the fragment stage after its two GLSL resource arrays are expanded, so shader
language coverage is not the limiting factor. The implemented material-feature planner specializes both generated
WGSL stages to the bindings active in each immutable `PbrSurface` snapshot, coalesces identical sampler states,
caches the resulting Dawn pipelines, and rejects snapshots that exceed captured device limits. The draw path owns
the complete 1952-byte uniform snapshot, eleven independently selected UV streams, tangent and vegetation streams,
skinning, shadows, environment lighting and all canonical/detail/field textures. WebGPU now also uploads RGBA16F
2D arrays and RGBA8 volume textures through the shared resource-factory contract, with dimensional validation before
material publication. Shared readback regressions exercise every glTF extension factor and texture role, TVE normal,
translucency, Extras, Colors, Vertex, Motion and volume-fade behavior on Vulkan and Dawn/WebGPU. Raising the whole
backend's device requirement would disable otherwise supported adapters and remains outside this design.

Vulkan's GPU-driven vertex pool now reserves a std430-aligned vegetation record for every pooled vertex. The
record preserves the authored tangent frame, all five variation/occlusion/detail factors, motion highlight, and
all nine rest-deformation values, with deterministic neutral values and explicit presence flags when a stream is
absent. Mesh-table registration is deferred until `gpuDrivenMeshRecord` is requested, after the asset loader has
published those optional streams; the previous factory-time registration captured an incomplete mesh. Released
meshes clear their owner slot so stale instance identifiers are rejected instead of dereferencing freed storage.
The registration test attaches full vegetation streams before the lazy snapshot, and the Vulkan forward, masked,
eligibility, and visibility-resolve render regressions pass with the expanded descriptor layout.

`prepareUnityVegetationConversionImport` completes the detached publication transaction. It assigns stable
content-role GUIDs to generated textures, encodes them as bounded linear PNG sources, builds a minimal in-memory
Unity project containing the converted material and every referenced texture, and sends that project through the
normal Unity collection importer. The converted canonical mesh is then added with authored TVE bounds and CPU
readability metadata before one final import report is generated. Asset-count and aggregate-byte budgets are
checked after all outputs exist. The focused transaction test builds and parses the resulting EVA and cooks its
material, generated image, derived ORM image, and mesh to a Windows Vulkan EVPACK.

The source collection deliberately cannot resolve dependencies absent from the supplied archive.
Those remain explicit report findings and are not replaced with fabricated assets. The importer
report wording for the now-translated render-state controls was corrected after this evidence run;
the correction changes diagnostics only and will appear in the next generated package.
Field-volume editing uses `VegetationFieldTarget` as the owning authoring document and treats `VegetationField` as its atomic runtime projection. Stable editor IDs are independent of priority-sorted runtime indices. Property edits and translate/rotate/bound gizmos produce the same invertible transform operation; staging validates a detached complete candidate, and commit rejects an externally changed runtime revision without partial publication.

Multi-material editing uses `MaterialBatchTarget` as one transaction participant. A selection may span stable material IDs and reports mixed property values. Set/reset operations build complete owning material snapshots, validate every document, then invoke one `IMaterialBatchRuntimeSink::publish` call before adopting authoring state. The sink contract requires all-or-nothing runtime replacement; rejection preserves every document and the batch revision. This avoids partial updates from sequential single-material sink calls.

`VegetationConverterController` is the UI-neutral converter workflow used by editor hosts. It owns the full batch request, prepares through the canonical `prepareUnityVegetationConversionBatch` transaction, binds candidates to a monotonic request revision, and publishes through an injected generation-checked atomic boundary. Changing input invalidates the previous candidate; preparation and publication failures remain observable, and a failed publication retains the complete candidate for retry.

## Current full-package audit

The production `eve asset import --from unity` path now imports the complete supplied 279,876,763-byte archive in
one collection transaction. A TVE material whose texture GUID is absent from the purchased archive is retained as
source and reported as an unsupported `resource.dependency`; it no longer aborts unrelated assets. Direct material
conversion remains strict, and malformed or conflicting inputs still fail the collection. A focused regression
covers this distinction.

The current local inspection archive is `build/tve-full-runtime-v3.eva`: 666,718,200 bytes, 504 canonical assets
and 2,354 ZIP entries. `eve asset validate` accepts it. The manifest contains 172 images, 34 materials, 108 meshes,
58 schema-3 scene templates, 121 conversion presets, 10 vegetation scenes and one volume texture. Its report
confirms 25 Prefabs with resolved render bindings. Five TVE materials reference textures absent from the supplied
package and are explicitly omitted rather than receiving fabricated pixels. The manager scenes contain 312
executable Elements; all 310 texture-backed Elements own a resolved package AssetRef.

The complete EVA cooks successfully for `windows-x86_64-vulkan` into the 753,113,554-byte
`build/tve-full-runtime-v3.evpack`, build identity `71700c04-54b1-5068-86b4-dab9affac7b0`, with 785 chunks. Both
archives pass the production validator. Their SHA-256 values are
`d12e34dc6085241fe0079b13bbbc1eee59831f308600bf444e79723fb395ce54` and
`c2d968f9d8591622e9758c90be3f1c698d61eed771289ccad88e744af163f130` respectively.

The remaining unsupported report is dominated by retained C# and shader authoring sources, non-Mesh `.asset`
files, demo-only components, and external GUIDs absent from the archive. The collection importer
now recognizes the four-component TVE manager set in Unity scenes and publishes `eve.vegetation-scene/1`
atomically. An isolated import of all 11 original scenes produces 10 validated scene-state assets; the remaining
scene contains no TVE manager and is explicitly retained. The strict EVPACK loader rejects unknown fields, malformed
GUIDs, arrays, enums and ranges. Import restores the canonical XZ wind direction from the referenced Transform's
complete parent rotation, retains each Colors/Extras/Motion/Vertex volume channel's render mode and dimensions, and
migrates the package's one legacy scalar-resolution scene to square InsideGlobalVolume channels. The projector applies
direction and time speed while composing Global Details, Control and Motion into one detached PBR/CPU-motion candidate.
The runtime decodes Element masks, evaluates and composes every admitted Element family, converts completed TVE
Motion targets to native signed directions, uploads all nine layers, and owns the bound material plus four GPU field
arrays through retryable release. A Vulkan and WebGPU pixel regression renders a deformed vegetation card through
that complete high-level path. Automatic scene-instance association is complete. Unity scene import now publishes
the hierarchy template and manager in one transaction, preserves their shared source GUID, and records an explicit
`vegetationScene` runtime dependency. The runtime requires a unique same-GUID manager and owns the hierarchy,
manager projection, mask images and dynamic Element registry as one `VegetationSceneInstance`; missing and duplicate
matches fail before publication. Per-channel manager resolutions
and world mappings are retained through independent Colors, Extras, Motion and Vertex bake/upload inputs. The owning
GPU runtime supports generation-checked transactional replacement: stale and invalid candidates preserve the live
surface, successful publication releases superseded textures, and failed cleanup remains owned and observable for retry.
The current 282,128-byte `build/tve-scenes-v1-complete-final.eva` validates and cooks to the validated 10-chunk
`build/tve-scenes-v1-complete-final.evpack` (build `eb146400-6444-5227-936f-e6dd9064e6f1`).
`VegetationSceneLiveElements` closes the runtime registration boundary. Dynamic Element volumes use
owner/index/generation handles and stable slot order. Register, withdraw and slot reuse are coupled to the complete
GPU scene replacement transaction; stale registry revisions, stale GPU revisions, foreign owners, invalid Elements
and stale generations preserve both the live registry and rendered surface. Imported Elements remain the immutable
scene prefix, and successful withdrawals increment the slot generation before reuse.

The rebuilt complete-package `build/tve-full-runtime-v4.eva` is 666,777,662 bytes and passes production validation.
It contains 10 vegetation scenes and 68 scene templates; all 10 manager GUIDs have exactly one matching template and
the manifest contains 10 `vegetationScene` runtime dependencies. This verifies automatic association against the
original TVE 12.6 package rather than only synthetic fixtures. Its `windows-x86_64-vulkan` cook is the validated
753,147,810-byte `build/tve-full-runtime-v4.evpack`, build `101c6b39-377e-502a-bc5f-d4bef4edcf67`, with 795 chunks.
The EVA and EVPACK SHA-256 values are `3fad26948ef8128cfe8795e17f4ba3f28ac1e9744bf18ca1f6793cb5307f6b0e`
and `f6cd6e598c83b7c34c39e9256aef4bd8a76ac3d047cbe8f3e9f41b80da475e73`.

The original-package FBX/Prefab subset audit imports all 27 FBX sources and restores 60 Unity Mesh subresources,
including generation-1 indexed IDs, generation-2 name hashes, and TreeIt's explicit legacy 64-bit identity. The
validated archive is `build/tve-fbx-current.eva`, 20,917,480 bytes with SHA-256
`f6fd9c31ff8f20146655331d70b7e6aee1be2165af511146acd95b92d3e823b8`. Its nine unsupported dependency
findings are material assets intentionally omitted from the isolated subset, rather than FBX conversion failures.

## Imported Prefab and Terrain Detail shadows

`eve.scene-template/3` now retains Unity `MeshRenderer.m_CastShadows` and `m_ReceiveShadows` per resolved submesh;
the v2 compatibility reader and v2-to-v3 migration use Unity-compatible all-on defaults. `EvpackStaticPrefab`
applies receive state in its color draw and exposes an explicit cascade shadow draw for enabled opaque and masked
casters. Terrain Detail owns a tokenized `RenderSystem3D` shadow contributor alongside its GPU opaque collector,
submits texture cards or every retained Prefab part in each cascade, and unregisters both callbacks before releasing
GPU leases. Runtime loading prepares prototype GPU resources before either callback is published, because uploading
inside an active render or shadow pass can block a backend.

The latest production import of the supplied package is `build/tve-full-runtime-v3.eva`, 666,718,200 bytes with
SHA-256 `d12e34dc6085241fe0079b13bbbc1eee59831f308600bf444e79723fb395ce54`. It contains 504 canonical assets and
2,354 ZIP entries. All 58 scene templates are schema 3; the report contains 25 translated `Prefab.shadowState`
findings and no `Prefab.shadowPass` findings. Production EVA and EVPACK validation pass. Focused Vulkan and WebGPU
runs each pass the same 11 import, migration, callback-lifecycle, Terrain Detail realization, and real-render tests.
