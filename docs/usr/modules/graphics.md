# 图形渲染模块

**脚本入口：** `eve.Graphics()`

清屏、2D 图元、纹理、Canvas、摄像机和 3D 渲染。Camera2D/Camera3D 提供屏幕与世界坐标换算，供 2D/3D 拾取使用；形状命中测试见 [Math](math.md)，物理体查询见 [Physics](physics.md)。

## 基本用法

```squirrel
gfx.setBackgroundColor(0.08, 0.1, 0.16, 1.0);
gfx.clear();
gfx.drawSolidRect(40, 40, 160, 80, 0.2, 0.7, 1.0, 1.0);
```

## 对象关系与调用时机

`Graphics` 管理设备、swapchain、camera、light 和提交；Texture/Shader/Mesh/Renderable 是资源或场景对象。CPU 资源创建在 init，帧内只更新参数并 draw。

## 目标导向指南

### 绘制 2D 游戏帧

在 `eve_render()` 开始调用 `clear()`，随后按背景、地图、角色、粒子、UI 的顺序提交。纯色占位使用 `drawSolidRect()`；已有 Texture 使用 `drawTexturedRect()`。需要围绕矩形中心旋转的精灵可调用 `drawTexturedRectRotated(texture, centerX, centerY, width, height, degrees, r, g, b, a)`；屏幕坐标 Y 轴向下，因此正角度表现为顺时针旋转。正常主循环由引擎负责 present。

需要 UV 动画、旋转、独立混合模式或程序化变换时，使用脚本精灵对象：

```squirrel
local sprite = gfx.newSprite2D();
sprite.setTexture(sheet.getTexture());
sprite.setQuad(quad);
sprite.setPosition(400, 270);
sprite.setScale(2.0, 2.0);
sprite.setRotation(30.0); // degree, rotate around rect center
sprite.setAnchor(0.25, 0.75); // normalized rotation pivot
sprite.setFlip(true, false);  // mirror UV without negative scale
sprite.setBlend("alpha"); // alpha | premultiplied | additive | multiply

// eve_render: clear/draw background first, then submit all live Sprite2D objects
gfx.renderSprites();
```

`Sprite2D` 还提供 size、color、layer、visible、receiveLight、castOcclusion 等属性。
裁边动画通常由 `SpriteAnim.bindSprite(sprite)` 自动调用 `setFrameLayout`，无需游戏代码逐帧修正偏移。
不再使用时调用 `destroy()`；`renderSprites()` 是聚合提交接口，不要再把同一精灵加入另一条 2D 队列，以免重复绘制。

### 渲染带光照的 3D 对象

初始化时创建 mesh、shader 和 renderable，设置 camera、ambient 和 directional light；每帧只更新 transform/material 参数，最后调用 `render3D()`。阴影开关、bias 和 strength 应逐场景调节。

`Camera3D` 默认使用透视投影。等距视图可调用 `setOrthographic(height)`，其中
`height` 是世界空间中的垂直可视范围；`setPerspective()` 恢复透视投影。
`setClipPlanes(near, far)` 配置两种投影共用的近、远裁剪面，并要求
`0 < near < far`。

### 纹理过滤（mipmap / 各向异性 / LOD）

默认 `newTexture` 仍为线性过滤、单级 mip（兼容旧行为）。需要三线性与各向异性时：

```squirrel
// generateMipmaps=true, maxAnisotropy=16, filter/mipmap="linear", lodBias=0
tex <- gfx.newTextureWithSampler(img, false, false, true, 16.0, "linear", "linear", 0.0);
print(tex.getMipmapCount());
gfx.setTextureSampler(tex, "nearest", "none", 1.0, 0.0); // 像素风
print(gfx.getMaxAnisotropy());
```

