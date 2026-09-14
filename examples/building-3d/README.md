# 3D 地面放置示例

演示在 3D 场景地面（XZ 平面）上放置建筑：

- `world.setGridPlane("xz")`：网格第二轴映射到世界 Z，世界 Y 为高度
- 内置 `"plane"` 放置表面 + 脚本侧 `Camera3D.screenToRay` 与 Y=0 平面求交
- `eve.BuildingFx` 按定义 `renderMode:"3d"` / `visual3d` 生成 `Renderable3D` 鬼影与建筑
- 3D 网格线框叠加（`drawGrid3D`）与 `PlacementSession` 放置 / 拆除

```bash
make run/<platform>-debug GAME=examples/building-3d
```

- `1` / `2` / `3`：山墙小屋 / 长屋谷仓 / 尖顶小屋。
- 鼠标移动：射线与地面求交，鬼影吸附到 XZ 网格。
- `R`：旋转；用 3x2 谷仓检查旋转后模型、预览和占地是否一致。
- 左键放置，右键拆除，`T` 切换网格。

三款模型位于 [assets](assets/README.md)，使用 OBJ 三角面和显式法线，无贴图依赖。
启动时通过 Model3D 加载，再安装到 BuildingFx 的 mesh resolver；预览和正式
建筑共用同一网格。模型加载失败会直接报错，避免测试时悄悄变回立方体。

验证交互时，可先放谷仓，旋转 90 度后在旁边放第二座；两者分别占 3x2 / 2x3
格。重叠放置应拒绝，右键拆除后应能重新放置。鼠标射线未命中地面时隐藏
鬼影并忽略放置/拆除点击，避免操作上次的位置。热重载会清空放置世界。
