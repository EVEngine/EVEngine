# 3D 地面放置示例

演示在 3D 场景地面（XZ 平面）上放置建筑：

- `world.setGridPlane("xz")`：网格第二轴映射到世界 Z，世界 Y 为高度
- 内置 `"plane"` 放置表面 + 脚本侧 `Camera3D.screenToRay` 与 Y=0 平面求交
- `eve.BuildingFx` 按定义 `renderMode:"3d"` / `visual3d` 生成 `Renderable3D` 鬼影与建筑
- 3D 网格线框叠加（`drawGrid3D`）与 `PlacementSession` 放置 / 拆除

```bash
make run/<platform>-debug GAME=examples/building-3d
```

| 输入 | 作用 |
|------|------|
| `1`–`2` | 选择建筑（房屋 / 塔楼） |
| 鼠标移动 | 射线与地面求交，鬼影吸附到 XZ 网格 |
| `R` | 旋转 |
| 左键 | 放置 |
| 右键 | 拆除指针下建筑 |
| `T` | 切换网格线框 |
