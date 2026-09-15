# PCG Thunder Strike（雷击事件）

验证**雷击**这条事件链路：`eve.triggerThunderStrike` 用一个确定性 seed 在指定世界
坐标触发雷击，返回落点、作用半径、播放的音频片段索引与强度；脚本把一盏点光源放到
落点上，并用 `eve.advanceThunderStrike` 让它随雷击状态逐帧衰减。天气系统同时切到
`storm` 预置并触发一次它的内部雷击。

## 运行

```bash
cd examples/pcg-thunder-strike
../../build/linux-debug/src/engine/eve run
```

或：

```bash
make run/<platform>-debug GAME=examples/pcg-thunder-strike
```

## 验证内容

- `eve.ThunderStrikeSettings`：`intensity = 2.5`、`radius = 420.0`、`volume = 0.8`、
  `audioClipCount = 4`；
- `eve.triggerThunderStrike(state, settings, 0.0, 0.0, -8.0, 20260912)` 必须
  `assert(strike.ok)`；落点与半径写进 `eve.Light3D`（点光源，颜色 0.40/0.58/1.0，强度 2.5）；
- `eve.Weather` 预置 `"storm"`、强度 0.72，并调用 `strike()`；
- 每帧 `advanceThunderStrike(state, dt)`，光源 `setEnabled(state.playing)`、强度跟随
  `state.intensity`。

成功标记：`PCG_THUNDER_STRIKE_READY audioIndex=... radius=... seed=20260912`

## 文件

| 路径 | 说明 |
|---|---|
| `main.nut` | 雷击触发、光源绑定、天气预置、逐帧推进与成功标记 |
| `config.nut` | 引擎 `config`：960×640、`debug = true`、`hotReload = true` |

## 契约

本示例是可运行示例（`main.nut` + `config.nut`），参与 `scripts/smoke_examples.sh` 的
帧循环契约：CI 在 Linux 上无头启动它，要求至少 2 秒仍然存活且日志中没有错误标记。
