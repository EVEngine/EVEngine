# 2.5D Grid Walk Evaluation

Developer-evaluation sample for EVEngine's isometric grid/tilemap workflow. It
uses independent 300×300 PNG tiles from the supplied `C629/格子` asset pack.

Ground, route decals, actors, walls, and towers intentionally share one render
layer so projected foot depth controls occlusion. Separate layer values are for
hard barriers such as HUD overlays; same-cell ordering uses TileSet `sortBias`.

Regenerate the project-owned atlas and TileSet manifest with:

```powershell
python tools/tile-pipeline/tile_pipeline.py examples/iso-grid-walk/tile-pipeline.json
```

The JSON is intentionally the workflow entry point: replace stages, insert a
project plugin, or point `emit` at another packer without changing the engine.

- Arrow keys / WASD move exactly one logical cell.
- Click a walkable tile to run A* and follow the resulting route.
- Towers and walls are blocked through `Map.newPathfinder(layer)`.
- The invisible `TileLayer` owns projection, picking, and topology; rendering is
  a compatibility pass because the source pack is not a regular tileset atlas.

Run with:

```powershell
eve run examples/iso-grid-walk
```
