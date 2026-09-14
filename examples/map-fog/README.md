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
