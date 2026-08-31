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

`drawText(font, text, x, y, r, g, b, a, scale)` 在当前屏幕或 Canvas 坐标的任意
位置绘制 UTF-8 文本，`(x, y)` 是该行左上角。`font` 必须是 `newFont()` 返回的字体；
绘制调用不读取或修改当前字体状态。原有的 `print(text, x, y, r, g, b, a, scale)`
继续保留，并使用 `setFont()` 选择的当前字体。

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

需要把运行对象交给 Editor/MCP 等跨帧工具时，同时读取
`Renderable3D.getEntityId()` 与 `getEntityGeneration()`；两者共同构成临时 ECS handle，
generation 用于拒绝实体销毁后被复用的旧 id。不要只长期保存 entity id。
Agent 完成材质事务后，可用 `getTintR()`、`getTintG()`、`getTintB()` 与 `getRoughness()`
独立核对字段材质的运行时状态。

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
r.setPartSortPriority(0, 100);      // 每实例覆盖透明排序，不修改共享 Material
r.clearPartSortPriority(0);         // 恢复使用 Material.getSortPriority()
local effectiveOrder = r.getPartSortPriority(0);
```

遮罩材质使用 `setSurfaceMode("masked")`、`setAlphaCutoff()` 和
`setAlphaTechnique("cutoff" | "dither" | "coverage")`。对应查询接口为
`getSurfaceMode()`、`getBlendMode()`、`getDepthWrite()`、`getDoubleSided()`、
`getSortPriority()`、`getAlphaCutoff()` 和 `getAlphaTechnique()`。纹理 Alpha 数据可用
`Texture.setAlphaConvention("straight" | "premultiplied")` 声明，并通过
`Texture.getAlphaConvention()` 查询。

虚拟纹理材质通过 `setVirtualTexture(albedoAtlas, normalAtlas, pageTable,
pageCountX, pageCountY, borderFraction)` 显式启用 atlas/page-table 路径；
`usesVirtualTexture()` 可查询当前模式，`clearVirtualTexture()` 恢复传统材质贴图路径。
三个纹理对象必须至少存活到材质停止使用它们为止；
参数校验失败时 `setVirtualTexture()` 会抛出包含诊断信息的脚本异常。

### 高级渲染与屏幕空间效果

GBuffer、毛发、体积光、AO、GI 与抗锯齿集中在 [高级渲染与屏幕空间效果](graphics/rendering-effects.md)，避免各类渲染特性持续修改本概览。

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

## Agent 运行时观察

`eve_renderable3d_get` 使用完整 ECS `entityId` + `generation` 读取 live Renderable3D 的 transform、
field-backed PBR 参数与资源占用标志；陈旧 identity 返回结构化 `stale`。
`eve_editor_execute_observe` 的默认 `renderable3d` observer 在写入前验证同一 identity，再通过
Editor 事务修改目标，并在一个响应中返回 live `before/after`、事务回执和 Editor snapshot。传入
`expect` JSON 子集和可选 `tolerance` 后，引擎还会返回 `converged`、`maxError` 与不匹配字段路径，
Agent 可直接决定是否继续纠正；无效期望会在写入前拒绝。同一协议还支持 `scene-node` observer，
因此 Agent 不必为场景和渲染对象维护两套调用编排。

## API 快查

`drawScene3DRGBA(x, y, w, h, r, g, b, a)` 把最近一次 `render3D()` 产生的正式
场景颜色复合到当前目标（交换链或 `Canvas`）。它适合自定义编辑器把与游戏相同的场景
呈现在任意 Viewport 中；与 `renderScene3DToCanvas` 的独立预览渲染不同，它复用完整
运行时场景管线。

`updateTextureFromImageData(texture, imageData)` 将尺寸相同的 RGBA8 `ImageData`
重新上传到由当前 Graphics 后端创建的已有纹理；纹理对象保持不变，可继续被材质引用。
该方法必须在渲染线程调用，后端所有权、格式或尺寸不匹配时抛出异常。

C++ 渲染适配器可调用 `updateTextureRegion(texture,x,y,width,height,rgba,bytesPerRow)`
更新单 mip RGBA8 纹理的子矩形。Vulkan 使用 staging buffer 和原图 `copyBufferToImage`，
WebGPU 使用带 origin 的 `WriteTexture`；两者都不重建 Texture、采样器或描述符。
多个离散矩形应使用 `updateTextureRegions(texture, regions)`：它在任何写入前验证整个
批次；Vulkan 将所有 region 紧密打包进一个 staging buffer 并只提交一次。

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `bakeMeshMorph()`、`newMeshFromArrays()`、`updateMeshVertices()`、`clear()`、`clearMorphWeights()`、`declareFloat()`、`declareMatrix()`、`declareVec2()`、`declareVec3()`、`declareVec4()`
- `drawSolidRect()`、`drawTexturedRect()`、`drawTexturedRectRotated()`、`drawOcclusionSolid()`、`drawOcclusionTexture()`、`getCastShadow()`、`getCastOcclusion()`、`getDirX()`、`getDirY()`、`getDirZ()`、`getHeight()`、`getMorphCount()`
- `getMorphName()`、`getMorphWeight()`、`getName()`、`getRadius()`、`getScreenRayDirX()`、`getScreenRayDirY()`、`getScreenRayDirZ()`、`getScreenRayOriginX()`
- `getScreenRayOriginY()`、`getScreenRayOriginZ()`、`getShader()`、`getShadowBias()`、`getShadowStrength()`、`getType()`、`getUniformIndex()`、`getVertexCount()`、`getIndexCount()`
- `getVolumetric()`、`getVolumetricIntensity()`、`getWidth()`、`getX()`、`getY()`、`getYaw()`、`getZ()`、`getZoom()`、`hasMorph()`、`hasMorphData()`
- `hasUniform()`、`isEnabled()`、`isMorphDirty()`、`newHairShader()`、`newMeshCylinder()`、`newMeshShader()`、`newMeshShaderVF()`、`newMeshSphere()`、`newQuad()`、`newShader()`
- `newShaderFromSpvFile()`、`newTexture()`、`newTextureWithSampler()`、`updateTextureFromImageData()`、`setTextureSampler()`、`getMaxAnisotropy()`、`newVolumetric()`、`newAmbientOcclusion()`、`newGlobalIllumination()`、`newAntiAliasing()`、`setMsaaSamples()`、`getMsaaSamples()`、`present()`、`render3D()`、`reset()`、`screenToRay()`、`screenToWorldX()`、`screenToWorldY()`
- `sendFloat()`、`sendVec2()`、`sendVec3()`、`sendVec4()`、`setActive()`、`setAmbient()`、`setBackgroundColor()`、`setCamera()`
- `setCanvas()`、`setCastOcclusion()`、`setCastShadow()`、`setCloudShadows()`、`setColor()`、`setDirection()`、`setDirectionalLight()`、`setEnabled()`、`setEnvIntensity()`、`setEnvMap()`
- `setEye()`、`setFov()`、`setMesh()`、`getMesh()`、`setMeshLod()`、`clearMeshLod()`、`getMeshLodCount()`、`getMeshLodLevelAtDistance()`、`setMetallic()`、`setMorphWeight()`、`setNormalTexture()`、`setHeightTexture()`、`setPosition()`、`setRadius()`
- `setReceiveLight()`、`setReceiveShadow()`、`setRotation()`、`setRoughness()`、`setScale()`、`setShader()`、`setHair()`、`getHair()`、`setShadowBias()`、`setShadowStrength()`
- `setTarget()`、`setTexCellBomb()`、`getTexCellBombScale()`、`getTexCellBombStrength()`、`getTexCellBombRotation()`、`setParallax()`、`getParallaxScale()`、`getParallaxMinLayers()`、`getParallaxMaxLayers()`、`setTexture()`、`setTint()`、`setType()`、`setUp()`、`setViewport()`、`setVisible()`、`setVolumetric()`、`setVolumetricIntensity()`、`setYaw()`
- `setZoom()`、`worldToScreenX()`、`worldToScreenY()`、`Texture.getMipmapCount()`
- 字体：`newFont()`、`setFont()`、`getFont()`、`drawText()`、`print()`、`getAscent()`、`getBaseline()`、`hasGlyph()`
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
### HDR 与反射探针绑定

下列绑定用于 HDR 图像、反射探针采集/注册、天空环境、曝光、Bloom 与反射链质量控制：

`applyConfiguredToCamera`、`applyToCamera`、`clearReflectionProbe`、`configureInfluence`、`filterAndPublish`、`getActiveCubemap`、`getAdaptiveFaceBudget`、`getAdaptiveFilterSamples`、`getBloomIntensity`、`getBloomThreshold`、`getCaptureClusteredLighting`、`getCaptureFarDistance`、`getCaptureLodDistanceScale`、`getCaptureMask`、`getCaptureTransparent`、`getCenterX`、`getCenterY`、`getCenterZ`、`getCount`、`getEnvProbeCenterX`、`getEnvProbeCenterY`、`getEnvProbeCenterZ`、`getEnvProbeExtentX`、`getEnvProbeExtentY`、`getEnvProbeExtentZ`、`getEnvironmentLighting`、`getEnvironmentLightingIntensity`、`getExposure`、`getFaceCanvas`、`getGpuBudgetMs`、`getInfluenceBlendDistance`、`getInfluenceExtentX`、`getInfluenceExtentY`、`getInfluenceExtentZ`、`getInfluenceIntensity`、`getInfluencePriority`、`getLastCandidateCount`、`getLastCapturedFaceCount`、`getLastFilterSampleCount`、`getLastPublishedCount`、`getLastSelectedCount`、`getMaxRoughness`、`getPendingFaceCount`、`getPostProcessQuality`、`getPublishedRevision`、`getReflectionCaptureEnabled`、`getReflectionCaptureMask`、`getReflectionProbeCount`、`getReflectionQuality`、`getRefreshInterval`、`getResolution`、`getResolutionScale`、`getRevision`、`getSelectionHysteresis`、`getSkyB`、`getSkyFaceColor`、`getSkyFaceTexture`、`getSkyFaceTextureScale`、`getSkyG`、`getSkyIntensity`、`getSkyR`、`getSmoothedGpuDurationMs`、`getStagedRevision`、`getStagingCubemap`、`getThickness`、`getTotalCapturedFaceCount`、`getUpdateMode`、`hasEnvProbe`、`isAutoExposure`、`isCaptureComplete`、`isCapturePending`、`isRecaptureQueued`、`newHDRImageData`、`newReflectionProbeCapture`、`newReflectionProbeRegistry`、`queueCapture`、`queueCaptureAABB`、`remove`、`reportGpuDurationMs`、`requestCapture`、`setAutoExposure`、`setBloom`、`setCaptureClusteredLighting`、`setCaptureLodDistanceScale`、`setCaptureMask`、`setCaptureTransparent`、`setEnvironmentLighting`、`setExposure`、`setGpuBudgetMs`、`setMaxRoughness`、`setReflectionCaptureEnabled`、`setReflectionCaptureMask`、`setReflectionProbe`、`setReflectionQuality`、`setRefreshInterval`、`setResolutionScale`、`setSelectionHysteresis`、`setSkyColor`、`setSkyFaceColor`、`setSkyFaceTexture`、`setSkyFaceTextureScale`、`setUpdateMode`、`stageCapturedFaces`、`tick`、`tickAdaptive`、`updateCamera`。
