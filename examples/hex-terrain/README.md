# Procedural Hex Terrain

Civilization-style 3D hex world generated without source art. `mesh.hexterrain`
creates continental elevation, latitude temperature, moisture biomes, downhill
rivers, coast/deep-ocean bands, cliff skirts and deterministic 3D vegetation. The
custom vertex/fragment shader pair synthesizes all PBR detail, blends neighboring
biomes and animates water, shoreline foam, tree wind and moving cloud shade.
Mountain cells use deterministic asymmetric multi-peak geometry rather than a
single repeated cone.
River edges are joined by seeded quadratic ribbons with shared edge seeds;
cliffs use five independently perturbed wall segments and mountain peaks use a
broken base ring, offset shoulder ring and eccentric apex.
Swamps add masked reflective puddles, rainforest soil receives a procedural wet
layer, and ice uses blue fissures with locally reduced roughness. Water includes
depth-dependent spectral absorption, shallow caustics and Fresnel reflection.

Biomes: deep ocean, ocean, coast, grassland, hills, mountains, forest, swamp,
rainforest, ice, cliffs and rivers.

Run with `make run/<platform>-debug GAME=examples/hex-terrain`. Press `R` for a
new deterministic seed. The recipe accepts `width`, `height`, `seed`, `radius`,
`seaLevel`, `heightScale`, `riverCount`, `decorations` and
`vegetationDensity`.

Generated mesh metadata exposes per-biome cell counts (`cells.deepOcean` through
`cells.ice`), plus `cells.river`, `cells.lake` and `edges.cliff`, so tools and
tests can verify that a seed contains the requested terrain features.

On Linux, `scripts/capture_hex_terrain.sh` performs a reproducible headless
capture with Xvfb, Mesa Lavapipe and ffmpeg. Set `VULKAN_SDK` when `glslc` is not
already on `PATH`; the output defaults to `hex-terrain-vulkan-latest.png`.

UV encoding is intentionally documented by the mesh metadata:
`u = primaryBiome + transitionWeight`, `v = secondaryBiome + riverCoverage`.
This is a compatibility bridge for the current two-channel `MeshVertex`; a
production terrain renderer should use integer biome ids plus 4-8 material
weights and texture-array bindings.
