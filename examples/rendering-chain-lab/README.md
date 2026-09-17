# Rendering Chain Lab — TAA / SSR / RTGI 与自动反射链运行时对比场景

本示例提供一个固定视角的对比场景，用同一条渲染链交替开关 TAA、SSR、RTGI 与自动反射链
（`reflectionChain`），让屏幕空间反射、间接光颜色溢出和时间性闪烁/拖影的差异在同一画面上
直接对照。场景里的反光地面、饱和遮挡盒、五种粗糙度的球体和一根来回移动的细杆，都是为暴露
这些通道的典型缺陷而摆放的。

## 运行

```bash
make run/<platform>-debug GAME=examples/rendering-chain-lab
```

也可以进入示例目录后直接启动引擎：

```bash
cd examples/rendering-chain-lab && ../../build/linux-debug/src/engine/eve run --debug --mcp-port=7529
```

Windows 下引擎可执行文件为 `build/win32-debug/src/engine/eve.exe`。脚本头部记录的等价形式是
`eve run --debug --mcp-port=7529 examples/rendering-chain-lab`。

## 演示内容

- 场景搭建：`buildScene()` 用 `eve.Renderable3D` 配 `gfx.newMeshCube(1.0)` /
  `gfx.newMeshSphere(40, 24)` 生成反光地面与后墙（`setMetallic(0.72)`、`setRoughness(0.12)`）、
  红蓝两块高饱和遮挡盒，以及 5 个球体（`setMetallic(0.48)`，粗糙度依次为
  0.06 / 0.16 / 0.30 / 0.52 / 0.82）和各自的底座。
- 时间性伪影诱因：细杆 `labMover` 在 `eve_update` 中按 `sin(labTime * 0.82) * 2.8` 横向移动，
  用来观察 TAA 的闪烁与拖影。
- 相机与光照：`eve.Camera3D` 设 `setFov(48.0)`、`setAmbient(...)`、`setExposure(0.85)`、
  `setBloom(0.18, 1.15)` 并 `setActive(true)`；`gfx.setDirectionalLight` 提供主光，两个
  `eve.Light3D` 暖 / 冷点光源补光，`gfx.setBackgroundColor` 定下暗色基调。
- 渲染链开关：`configureEffects()` 取 `gfx.getRenderControl()`，先执行
  `rc.setPostProcessQuality("high")`，再对 `gi`、`rtgi`、`aa`、`taa`、`ssr`、`reflectionChain`
  逐个 `enable` / `disable`，最后 `rc.compile()`。
- 反射链是一键式的：开启 `reflectionChain` 会同时打开 `aa`、`taa`、`rtgi` 并关闭 `msaa`，
  三者共用同一套后处理质量档位。
- 回调结构：`eve_init` 建场景与 HUD，`eve_update(dt)` 推进动画并处理按键，`eve_render` 依次
  调用 `gfx.clear()`、`gfx.render3D()`、`ui.beginFrameAndRender()`。

## 操作

| 输入 | 作用 |
|---|---|
| `Space` | 在 FULL（TAA + SSR + RTGI + 反射链）与 BASELINE（上述特性全部关闭）之间切换 |
| `R` / `r` | 复位相机到 `setEye(0.0, 4.4, 12.8)` / `setTarget(0.0, 1.0, -0.8)` |

HUD（`ui.beginWindow("RenderingChainLab")`，经 `ui.mountBuildAs("render-lab-hud")` 挂载为
宿主叠加层）只显示标题、当前模式和这两条按键提示，没有可点击控件；脚本也没有注册任何鼠标
操作，相机是固定视角。

## 相关文件

- `main.nut`：示例全部逻辑——场景搭建、渲染链开关、HUD 以及三个引擎回调。
- `config.nut`：窗口配置，1280×720，标题 `EVEngine Rendering Chain Lab`，`debug = true`。
- `README.md`：本文件。
