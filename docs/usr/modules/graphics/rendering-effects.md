# Graphics 高级渲染与屏幕空间效果

[返回模块概览](../graphics.md)

### 可编译渲染控制与 GBuffer

`RenderControl` 用字符串特性开关，再 `compile()` 成有序 Pass（默认：shadow → forward → hair）：

显式请求 GBuffer 相关效果时使用 `requestFeature(name, true)`，并检查返回的
`Result`。WebGPU 设备的颜色附件预算不足 40 字节/采样时，该方法返回
`Unsupported`；`enable`/`disable` 是不报告失败的兼容接口。低能力设备仍可
运行前向渲染，`isEnabled` 对不可用的 GBuffer 相关效果返回 `false`，
`compile()` 不会安排 GBuffer、decal 或延迟光照。

```squirrel
local rc = gfx.getRenderControl();
local gbufferRequest = rc.requestFeature("gbuffer", true);
if (gbufferRequest.ok) rc.requestFeature("gbufferAlbedo", true).ignore();
rc.compile();
gfx.render3D();
local gb = rc.getGBuffer();
if (gb.isValid()) {
    local depth = gb.getDepthTexture();     // RGBA8，R = 线性深度 0..1（Canvas / 体积雾）
    local hwDepth = gb.getHwDepthTexture(); // D32，.r = Vulkan NDC z（3D AO / GI）
    local nrm = gb.getNormalTexture();      // RGB = 法线*0.5+0.5
}
```

**光照模式（LightingMode）**：不透明核心 PBR 可选 Clustered Deferred。

| API | 作用 |
|-----|------|
| `rc.setLightingMode("forwardPlus"\|"hybrid")` | 请求模式；`hybrid` 需要 `isDeferredLightingAvailable()` |
| `rc.getLightingMode()` / `getEffectiveLightingMode()` | 请求值 vs `compile()` 后实际值 |
| `rc.isDeferredLightingAvailable()` | 当前 backend 是否能跑 Hybrid deferred lighting |
| `rc.applyLightingPreset("desktop"\|"mobile"\|"ci")` | 桌面在可用时切 Hybrid；移动/CI 保持 ForwardPlus |
| `rc.applySuggestedLightingPreset()` / `getLightingPreset()` | 按 OS/GPU（Lavapipe→ci）+ `EVENGINE_LIGHTING_PRESET` / `EVENGINE_LIGHTING_MODE`；读取上次预设 |
| `rc.hasHybridLightingFallback()` | 请求了 Hybrid 但 compile 回退到 ForwardPlus |

Hybrid 下 Pass 序为 shadow → gbuffer → **deferredLighting** → forward（半透明仍 Forward+）；MSAA 会被关掉。

3D 前向仍启用硬件 z-buffer；GBuffer 是给 AO / 体积雾 / 接触阴影 / 风格描边 / **X-ray 遮挡高亮**等中后期用的采样目标。阴影仍走 CSM shadow map。

**X-ray 透视高亮（穿墙轮廓）**：`stylize.newMeshShader(gfx, "xray")` 生成 overlay shader，再挂到目标：

```squirrel
target.setXRayShader(xrayShader);
target.setXRayHighlight(true);   // 仅在需要透视时打开
```

开启后，目标在 GBuffer 深度中被遮挡物“挖空”，再用 xray shader 重绘被挡住的轮廓。需要 `RenderControl.enable("gbuffer")`。可运行演示：`examples/bullet-time`。

**接触阴影（contactShadow）**：沿主方向光做短距离屏幕空间射线步进，叠黑 alpha 暗化。默认关；需要 GBuffer 深度（可选法线）。质量档 `low|medium|high` 由管线内 `ContactShadow` 控制。对比台见 `examples/render-fidelity`。

一帧里各 buffer 谁写谁读、对应函数和 shader，见开发文档 [`3D渲染管线.md`](../../../dev/3D渲染管线.md)。

