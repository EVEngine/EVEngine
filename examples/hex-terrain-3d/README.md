# Hex Map 3D

An interactive pointy-top 3D hex map: elevation terraces and cliffs, water,
rivers, roads, walls and decorations, units with A* pathfinding, fog of war,
procedural map generation, save/load, an orbit camera, mouse picking and a
hex-shaped brush. It is a port of the Catlike Coding
[Hex Map](https://catlikecoding.com/unity/hex-map/) reference project onto
EVEngine's `hexmap` module.

This directory is the module's interactive reference example. The separate
[`hex-terrain`](../hex-terrain/README.md) example is unrelated: it is the
asset-free `mesh.hexterrain` procgen recipe and it still ships unchanged.

Everything geometric and everything the fog/units/pathfinding rules decide lives
in C++ (`src/modules/hexmap`): the editable cell grid, its 5x5 chunk partition,
the picking query, the per-chunk mesh builders (terrain, water, river, road, the
fog overlay, walls and decorations), the search/visibility scratch, the unit
registry, the save-payload codec and the procedural map generator. This example
owns the camera, the input mapping, the brush tool, the per-chunk renderables, the
unit markers and the HUD.

Run with `make run/win32-debug GAME=examples/hex-terrain-3d` (Linux:
`make run/linux-debug GAME=examples/hex-terrain-3d`).

## Controls

| Key / input | Action |
|---|---|
| `W` `A` `S` `D` (or arrows) | Pan the camera on the XZ plane |
| `Q` / `E` | Rotate the camera |
| Mouse wheel (or `Z` / `X`) | Zoom; the far end tracks the map extent |
| `1` .. `5` | Edit mode: Elevation, Water, Terrain, River, Road |
| `[` / `]` | Decrease / increase the hex brush radius (`0`..`6`) |
| `,` / `.` | Cycle the terrain palette (Sand, Grass, Mud, Stone, Snow) |
| Left mouse | Raise / paint / start a river or road drag; plan a unit path |
| Right mouse | Lower / erase (removes terrain paint, rivers or roads) |
| `R` | New deterministic seed and rebuild the starter map |
| `F` | Cycle the map size (20, 30, 40) |
| `M` | Generate a procedural map (fresh seed) at the size `F` selected |
| `N` | Mark every chunk dirty and rebuild |
| `tab` | Select the next unit |
| `G` | Plan a path from the selected unit to the hovered cell |
| `space` | Send the selected unit along the planned path |
| `escape` | Clear the planned path |
| `T` / `Y` | Add a unit on the hovered cell / remove the selected unit |
| `J` / `K` | Toggle the hovered cell's *explored* / *explorable* flag |
| `F5` / `F9` | Save / load the map, its units and its explored flags |

Rivers and roads are painted by dragging from a cell to its neighbour, matching
the reference editor. The module rejects uphill rivers, roads that climb more
than one elevation step, and roads that cross a river mid-channel.

## What is implemented

- Pointy-top hex grid with the reference metrics (`outerRadius 10`,
  `elevationStep 3`, `solidFactor 0.8`, `waterFactor 0.6`, two terraces per
  slope, `chunkSize 5x5`).
- Ground fans, blend strips between neighbours, terraced slopes and cliffs, and
  the corner matrix that closes the triple junctions (7 cases).
- Water surface with a shore band, river channels carved to the stream bed, and
  road ribbons. The water is triangulated per direction like the reference: an
  open-water wedge plus a bridge quad towards flooded neighbours, or a subdivided
  four-triangle shore wedge plus a four-quad bank strip towards land — and that
  strip stays **flat at the water height**, borrowing only the neighbour's solid
  corner positions. Tilting it up to the bank reads as a bright band tracing every
  hexagon edge, which is why the whole surface is now provably horizontal.
- Deterministic per-cell XZ and height noise, so no texture assets are required.
- Five terrain layers blended per vertex and shaded with GGX lighting, slope
  rock, a snow line and moving cloud shade.
- Fog of war: cells are counted per viewer, the first viewer latches a cell
  *explored* one-way, and the overlay draws a hexagonal column over every cell
  no viewer currently sees (dim for remembered ground, near black for ground
  never seen). The column also carries a skirt so the cliff faces along a fog
  boundary are covered.
- Units on a hex grid: A* over the cell graph with roads, slopes, cliffs, walls,
  water and other units as movers, a turn budget derived from the unit's speed,
  and a Bezier walk whose vision follows the corridor it travels.
- Save/load as a self-describing little-endian payload (grid + explored flags +
  units) written through `eve.Filesystem()`; the payload is validated in full
  before any observable state changes.
- City/farm walls, wall towers and bridges generated as geometry (the reference
  instantiates prefabs; EVEngine ships none). Walls follow the reference's
  `wallLerp`/`wallThicknessOffset` construction, open a gate where a road crosses
  the boundary, and fill the high side of a step with a wedge.
- Urban, farm, plant and special decorations, picked exactly like the reference:
  the three collections are each tested against a deterministic per-cell hash and
  the lowest passing hash wins, with a special feature suppressing the ordinary
  decoration of its cell.
- A procedural map generator ported from the reference `MapGenerator`: seeded land
  regions grown with jitter and occasional rises/sinks, coastal erosion towards a
  land-percentage target, downhill rivers with lakes, a 40-cycle downwind moisture
  transport, and the reference's temperature/moisture biome table with plant
  levels. Generation is reproducible from a seed alone.
- Orbit camera, ray picking with an iterative elevation refinement, a hex cursor
  and an immediate-mode HUD.

The example seeds a deterministic starter map (a two-step central plateau, a
flooded basin, two rivers draining off the plateau, a road crossing the lowland
and a walled district with gates where the road crosses its wall) plus three
units, so every system is visible on the first frame. `M` replaces it with a
procedurally generated map.

## Camera

`eve.Camera3D()` defaults to a `0.1 .. 100` clip range. That is smaller than a
single map span once the grid passes a few chunks, so anything beyond 100 world
units was silently clipped and the far half of the zoom range showed an empty
frame. The example now derives the far plane from the map extent every frame and
scales the far zoom distance with it (`ZOOM_FAR_MARGIN`), so the whole map always
fits the view whatever the map size.

## Shaders

The five fragment shaders are shipped as SPIR-V next to their GLSL sources so no
runtime compiler is needed. The engine's built-in Mesh3D vertex stage already
produces the varyings they consume, so no
vertex stage is shipped. Regenerate them after editing a `.frag`:

```sh
glslc -fshader-stage=frag shaders/hex_map_terrain.frag -o shaders/hex_map_terrain.frag.spv
glslc -fshader-stage=frag shaders/hex_map_water.frag   -o shaders/hex_map_water.frag.spv
glslc -fshader-stage=frag shaders/hex_map_river.frag   -o shaders/hex_map_river.frag.spv
glslc -fshader-stage=frag shaders/hex_map_road.frag    -o shaders/hex_map_road.frag.spv
glslc -fshader-stage=frag shaders/hex_map_fog.frag     -o shaders/hex_map_fog.frag.spv
glslc -fshader-stage=frag shaders/hex_map_feature.frag -o shaders/hex_map_feature.frag.spv
```

There are six stages but seven surface streams: `hex_map_feature` serves both the
wall stream and the decoration stream, because a vertex's first texture coordinate
already says which of the seven parts it belongs to
(`0` wall, `1` tower, `2` bridge, `3` urban, `4` farm, `5` plant, `6` special).

The terrain fragment stage decodes three terrain layers from the two available
texture coordinates (`u = layerA + layerB*8 + layerC*64`,
`v = weightB + weightC*16`); `HexMapMesh.h` documents that encoding as a
compatibility bridge, not as the terrain data format. The fog stage reads its
single coordinate as the shade (`0` explored-but-unseen, `1` never explored).

## Not ported yet

Waterfalls and estuaries, the reference's wall-tower *prefab* proportions, and
the fog overlay's per-cell visibility blend (it is a two-shade
remembered/unseen pair). The map generator's erosion takes the lowest cell from
the shared search frontier where the reference erodes a uniformly random cell,
and its temperature jitter uses a private value noise instead of the reference's
noise texture sample. `docs/usr/modules/hexmap.md` lists the same boundary from
the module side.

`hex-map.png` is captured by the example itself after 90 frames.
