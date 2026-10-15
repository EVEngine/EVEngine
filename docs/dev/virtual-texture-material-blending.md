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
can vary. The implementation accepts at most 16 layers and supports RGBA8 inputs. Physical residency
is currently mip-zero only (higher-mip feedback collapses onto the parent mip-zero page). It does
not yet implement a GPU-written feedback append buffer, asynchronous disk I/O, persistent cooked-page
schemas, or sparse binding. GPU-driven visibility currently reports an explicit unsupported result
for virtual-texture materials and falls back to the standard PBR draw path.

## Runtime streaming (UE5 RVT correspondence)

`graphics/VirtualTextureStreaming` owns the feedback → decode → publish loop:

1. **Feedback** — `recordFeedback` / `recordFeedbackUv` / `recordFeedbackGrid` / `ingestPackedFeedback`
   retain page requests. Packed IDs use the same 32-bit layout as `packVirtualTextureFeedback` in
   `virtual_texture.glsl` (pageX 12 | pageY 12 | mip 8).
2. **Compact + decode** — `update()` deduplicates feedback, allocates physical slots with LRU
   eviction, and decodes at most `maxDecodesPerUpdate` pages through `VirtualTextureBlend`.
3. **Atomic publish** — staged upload batches apply page-table clears, then atlas rectangles, then
   page-table sets. `commitPublish` bumps the generation only after GPU uploads succeed;
   `abandonPublish` restores the previous CPU mapping. `syncToMaterial` wires Graphics textures and
   rolls GPU atlases back to the published snapshot if a mid-batch upload fails.
4. **Writers** — `invalidatePage` / `invalidateRect` mark resident pages dirty so the next update
   re-decodes them in place (terrain paint / decal-style attribute edits).

Script surface: `gfx.newVirtualTextureStreaming(...)`, then `addLayer` / `initializePhysicalCache` /
`recordFeedback*` / `update` / `syncToMaterial`.

## Remaining renderer evolution

1. GPU feedback append/readback pass that writes `packVirtualTextureFeedback` into a buffer instead of
   CPU grid recording.
2. Optional JobSystem-backed page decode for large budgets (producer receipts stay immutable).
3. Persistent cooked-page schemas and asynchronous I/O for SVT-style producers.
4. Richer runtime writers (mesh/terrain intersections, RVT volume bounds).

The software producer and full-bake path remain the reference oracle and unsupported-platform
fallback throughout these stages.
