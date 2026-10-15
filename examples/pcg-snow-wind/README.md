# PCG Snow Wind（雪的全局风场）

验证天气系统里 **雪的风场** 是真正的世界空间向量而非屏幕偏移：脚本用
`setSnowWind(4.5, -2.8, 1.5)` 写入三分量风，再通过 `getSnowWindX/Y/Z` 读回校验，
并确认它作用于整个世界（`world=true`）而不是跟随相机。

## 运行

```bash
cd examples/pcg-snow-wind
../../build/linux-debug/src/engine/eve run
```

或：

```bash
make run/<platform>-debug GAME=examples/pcg-snow-wind
```

## 验证内容

- `eve.Weather` 使用预置 `"snow"`、强度 1.0，随后 `init(gfx)`；
- `setSnowWind(4.5, -2.8, 1.5)` 必须返回 `ok`，否则 `throw result.status.summary`；
- 读回 `getSnowWindX()` / `getSnowWindY()` / `getSnowWindZ()`，确认与写入一致；
- 场景是三块立方体（地面 / 两根立柱）加一盏平行光，专门给雪提供可读的背景与光照；
- 每帧 `snowWeather.update(dt, gfx)`。

成功标记：`PCG_SNOW_WIND_READY x=4.5 y=-2.8 z=1.5 world=true`

## 文件

| 路径 | 说明 |
|---|---|
| `main.nut` | 天气与风场设置、场景搭建、成功标记 |
| `config.nut` | 引擎 `config`：960×640、`debug = true`、`hotReload = true` |

## 契约

本示例是可运行示例（`main.nut` + `config.nut`），参与 `scripts/smoke_examples.sh` 的
帧循环契约：CI 在 Linux 上无头启动它，要求至少 2 秒仍然存活且日志中没有错误标记。
