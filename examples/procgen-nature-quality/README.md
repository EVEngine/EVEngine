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

## 2026-11-11 视觉改善

扩大取景使树冠完整入镜，降低暖光强度、土壤黄染和叶片发白；保留现有程序化纹理、法线、树木和花卉配方。

[当前实帧](../../docs/images/examples/review-2026-11-11/next10/procgen-nature-quality.png) · [前后对照与验证](../../docs/images/examples/QUALITY_REVIEW.md)
