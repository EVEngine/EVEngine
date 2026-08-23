# Urban Generator example

Interactive showcase of the **`mesh.urban`** / **`urban.parcels`** procgen
algorithms — an engine port of the Eurographics 2024 paper *Hierarchical
Co-generation of Parcels and Streets in Urban Modeling* (Chen, Song & Ortner,
CGF 43(2), [10.1111/cgf.15053](https://doi.org/10.1111/cgf.15053)).

Run:

```sh
make run/win32-debug GAME=examples/urban-generator
# or, from inside this directory:
../../build/win32-debug/src/engine/eve.exe run
```

## What it generates

From a user-specified land polygon the generator co-produces a parcel mesh and a
connected street network in two stages, exactly as the paper describes:

1. **Hierarchical co-generation** — at every level each splittable parcel is
   binary-partitioned by the best of ~20 streamline candidates (a cross field
   aligned with the boundary, smoothed over the parcel, then traced), scored by
   the paper's quality metric `Q = λ1·Q_size + λ2·Q_regularity + λ3·Q_access`
   (Eq. 2). Short edges in the parcel mesh are removed by vertex merging.
   Unreachable parcels are grouped and given street access: an I-shaped or
   L-shaped access along parcel boundaries plus a turn-aware Dijkstra connection
   to the existing street network (Section 4).
2. **Global geometric optimization** — vertex positions are relaxed with the
   five-term energy of Eq. (3) (parcel regularity, side smoothness, street
   smoothness, right-angle junctions, closeness), constrained to preserve the
   land boundary. A built-in rollback keeps the initial layout when the
   relaxation cannot improve it.

Two output paths are exposed on `eve.Procgen()`:

- `generate("urban.parcels", p)` → `Grid2D` with `Semantic::Road` streets,
  `Semantic::Floor` parcels (per-parcel id in `getDetail`), parcel anchor
  objects and metadata.
- `buildMesh("mesh.urban", p, gfx)` / `generateMesh(...)` → flat or extruded
  parcel blocks + street ribbons, ready for the 3D renderer.

## Parameters

| param | default | meaning |
|-------|---------|---------|
| `land` | `rect` | `rect` / `triangle` / `ellipse` / `l` / `hexagon`, or explicit `x,y;x,y;...` points |
| `landWidth`, `landHeight` | 100 / 60 | preset land size |
| `minParcelArea` | 4 | minimal allowed parcel area (termination condition) |
| `targetParcels` | 120 | desired parcel count (0 = split until no parcel is splittable) |
| `maxLevels` | 10 | hierarchy depth cap |
| `lambdaSize/Regu/Acce` | 0.3 / 0.5 / 0.2 | Eq. (2) weights (parcel shape control) |
| `streetPattern` | `default` | `default` / `loop` / `culdesac` / `tree` |
| `boundaryStreet` | `all` | `all` / `none` / `random` land-boundary streets |
| `orientation` | `none` | `none` / `east-west` / `north-south` parcel orientation |
| `optimize` | 1 | run the global geometric optimization |
| `cellSize` (grid) / `extrude` (mesh) | 1 / 0 | raster resolution / block height |

Controls: `R` reseed, `1..4` land presets, `P` street pattern, `O` optimization,
`[`/`]` parcel count, `-`/`=` parcel size, `E` extrusion, `L` grid overlay.
