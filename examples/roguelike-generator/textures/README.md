# Texture asset (CC0)

`dungeon_tiles.png` is the **"Dungeon tileset"** by **Buch**, published on
[OpenGameArt](https://opengameart.org/content/dungeon-tileset) under the
**CC0** license (public domain). Attribution is not required, but appreciated:

> Dungeon tileset by Buch — OpenGameArt
> https://opengameart.org/content/dungeon-tileset

Downloaded from:
`https://opengameart.org/sites/default/files/dungeon_tiles_0.png`

The example loads it with `gfx.newTextureFromFile("textures/dungeon_tiles.png")`
and shows it in the preview panel (toggle with **T**). The tiles are 16×16 with
an irregular/offset layout, so the demo renders the procedurally generated
dungeon with its own colored tiles and uses this texture as the loaded-art
preview. To texture the map cells directly, replace it with a tightly packed
tileset and drive `map`/`gfx` tile UVs from the generator's `detail` data.
