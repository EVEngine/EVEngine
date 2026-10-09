# Lighting Showcase — 环境光 / 有向光 CSM / 旋转聚光灯 / 体积光柱 / 发光体 / 可选光追

展示 3D 光照组合：相机环境光、带 CSM 阴影的有向光、绕场旋转的暖/冷聚光灯
（perspective 局部阴影 + froxel 体积光柱）、彩色发光体（高亮网格 + bloom + 点光源），
以及可选的硬件光追 / 便携反射链。阴影默认开启；各光源族的阴影方案可通过
`gfx.setShadowScheme*` 配置。

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
- **有向光 + CSM**：`eve.Light3D()` + `setType("dir")` + `setCastShadow(true)` +
  `setShadowMethod("csm")`。`gfx.setDirectionalLight` 只做弱补光（本身不投射阴影）。
  按 `2` 开关。
- **旋转聚光灯 + perspective 阴影**：两盏暖/冷 `spot`（`setSpotAngle` /
  `setSpotSoftness` + `setShadowMethod("perspective")`）绕场反向公转并始终朝向地面，
  锥体与阴影在场景上扫动。按 `4` 开关。
- **体积光柱（froxel）**：聚光灯与有向光开启 `setVolumetric(true)`；每帧用
  `Volumetric.configureFroxelGrid` / `injectFroxelHeightFog` /
  `integrateFroxelFromSceneLights` / `applyFroxel` 合成薄尘介质中的光束。按 `5` 开关。
- **发光体**：暖 / 冷 / 品红球体保留原始 tint 材质；按 `3` 开关点光源与 bloom 发光，
  关闭时球体仍以普通材质受场景光照（不再涂成近黑 stub）。
- **阴影方案**（进程级）：`gfx.setShadowSchemeDirectionalEnabled` /
  `setShadowSchemeSpotEnabled` / `setShadowSchemePointEnabled` /
  `setShadowSchemeMaxSpotCasters`。本地阴影 atlas 固定 4 槽，候选更多时按屏幕重要性分页；
  `setShadowSchemePagingEnabled` / `setShadowSchemeMaxLocalUpdates` /
  `setShadowSchemeHysteresisBonus` 控制分页与每帧重绘预算。点光 cube 路径预留，默认关闭。
- **可选光追**：硬件 RTX 或 portable `reflectionChain`。按 `Space` 开关。

## 操作

| 输入 | 作用 |
|---|---|
| `1` | 开关环境光 |
| `2` | 开关有向光（CSM 阴影） |
| `3` | 开关发光体点光源与 bloom（网格始终保留原始材质） |
| `4` | 开关旋转聚光灯（perspective 阴影） |
| `5` | 开关 froxel 体积光柱 |
| `Space` | 开关光追（硬件 RTX，或 portable `reflectionChain`） |
| `R` / `r` | 复位相机 |

## 相关文件

- `main.nut`：场景、光照、阴影方案与 HUD；约第 36 帧后自动写出 `lighting-showcase.png`。
- `config.nut`：1280×720，标题 `EVEngine Lighting Showcase`。
- `README.md`：本文件。
