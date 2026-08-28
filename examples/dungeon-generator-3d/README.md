# Configurable 3D Dungeon Generator

This is the 3D presentation example for `procgen.generate("level.roguelike", ...)`.
It builds floors and boundary walls from semantic cells, then instantiates the
generator's automatically placed themed props with their emitted asset id, yaw,
footprint and placement flags.

The showcase uses the generator's `clustered` room growth and `growth`
connections, which preserve the generated parent-child tree and make short,
direct door links. `clusterGapMin` and `clusterGapMax` configure the structural gap
between clustered rooms; setting both to `1` produces the compact, short-link
composition used here. It also uses an orthographic camera framed from the occupied bounds, merged
cut-corner stone floor meshes, modular wall/corner/doorway pieces, perimeter
stairs, theme-aware furniture clusters and evenly distributed warm point lights.

The renderer is not coupled to KayKit. Edit `assetpack.nut` to change the asset root,
file extension and architecture models, and replace `configureDungeonAssetPack(p)`
with another semantic-pool adapter.

`assetpack.nut` is the renderer-side contract: `walls`, `corners`, `doorways`,
`stairs`, `details`, and `roles` can point at OBJ names, GLTF-derived prefabs, or
another pack's equivalent pieces. `usePackFloors=false` keeps the neutral merged
stone mesh when a pack's floor atlas is unsuitable; set it to true to render the
configured `floor` model directly. The generator-side adapter independently maps
comma-separated `assets.<role>` pools, so generation never branches on pack names.

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
