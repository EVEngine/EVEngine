# Tilemap 建筑放置示例

演示在多样式 tilemap 上放置建筑：

- `map` 模块创建等距（或六角）`TileLayer`，`world.bindTileLayer(layer)` 直接复用其投影
- `world.setTerrainGid(gid, semantic)` 让地形语义从 tile GID 懒解析（水域/陆地）
- `eve.BuildingFx` 按定义 `visual2d` 生成 2D 精灵，进入统一 2D 队列按脚点 Y 穿插
- `PlacementSession` 封装放置 / 拆除 / 旋转 / 鬼影校验

```bash
make run/<platform>-debug GAME=examples/building-tilemap
```

| 输入 | 作用 |
|------|------|
| `1`–`3` | 选择建筑（房屋 / 码头 / 塔楼） |
| 鼠标移动 | 鬼影跟随并吸附（等距/六角投影） |
| `R` | 旋转 |
| 左键 | 放置 |
| 右键 | 拆除指针下建筑 |
| `T` | 切换网格叠加 |
