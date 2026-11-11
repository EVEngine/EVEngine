# Sky Lab — Procedural Skyscraper Generator

Interactive showcase for the `procgen` `mesh.skyscraper` recipe. The example rebuilds a
deterministic setback-tiered tower with a window grid, an optional spire, and a
procedural facade texture.

## Run

```sh
make run/win32-debug GAME=examples/skyscraper-generator
make run/linux-debug  GAME=examples/skyscraper-generator
```

## Controls

| Key | Action |
|-----|--------|
| `R` | Next seed and a new tier count |
| `1` / `2` | Fewer / more setback tiers |
| `W` | Toggle window density |
| `S` | Toggle spire |
| `T` | Cycle cool, warm and evening facade palettes |
| `Space` | Pause / resume rotation |

## Notes

- Mesh parameters: `baseWidth`, `baseDepth`, `tiers`, `tierHeight`, `setback`,
  `windowCols`, `windowRows`, `windowDepth`, `spireHeight`.
- Window quads are raised a thin lip in front of the wall so openings read under
  directional light without coplanar z-fighting. UV mapping lets a small atlas tint
  blue glass vs stone walls. Every setback tier has a closed roof; all four facade normals point outward.
- The camera fits the current tier count after each rebuild. Yaw is expressed in radians.
- A render smoke test (`procgen.render.skyscraperPng`) outputs
  `build/<platform>/test/out/skyscraper.png`.
