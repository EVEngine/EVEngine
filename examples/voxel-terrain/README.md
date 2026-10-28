# Voxel Terrain — 体素引擎 + 地形生成测试场景

把 `eve.Voxel`（32³ chunk、贪婪矩形合并、视锥/视距裁剪、顶点 AO、DDA 射线
拾取、自动流式生成）和 `procgen` 的 `TerrainSampler`（岛屿衰减、大陆形状、
山脊/域扭曲、多八度 fBm）接在一起的可交互测试场景。

运行：

```sh
make run/win32-debug GAME=examples/voxel-terrain
```

操作：WASD 飞行、Space/C 升降、Shift 加速、右键拖拽视角、左键放置木头、
快速右键点按破坏；`1-4` 切换群岛/大陆/山脉/沙海预设，`R` 随机种子，
`=/ -` 调整流式半径，`Z/Y` 撤销/重做体素笔刷。

放置和破坏并不直接修改一个“固定编辑器”的状态。项目脚本把 live `VoxelWorld`
包装为 `VoxelWorldTarget`，再组合 falloff、球形 kernel、整数 operation 与
`VolumeBrushTool`；raycast 只负责把三维坐标交给 `EditorSession`。因此同一工具可用于
开发视口、游戏内建造模式或项目自定义 UI，并共享 transaction、constraint 与 revision。

脚本侧通过 `world.setTerrainParam(key, value)` 驱动完整地形参数（见
`docs/usr/modules/voxel.md`），因此预设切换会整体重建世界，可在不同地形
风格间直观对比流式生成、沙滩带与射线拾取。
