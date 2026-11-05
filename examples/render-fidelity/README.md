# Render Fidelity — P0 / P1 / P2 观感对比台

把“相对 UE5 观感差距”里列过的优先项做成同一场景下的可切换选项，便于肉眼对比细腻度。

## 运行

```bash
make run/<platform>-debug GAME=examples/render-fidelity
# 或
cd examples/render-fidelity && ../../build/linux-debug/src/engine/eve run
```

无人值守截图（每个 preset 单独跑一次，写出对应 PNG）：

```bash
cd examples/render-fidelity
# 编辑 config.nut：fidelityPreset = "baseline"|"p0"|"p1"|"all"，fidelityCapture = true
../../build/linux-debug/src/engine/eve run
# → render-fidelity-<preset>.png
```

Headless（CI / Cloud VM）：

```bash
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
export ALSOFT_DRIVERS=null
export XDG_RUNTIME_DIR=/tmp/xdg-runtime && mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"
xvfb-run -a ../../build/linux-debug/src/engine/eve run --debug --mcp-port=7529
```

截图请用引擎 MCP `eve_screenshot`，不要用 Xvfb 帧缓冲。

## 选项分层

| 层 | 键 | 效果 |
|----|----|------|
| P0 | `1` | 间接光：`gi` + `rtgi` |
| P0 | `2` | 接触阴影：`contactShadow`（抖动步进 + 二分细化，减轻斑马纹） |
| P0 | `3` | CSM 质量：更高 atlas、更近覆盖、自适应 cascade |
| P0 | `4` | AO：`ao`（GTAO 路径的默认管线 AO） |
| P0 | `5` | 体积雾：froxel + 场景光注入（真实 CSM 采样仍是后续项） |
| P1 | `6` | 材质扩展：叶片透射 SSS + clearcoat 球 |
| P1 | `7` | IBL / probe：预过滤 cubemap + box env probe |
| P1 | `8` | 反射链：TAA + RTGI（本例关掉 SSR：凸面金属上 SSR 会盖住房间探针，出现竖缝） |
| P2 | `9` | 虚拟纹理地面 |
| P2 | `0` | 几何细节：POM + cell bombing |

预设：`F1` baseline · `F2` P0 · `F3` P0+P1 · `F4` 全开 · `R` 复位相机。

`config.fidelityPreset` 可设为 `baseline` / `p0` / `p1` / `p2` / `all`。

## 诚实边界

- **点光 cube 阴影**：引擎仍标记为 reserved（`setShadowSchemePointEnabled(false)`）；P0 局部阴影用 spot perspective。
- **体积雾绑真实 CSM**：本例 froxel 采场景灯；不是 shadow-map 体积阴影。
- **材质图 MVP / Substrate**：本例用 `setFoliageTranslucency` / `setClearcoat`，不是节点材质图。
- **IBL 探针**：`setCaptureMask(-1)` 捕捉封闭房间；镜面球 `setReflectionCaptureMask(0)` 不写入 cubemap。相机在室内，前墙带亮窗——开敞入口会在球心反射成黑带。Box probe 对齐墙体半extents。
- **Virtual Geometry（Nanite 风格）**：另有 `examples/virtualgeometry`；本例 P2 用 VT + POM 代表“细节密度”。

## 相关实现

- 引擎新增：`RenderControl "contactShadow"` → `ContactShadow`（`src/modules/graphics/ContactShadow.*`）。
- 材质：`Material.setClearcoat(factor, roughness)`。
