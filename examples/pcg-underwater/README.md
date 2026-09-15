# PCG Underwater Effects（水下效果全链路）

本仓库里覆盖最完整的一个 PCG 示例：把**进入 / 离开水面的整套表现**串成一条链路——
深度雾与颜色梯度、时间驱动的后期曝光与色彩曲线、水焦散（caustic）投影、水下材质替换、
三路音频（入水 / 出水 / 循环环境音）、粒子生命周期、触发体、原点平移与地形重力跟随。
所有采样都是脚本显式驱动的，因此无头运行时结果确定。

## 运行

```bash
cd examples/pcg-underwater
../../build/linux-debug/src/engine/eve run
```

或：

```bash
make run/<platform>-debug GAME=examples/pcg-underwater
```

## 验证内容

- **状态推进**：`eve.advanceWaterUnderwaterEffects(state, output, settings, input, depthGradient, timeGradient, postExposureCurve, postColorGradient)`
  先在水面上"播种"（`cameraY = 6.0`，`timeOfDay = 0.5`），再把相机压到 `4.21` 完成入水；
  断言 `output.entered`、`output.loopAudio` 与 `causticFrame == 3`；
- **雾**：`WaterUnderwaterSettings`（`causticTextureCount = 16`、`fogDepth = 20`、
  `fogDensity = 0.052`、`fogDistance = 42`）配合三段深度雾梯度与两段昼夜雾梯度
  （`UnderwaterColorGradient.addStop`），经 `eve.applyWaterUnderwaterFog` 写进体积雾；
- **后期**：`UnderwaterScalarCurve` 的曝光关键帧与后期色彩梯度经
  `eve.applyWaterUnderwaterPostFx` 应用；
- **材质**：`WaterSurfaceMaterialSnapshot` 经 `eve.applyWaterUnderwaterMaterial` 替换水下
  物体着色器/颜色，断言读回的 `getTintR()` 为 0.72；
- **音频**：`eve.applyUnderwaterAudio(入水, 出水, 循环环境音, ...)` 之后断言环境音
  `isLooping()` 且 `isPlaying()`；音频文件取自本目录的三个 wav；
- **水焦散**：`gfx.newTextureFromFile("caustic-cookie.png")` 作为方向光 cookie，
  在渲染阶段 `fog.projectDirectionalCookie(...)` 投影到深度图；
- **其它**：粒子发射器生命周期（水面 VFX 暂停、地面粒子被剔除、原点平移后的粒子数）、
  触发体恢复、地形重力激活、跟随水面的位置与着色器、地平线可见性。

成功标记：

```
PCG_UNDERWATER_READY depth=... caustic=... fog=... transition=... audio=looping
particles=active surfaceVfx=paused trigger=restored groundParticles=culled
originParticles=... terrainGravity=activated followWater=applied material=mainLight horizon=visible
```

## 文件

| 路径 | 说明 |
|---|---|
| `main.nut` | 入水/出水状态推进、雾与后期与材质 provider、音频、粒子、渲染阶段焦散投影 |
| `config.nut` | 引擎 `config`：960×640、`hotReload = true` |
| `caustic-cookie.png` | 方向光水焦散 cookie |
| `submerge-down.wav` / `submerge-up.wav` | 入水 / 出水音效 |
| `underwater-loop.wav` | 水下循环环境音 |

## 契约

本示例是可运行示例（`main.nut` + `config.nut`），参与 `scripts/smoke_examples.sh` 的
帧循环契约：CI 在 Linux 上无头启动它，要求至少 2 秒仍然存活且日志中没有错误标记。