大面积平铺 albedo（地面、墙面）若出现明显重复，可对实体调用 `setTexCellBomb(cellScale, strength, rotAmount=1)`：按 UV 划分 cell，对邻接 cell 做随机偏移/旋转并混合。`strength=0`（默认）关闭，行为与原先一致；`cellScale` 一般为 2～16。

砖墙、石板等需要假深度时，用 `setHeightTexture(heightTex)` + `setParallax(scale, minLayers=8, maxLayers=32)` 开启视差遮蔽贴图（POM）。高度图取 **R 通道**（白=凸起朝向观察者）；`scale=0`（默认）关闭。典型 `scale` 为 0.02～0.08；掠射角下层数会自适应增加。

### 毛发 / 皮毛渲染（Hair Cards）

适用于 VRoid / 角色发片、动物皮毛等 alpha 卡片网格。引擎提供内置 **Kajiya-Kay 各向异性高光** shader（含 soft wrap 漫反射与廉价 back-scatter），并在 `RenderSystem3D` 中于不透明物体之后、按距离从远到近绘制。

**推荐路径（发卡，非 strands）：**

```squirrel
local hairMesh = gfx.newHairCardMesh(0.12, 0.45)   // 程序化单片；也可用 DCC 发卡模型
local farMesh  = gfx.newHairCardMesh(0.2, 0.35)    // 远距更宽更矮的代理片
local hairMat  = gfx.newHairCardMaterial(hairAlbedo) // 自动 transparent/双面/不写深度/不投阴影
hairMat.getShader().sendFloat("specExp", 90.0)
hairMat.getShader().sendFloat("diffuseWrap", 0.4)
hairMat.getShader().sendFloat("scatterStrength", 0.3)

local hair = Renderable3D.create()
hair.setMaterial(hairMat)
// LOD：近距发卡 → 远距代理片
// C++: eve::graphics::hair::configureCardLod(hair, hairMesh, farMesh, 8.0)
hair.setMeshLod(0, hairMesh, 0)
hair.setMeshLod(1, farMesh, 8)
hair.setHair(true)
```

等价手写：

```squirrel
local hairShader = gfx.newHairShader()
local hair = Renderable3D.create()
hair.setMesh(hairCardMesh)
hair.setTexture(hairAlbedo)
hair.setShader(hairShader)
hair.setHair(true)   // Material/标志会带上双面、透明排序等发卡默认值
```

可调 push 参数：`specExp`、`specStrength`、`primaryShift`、`secondaryShift`、`alphaCutoff`、`rimStrength`、`strandDir`（发束方向，全 0 时由顶点自动推导）、`diffuseWrap`、`scatterStrength`。

发骨摆动请用 animation 的 `setupHairChain` / `DynamicBoneSolver`（见 animation 模块），不要在 graphics 里直接依赖骨骼求解器。

### 屏幕空间体积光（尘雾光柱）与体积雾

`vol <- gfx.newVolumetric()`。`setQuality("low"|"medium"|"high")` 控制采样与 `resolutionFor`。

- **screenspace**：`beginOcclusionMap` → `drawOccluders2D` → `scatter`；或 `applyFromScene`。多光源：`Light2D.setVolumetric(true)` 后用 `beginOcclusionMapFromSceneLights2D` / `scatterFromSceneLights2D`。`spot` 灯的体积光柱按 `setDirection` / `setSpotAngle` / `setSpotSoftness` 裁成锥形（与 lit2d 表面锥一致）；`point` 仍为全向径向。
- **raymarch**：`setMode("raymarch")` + `setCamera` + 线性深度 → `rayMarch`；可用 `driveFromLight3D` / `driveFromPrimarySceneLight3D` 自动写入光向与屏坐标，`setAnisotropy` 控制双叶 HG。
- **fog**：`setMode("fog")` + `setFogHeight*` / `setFogStart`/`End` + 线性深度 → `applyFog`（雾色 alpha 叠加场景）
- **froxel**：`configureFroxelGrid` → `clearFroxelGrid` →
  `injectFroxelHeightFog` → `integrateFroxel`（或 `integrateFroxelFromSceneLights` /
  `injectEmissiveLightProxy`）→ `uploadFroxel`；在
  `gfx.render3D()` 后将 GBuffer 线性深度传给 `applyFroxel` 或
  `applyFroxelTo`。介质未变化时不必每帧重新上传。