C++ 侧使用 `TextureCreateInfo::withMipmaps()` / `TextureSampler::anisotropic()`，并通过同名 `setTextureSampler` 重载热更新采样状态（字符串版参数与脚本一致）。`filter` 只接受 `nearest`/`linear`，`mipmap` 只接受 `none`/`nearest`/`linear`，传错会抛异常而不是静默回退。Cubemap（IBL）默认生成完整 mip 链，供 `textureLod` 按粗糙度采样。

几何 LOD：`Renderable3D.setMeshLod(index, mesh, switchDistance)`，`RenderSystem3D` 按相机距离选择网格。

### 材质（Material）与模型部件

把着色方法（`pbr` / `unlit` / `hair` / `custom`）、贴图和 PBR 参数打成一个 `Material`，挂到整模或某个部件：

```squirrel
local mat = gfx.newMaterial();
mat.setShadingModel("pbr");
mat.setAlbedoTexture(albedo);
mat.setNormalTexture(nrm);
mat.setSurfaceMode("transparent"); // opaque | masked | transparent
mat.setBlendMode("alpha");         // alpha | premultiplied | additive | multiply
mat.setDepthWrite(false);
mat.setDoubleSided(true);
mat.setSortPriority(0);             // 同优先级按相机深度从后向前排序
mat.setMetallic(0.2);
mat.setRoughness(0.5);

local r = Renderable3D.create();
r.setMaterial(mat);                 // 整模
// 或多部件：r.setPart(0, "body", bodyMesh, bodyMat);
```

遮罩材质使用 `setSurfaceMode("masked")`、`setAlphaCutoff()` 和
`setAlphaTechnique("cutoff" | "dither" | "coverage")`。对应查询接口为
`getSurfaceMode()`、`getBlendMode()`、`getDepthWrite()`、`getDoubleSided()`、
`getSortPriority()`、`getAlphaCutoff()` 和 `getAlphaTechnique()`。纹理 Alpha 数据可用
`Texture.setAlphaConvention("straight" | "premultiplied")` 声明，并通过
`Texture.getAlphaConvention()` 查询。

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

一帧里各 buffer 谁写谁读、对应函数和 shader，见开发文档 [`3D渲染管线.md`](../../dev/3D渲染管线.md)。

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

细节见 [`体积光模块设计.md`](../../dev/体积光模块设计.md)。

### 屏幕空间环境光遮蔽（SSAO / HBAO / GTAO）

`ao <- gfx.newAmbientOcclusion()`。`setMode("ssao"|"hbao"|"gtao")`，`setQuality` 控制采样与 `resolutionFor`。

1. `setCamera` + 线性深度纹理（与体积雾相同约定）
2. `compute` / `computeTo` → AO 图（RGB=遮蔽因子，A=深度）
3. 可选 `blur` / `blurTo`（双边）
4. `applyOverlay` 以黑 + `alpha=(1-ao)*intensity` 叠到已有场景

3D 默认路径：`RenderControl` 特性 `"ao"`（默认开）会在 forward 之后对 GBuffer 的 D32 + 法线做 `applyFromGBuffer`，不必手动建 Canvas。Canvas 上的 `compute` 仍用 8-bit 线性深度。

细节见 [`环境光遮蔽模块设计.md`](../../dev/环境光遮蔽模块设计.md)。

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

3D 默认路径：`begin3DFrame` 画到可采样的 scene color（`"msaa"` 开时先 Nx 多重采样再 resolve 到 1x），present 时按 `RenderControl "aa"`（默认开）做 FXAA resolve 再叠 AO/HUD。手动 Canvas 路径仍然可用。细节见 [`抗锯齿模块设计.md`](../../dev/抗锯齿模块设计.md)。

### 2D 屏幕拾取

`Camera2D` 用 `setPosition` / `setZoom` 控制视口中心与缩放；`screenToWorldX/Y(screenX, screenY, viewW, viewH)` 把鼠标像素换成世界坐标（`viewW/H` 通常取 `gfx.getWidth/Height`），再交给 Math 的 `pointIn*` 或 Physics 的 `testPoint` / `queryAABB`。

