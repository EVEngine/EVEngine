# FLOOR LAB — 程序化地板纹理

通过参数组合生成木地板、花纹地砖与各类瓷砖贴图，无需外部图片资源。
配方为 `tex.floor.wood` 与 `tex.floor.tile`，同一套参数在示例 UI 中可实时调节。

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
| **Next preset** | 在内置预设间切换（橡木拼板、人字拼、棋盘砖、六角砖等） |
| 滑条 | 调节排数 / 缝隙 / 木纹 / 勾缝 / 釉面 / 磨损等 |
| **Pause rotation** | 暂停转台 |

## 配方参数

### `tex.floor.wood`

- `layout`: `planks` / `staggered` / `herringbone` / `chevron` / `parquet` / `basket`
- `tone`: `oak` / `walnut` / `pine` / `cherry` / `ebony` / `ash`
- `rows`, `cols`, `gap`, `grain`, `warp`, `wear`, `stain`, `bevel`

### `tex.floor.tile`

- `pattern`: `square` / `checker` / `diamond` / `hex` / `subway` / `mosaic` / `basket` / `herringbone`
- `palette`: `ceramic` / `terracotta` / `slate` / `porcelain` / `marble` / `black` / `mosaic` / `subway`
- `tilesX`, `tilesY`, `grout`, `bevel`, `glaze`, `wear`, `speckles`, `motif`

脚本侧用 `procgen.generateTexture(recipeId, params, gfx)` 生成可平铺贴图，
`generateNormalImage` 可从同一参数得到法线预览。
