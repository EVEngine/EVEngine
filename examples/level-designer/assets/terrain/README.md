# Terrain reference fixtures

`ridge.evtrn` is a raw `EVTRN` heightfield — the payload the asset importer writes
next to an `eve.terrain/1` asset definition (`heightfield.bin`).

Layout (all little-endian, no padding):

| offset | size | field |
| --- | --- | --- |
| 0 | 8 | magic `EVTRN\0\1\0` |
| 8 | 4 | `u32` width |
| 12 | 4 | `u32` height |
| 16 | 4 | `f32` spacingX (metres per cell) |
| 20 | 4 | `f32` spacingZ (metres per cell) |
| 24 | `width*height*4` | `f32` heights, row-major (z outer, x inner) |

This fixture is 65x65 cells at 3 m spacing, with heights in roughly
`[-8.0, 8.2]` m from two summed sine waves. It exists so the level designer can
demonstrate `mode = "asset"` — referencing terrain data that already exists on
disk instead of regenerating it from sampler parameters.

Both encodings the runtime decodes are documented in
`src/modules/procgen/heightmap/TerrainFile.h`. An `EVTR` archive
(`TerrainAsset::bake`) is also accepted; it stores no metres-per-cell, so the
level document supplies the spacing in that case.
