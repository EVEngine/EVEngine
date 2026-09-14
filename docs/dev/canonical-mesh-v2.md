# Canonical mesh v2 and multiple UV sets

`eve.mesh/2` preserves source UV set identifiers and all supported coordinate
streams. There is no fixed UV0/UV1/UV2 array size. Channel descriptors and numeric
payloads are bounded by the caller's byte, vertex and index budgets.

## Binary layout

New imports emit EVMESH binary version 2. All integers and IEEE float32 values are
little-endian. The first eight bytes are `EVMESH`, zero, version byte 2.
The next four uint32 fields are vertex count, index count, flags, and UV set count.
Flags currently allow only bit 0 (normals present); unknown bits are rejected.
After the 24-byte header come UV set identifiers as strictly increasing uint32s.
Sparse identifiers are retained without allocating arrays up to the largest ID.

Each interleaved vertex contains POSITION float3, optional NORMAL float3, then
one float2 for every UV descriptor in descriptor order. The final array contains
uint32 triangle indices. Exact byte size, nonzero triangle counts, finite values,
indices, descriptor uniqueness/order, overflow and budgets are checked before a
complete result is published. Unknown binary versions and trailing bytes fail.

The JSON definition records `texcoordSets` in descriptor order instead of the v1
`texcoord0` flag. Identity, topology, positions, bounds and coordinate conventions
remain unchanged. FLOAT source UV bits are preserved; normalized unsigned 8/16-bit
VEC2 accessors are converted to float32 using division by 255/65535, respectively.
Source strides and offsets are honored. Signed, unnormalized integer, mismatched
count and malformed semantic names are rejected. Tangents and morph targets are
outside this change.

## Material bindings

Each texture's effective UV set must exist on every primitive bound to that
material. A missing channel fails admission instead of falling back to UV0.
With `KHR_texture_transform`, the canonical `<slot>Transform.texCoord` owns the
selection, following the source override rule. Without a transform,
`<slot>TexCoord` owns it. The importer never emits duplicate selection fields.
Texture offset, scale and rotation do not modify the stored mesh coordinates.

## Migration and runtime ownership

Material definitions remain v2. Mesh definitions migrate from v1 to v2 without
changing asset identity or source mesh bytes. The binary layout has its own
version: migrated legacy bulk remains binary v1 and is read by the compatibility
branch of the canonical decoder. A legacy `texcoord0` flag becomes `[0]` or `[]`;
when older metadata omitted the flag, the binary remains authoritative and no
UV list is invented. Unknown JSON fields are preserved; the reserved v2
`texcoordSets` field on a v1 definition is rejected to avoid an ambiguous migration.
Matching local dependency type versions are updated, including skin/scene links.
Migration operates on an owned candidate and never partially mutates the input.

`asset::decodeCanonicalMesh` is the renderer-independent inward decoder for both
binary versions. Its return value owns positions, normals, indices and UV arrays;
it retains no archive spans and is worker-safe, reentrant and free of IO/callbacks.
Malformed data returns a checked Result before any backend upload.

`EvpackGraphicsLoader::loadMesh(..., limits, texcoordSet)` explicitly selects one
UV set for the existing single-UV graphics factory. Its returned `texcoordSet`
records that projection; requesting a missing set fails before calling the factory.
Default UV0 remains compatible with untextured meshes. The backend mesh retains
its existing factory-owned lifetime and graphics-thread restriction. Other UV
arrays remain accessible through the canonical decoder. This does not implement
simultaneous per-texture UV selection or the seven extension shading effects.
The legacy static-prefab material reader explicitly rejects nonzero per-texture
UV selections and transforms it cannot render.

## Verification and remaining source error

Tests cover UV0/UV2/UV7, FLOAT/normalized U8/U16, import/Cook/read retention,
legacy binary decoding, definition migration and dependency versions, missing
channels, malformed descriptors/counts, non-finite values, byte budgets, and
explicit graphics upload selection. Existing graphics failure-injection, Unity
static-prefab and basic import tests remain in the compatibility target. The
same data-only tests also run in `asset-core-only` with no renderer.

The original Mech GLB now passes UV parsing and reaches a separate source error:
`Mech_Lenses` exports `KHR_materials_specular.specularFactor = 38406.7422`.
The [Khronos schema](https://raw.githubusercontent.com/KhronosGroup/glTF/main/extensions/2.0/Khronos/KHR_materials_specular/schema/material.KHR_materials_specular.schema.json)
requires this factor to be between 0 and 1. The original remains rejected.
For UV isolation only, a separate diagnostic copy sets that one factor to 1;
its binary chunks and every mesh UV value are unchanged. This copy's success
must not be reported as acceptance or visual fidelity of the original material.
Evidence and the exact edit/hashes are under `.local-debug/ue-broad-acceptance/`.

## Local acceptance (2026-09-10)

The diagnostic Mech copy passed import, Windows Vulkan Cook (105 chunks), EVA
validation and EVPACK validation with the final executable. Across 14 primitives
and 203,324 vertices, all 42 UV streams (609,972 ST pairs) match the **original**
GLB bit for bit. Positions, normals and indices also match; all five nonzero UV
material bindings survive. Every cooked mesh bulk SHA-256 matches its EVA bulk.
The original GLB remains rejected for its out-of-range specular factor, so the
unmodified broad-suite acceptance remains 18/21, not 19/21.

A real legacy UEFN animation archive was also re-Cooked and validated: the mesh
asset version advanced from 1 to 2 while its legacy binary payload hash remained
unchanged. Runtime compatibility tests include v1 binary decode and backend
upload failure injection.

Final tests: `eve_gltf_animation_check` in asset-core-only passed 23 cases / 223
assertions; `eve_material_compat_check` passed 20 cases / 217 assertions. Source
architecture contracts, all seven contract fixtures, module layering and diff
whitespace checks pass. Applicable rules: versioned persistent formats with
explicit unknown-field policy and migration, checked Result operations, atomic
candidate publication, owned decoding snapshots and renderer-independent core.
No architecture exception was taken. These results are format/adapter evidence,
not proof of multi-UV material shader rendering.

Local evidence: `mesh-v2-uv-retention.json`, `package-mesh-v2-results.json`,
`package-mesh-v2-control-results.json`, `uv-isolation/diagnostic-change.json` under
`.local-debug/ue-broad-acceptance/`, plus `.local-debug/mesh-v1-migration-retention.json`.

## Runtime multi-UV PBR update (2026-09-10)

`loadMesh(..., preserveAllTexcoords=true)` retains every UV stream through the
checked mesh-factory interface; the default preserves the earlier explicit
single-channel projection. Missing provider support releases the candidate mesh.
`PbrSurface` selects and transforms each texture's channel in Vulkan forward draws.
Mech's invalid specular factor now clamps to 1 with a warning in default import
mode; `--strict` keeps rejection. See `gltf-material-extensions.md` for scope and
verification; the earlier blocked results above are historical.
