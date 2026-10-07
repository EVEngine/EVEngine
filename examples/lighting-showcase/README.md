# Lighting Showcase — 环境光 / 有向光阴影 / 发光体 / 可选光追

展示 3D 光照组合：相机环境光、带阴影的有向光（`Light3D` dir + `setCastShadow`）、
彩色发光体（高亮网格 + bloom + 点光源），以及可选的硬件光追 / 便携反射链。
阴影默认开启。

## 运行

```bash
make run/<platform>-debug GAME=examples/lighting-showcase
```

也可构建后进入示例目录直接启动：

```bash
cd examples/lighting-showcase && ../../build/linux-debug/src/engine/eve run
```

Windows 上引擎可执行文件为 `build/win32-debug/src/engine/eve.exe`。

## 演示内容

- **环境光**：`camera.setAmbient(0.22, 0.24, 0.30)`，按 `1` 开关。
- **有向光 + 阴影**：`eve.Light3D()` + `setType("dir")` + `setCastShadow(true)` +
  `setShadowStrength(0.88)`。`gfx.setDirectionalLight` 只做弱补光（本身不投射阴影）。
  地面 `setReceiveShadow(true)`，柱体 / 金属球 `setCastShadow(true)`。按 `2` 开关有向光。
- **发光体**：暖 / 冷 / 品红三颗球体用亮 tint + `camera.setBloom(0.28, 1.05)` 呈现自发光外观，
  同位置的 `Light3D` 点光源照亮邻近表面。按 `3` 开关。
- **可选光追**：`eve.RayTracing().isAvailable()` 为真时 `rc.enable("rtx")`；否则回退到
  `rc.enable("reflectionChain")`（与 `rendering-chain-lab` 相同的便携路径）。按 `Space` 开关。
  阴影特性始终 `rc.enable("shadow")`。
- 渲染：`gfx.clear()` + `gfx.render3D()`，HUD 用 `ui.beginFrameAndRender()`。

## 操作

| 输入 | 作用 |
|---|---|
| `1` | 开关环境光 |
| `2` | 开关有向光（含阴影） |
| `3` | 开关发光体点光源与发光网格 |
| `Space` | 开关光追（硬件 RTX，或 portable `reflectionChain`） |
| `R` / `r` | 复位相机 |

## 相关文件

- `main.nut`：场景、光照、光追开关与 HUD。
- `config.nut`：1280×720，标题 `EVEngine Lighting Showcase`。
- `README.md`：本文件。
