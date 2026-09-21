# hex-planet

The `hexmap` module's hex terrain, wrapped onto a sphere and viewed from orbit.

This is the spherical counterpart of [`examples/hex-terrain-3d`](../hex-terrain-3d):
the same module, the same `HexCellData` / `HexValues` / `HexFlags` cell records, the
same `HexMetrics` geometry constants, the same `HexMeshData` container and the same
`HexTerrainVertexCode` vertex encoding. What changes is the surface: an icosahedral
(Goldberg) topology instead of a rectangular chunk grid, and a radial "up" instead of
world Y.

```
make run/win32-debug GAME=examples/hex-planet        # Windows
make run/linux-debug  GAME=examples/hex-planet       # Linux (headless: see AGENTS.md)
```

## What it looks like

Orbit camera around a generated planet, with a hexagonal tessellation you can read at
the limb and in the faceted coastlines. `hex-planet.png` in this directory is captured
by the example itself after 90 frames. The default pose is static, so two runs agree to
within the ImGui overlay: measured across two runs, **25 of 1,024,000 pixels differ**
(the HUD), and the globe itself is identical.

## Controls

| Input | Action |
|---|---|
| LMB drag | orbit / pitch |
| wheel | zoom |
| `space` | toggle auto-spin |
| `R` | new seed |
| `[` `]` | subdivision level down / up |
| `-` `=` | land share down / up |
| `esc` | reset the view |
| `F5` | save `hex-planet.png` |

## How it is put together

Everything geometric is C++; the script owns only the camera, the input mapping, the
two renderables and the HUD.

| Piece | Where | Role |
|---|---|---|
| `HexSphereTopology` | `src/modules/hexmap` | icosahedral cell graph: ids, neighbours, edge directions, corners |
| `HexSphereMap` | `src/modules/hexmap` | editable sphere: cell records, radial elevation, picking, brushes, dirty cells |
| `HexSphereMesh` | `src/modules/hexmap` | builds the terrain and ocean meshes of the whole sphere |
| `HexSphereGenerator` | `src/modules/hexmap` | continents, sea level, biomes from 3D noise |
| script bindings | `HexMapModule` | `hexmap.sphere*` |
| this directory | — | orbit camera, renderables, HUD, shaders |

### The four things that do not survive the change of surface

1. **Edge count.** A cell has six edges, or five if it is one of the twelve pentagons.
   Nothing iterates a fixed six directions, and there is no `opposite(direction)`
   offset that works everywhere: the sphere map uses `directionOf(cell, other)`
   throughout, because a hexagon–pentagon edge has no arithmetic opposite.
2. **Edge ownership.** The planar builder relies on the chunk grid to emit each shared
   boundary exactly once. A sphere has no chunk grid, so the lower cell id owns the
   edge instead — still a total order over the same set of edges.
3. **"Vertical" is radial.** Positions are a unit direction times a radius, and the
   terrace interpolation of a slope moves along the *arc* by the horizontal step and
   along the *radius* by the vertical one.
4. **Shading has no horizon.** The planar terrain shader reads `vWorldPos.y` as
   altitude, `vNormal.y` as up, projects detail noise into XZ and fades into distance
   fog. On a sphere Y spans the whole planet, XZ collapses at the poles, the normal is
   the radial direction, and there is no horizon to fade into. `hex_planet_terrain`
   keeps the palette and the BRDF byte-for-byte and replaces only those four.

### Notes for anyone editing the shaders

The host applies its own Reinhard-style tonemap to this fragment stage's output — a
debug shader emitting a constant `1.0` arrives in the frame buffer as `224/255`, i.e.
`y = x / (x + 0.139)`. Both planet shaders invert that curve
(`hostTonemapInverse`) so the palette they author is the sRGB that actually ships;
without it every colour is silently compressed twice and cannot be tuned.

The two fragment shaders are compiled to SPIR-V, so the example needs no runtime
compiler:

```sh
glslc -o shaders/hex_planet_terrain.frag.spv shaders/hex_planet_terrain.frag
glslc -o shaders/hex_planet_water.frag.spv   shaders/hex_planet_water.frag
```

## Relationship to the other hex examples

| Example | Surface | Recipe / module | Camera |
|---|---|---|---|
| `hex-terrain-3d` | flat, chunked | `hexmap` (editable map) | orbiting a ground map |
| `hex-planet` | spherical, one mesh | `hexmap` sphere backend | orbiting the globe |
| `hex-terrain` | flat mesh | `mesh.hexterrain` (procgen) | ground level |

## Known gaps

- The ocean is a plain hexagonal cap per flooded cell: it has no shore shelf, no
  refraction and no shoreline foam, and it does not cover beaches at sea level
  (`waterLevel == elevation` is not flooded, matching the module's own rule). Sea ice
  is a shading term on that cap, not separate geometry.
- Cliffs are radial walls with no overhang or erosion detail.
- The generator has no rivers, erosion or plate tectonics; the planar
  `HexMapGenerator` did not carry over.
- The sphere has no rivers, roads, walls, features, fog-of-war or units, and no save
  format. The cell *flags* can be set, but there is no spherical geometry, search or
  serialization behind them.
