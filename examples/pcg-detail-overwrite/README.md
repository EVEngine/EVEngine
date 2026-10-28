# PCG Terrain Detail Overwrite（地形细节覆盖）

验证 **Pcg 细节覆盖**：脚本用 `PointSet` 描述一批草地点（位置、缩放、颜色、`asset`
字符串属性与稳定 Point ID），把程序化生成的 albedo / normal / mask 三张图烘焙成
`GrassField` 植被，再用 `setTerrainDetailOverwrite` 一次性覆盖地形的细节距离、淡出
距离、密度与每 patch 分辨率，并读回校验。整条链路不依赖任何下载资源或运行时 GLSL。

## 运行

```bash
cd examples/pcg-detail-overwrite
../../build/linux-debug/src/engine/eve run
```

或：

```bash
make run/<platform>-debug GAME=examples/pcg-detail-overwrite
```

## 验证内容

- 25×17 的草地点阵：逐点 `setScale` / `setColor` / `setStringAttribute(i, "asset", "pcg:detail")`，
  最后 `assignPointIds("4242")` 固定 Point ID；
- 32×32 的 RGBA8 albedo / normal / mask 图像逐像素程序化生成（草叶 alpha 与 mask 通道）；
- `eve.bakeTerrainGrassFoliage(field, points, "pcg:detail", 0.8, 1.25, albedo, normal, mask, foliage)`
  上传植被，`GrassFoliageSettings` 给出基础色、alpha 裁剪、法线强度、渲染/淡出/硬距离与密度；
- `field.setTerrainDetailOverwrite(settings)` 覆盖细节参数：Pcg 细节距离 70 / 淡出 18，
  Unity 细节距离 110 / 密度 0.55 / 每 patch 8；
- 断言返回质量档位为 `3`（High8）、`getTerrainDetailHardDistance() == 110.0`、
  `getTerrainDetailDensity() == 0.55`，任一不符即 `throw`。

成功标记：`PCG_DETAIL_OVERWRITE_READY quality=High8 hard=110 density=0.55 fade=70+18 points=<点数>`

## 文件

| 路径 | 说明 |
|---|---|
| `main.nut` | 点阵构造、程序化贴图、植被烘焙、细节覆盖与断言 |
| `config.nut` | 引擎 `config`：960×640、`hotReload = true` |

## 契约

本示例是可运行示例（`main.nut` + `config.nut`），参与 `scripts/smoke_examples.sh` 的
帧循环契约：CI 在 Linux 上无头启动它，要求至少 2 秒仍然存活且日志中没有错误标记。
