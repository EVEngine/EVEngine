# FLOOR LAB — 程序化地板纹理（完整 PBR）

通过参数组合生成木地板、花纹地砖与各类瓷砖的完整 PBR 套图，无需外部图片资源。
配方：

- Albedo：`tex.floor.wood` / `tex.floor.tile`
- PBR：`pbr.floor.wood` / `pbr.floor.tile`（albedo + normal + roughness + metallic + height + AO）

示例 UI 调用 `generatePbrMaterial`，把 albedo / normal / height 绑到 `Material`，并设置
scalar roughness / metallic 与 parallax。

## 运行

```bash
make run/<platform>-debug GAME=examples/procedural-textures
```

## 操作

| 输入 | 作用 |
|---|---|
| `R` / **New seed** | 换种子重新生成 |
| `Tab` / **Mode** | 在木地板与瓷砖模式间切换 |
| **Layout / Pattern** | 循环木地板排版或瓷砖花纹 |
| **Tone / Palette** | 循环木材色调或瓷砖调色板 |
| **Next preset** | 在内置预设间切换（凡尔赛拼花、八边形、鱼鳞、摩洛哥星等） |
| 滑条 | 调节排数 / 缝隙 / 木纹 / 勾缝 / 釉面 / 磨损等 |
| **Pause rotation** | 暂停转台 |

## 配方参数

### `tex.floor.wood` / `pbr.floor.wood`

- `layout`: `planks` / `staggered` / `herringbone` / `chevron` / `parquet` / `basket` / `diagonal` / `ladder` / `finger` / `versailles`
- `tone`: `oak` / `walnut` / `pine` / `cherry` / `ebony` / `ash` / `maple` / `teak`
- `rows`, `cols`, `gap`, `grain`, `warp`, `wear`, `stain`, `bevel`
- PBR: `roughnessLow`, `roughnessHigh`, `metallic`, `normalStrength`, `aoStrength`, `heightStrength`

### `tex.floor.tile` / `pbr.floor.tile`

- `pattern`: `square` / `checker` / `diamond` / `hex` / `subway` / `brick` / `stack` / `mosaic` / `basket` / `herringbone` / `octagon` / `fishscale` / `scallop` / `pinwheel` / `windmill` / `star` / `moroccan` / `cobble` / `arabesque` / `terrazzo`
- `palette`: `ceramic` / `terracotta` / `slate` / `porcelain` / `marble` / `black` / `mosaic` / `subway` / `encaustic` / `jade` / `cobalt`
- `tilesX`, `tilesY`, `grout`, `bevel`, `glaze`, `wear`, `speckles`, `motif`
- PBR: 同上；釉面越高，板面粗糙度越低

脚本侧用 `procgen.generatePbrMaterial(recipeId, params)` 生成完整套图。