- **cloud**：`setMode("cloud")`，用 `setCloudLayer`、`setCloudCoverage`、
  `setCloudDensity`、`setCloudScale`、`setCloudWind` 和 `setCloudLightColor`
  调整云层；线性深度输入通过 `renderClouds` 或 `renderCloudsTo` 渲染，
  `getCloudShader` 可用于高级参数检查与调试。

发光体代理：`eve.createEmissiveLight2D` / `eve.createEmissiveLight3D` 创建
`volumetricOnly` 点光——只进体积通道、跳过表面光照；表面自发光仍用 PBR `emissive` + bloom。

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

### AAA 时域反射链（TAA + RTGI + SSR）

`"reflectionChain"` 一次启用 TAA、fullscreen RTGI、PBR 感知 SSR 及其 GBuffer 依赖，并默认关闭重复的硬件 MSAA 成本。基础材质的环境 IBL 是 SSR 未命中、出屏和高粗糙度区域的后备反射；SSR 结果按时域置信度覆盖，避免屏幕边界硬切。

```squirrel
local rc = gfx.getRenderControl();
rc.setPostProcessQuality("high"); // low | medium | high | ultra
rc.enable("reflectionChain");
rc.compile();
```

室内或盒状空间可在相机环境 cubemap 上启用 box-projected reflection probe，使墙面和物体反射对应有限房间而不是无限远天空：

```squirrel
camera.setEnvMap(roomCubemap);
camera.setEnvIntensity(1.0);
camera.setEnvProbe(0, 2, 0, 8, 2, 6); // center xyz, half-extent xyz
```

只有位于 probe AABB 内的 fragment 会修正镜面反射方向；盒外自动使用普通环境方向。`camera.clearEnvProbe()` 恢复无限远 IBL。任一 extent 为零时 probe 视为关闭。

运行时切档会同步更新 TAA、RTGI 与 SSR，并废弃不兼容的历史帧；`ultra` 在 TAA 上映射到其最高 `high` 档，在 RTGI/SSR 上使用原生 `ultra`。关闭 `reflectionChain` 会同步关闭三个子链路；如果需要逐项调试，也可分别启用 `"taa"`、`"rtgi"`、`"ssr"`。

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

### 实验性路径追踪模式

`rc.enable("pathTracing")` 请求正常 `gfx.render3D()` 使用 Vulkan KHR 路径追踪；
`rc.disable("pathTracing")` 恢复光栅路径。该开关与只计算反射的 `rtx` 不同。

实验性的 `rc.enable("pathTemporal")` 在路径追踪开启时启用时间重建，再执行空间降噪；默认关闭。当前支持静态场景的相机重投影，通过世界位置、实例、法线和反照率检查历史，最多保留 32 帧对应的样本置信权重（每帧最多按 16 spp 计），多采样时域模式使用 7×7 几何与反照率引导空间滤波。相机、场景和采样条件完全不变时直接累积同一像素，保留随机透明覆盖及亚像素表面的贡献；相机移动时才执行几何重投影检查。场景、物体变换、材质或光照变化会清空历史，因此尚不支持运动物体的历史复用。低层 `PathTraceView` 使用 `PathDenoise::Temporal`，要求 `historySamples = 0` 和连续 `firstSample`；`resetTemporalHistory` 可显式重置，相机矩阵必须可逆。

当前所有路径模式共用十张存储图像，需要设备每阶段至少支持十个 storage image；1080p 图像本体约 253 MiB，另有场景资源和驱动分配开销。此实验实现尚待复杂遮挡、光泽表面与真实场景验收，不能仅凭小幅相机移动的降噪测试判断没有拖影。
路径输出进入正常 HDR 色调映射，随后合成 2D UI。

