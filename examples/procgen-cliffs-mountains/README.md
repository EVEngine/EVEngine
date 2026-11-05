# Procedural Cliffs & Mountains

Bakes an alpine `MountainRecipe`, carves a midfield basin so a long vertical scarp
is unmistakable, overlays cliff-band wall shells extracted from steep heightfield
edges (strata + ledge displace), and scatters cliff rock props on the fitness mask.
After ~1.5 s captures `procgen-cliffs-mountains.png` for visual audit.

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
