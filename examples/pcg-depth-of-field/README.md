# PCG Auto Depth of Field（自动景深对焦）

验证 **Pcg 自动对焦**这条纯脚本链路：脚本提供一条"视线命中距离"采样，引擎按设置
（最大距离、响应速度、跟踪模式）推进对焦状态机，输出的 `focusDistance` / `aperture`
再由脚本写回相机景深。示例同时打开 `gbuffer` 渲染通道，保证景深合成走 GPU 路径。

场景是一块地面加三根近景立方体，远处再放一排立方体作为虚化对象；对焦输入是固定
20.0 的命中距离（不依赖鼠标），因此无头运行也能得到确定结果。

## 运行

```bash
cd examples/pcg-depth-of-field
../../build/linux-debug/src/engine/eve run
```

或：

```bash
make run/<platform>-debug GAME=examples/pcg-depth-of-field
```

## 验证内容

- `eve.DepthOfFieldFocusSettings`（`tracking = 0` 手动、`maximumDistance = 100.0`、
  `response = 3.5`）与 `eve.DepthOfFieldFocusInput`（`hasRayHit = true`、
  `rayHitDistance = 20.0`、`deltaSeconds = 1.0`）驱动
  `eve.evaluateDepthOfFieldFocus(state, output, settings, input)`；
- 返回结果 `!result.ok` 时直接 `throw`，不接受静默失败；
- 求得的 `focusDistance` / `aperture` 交给 `camera.setDepthOfField(focusDistance, 5.0, 12.0)`。

成功标记：`PCG_DEPTH_OF_FIELD_READY focus=... aperture=... gpuComposite=active`

## 文件

| 路径 | 说明 |
|---|---|
| `main.nut` | 场景搭建、对焦求值、写回相机景深、成功标记 |
| `config.nut` | 引擎 `config`：960×640、`debug = true`、`hotReload = true` |

## 契约

本示例是可运行示例（`main.nut` + `config.nut`），参与 `scripts/smoke_examples.sh` 的
帧循环契约：CI 在 Linux 上无头启动它，要求至少 2 秒仍然存活且日志中没有错误标记。
