# Virtual texture material blending

## Scope

`graphics/VirtualTextureBlend` is the backend-neutral authoring and reference path for large,
layered surface materials. It targets terrain, roads, decals, mesh-terrain transitions, and other
surfaces that need continuous blending across source-material and physical-page boundaries.

The implementation separates the material-authoring contract from hardware residency:

- source albedo, normal, and weight rasters are copied into immutable layers;
- requested mip pages are generated deterministically and stored in an LRU cache;
- every physical page contains neighbour texels around its payload, so linear and anisotropic
  filtering do not reveal tile seams;
- layer influence uses normalized painted weights plus optional source-alpha height contrast;
- tangent-space normals are decoded, blended as vectors, normalized, and encoded again;
- `buildResidentSet()` emits an atomic albedo atlas, normal atlas, RGBA8 page table, and slot-zero
  low-resolution fallback; `bakeAlbedo()` and `bakeNormal()` remain reference oracles.

`Material::setVirtualTexture()` opts a material into the atlas/page-table sampling path. Vulkan and
WebGPU share the same page-table encoding, gutter contraction, fallback selection, and explicit
derivative scaling. The implementation intentionally uses portable physical atlases rather than
claiming Vulkan sparse-image residency; no backend object leaks into the public contract.

## Mainstream-engine correspondence

Unreal Runtime Virtual Texturing separates material writers from samplers, caches camera-independent
material attributes over a bounded world-space volume, and keeps a conventional rendering fallback.
Its Landscape materials use painted weight blending and height blending. EVEngine adopts the same
separation at a smaller scope: layer admission and page production are backend-neutral, while each
renderer uploads ordinary textures and samples the same page-table contract.

Primary references:

- <https://dev.epicgames.com/documentation/unreal-engine/runtimevirtual-texturing-quick-start-in-unreal-engine>
- <https://dev.epicgames.com/documentation/unreal-engine/landscape-materials-in-unreal-engine>
- <https://dev.epicgames.com/documentation/unreal-engine/streaming-virtual-texturing-in-unreal-engine>

The page border follows the established virtual-texture gutter rule: a page's stored extent is
`tileSize + 2 * borderSize`, but only the inner `tileSize` texels belong to its virtual address.
Sampling the border from neighbouring virtual coordinates makes adjacent pages numerically agree.

## Ownership and lifecycle

`VirtualTextureBlend` is the only mutable owner of layer ordering, page-cache state, and residency
counters. `addLayer` deep-copies every supplied `ImageData` before publishing the layer; a failed
admission leaves the layer list and cache unchanged. Page requests return shared immutable receipts,
which remain valid if that page is later evicted. Adding a layer invalidates generated pages but does
not invalidate already returned receipts.

The object is single-thread affine and invokes no callbacks. A renderer must copy or upload page
pixels before releasing its receipt. `Material` borrows the three textures and copies all layout
values during bind; WebGPU additionally snapshots those resources per queued draw. GPU resources
remain owned by the selected `Graphics` backend, consistent with ordinary engine textures.

## Determinism and limits

Given identical RGBA8 sources, configuration, layer order, and page coordinate, CPU page pixels are
deterministic. Backend sampling is tolerance-bounded because texture filtering and sRGB conversion
can vary. The initial implementation accepts at most 16 layers and supports RGBA8 inputs. It does not
yet implement GPU feedback, asynchronous I/O, persistent cooked-page schemas, sparse binding, or
runtime writer primitives. GPU-driven visibility currently reports an explicit unsupported result
for virtual-texture materials and falls back to the standard PBR draw path.

## Intended renderer evolution

1. Add generation-qualified incremental page-table updates over the existing atomic resident set.
2. Collect GPU feedback, compact and deduplicate requests, then schedule bounded worker decoding.
3. Atomically publish page-table changes only after upload completion; retain the previous mapping on
   failure.
4. Add runtime material-attribute writers for terrain decals and mesh/terrain intersections.

The software producer and full-bake path remain the reference oracle and unsupported-platform
fallback throughout these stages.