每次 `render3D()` 后可查询 `rc.pathTracingFrameState()`：`active` 表示已记录路径帧，
`unavailable` 表示设备或模块不可用，`sceneRejected` 表示当前场景转换不支持，
`disabled` 表示未请求此模式。`unavailable` 和 `sceneRejected` 会使用光栅渲染；原因通过
`rc.pathTracingDiagnostic()` 返回。`active` 不代表 GPU 已完成或达到目标帧率。

当前自动入口支持透视相机、基础不透明／遮罩／普通 alpha 透明材质、共享静态网格、
点光、方向光和聚光灯。透明使用随机表面覆盖，影响主射线、阴影和间接光，低采样下会有噪声；
它不表示玻璃折射。基础颜色／alpha 的 RGBA8 引擎贴图可直接使用，按 sRGB 解码并复制 GPU 快照；
当前只采样基础 mip 级，遵循放大过滤与重复／钳位寻址。基础 PBR 的颜色、金属粗糙度、发光及切线空间法线贴图也可自动转换，保留 UV0 变换与独立采样器。其余纹理角色、
高级 PBR 扩展、折射、GPU 变形、环境贴图及额外几何贡献者仍需补齐；这些场景不会
静默删除不支持的物体。自动入口目前每帧重新捕获场景，采用每像素一次采样、最多
四次反弹和空间降噪。相同快照复用 GPU 场景，仅变换变化时更新 TLAS；网格快照
首次或变化后的网格捕获仍有同步读取成本；未变化的网格按版本复用。时间重投影和完整性能验收尚未完成。
本功能处于开发阶段，不能据此承诺原生 1080p／60 FPS。

C++ 显式场景接口 `PathMaterial` 另支持 `metallicRoughnessTexture` 与 `emissionTexture`。
金属粗糙度贴图使用线性 G/B 通道分别乘粗糙度／金属度；原生数据贴图须设
`srgbDecode=false`。发光贴图 RGB 乘发光因子，alpha 不参与发光颜色；表面覆盖仍由
基础颜色贴图和材质 alpha 决定。发光贴图同时用于表面命中与面积光直接采样。
每个 `PathColorTexture` 可设置 `offset`、`scale`、弧度制 `rotation`，按先缩放、
再旋转、最后平移应用于 UV。上述额外纹理角色已接入自动世界材质转换；当前仅支持 UV0 和默认高级扩展参数。
材质绑定的最近邻／线性过滤及重复／钳位／镜像重复寻址独立于纹理资源自身设置。
C++ 显式纹理输入也可用 `filter`、`addressU`、`addressV` 覆盖源采样器；`Source` 保留源设置，
像素数组的默认设置为线性过滤、重复寻址。当前均只采样基础 mip，尚不进行缩小过滤的 mip 选择。

法线贴图按线性 XYZ 解码，`PathMaterial.normalScale` 作用于切线方向的 X/Y，支持有符号强度。
PBR 的 `normalScale` 和普通材质的法线纹理均接入自动入口。优先使用网格作者提供的
切线／副切线；显式 `PathGeometry` 可传入 `tangents` 与 `bitangents`。未提供时从三角形
变换后的 UV0 构建基底，退化 UV 会被拒绝。当前尚未生成 MikkTSpace 平滑切线，因此缺少作者切线的
复杂网格仍需检查接缝画质。UV 变换用于纹理采样，基底使用作者切线；未提供时采用变换后的 UV0。
实例法线使用逆转置矩阵，切线使用对象变换；阴影和后续射线偏移仍使用几何法线。

C++ 调用 `RenderControl::setPathTracingSamples(samples)` 可设置自动路径渲染每像素每帧 1–16 条独立路径（默认 1），必须检查返回的 `Result<void>`。非法值不改变配置；更改采样数会重置历史，后续帧按实际采样数推进绝对样本编号。接口仅在渲染线程调用，设置本身不提交 GPU 工作；实际光线成本在下一次 `render3D()` 中承担，通常随采样数增长。展厅可通过 `EVE_PT_GALLERY_SPP` 选择测试档位。
