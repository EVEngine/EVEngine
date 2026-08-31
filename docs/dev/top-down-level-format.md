# Top-down level documents

The editor uses `eve::editor::LevelDocument` as its format-neutral working model. A document
contains an ordered stack of tile and object layers. It supports `orthogonal`, `isometric`,
`staggered`, and `hexagonal` maps; coordinates and object sizes are world pixels, while tile
layers are dense row-major GID grids where `0` is empty.

## Native JSON format

Native files use `.level.json` or `.evelevel`. Version 1 has this shape:

```json
{
  "format": "eve.level",
  "version": 1,
  "orientation": "orthogonal",
  "width": 64,
  "height": 48,
  "tilewidth": 32,
  "tileheight": 32,
  "properties": { "music": "forest" },
  "layers": [
    {
      "id": "layer-1",
      "name": "Ground",
      "type": "tilelayer",
      "visible": true,
      "opacity": 1,
      "offsetx": 0,
      "offsety": 0,
      "properties": {},
      "width": 64,
      "height": 48,
      "data": [0, 0, 1]
    },
    {
      "id": "layer-2",
      "name": "Gameplay",
      "type": "objectgroup",
      "properties": {},
      "objects": [
        {
          "id": "object-3",
          "name": "Start",
          "type": "spawn",
          "x": 64,
          "y": 96,
          "width": 0,
          "height": 0,
          "rotation": 0,
          "visible": true,
          "properties": { "team": "player" }
        }
      ]
    }
  ]
}
```

`properties` are deliberately open string maps at document, layer, and object scope. Games can
therefore define their own schema without forking the editor's core representation. A project
should validate those values in its own importer or inspector plug-in.

## Formats and extension points

`LevelFormatRegistry` ships with `eve.level` and `tiled.json`. Tiled JSON (`.tmj`/`.json`) is
imported into the same document and can be exported again. Tile layers, object layers, visibility,
opacity, offsets, object transforms, and custom properties are mapped directly.

To support another source, derive from `LevelFormat`, implement `id`, `extensions`, `canRead`,
`read`, and `write`, then transfer ownership with `registerFormat(std::unique_ptr<LevelFormat>)`.
Replacing a registered ID intentionally overrides that format, allowing a game to customize even
the built-in serializers. Editors should edit only the neutral document; format-specific logic
belongs in these adapters.

```cpp
LevelFormatRegistry formats;
formats.registerFormat(std::make_unique<MyWorldFormat>());
auto level = formats.load("world.custom");
formats.save("world.level.json", *level, "eve.level");
```

The separation keeps brush/history/UI code independent of storage and makes format conversion a
normal load-edit-save workflow.
