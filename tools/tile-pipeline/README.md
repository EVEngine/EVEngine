# Composable 2.5D Tile Pipeline

This directory is a foundation, not a finished importer UI. EVEngine defines a
small stage protocol and the `eve.tileset/1` output contract; each game owns its
pipeline JSON, stage order, metadata rules and optional Python plugins.

```powershell
python tools/tile-pipeline/tile_pipeline.py path/to/tile-pipeline.json
```

Built-in stages are deliberately small:

- `scan`: select files and assign GIDs;
- `analyze`: decode PNGs, find alpha bounds and apply project overrides;
- `pack`: basic deterministic shelf packing;
- `emit`: write an atlas and runtime TileSet manifest.

A project can insert or replace stages. A plugin exports either
`register(registry)` or a `STAGES` dictionary. A stage receives
`(PipelineContext, options)` and may modify `context.tiles`, `context.atlas`,
`context.artifacts` or `context.diagnostics`. See `plugin.example.py`.

The runtime contract is independent of this Python implementation. A studio can
replace the packer with TexturePacker, Aseprite, an internal farm, or an editor
tool as long as it emits:

```json
{
  "schema": "eve.tileset/1",
  "tileset": {
    "image": "assets/tiles.atlas.png",
    "tiles": [{
      "gid": 1,
      "region": [0, 0, 240, 180],
      "pivot": [120, 135],
      "footprint": [1, 1],
      "walkable": true,
      "cost": 1,
      "sortBias": 0,
      "tags": ["ground"]
    }]
  }
}
```

Squirrel consumption:

```squirrel
local layer = map.newLayer(32, 32, 150, 75);
layer.applyConfig(@"{""orientation"":""isometric""}");
layer.loadTilesetManifest("assets/tiles.tileset.json");
```

`pivot` is the point inside the trimmed atlas region aligned to the projected
tile origin. `footprint`, `walkable`, `cost`, `tags` and future project fields
remain data: tools can consume them without forcing one editor or game design.
