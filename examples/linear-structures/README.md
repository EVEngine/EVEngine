# Linear Structures — tileable procedural 3D props

A rendering test for the procgen **mesh recipes** that build *linear, tileable*
structures from a single unit segment repeated along the X axis. Six recipes are
demonstrated side by side:

| Recipe | Structure |
|--------|-----------|
| `mesh.fence`        | wooden fence (posts + 3 rails) |
| `mesh.stonewall`    | stone wall (rubble body + cap) |
| `mesh.bridge`       | bridge (deck + handrails + cross beams) |
| `mesh.greatwall`    | Great Wall (body + walkway + outer merlons + inner guard) |
| `mesh.hedge`        | hedgerow (leafy base + bush blobs) |
| `mesh.chevaldefrise`| cheval de frise (top rail + crossed X legs + feet) |

The textures are **CC0** images downloaded from Wikimedia Commons (see
`ATTRIBUTION.md`), loaded with wrap sampling so the grain/brick tiling stays
continuous across unit seams. If an asset file is missing the example falls back
to a procedural texture, so it still runs.

## Run

```sh
make run/win32-debug GAME=examples/linear-structures
```

## Controls

- **Tab** — cycle through the six structures.
- **+ / -** — grow / shrink the number of repeated units.
- **Segments / Unit length** sliders rebuild the active structure live.
- **Pause rotation** button (or toggle in code) orbits the camera.

## Reusable API

```squirrel
local paramsResult = procgen.newParams();
if (!paramsResult.ok) throw paramsResult.status.summary;
local p = paramsResult.value;
p.setInt("segments", 8);      // repeat count along X
p.setFloat("segLength", 2.0); // length of one tileable unit
p.setFloat("height", 1.5);    // per-kind override
p.setFloat("uvRepeat", 2.0);  // texture repeats per world unit
local meshResult = procgen.generateMesh("mesh.stonewall", p, gfx);
if (!meshResult.ok) throw meshResult.status.summary;
local mesh = meshResult.value;
```

Shared params: `segments`, `segLength`, `height`, `depth`, `thickness`, `scale`,
`uvRepeat`. Each unit is designed so its cross-section at `x=0` matches `x=segLength`,
so tiling is seamless.