### 3D 屏幕拾取

`Camera3D.screenToRay(screenX, screenY, viewW, viewH)` 写入眼点与单位方向，用 `getScreenRayOrigin*` / `getScreenRayDir*` 读取，再对包围球/盒调用 Math 的 `raycastSphere` / `raycastBox`。

### 曲面瀑布

`Waterfall.createCurvedSheet(width, height, segX, segY, curveDepth, lipOverhang)` 创建带横向弧度和顶部探出段的细分瀑布网格，适合贴合崖壁并形成自然的离壁水帘；原有 `createSheet(width, height)` 仍用于平面瀑布。

```squirrel
local fall = gfx.newWaterfall();
fall.createCurvedSheet(3.0, 7.0, 28, 48, 0.75, 0.85);
```

## 常见问题

- 忘记每帧 `clear()`，保留未定义的旧帧内容。
- 每帧编译 shader 或上传纹理。
- 2D/UI/3D 提交顺序错误导致覆盖。
- 拾取时 `viewW/H` 与实际渲染 drawable 不一致，射线会偏。

## API 快查

`drawScene3DRGBA(x, y, w, h, r, g, b, a)` 把最近一次 `render3D()` 产生的正式
场景颜色复合到当前目标（交换链或 `Canvas`）。它适合自定义编辑器把与游戏相同的场景
呈现在任意 Viewport 中；与 `renderScene3DToCanvas` 的独立预览渲染不同，它复用完整
运行时场景管线。

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `bakeMeshMorph()`、`newMeshFromArrays()`、`updateMeshVertices()`、`clear()`、`clearMorphWeights()`、`declareFloat()`、`declareMatrix()`、`declareVec2()`、`declareVec3()`、`declareVec4()`
- `drawSolidRect()`、`drawTexturedRect()`、`drawTexturedRectRotated()`、`drawOcclusionSolid()`、`drawOcclusionTexture()`、`getCastShadow()`、`getCastOcclusion()`、`getDirX()`、`getDirY()`、`getDirZ()`、`getHeight()`、`getMorphCount()`
- `getMorphName()`、`getMorphWeight()`、`getName()`、`getRadius()`、`getScreenRayDirX()`、`getScreenRayDirY()`、`getScreenRayDirZ()`、`getScreenRayOriginX()`
- `getScreenRayOriginY()`、`getScreenRayOriginZ()`、`getShader()`、`getShadowBias()`、`getShadowStrength()`、`getType()`、`getUniformIndex()`、`getVertexCount()`、`getIndexCount()`
- `getVolumetric()`、`getVolumetricIntensity()`、`getWidth()`、`getX()`、`getY()`、`getYaw()`、`getZ()`、`getZoom()`、`hasMorph()`、`hasMorphData()`
- `hasUniform()`、`isEnabled()`、`isMorphDirty()`、`newHairShader()`、`newMeshCylinder()`、`newMeshShader()`、`newMeshShaderVF()`、`newMeshSphere()`、`newQuad()`、`newShader()`
- `newShaderFromSpvFile()`、`newTexture()`、`newTextureWithSampler()`、`setTextureSampler()`、`getMaxAnisotropy()`、`newVolumetric()`、`newAmbientOcclusion()`、`newGlobalIllumination()`、`newAntiAliasing()`、`setMsaaSamples()`、`getMsaaSamples()`、`present()`、`render3D()`、`reset()`、`screenToRay()`、`screenToWorldX()`、`screenToWorldY()`
- `sendFloat()`、`sendVec2()`、`sendVec3()`、`sendVec4()`、`setActive()`、`setAmbient()`、`setBackgroundColor()`、`setCamera()`
- `setCanvas()`、`setCastOcclusion()`、`setCastShadow()`、`setCloudShadows()`、`setColor()`、`setDirection()`、`setDirectionalLight()`、`setEnabled()`、`setEnvIntensity()`、`setEnvMap()`
- `setEye()`、`setFov()`、`setMesh()`、`getMesh()`、`setMeshLod()`、`clearMeshLod()`、`getMeshLodCount()`、`getMeshLodLevelAtDistance()`、`setMetallic()`、`setMorphWeight()`、`setNormalTexture()`、`setHeightTexture()`、`setPosition()`、`setRadius()`
- `setReceiveLight()`、`setReceiveShadow()`、`setRotation()`、`setRoughness()`、`setScale()`、`setShader()`、`setHair()`、`getHair()`、`setShadowBias()`、`setShadowStrength()`
- `setTarget()`、`setTexCellBomb()`、`getTexCellBombScale()`、`getTexCellBombStrength()`、`getTexCellBombRotation()`、`setParallax()`、`getParallaxScale()`、`getParallaxMinLayers()`、`getParallaxMaxLayers()`、`setTexture()`、`setTint()`、`setType()`、`setUp()`、`setViewport()`、`setVisible()`、`setVolumetric()`、`setVolumetricIntensity()`、`setYaw()`
- `setZoom()`、`worldToScreenX()`、`worldToScreenY()`、`Texture.getMipmapCount()`
- 字体：`newFont()`、`setFont()`、`getFont()`、`getAscent()`、`getBaseline()`、`hasGlyph()`
- `AlphaMask`：`newAlphaMask()`、`setThreshold()`、`getThreshold()`、`setSoftness()`、`getSoftness()`、`setInverted()`、`getInverted()`
- `Sprite2D`：`setPosition()`、`getX()`、`getY()`、`setRotation()`、`getRotation()`、`setScale()`、`getScaleX()`、`getScaleY()`、`setSize()`、`getWidth()`、`getHeight()`、`setTexture()`、`getTexture()`、`setQuad()`、`getQuad()`、`setColor()`、`setLayer()`、`getLayer()`、`setVisible()`、`getVisible()`、`setReceiveLight()`、`getReceiveLight()`、`setBlend()`、`getBlend()`、`setAnchor()`、`getAnchorX()`、`getAnchorY()`、`setFlip()`、`getFlipX()`、`getFlipY()`、`setFrameLayout()`、`setCastOcclusion()`、`getCastOcclusion()`、`destroy()`
- `Volumetric`：`setQuality`、`setMode`、`scatter`、`applyFromScene`、`rayMarch`、`applyFog`、`setFogHeight`、`setFogStart`、`setFogEnd`、`setCamera`、`setLightDirection`、`setDensity` 等
- `FogVolume`：`setShape/getShape`、`setPosition`、`setSize`、`setExtinction/getExtinction`、`setAlbedo`、`setEmissive`、`setAnisotropy/getAnisotropy`、`setEdgeFalloff/getEdgeFalloff`；调用 `Volumetric.setCamera()` 后，通过 `injectFroxelLocalVolume(volume)` 按当前视锥注入 froxel 网格。
- `Volumetric` froxel：`configureFroxelGrid`、`clearFroxelGrid`、
  `injectFroxelHeightFog`、`integrateFroxel`、`uploadFroxel`、
  `applyFroxel`、`applyFroxelTo`
- `AmbientOcclusion`：`setQuality`、`setMode`、`setCamera`、`setRadius`、`setBias`、`setIntensity`、`setPower`、`compute`、`blur`、`applyOverlay`、`applyFromDepth`、`resolutionFor` 等
- `GlobalIllumination`：`setQuality`、`setCamera`、`setRadius`、`setIntensity`、`setLightDirection`、`setLightColor`、`applyFromDepth`、`getSampleCount` 等
- `AntiAliasing`：`setQuality`、`setMode`、`apply`、`applyTo`、`applyCanvas`、`applyCanvasTo`、`suggestScale`、`resolutionFor`、`setFloat`、`getFloat` 等

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/graphics/`](../../../src/modules/graphics/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `graphics`、`Camera2D`、`Camera3D`。
