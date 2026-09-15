# PCG Parent Scaler（父级缩放 / 画布高度适配）

验证 **UI 面板按父画布高度自适应并夹紧到上限** 的求值接口：给定画布高度 640、目标
数量 2 与最大高度 500，`eve.evaluateParentScaler` 必须给出 `applyHeight = true` 且
`height = 500`（被上限夹紧，而不是取 640）。求值结果随后真的用于搭建一个 UI 面板，
所以这是"求值 + 落地"两条都覆盖的示例。

## 运行

```bash
cd examples/pcg-ui-scaler
../../build/linux-debug/src/engine/eve run
```

或：

```bash
make run/<platform>-debug GAME=examples/pcg-ui-scaler
```

## 验证内容

- `eve.ParentScalerSettings`：`partScreen = true`、`maxHeight = 500.0`；
- `eve.ParentScalerInput`：`hasCanvas = true`、`targetCount = 2`、`canvasHeight = 640.0`；
- `eve.evaluateParentScaler(state, output, settings, input)` 必须返回 `ok`，且
  `output.applyHeight` 为真、`output.height == 500.0`，否则 `throw`；
- 用 `ui.*` 搭建暗色主题面板复现这组数值（画布 640 px、上限 500 px、2 个调用方持有的
  目标、进度 500/640），`mountBuildAs("pcg-parent-scaler")` 后设置宿主尺寸 620×500、
  位置 (170, 70)，并禁止拖动与缩放；
- 每帧 `ui.beginFrameAndRender()`。

成功标记：`PCG_PARENT_SCALER_READY canvas=640 mode=part targets=2 height=500`

## 文件

| 路径 | 说明 |
|---|---|
| `main.nut` | 缩放求值与断言、UI 面板搭建、宿主尺寸设置、成功标记 |
| `config.nut` | 引擎 `config`：960×640、`debug = true`、`hotReload = true` |

## 契约

本示例是可运行示例（`main.nut` + `config.nut`），参与 `scripts/smoke_examples.sh` 的
帧循环契约：CI 在 Linux 上无头启动它，要求至少 2 秒仍然存活且错误标记不出现在日志中。
