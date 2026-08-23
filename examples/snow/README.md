# Interactive Snow — 可交互积雪

演示深度场积雪：雪层是一张与地形高度图同尺寸的 `SnowField` 网格，同时驱动
**真实网格位移**（地形高度 = 地形 + 雪深，脚印/弹坑是真实几何凹陷）与
**POM 视差细节**（同一网格上传为材质的 height texture，`setHeightTexture` +
`setParallax`），并支持降雪回填恢复。

## 运行

```bash
make run/<platform>-debug GAME=examples/snow
```

## 操作

| 输入 | 作用 |
|---|---|
| 左键 | 在射线落点踩出脚印（方向 = 相机朝向在 XZ 的投影） |
| 右键 | 砸出弹坑：清空雪 + 压出地形凹陷 + 边缘堆雪 |
| `W` | 开关自动行走者，沿途自动留脚印 |
| `S` | 开关降雪回填（雪面慢慢恢复） |
| `P` | 开关 POM（对比真实位移与视差微细节） |
| `R` | 重置雪面 |

渲染路径：`editor.newHeightmapMesh/updateHeightmapMesh` 原地重建地形网格；
雪纹理的 R 通道即 POM 高度（白 = 隆起），G/B/A 是随雪深变暗的冷白 albedo，
所以坑底自然露出深色地表。
