# LITHIC — Random 3D Rock Generator

A deterministic real-time rock authoring example for EVEngine. Its default path deforms a
shared-vertex icosphere, adds seeded procedural stone albedo and normals, and exposes the defining
parameters through an in-engine UI. Three geometric LODs are generated automatically.

Run it with the same command used for other examples, selecting `examples/rock-generator` as the game.

Controls:

- **New specimen** or **R** advances to a new deterministic seed.
- **Next base shape** cycles through mixed, boulder, slab, block, and shard silhouettes.
- Shape sliders control flattening, fracture planes, erosion, and geologic noise scale.
- Surface roughness updates without rebuilding the mesh.
- **Pause rotation** freezes the turntable for inspection.

The reusable default recipe is `mesh.rock`. `baseShape` accepts `mixed`, `boulder`, `slab`,
`block`, or `shard`; `mixed` chooses a profile deterministically from the seed. Shape composition
can be tuned further with `variation`, `axisX/Y/Z`, `shapePower`, `skewX/Z`, `cutCount`, and
`cutDepth`, alongside `radius`, `flattening`, `angularity`, `erosion`, `scale`, `octaves`,
`subdivisions`, and `seed`. Subdivision levels 3, 2, and 1 contain 1,280, 320, and 80 triangles
respectively. The volumetric
`mesh.marchingcubes`/`field = "rock"` path remains available for complex topology.
