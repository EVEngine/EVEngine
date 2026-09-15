# Voxel — 流式体素地形与方块建造示例

自包含的最小体素示例：脚本侧注册方块类型并程序化生成 4×4 图集，用内置噪声地形
（`setTerrain` + `streamAround`）自动流式补建玩家周围的 chunk，再通过 DDA 射线
拾取实现按名字放置与破坏方块。与 `examples/voxel-terrain`（结合 `procgen` 的
`TerrainSampler`、WASD 飞行与编辑器笔刷）相比，本示例只做最小闭环：脚本程序化
图集 + 内置噪声地形 + 自动环绕相机。

## 运行

```sh
make run/<platform>-debug GAME=examples/voxel
```

也可以直接进入示例目录运行引擎（Linux）：

```sh
cd examples/voxel && ../../build/linux-debug/src/engine/eve run
```

Windows 下可执行文件为 `build/win32-debug/src/engine/eve.exe`。

## 演示内容

- 方块类型注册表：`voxel.newCubeTypes()` 加 `loadFromJson`，注册 `grass`、`stone`、
  `wood` 三种方块，用 `faceTex` 指定每个面的图集编号。
- 程序化图集：`gfx.newCanvas` 绘制 4×4 个 32 像素图块（1 草顶、2 泥土、3 石头、
  4 木头），转成纹理后交给 `world.drawVisible(gfx, atlas, TILES_PER_ROW)`。
- 地形：`world.setTerrain(20260822, 1, 2, 3, 8.0, 14.0, 1.0 / 32.0)` 配置种子、
  草/土/石纹理 id、基准高度、幅度与缩放。
- 流式加载：`world.streamAround(0, 0, 0, 2)` 以球形半径 2 个 chunk 自动补建。
- 网格更新：`world.remeshDirty()` 重建脏 chunk（含边界接缝）。
- 剔除与绘制：`world.selectVisible(...)` 传入 16 个 float 的 view-proj 做视锥剔除，
  随后 `gfx.setMesh3DViewProj` / `gfx.setMesh3DView` / `gfx.setMesh3DCameraPos`
  与 `gfx.begin3DFrame()`。
- 射线拾取：`cam.screenToRay` 生成屏幕射线，`world.raycast(...)` 命中后用
  `getRaycastHitX/Y/Z` 与 `getRaycastPrevX/Y/Z` 取命中块与前一格空气块。
- 相机：单个 `eve.Camera3D`（`setActive(false)`，只用来算矩阵和射线）。

## 操作

- 鼠标左键点击：在命中面前一格的空气位置放置木头块（`setVoxelByName(..., "wood")`）。
- 鼠标右键点击：破坏命中方块（`setVoxel(hx, hy, hz, 0)`）。
- `R`：立即重建所有脏 chunk（`keyboard.isDown("r")` → `remeshDirty()`）。
- 相机自动环绕：代码用 `elapsed` 的 sin/cos 驱动 `setEye`，没有键盘移动键。

## 相关文件

- `main.nut`：全部示例逻辑——图集、世界与地形、轨道相机、拾取与放置/破坏。
- `config.nut`：窗口 900×600、标题 `voxel`、`hotReload = true`。
- `README.md`：本说明。
