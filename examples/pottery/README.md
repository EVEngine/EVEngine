# Pottery / 器皿生成器

Run from the repository root: `make run/linux-debug GAME=examples/pottery`.
The gallery contains a planter, jar, custom Bézier vessel, bowl, plate and saucer,
with seeded terracotta, blue glaze, porcelain, crackled celadon and brown stoneware PBR finishes. Each object has a distinct reproducible seed and 512×512 albedo/normal textures.
The camera orbits
above the open mouths to expose the inside walls and floors.

Edit `potteryObject` in `main.nut` to change the four radius controls, height,
wall/bottom thickness, seed or finish. Geometry is generated once in `eve_init` and
uploaded separately from material generation. Failures stop initialization with
the structured Result diagnostic.

See [the generator guide](../../docs/usr/modules/procgen/pottery.md).


The custom height-blend shader exposes a clay foot, blends into the selected glaze,
and adds a darker, smoother rim. Albedo, normal and roughness textures share the
same noisy blend mask in **local model height**, so moving/scaling the object does
not slide the glaze line. The height range is measured from the generated mesh.

- `H`: cycle shaded / blend-weight / normalized-height views.
- Space: pause/resume the camera orbit.
- Edit `baseEnd`, `blendWidth`, `rimStart`, `noiseAmount`, `noiseScale` in `main.nut`
  to change the glaze coverage. Each object has its own shader instance.

Shaders and committed SPIR-V are in `shaders/`. Regeneration commands and the
texture binding layout are documented in the generator guide.
