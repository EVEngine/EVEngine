# Configurable 3D Dungeon Generator

This is the 3D presentation example for `procgen.generate("level.roguelike", ...)`.
It builds floors and boundary walls from semantic cells, then instantiates the
generator's automatically placed themed props with their emitted asset id, yaw,
footprint and placement flags.

The renderer is not coupled to KayKit. Edit `assetpack.nut` to change the asset root,
file extension and architecture models, and replace `configureDungeonAssetPack(p)`
with another semantic-pool adapter.

For the included KayKit preset, place the pack's OBJ directory at:

```text
examples/dungeon-generator-3d/assets/kaykit-dungeon/obj/
```

The directory must contain each `.obj` together with its `.mtl` and shared texture
files. KayKit Dungeon Remastered is CC0; it is intentionally not vendored here.

Run with:

```sh
make run/macosx-debug GAME=examples/dungeon-generator-3d
```

Press `R` to generate another deterministic layout and dressing pass.
