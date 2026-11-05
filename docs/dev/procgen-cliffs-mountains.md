# Procedural cliffs and mountains

Heightmap-owned mountain bake plus Pegasus-style cliff-band mesh extraction.
Volumetric overhangs (Paris TOG 2019 / SDF differential erosion) are deferred.

## References mapped into this stack

| Source | What we copy |
|---|---|
| Musgrave ridged multifractal + Quilez domain warp | `TerrainSampler` ridge/warp presets in `MountainRecipe` |
| Tzathas et al. CGF 2024 / Cordonnier TOG 2024 | Thermal → hydraulic → fluvial aging via `TerrainPipeline` (iterative, not analytical SPL yet) |
| UE5 / Houdini Project Pegasus | Steep heightfield quads → mesh → subdivide → height/noise displace → rock scatter |
| Ubisoft GDC cliff tool (Houdini-Cliff-Tool) | Slope threshold → remesh → strata boxes → extrusion + blur |
| Houdini HeightField Strata | Soft-tilted sedimentary beds with amplitude / size / mask |
| UE5 Landscape Auto Material | Cliff fitness from `deriveTerrainSlope` (`1-normal.y`) for dressing |

Deferred Phase 2: amplify selected cliffs with `ScalarField3D` / implicit blocks for overhangs and arches.

## Ownership

- **Authoritative surface:** `Heightmap` (one elevation per XZ).
- **Cliff band mesh:** derived, non-authoritative detail; rebuild after height edits.
- **Rock scatter points:** `PointSet` from `exportTerrainObjectPoints` on cliff fitness.

## API surface

| C++ | Role |
|---|---|
| `bakeMountainRecipe` | Preset noise + erosion into a heightmap; optional slope/cliff masks |
| `generateCliffBandMesh` | Extract steep heightfield quads, subdivide, Ubisoft strata extrusion + Worley rock + blur → `MeshBuild` |
| `generateCliffFitnessMask` | Slope → cliff fitness via `deriveTerrainSlope` + range |
| `configureCliffRockScatterDefaults` | Fill `TerrainObjectPlacementSettings` for steep-band rock dressing |

Script: `eve.MountainRecipeSettings`, `eve.CliffBandMeshSettings`, Heightmap
`bakeMountain` / `generateCliffFitness`, and `procgen.generateCliffBandMesh`.

## Material / geometry contract

Cliff cladding uses `tex.cliff.strata` / `pbr.cliff.strata` (albedo + normal + height).
The mesh is the steep heightfield surface itself (Pegasus), not dual-edge fence wafers:
full-cell emission (no diagonal holes), soft sine bed undulation, Worley F2−F1 rock
blocks, cladding offset, optional `shellThickness` back face, and Laplacian blur
(`claddingOffset` / `shellThickness` / `macroAmplitude` / `smoothIterations` /
`strataTilt`). UVs stay world-XZ → U and world-Y → V so beds read horizontal. The
example authors a multi-cell steep scarp, extracts the band, then buries the
heightfield face under a soft ramp so the green terrain skin does not double the
beige cliff shell.

## Example

[`examples/procgen-cliffs-mountains`](../../examples/procgen-cliffs-mountains/) bakes an alpine
recipe, renders terrain chunks, overlays cliff-band mesh with strata PBR, dresses the
toe with `mesh.rock` + `pbr.rock`, and captures a PNG.
