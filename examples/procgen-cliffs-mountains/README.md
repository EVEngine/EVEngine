# Procedural Cliffs & Mountains

Bakes an alpine `MountainRecipe`, carves a midfield basin for a long scarp, extracts
continuous cliff-band rock strips (shared verts, smoothed lip, strata displace +
bedding albedo), softens the heightmap ramp so terrain splat does not own the sheer
face, and dresses the toe with varied `mesh.rock` props. After ~1.5 s captures
`procgen-cliffs-mountains.png` for visual audit.

## Run

```bash
make run/<platform>-debug GAME=examples/procgen-cliffs-mountains
```

Headless smoke:

```bash
MIN_RUN_SECONDS=2 bash scripts/smoke_examples.sh procgen-cliffs-mountains
```

## Pipeline

1. `eve.MountainRecipeSettings` + `eve.bakeMountainRecipe` (alpine ridge, seed `20261105`)
2. `eve.generateCliffFitnessMask` + `eve.generateCliffBandMesh`
3. Terrain chunks via `procgen.buildTerrainChunk` / `generateTerrainChunkMesh`
4. Rock dressing via `eve.configureCliffRockScatterDefaults` + `mesh.rock` cliff shape

Design notes: [`docs/dev/procgen-cliffs-mountains.md`](../../docs/dev/procgen-cliffs-mountains.md).
