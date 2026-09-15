# Spline Tube Lab（样条管道与截面挤出）

演示**不依赖任何源网格**的程序化生成：一条样条路径可以直接驱动管道、带状体与自定义
截面挤出三种生成器，覆盖开放路径与闭合回路、逐点截面 / 旋转 / 分块（chunk break）、
分布采样与沿路径行进（travel）坐标系。没有输入，启动即静态展示。

## 运行

```bash
cd examples/spline-tube-lab
../../build/linux-debug/src/engine/eve run
```

或：

```bash
make run/<platform>-debug GAME=examples/spline-tube-lab
```

## 4 个变体

| 变体 | 生成器 | 路径与参数 |
|---|---|---|
| 开放管道 | `mesh.splineTube` | 4 点 Catmull-Rom 开放路径、半径 0.48、48 路径段 × 16 径向段、封盖 |
| 带状体 | `mesh.splineRibbon` | 带坡度的道路路径、宽 1.8、厚 0.28 |
| 自定义截面 | `mesh.splineExtrude` | U 形开口截面（`setNodeSplineProfile`），沿开放路径挤出 |
| 闭合回路 | `mesh.splineTube` | `applyShapePreset("circle", 16, 2.0, 0.0, 1.0)` 预置圆环、无缝闭合、半径 0.32 |

细节：

- 逐点轮廓与旋转：`setPointProfile` / `setPointRotation`（管道与道路倾斜）。
- 分块：`setPointChunkBreak` 把一条样条切成多个 chunk，路径线用 `polylineResult` 分块绘制。
- 分布与行进：`distributeResult(6, ...)` 取 6 个等距采样点及其朝向，
  `travelResult` / `travelFrameResult` 取沿路径行进的坐标与 up/side 基向量。

成功标记：

- 控制台打印 `SPLINE_TUBE_LAB_PASS variants=4 open=capped closed=seamless ...`
- 当前目录写出 `spline-tube-lab.png`

## 文件

| 路径 | 说明 |
|---|---|
| `main.nut` | 全部逻辑：样条构造、四种生成器、路径线、相机、截图 |
| `config.nut` | 引擎 `config`：1100×720、`hotReload = true` |
| `spline-tube-lab.png` | 期望产出的画面（示例自带参考图） |

## 契约

本示例是可运行示例（`main.nut` + `config.nut`），参与 `scripts/smoke_examples.sh` 的
帧循环契约：CI 在 Linux 上无头启动它，要求至少 2 秒仍然存活且日志中没有错误标记。
