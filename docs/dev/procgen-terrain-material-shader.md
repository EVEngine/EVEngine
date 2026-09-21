# Procedural terrain material shader

`Procgen.createTerrainMaterialShader` provides the native terrain path used by canonical
`eve.terrain-material/3` assets. `asset_procgen::buildTerrainMaterialAtlases` resolves bounded
EVIMG data into four-layer CPU groups. `packTerrainMaterialAtlases` then creates the production
single-draw representation for one through sixteen layers.

The packed representation uses the draw texture for a 3x2 control atlas. Cells 0 through 3 hold
the four RGBA control maps and cell 4 holds the holes mask. The shader borrows four textures
through `Shader.setMeshTexture`:

- slot 0: a 4x4 albedo atlas;
- slot 1: a 4x4 tangent-space normal atlas;
- slot 2: a 4x4 mask atlas, with metallic in R and smoothness in A;
- slot 3: a 7x16 parameter texture.

Each parameter texel stores one IEEE-754 float as four little-endian bytes. Columns contain XY
world scale, XY offset, metallic, signed normal scale, and smoothness. Both shaders fetch texels
without filtering and reconstruct the float bits. This avoids exceeding Vulkan's minimum
128-byte push-constant guarantee. `terrainFeatures.x == 2` selects this contract;
`terrainFeatures.y` enables holes and `terrainFeatures.z` carries the layer count.

All active control channels contribute to one weight sum before material evaluation. An all-zero
control set deterministically selects layer zero. This prevents per-control normalization and
repeated lighting or tone mapping. Group binding remains available as a four-layer compatibility
path (`terrainFeatures.x == 1`), while disabled atlas sampling preserves the legacy procedural
colors.

CPU construction owns its output and validates dimensions, byte budgets, group ordering, layer
counts, and finite nonzero tiling before publication. GPU upload is transactional. Explicit
release clears every handle and reports backend release failure. Graphics resources are borrowed
through the draw and must be released on the graphics thread after their final use.

`scripts/compile_procgen_terrain_shader.py` embeds matching SPIR-V and native WebGPU WGSL. The
render test uploads the public CPU representation, binds it through the production adapter, and
reads pixels from an engine Canvas. Its second draw sets only control-map group two and proves
that layer five is selected in one draw on Vulkan and WebGPU.
