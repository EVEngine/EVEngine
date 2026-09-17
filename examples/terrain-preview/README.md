# Terrain Preview — 直写交换链的 3D 地形预览与视觉审计抓帧

本示例在脚本内用 `procgen` 生成一块 257×257 高度图，经热力、水力与河流三种侵蚀后
切分成 2×2 个地形区块，并直写交换链渲染完整的 3D 地形。除地表 splat 贴图外，它还按
高度图与分层数据生成程序化河道与湖面水体，并在演示稳定后自动抓取一帧 PNG 供视觉审计。

## 运行

```bash
make run/<platform>-debug GAME=examples/terrain-preview
```

也可以进入示例目录后直接启动引擎：

```bash
cd examples/terrain-preview && ../../build/linux-debug/src/engine/eve run
```

Windows 下引擎可执行文件为 `build/win32-debug/src/engine/eve.exe`。

## 演示内容

- 高度图生成：`procgen.newParams()` 配置 `setSize(257, 257)`、`setSeed(20260826)`，
  以及 `frequency`、`octaves`、`gain`、`ridge`、`warp`、`exponent`、`continent`、
  `island`、`coast` 等参数，再交给 `procgen.generateHeightmap(params)` 求值。
- 三层侵蚀：`procgen.erodeTerrainThermal`、`procgen.erodeTerrainHydraulic`、
  `procgen.erodeTerrainFluvialAdvanced` 依次作用，随后 `procgen.analyzeTerrain`
  从高度图得到地表分层数据（layers），供后续区块与水体网格使用。
- 地形区块：`procgen.buildTerrainChunk` 生成 2×2 个 128×128 区块，
  `procgen.generateTerrainChunkMesh` 转成渲染网格，`procgen.generateTerrainSplatMap`
  生成反照率贴图，再由 `gfx.newTexture` 上传。
- 材质：地表用 `procgen.createTerrainMaterialShader`，水体用
  `procgen.createTerrainWaterShader`；地表 `setMetallic(0.0)` / `setRoughness(0.92)`，
  水体则 `setMetallic(0.05)`～`0.08`、`setRoughness(0.20)`～`0.12`，比地表更光滑、略更反光。
- 水系：`procgen.generateTerrainRiverMesh` 与 `procgen.generateTerrainLakeMesh`
  在高度图与分层数据上生成河道和湖面，非空时各自包成 `eve.Renderable3D`。
- 相机与光照：`eve.Camera3D` 以 `setEye(62.0, 31.0, 64.0)`、`setTarget(22.4, 4.0, 22.4)`、
  `setFov(48.0)`、`setAmbient(...)` 俯视地形并 `setActive(true)`；
  `gfx.setDirectionalLight` 与 `gfx.setBackgroundColor` 设定场景基调。

## 截图

`eve_render` 每帧调用 `gfx.clear()` 与 `gfx.render3D()` 并累计已渲染帧数。当
`elapsed > 1.5`（约 1.5 秒）且 `renderedFrames >= 8` 时，脚本调用
`gfx.saveFramePng("/private/tmp/evengine-terrain-preview.png")` 写出这一帧 PNG；
成功后打印 `terrain preview frame saved: /private/tmp/evengine-terrain-preview.png`
并把 `saved` 置为 `true`，因此整个进程只抓一帧。

## 相关文件

- `main.nut`：示例全部逻辑——高度图与侵蚀、区块网格与 splat 贴图、河流 / 湖泊、相机与抓帧。
- `config.nut`：窗口配置，标题为 `EVEngine Terrain Preview`，尺寸 1000×700。
- `README.md`：本文件。
