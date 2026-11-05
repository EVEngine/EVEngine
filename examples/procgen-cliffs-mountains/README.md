# Procedural Cliffs & Mountains

Bakes an alpine `MountainRecipe`, authors a multi-cell steep scarp, then runs a
Pegasus/Ubisoft cliff extract: steep heightfield quads → subdivide → soft bed
undulation + Worley rock displace + blur + optional shell thickness, shaded with
`pbr.cliff.strata`, and toe-dressed with `mesh.rock`. The heightfield face is
buried under a soft ramp after extraction so the green terrain skin does not
double the beige cliff shell. After ~1.5 s captures `procgen-cliffs-mountains.png`.

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
