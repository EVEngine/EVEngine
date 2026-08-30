# Graphics 高级渲染与屏幕空间效果

[返回模块概览](../graphics.md)

### 可编译渲染控制与 GBuffer

`RenderControl` 用字符串特性开关，再 `compile()` 成有序 Pass（默认：shadow → forward → hair）：

```squirrel
local rc = gfx.getRenderControl();
rc.enable("gbuffer");      // 额外填充可采样深度/法线
rc.enable("gbufferAlbedo");
rc.compile();
gfx.render3D();
local gb = rc.getGBuffer();
if (gb.isValid()) {
    local depth = gb.getDepthTexture();     // RGBA8，R = 线性深度 0..1（Canvas / 体积雾）
    local hwDepth = gb.getHwDepthTexture(); // D32，.r = Vulkan NDC z（3D AO / GI）
    local nrm = gb.getNormalTexture();      // RGB = 法线*0.5+0.5
}
```

3D 前向仍启用硬件 z-buffer；GBuffer 是给 AO / 体积雾 / 风格描边等中后期用的采样目标。阴影仍走 CSM shadow map。

一帧里各 buffer 谁写谁读、对应函数和 shader，见开发文档 [`3D渲染管线.md`](../../../dev/3D渲染管线.md)。

大面积平铺 albedo（地面、墙面）若出现明显重复，可对实体调用 `setTexCellBomb(cellScale, strength, rotAmount=1)`：按 UV 划分 cell，对邻接 cell 做随机偏移/旋转并混合。`strength=0`（默认）关闭，行为与原先一致；`cellScale` 一般为 2～16。

砖墙、石板等需要假深度时，用 `setHeightTexture(heightTex)` + `setParallax(scale, minLayers=8, maxLayers=32)` 开启视差遮蔽贴图（POM）。高度图取 **R 通道**（白=凸起朝向观察者）；`scale=0`（默认）关闭。典型 `scale` 为 0.02～0.08；掠射角下层数会自适应增加。

### 毛发 / 皮毛渲染（Hair Cards）

适用于 VRoid / 角色发片、动物皮毛等 alpha 卡片网格。引擎提供内置 **Kajiya-Kay 各向异性高光** shader，并在 `RenderSystem3D` 中于不透明物体之后、按距离从远到近绘制。

```squirrel
local hairShader = gfx.newHairShader()
hairShader.sendFloat("specExp", 90.0)
hairShader.sendFloat("specStrength", 0.9)
hairShader.sendFloat("alphaCutoff", 0.12)

local hair = Renderable3D.create()
hair.setMesh(hairCardMesh)
hair.setTexture(hairAlbedo)
hair.setShader(hairShader)
hair.setHair(true)   // 启用透明毛发 pass（背面优先排序）
hair.setCastShadow(false)  // 发片通常不参与阴影投射
```

可调 push 参数：`specExp`、`specStrength`、`primaryShift`、`secondaryShift`、`alphaCutoff`、`rimStrength`、`strandDirX/Y/Z`（发束方向，全 0 时由顶点自动推导）。

### 屏幕空间体积光（尘雾光柱）与体积雾

`vol <- gfx.newVolumetric()`。`setQuality("low"|"medium"|"high")` 控制采样与 `resolutionFor`。

- **screenspace**：`beginOcclusionMap` → `drawOccluders2D` → `scatter`；或 `applyFromScene`
- **raymarch**：`setMode("raymarch")` + `setCamera` + 线性深度 → `rayMarch`
- **fog**：`setMode("fog")` + `setFogHeight*` / `setFogStart`/`End` + 线性深度 → `applyFog`（雾色 alpha 叠加场景）
- **froxel**：`configureFroxelGrid` → `clearFroxelGrid` →
  `injectFroxelHeightFog` → `integrateFroxel` → `uploadFroxel`；在
  `gfx.render3D()` 后将 GBuffer 线性深度传给 `applyFroxel` 或
  `applyFroxelTo`。介质未变化时不必每帧重新上传。
