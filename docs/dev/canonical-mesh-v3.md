# Canonical mesh authoring attributes

EVMESH binary version 3 preserves named float vertex attributes alongside the
existing positions, normals, UV sets and indices. This enables lossless TVE
packed vertex admission without repurposing render UV coordinates as masks.

## Binary contract

Integers and float32 values are little-endian. The first eight bytes are
`EVMESH`, zero, version 3. Five uint32 fields follow: vertex count, triangle
index count, flags, UV set count and attribute count. Only flag bit 0, normals
present, is admitted. UV identifiers follow as strictly increasing uint32s.

Each named attribute descriptor occupies 72 bytes: a 64-byte zero-padded ASCII
name, uint32 component count and a reserved uint32 that must be zero. Names
contain 1–63 uppercase letters, digits or underscores, with every byte after
the terminator zero. Descriptors are strictly lexicographically increasing.
Names POSITION, NORMAL and names beginning TEXCOORD_ are reserved for existing
streams and rejected here. Component counts are 1–4. Unrecognized valid names
are retained as opaque authoring attributes, without inventing shader behavior.

Each vertex contains position float3, optional normal float3, render UV float2s
in UV descriptor order, then named floats in attribute descriptor order. uint32
indices terminate the buffer. Trailing bytes, nonfinite values, malformed or
duplicate descriptors, invalid indices and budget excess are rejected.

The JSON schema remains `eve.mesh/2`: its version and the typed binary version
are independent. No JSON field changes or identity migration are required.
Binary v1/v2 remain compatibility inputs to the same inward decoder. The single
encoder emits v2 when no named attributes exist, preserving existing imports;
otherwise it emits v3. Legacy bytes need no rewrite. Decode and encode publish
only owning complete results; inputs are borrowed for the synchronous call.
Both operations are renderer-independent, worker-safe and callback-free.

## Admission and consumption

The glTF importer routes canonical encoding through `encodeCanonicalMesh` and
retains TANGENT, COLOR_n and custom `_` attributes with admitted float or
normalized unsigned input. Unity text Mesh v9/v11 admission retains tangent,
color and all float32 UV components. `_UNITY_UVn` is a raw source authoring
stream: no UV-origin or axis conversion is applied to it. TEXCOORD_n contains
the independently interpreted render ST projection with flipped T coordinate.
Canonical positions/normals reflect Z; tangent Z and handedness also reflect.
The TVE adapter interprets raw X/Z/Y pivots before canonical axis conversion.

The runtime loader moves named arrays into `LoadedGraphicsMesh::attributes`.
They own their values independently of the archive and GPU mesh. Existing PBR
upload does not consume these streams; vegetation/shader adapters must do so
explicitly. No new backend capability or silently ignored upload is introduced.
Packed fields are not reconstructed from truncated render UVs.

`asset_graphics::VegetationAsset::load` is the explicit TVE consumer. It reads a
canonical EVPACK mesh and owns decoded rest vertices/indices/UVs after staging
and archive destruction. `fromCanonical` admits the same data without a pack.
Missing or malformed TVE streams fail instead of guessing authoring masks.
`evaluate` returns detached world geometry. `createMesh` returns a Graphics-owned
mesh, and `updateMesh` updates a borrowed mesh from rest data each frame; neither
retains the mesh. This asset can therefore outlive graphics or be destroyed first.
GPU operations require the graphics thread; immutable CPU evaluations can run
concurrently. Render the animated world-space mesh with an identity model matrix.

Unity compressed, non-float or multiple-stream layouts remain explicitly
unsupported in this change. Desktop FBX conversion retains every available UV
and color set plus float4 tangent space; missing tangent space is calculated
from imported normals and UV0. Legacy Unity FBX metadata without stable hashed
subasset IDs remains rejected rather than mapped heuristically.

## Evidence

`asset_mesh_attributes.cpp` covers packed-bit retention, malformed descriptors,
byte budgets, v2 compatibility and Unity v11 CR-CR-LF input through EVA, Cook,
EVPACK and the runtime loader. Existing multi-UV, importer and graphics tests
exercise compatibility. `asset_import_unity_fbx.cpp` exports and reimports a
desktop FBX containing two UV sets and vertex colors, calculates its tangent
space, and asserts the exact decoded canonical streams. The local supplied Grass Quad LOD1 asset was imported
without changing its mesh bytes: four vertices, six indices, and COLOR_0,
TANGENT, _UNITY_UV0, _UNITY_UV1, _UNITY_UV3 match source bits after the documented
tangent coordinate conversion. Local evidence is `build/tve-retention.json`;
the proprietary source stays under ignored build paths.

These are storage/admission results. They do not establish vegetation shading,
terrain, editor tooling or complete TVE equivalence.

MSVC Debug validation passed all 87 selected asset mesh/import/graphics and
vegetation cases, with Vulkan validation enabled and no validation errors.
An initial document-store failure was isolated to sandbox denial of the system
temporary directory; the unchanged test passed when directory access was allowed.
The renderer-free asset-core-only profile passed 25 cases / 249 assertions,
including the new attribute codec. Source architecture contracts and their nine
fixtures, dependency layering, discovery and quality metadata passed. Rules
applied: versioned persistence, explicit unknown-data policy, compatibility
decoding, checked Results, owning snapshots and atomic publication. No exception
or new baseline was used.

Runtime-adapter checks include canonical pivot rotation, returning exactly to
rest after animation, malformed/missing stream rejection and destroying source
staging before evaluation. The real vegetation render fixture now uses
`VegetationAsset::createMesh/updateMesh` for both summer and autumn captures.
