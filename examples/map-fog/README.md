# Map Fog（大地图迷雾）

演示 `gfx.newMapFog()`：双层反向滚动云贴图 + RGBA mask 桥接（R 解锁 / G 选中闪烁 / B 溶解），并带投影 pass。

```bash
make run/linux-debug GAME=examples/map-fog
# 或
eve run examples/map-fog
```

操作：

- 左键：选中迷雾格
- Space：解锁（数据立刻写入 R，B 通道播溶解）
- R：重置迷雾
- `[` / `]`：调整雾透明度

## 2026-11-13 画面复查

补充确定性策略地图：河流、林地、道路和聚落；迷雾仍由原 MapFog 与 RGBA 掩码驱动。增加选中边框、揭示状态和操作说明。

[前后截图与运行验证](../../docs/images/examples/review-2026-11-13/nine-improved/README.md)。