- **cloud**：`setMode("cloud")`，用 `setCloudLayer`、`setCloudCoverage`、
  `setCloudDensity`、`setCloudScale`、`setCloudWind` 和 `setCloudLightColor`
  调整云层；线性深度输入通过 `renderClouds` 或 `renderCloudsTo` 渲染，
  `getCloudShader` 可用于高级参数检查与调试。

细节见 [`体积光模块设计.md`](../../../dev/体积光模块设计.md)。

### 屏幕空间环境光遮蔽（SSAO / HBAO / GTAO）

`ao <- gfx.newAmbientOcclusion()`。`setMode("ssao"|"hbao"|"gtao")`，`setQuality` 控制采样与 `resolutionFor`。

1. `setCamera` + 线性深度纹理（与体积雾相同约定）
2. `compute` / `computeTo` → AO 图（RGB=遮蔽因子，A=深度）
3. 可选 `blur` / `blurTo`（双边）
4. `applyOverlay` 以黑 + `alpha=(1-ao)*intensity` 叠到已有场景

3D 默认路径：`RenderControl` 特性 `"ao"`（默认开）会在 forward 之后对 GBuffer 的 D32 + 法线做 `applyFromGBuffer`，不必手动建 Canvas。Canvas 上的 `compute` 仍用 8-bit 线性深度。

细节见 [`环境光遮蔽模块设计.md`](../../../dev/环境光遮蔽模块设计.md)。

### 屏幕空间全局光照（SSGI）

`gi <- gfx.newGlobalIllumination()`。`setQuality` 控制采样数与半径；`setLightDirection` / `setLightColor` 提供反弹用的太阳光。

3D 默认路径：`RenderControl` 特性 `"gi"` 仍默认开（mesh 半球天空/地面 + wrap fill）。fullscreen `applyFromScene` 不会自动叠到 3D 回读：从 lit scene color 采样会把帘子/花盆印到地面上形成游走鬼影。需要时仍可手动 `applyFromScene` / `applyFromDepth`。Canvas 测试仍可用打包的 `applyFromDepth`（A=线性深度）。

```squirrel
local rc = gfx.getRenderControl();
rc.enable("gi");   // 默认已开
rc.compile();
```

### 抗锯齿（硬件 MSAA + 经典后处理）

**硬件 MSAA**（3D 模型/体素边缘效果最好）：3D scene color pass 默认 4x 多重采样再 resolve。用 `RenderControl "msaa"`（默认开）开关，`gfx.setMsaaSamples(n)` 设采样数（0/1=关，2/4/8 按设备能力 clamp）：

```squirrel
gfx.setMsaaSamples(8);          // 升到 8x（若设备支持）
gfx.setMsaaSamples(0);          // 关闭硬件 MSAA
rc.disable("msaa");             // 或通过 RenderControl 特性关
```

**经典后处理**：`aa <- gfx.newAntiAliasing()`。`setQuality("low"|"medium"|"high")` 调整阈值与搜索；`setMode` 选择算法：

- **fxaa**：FXAA 3.11 风格亮度边搜索
- **smaa**：SMAA 启发的单 Pass 形态学 AA
- **ssaa**：超采样 Resolve（先画到 `resolutionFor` 尺寸的 Canvas）
- **nfaa**：沿亮度梯度切向的 Normal Filter AA

典型流程：场景 → Canvas → `aa.applyCanvas` / `applyCanvasTo` → 屏幕。

3D 默认路径：`begin3DFrame` 画到可采样的 scene color（`"msaa"` 开时先 Nx 多重采样再 resolve 到 1x），present 时按 `RenderControl "aa"`（默认开）做 FXAA resolve 再叠 AO/HUD。手动 Canvas 路径仍然可用。细节见 [`抗锯齿模块设计.md`](../../../dev/抗锯齿模块设计.md)。
