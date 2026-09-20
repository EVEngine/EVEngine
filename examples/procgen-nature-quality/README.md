# Procedural Nature Quality

Close-up gallery that pushes EVEngine's **procedural** tree / bush / rock / flower materials
toward the density of handcrafted ARPG props (without shipping art assets).

| Prop | Mesh recipe | Surface |
|---|---|---|
| Tree | `mesh.tree` (realistic clusters) | `tex.tree_atlas` (bark left / foliage right, matches mesh UVs) |
| Bush | `mesh.bush` | `tex.foliage` + generated normals |
| Flower | `mesh.flower` | `tex.flower` (stem + petal alpha), tinted red/blue/white/yellow |
| Cliff | `mesh.rock` `baseShape=cliff` | `tex.moss` + normals |
| Boulder | `mesh.rock` `baseShape=boulder` | `tex.rock` + normals |

New texture recipes also available: `tex.bark`, `tex.foliage`, `tex.moss`, `tex.tree_atlas`, `tex.flower`.

## Run

```sh
make run/<platform>-debug GAME=examples/procgen-nature-quality
```

After ~2 s writes `procgen-nature-quality.png`.
