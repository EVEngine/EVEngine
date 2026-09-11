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
`0 < near < far`。`getEyeX/Y/Z` 与 `getTargetX/Y/Z` 读取世界空间眼点和注视点；
`getFov()` 返回垂直视野（度），与 `setFov()` 对应。

### 跨帧绘制 3D 基础图形

调试可视化、编辑器辅助线和玩法范围提示应使用持久 `Primitive3D`，不需要保留帧内
Canvas。Graphics 是图形场景的唯一所有者；脚本对象只保存带 owner、index 和 generation
的句柄。对象被移除、Graphics 销毁或槽位复用后，旧代理会通过 `isStale()` 明确失效。

```squirrel
local created = gfx.newPrimitiveLine3D(
    0.0, 0.0, 0.0, 4.0, 1.0, -2.0, 1.0, 0.7, 0.1, 1.0, 3.0);
if (created.ok) {
    debugLine <- created.value;
    debugLine.setDash(12.0, 8.0, 0.0, "screen"); // screen | world
    debugLine.setDepthMode("test");         // test-write | test | ignore
}

local sphereResult = gfx.newPrimitiveSphere3D(0.0, 1.0, -4.0, 1.5, 0.2, 0.8, 1.0, 1.0, 2.0);
local boxResult = gfx.newPrimitiveAabb3D(-1.0, 0.0, -1.0, 1.0, 2.0, 1.0, 1.0, 1.0, 0.0, 1.0, 2.0);
```

`newPrimitiveLine3D(...)`、`newPrimitiveSphere3D(...)` 和
`newPrimitiveAabb3D(...)` 返回统一 Result 表；成功后的 `value` 是 owned `Primitive3D`。
代理提供 `setVisible(bool)`、`setColor(r,g,b,a)`、`setLineWidth(width)`、
`setDash(draw,gap,phase,space)`、`clearDash()`、`setDepthMode(mode)`、`remove()` 和
`setObjectId(id)`、`isStale()`；`ownership()` 固定返回 `"owned"`。所有修改和移除操作都返回 Result，必须检查 `ok` 或显式忽略；代理析构会
移除仍存活的图形。

持久图形还提供以下创建入口，参数末尾均为 `r,g,b,a,width`（浮点数），返回相同 Result/owned proxy：

| 方法 | 几何参数（在颜色和线宽之前） |
| --- | --- |
| `newPrimitiveDisk3D` | `x,y,z,nx,ny,nz,radius` |
| `newPrimitiveCylinder3D` | `ax,ay,az,bx,by,bz,radius` |
| `newPrimitiveCapsule3D` | `ax,ay,az,bx,by,bz,radius` |
| `newPrimitiveCone3D` | `x,y,z,dx,dy,dz,height,radius` |
| `newPrimitiveArrow3D` | `ax,ay,az,bx,by,bz,headLength,headRadius` |
| `newPrimitivePolyline3D` | `xyz,closed`；xyz 为至少两个点的扁平浮点数组，闭合至少三个点 |
| `newPrimitiveObb3D` | `xyz`；中心和三个 halfAxis 共 12 个浮点数 |
| `newPrimitiveFrustum3D` | `xyz`；8 个角点，共 24 个浮点数，顺序与 C++ Frustum 一致 |
| `newPrimitiveGrid3D` | `xyz,cellsU,cellsV`；原点、axisU、axisV 共 9 个浮点数，格数为 1–4096 的整数 |
| `newPrimitiveArc3D` | `xyz,radius,start,sweep`；中心、法线、起始方向共 9 个浮点数，角度为弧度 |

这些图形默认描边；`setPaintMode("fill"|"stroke"|"fill-stroke")` 切换填充方式。
代理支持 `setWidthSpace("screen"|"world")`、`setLineCap("butt"|"square"|"round")`、
`setLineJoin("miter"|"bevel"|"round")`、`setCullMode("none"|"back"|"front")`、
`setBlendMode("alpha"|"opaque"|"additive"|"premultiplied"|"multiply")`。
`setTransform(elements)` 接受 16 个浮点数的列主序矩阵，整体替换局部到世界变换。
`setLayer(layer)` 设置非负 uint32 排序层。
`gfx.setPrimitiveTransforms3D(proxies,matrices)` 批量替换同一 Graphics 所属图形的变换。
`proxies` 为 Primitive3D 数组；`matrices` 是每对象 16 个浮点数的扁平列主序矩阵数组。
成功返回 Result 表与 `count`；任一代理失效、类型错误、owner 不匹配或矩阵非法时整批不修改。
同一代理重复出现时最后一个矩阵生效；空批次成功且 count 为 0。最多接受 65536 个对象。
`setPolyline(xyz,closed)` 将代理几何替换为新的 owning 折线，保留画笔、变换、可见性和句柄。
所有设置返回 Result；非法模式、矩阵长度或失效代理不会修改原图形。
上述操作在 VM 所在线程同步执行，不保留调用方数组，不调用外部回调。

### 实时编辑与替换 Shader

开发工具可保留已有 `Shader` 对象及其材质引用，仅替换内部后端资源：

```squirrel
local result = gfx.replaceShaderFromGlsl(shader, vertexSource, fragmentSource);
// Vulkan SPIR-V：gfx.replaceShaderFromSpv(shader, vertexWords, fragmentWords)
// words 数组须为 uint32 范围整数；先用 spirv-val 验证字节码。返回 Result，失败保留旧管线。
// WebGPU 后端使用：gfx.replaceShaderFromWgsl(shader, vertexSource, fragmentSource)
if (!result.ok) {
    print(result.diagnostics);
}
```

这些接口都返回统一的结构化 `Result`，调用方必须检查 `ok` 或显式忽略结果。编译或
管线创建失败时，原 `Shader` facade、已声明 uniform 以及上一份可用管线保持不变，
因此编辑器可继续显示最后一次成功的效果。空的 vertex source 表示沿用引擎默认顶点
阶段；调用必须发生在 Graphics 所属的渲染线程。GLSL 是 Vulkan 开发路径，WGSL 是
WebGPU 开发路径；当前后端不支持对应源码格式时会返回 `Unsupported` 和诊断信息。

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

### 风格化水体配置

`Water.applyConfigJson(json)` 使用 `eve.graphics.stylized-water` 版本化 schema 一次性校验并应用深浅水色、波浪、泡沫、透明度、折射、反射和焦散参数；失败时保留原配置。`Water.configJson()` 返回当前配置的规范 JSON，可用于编辑器属性面板、预设保存和运行时复制。

```squirrel
local water = gfx.newWater();
water.createPlane(14.0, 14.0, 64, 64);
local current = water.configJson();
water.applyConfigJson(current);
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
- `drawSolidRect()`、`drawTexturedRect()`、`drawTexturedRectRotated()`、`drawOcclusionSolid()`、`drawOcclusionTexture()`、`getCastShadow()`、`getCastOcclusion()`、`getDirX()`、`getDirY()`、`getDirZ()`、`getEyeX()`、`getEyeY()`、`getEyeZ()`、`getFov()`、`getHeight()`、`getMorphCount()`
- `getMorphName()`、`getMorphWeight()`、`getName()`、`getRadius()`、`getScreenRayDirX()`、`getScreenRayDirY()`、`getScreenRayDirZ()`、`getScreenRayOriginX()`
- `getScreenRayOriginY()`、`getScreenRayOriginZ()`、`getShader()`、`getShadowBias()`、`getShadowStrength()`、`getType()`、`getUniformIndex()`、`getVertexCount()`、`getIndexCount()`
- `getTargetX()`、`getTargetY()`、`getTargetZ()`、`getVolumetric()`、`getVolumetricIntensity()`、`getWidth()`、`getX()`、`getY()`、`getYaw()`、`getZ()`、`getZoom()`、`hasMorph()`、`hasMorphData()`
- `hasUniform()`、`isEnabled()`、`isMorphDirty()`、`newHairShader()`、`newMeshCylinder()`、`newMeshShader()`、`newMeshShaderVF()`、`newMeshSphere()`、`newQuad()`、`newShader()`
- `newShaderFromSpvFile()`、`replaceShaderFromGlsl()`、`replaceShaderFromWgsl()`、`newTexture()`、`newTextureWithSampler()`、`updateTextureFromImageData()`、`setTextureSampler()`、`getMaxAnisotropy()`、`newVolumetric()`、`newAmbientOcclusion()`、`newGlobalIllumination()`、`newAntiAliasing()`、`setMsaaSamples()`、`getMsaaSamples()`、`present()`、`render3D()`、`reset()`、`screenToRay()`、`screenToWorldX()`、`screenToWorldY()`
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
- `FogVolume`：`setShape/getShape`、`setPosition`、`setSize`、`setExtinction/getExtinction`、`setAlbedo`、`setEmissive`、`setAnisotropy/getAnisotropy`、`setEdgeFalloff/getEdgeFalloff`。`setNoise(amount, scale, seed)` 可加入确定性的世界空间密度变化，`getNoiseAmount/getNoiseScale/getNoiseSeed` 返回当前设置；调用 `Volumetric.setCamera()` 后，通过 `injectFroxelLocalVolume(volume)` 按当前视锥注入 froxel 网格。
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

### 预编译网格 Shader 与画布快照

`gfx.loadMeshShaderSpv(vertexPath, fragmentPath)` 从 VFS 加载 Vulkan SPIR-V，返回统一 Result。
空 vertexPath 使用默认 Mesh3D 顶点着色器；fragmentPath 必填。成功后 `value` 是 Graphics
所有的借用 Shader，只在 Graphics 生命周期内使用。调用者须预先编译匹配 Mesh3D 布局的程序。
加载失败返回诊断，不修改已有材质。仅在渲染/VM owner 线程同步调用。

`canvas.readPixels()` 返回独立 RGBA8 ImageData 的 Result；成功时脚本拥有 `value`，
之后修改或清空画布不影响快照。调用前先提交离屏绘制批次；此操作等待 GPU，适合验证、
导出，不适合每帧执行。失败返回诊断，不改变画布。HDR 使用既有独立接口。

Vulkan RGBA8 画布在分次提交之间保留像素，显式 clear 在下一次绘制提交时执行。
参考 `examples/ink-arena`，可用片元着色器和 alpha 混合在 GPU 上累积表面墨迹。

### 自定义网格资源和光栅状态

以下接口同步返回统一 Result，必须检查 `ok`。仅在 Graphics 所在线程、帧提交之外
调用，Shader 仍由 Graphics 所有；失败保留此前已提交的程序、资源和状态。
当前 Vulkan 提供实现，其他后端返回 Unsupported。

`gfx.replaceMeshShaderResourcesFromFiles(shader, vertexPath, fragmentPath, images, constantsPath)`
事务式替换普通网格 Shader 与独立 set 1 资源。vertexPath 为空使用默认顶点程序；
constantsPath 可为空。images 每项包含 `binding, path, format, dimension, width, height,
layers, mips, filter, wrapU, wrapV, anisotropy`。格式支持 `r8-unorm`、`rg8-unorm`、
`r16-unorm`、`rgba8-unorm`、`bgra8-unorm` 以及 RGBA/BGRA/BC1/BC3/BC7 对应
`-srgb` 格式；BC1/3/7 也支持 `-unorm`。dimension 为 `2d/array2d/cube`。
字节严格按 layer-major、mip-major 紧密排列，不经过图片解码、预乘或缩放。
filter 为 0 最近、1 双线性加最近 mip、2 三线性；wrapU/V 为 0 repeat、1 clamp。
Cube 必须是六个等宽高面；最多 16 个图像，binding 在 0..31，常量使用 binding 32，
16 字节对齐且最多 64 KiB。输入在调用内复制到 GPU 所有的状态，文件缓冲不跨帧借用。
普通 SPIR-V 重载会继续校验该资源布局。

`gfx.configureMeshShaderSurface(shader, blend, depthWrite, doubleSided)` 配置混合、
深度写入和剔除。blend 支持 `opaque/alpha/premultiplied/additive/multiply`。
`gfx.configureMeshShaderRaster(shader, compare, constantBias, slopeBias, colorMask)` 配置
深度比较、原生深度偏移和颜色写入。compare 支持 `less/lessEqual/always`；
colorMask 的位 0..3 对应 RGBA，7 表示仅 RGB。两个接口独立更新各自的状态，
状态在程序重载、资源替换和目标重建后保留。不适用于专用 hair/X-ray 管线。
偏移参数属于原生深度缓冲单位，跨后端不承诺相同数字产生逐像素相同结果。


### 自定义 Mesh Shader 实例资源

`replaceInstancedMeshShaderResourcesFromFiles(shader, vertex, fragment, images, constants, instances)`
与 `replaceMeshShaderResourcesFromFiles` 共享纹理/常量校验，并读取不可变实例矩阵文件。
`instances` 是小端 float32、列主序 mat4 记录（每条 64 字节，最多 64 MiB），必须有限、
仿射且可逆；上传复制到 Graphics 拥有的资源，调用者无需跨帧保留输入内存。
顶点程序可使用 set 1 / binding 33 的 `readonly buffer { mat4 transforms[]; }`，
其他可写或片段阶段存储缓冲不被接受。重载保留相同矩阵布局验证，失败不替换旧资源。

`drawMeshShaderInstances(mesh, shader, model, first, count)` 返回结构化 Result，必须检查 `ok`。
`model` 是 16 个列主序浮点值；`first`/`count` 选择已上传矩阵范围，顶点 Shader 的
`gl_InstanceIndex` 包含 first。零 count 为已校验的空操作；越界在绘制前拒绝。
仅在渲染线程的已开启 3D pass 内调用，不保留调用者引用、不调用脚本回调。
当前 Vulkan 实现该路径，其他后端明确返回 Unsupported；没有静默逐实例 CPU 绘制。
资源由 Graphics 统一释放，实例绘制本身不提供阴影、剔除或 ECS 注册，调用者按其
场景规则提交可见范围。小型 GPU 探针与地图装饰的集成证据分别记录，不能相互替代。

`Renderable3D.setInstanceRange(first, count, minimum, maximum, maximumHorizontalDistance)`
为场景对象配置值所有的实例范围并返回 Result；失败保留旧范围。
minimum/maximum 是实例矩阵输出空间中的 float3 包围盒，随后应用对象变换。
零距离禁用水平距离裁剪；正距离加上世界包围盒 X/Z 最大半径，与相机 X/Z
距离比较。主场景和离屏场景均执行 Vulkan 零到一深度的视锥裁剪。
`clearInstanceRange()` 恢复普通对象提交。范围元数据不持有资源指针，不是持久格式；
调用者在恢复场景时重新配置。更新必须在场景更新/渲染所属线程、绘制遍历以外执行。

当前仅支持单个静态自定义 Mesh Shader，使用 forward 绘制；调用者必须关闭
材质/对象投射阴影及对象遮挡投射，不支持 parts、蒙皮、LOD、hair 或 Xray。
设置时检查这些条件，后续改变对象能力时渲染阶段也检查，避免静默绘错。
实例缓冲实际容量仍由绘制接口校验。该路径读取既有 Renderable3D/Transform3D/
MeshRenderer 数据，不新增 ECS System 或在遍历中改变实体结构。

Vulkan 资源 Mesh Shader 的静态顶点输入允许 location 5 的 `vec4 tangent`：
xyz 是导入的对象空间切线，w 是相对于 `cross(normal, tangent)` 的副切线手性。
没有导入切线的网格提供零向量（w=0），消费者必须明确处理缺失数据；引擎不隐式
生成近似切线。带场景变换的导入会变换并归一化切线/副切线，保留镜像手性。
该输入目前属于 Vulkan 资源 Shader ABI；未实现此资源路径的后端仍返回 Unsupported。

`gfx.setSceneToneMapping("none" | "aces")` 返回 Result；默认 `aces` 保持既有输出。
`gfx.getSceneToneMapping()` 返回当前模式。`none` 仅在最终场景显示时将曝光后的
线性 RGB 裁剪到显示范围，然后执行需要的 sRGB 编码，不应用 ACES 曲线。
Bloom、曝光和 HDR 离屏纹理保持各自的职责；二维 UI 不经过此场景映射。
设置属于 Graphics 的显示状态，在 graphics/render 线程修改，不持有调用者引用
或触发回调。非法模式不改变旧值。Vulkan 支持两种模式；其他后端当前仅接受保留
默认模式的空操作，切换返回 Unsupported，不静默使用不同映射。
# Gaussian scatter bloom

`gfx.setBloomFilter("gaussianScatter", 0.68, 6, 65472.0)` returns a checked Result
and selects half-resolution prefiltering, separable Gaussian downsampling, and
linear interpolation between pyramid levels. `"karisTent"` restores the default
four-level filter. `gfx.getBloomFilter()` returns the selected name.
Camera bloom intensity and linear threshold remain controlled by `Camera3D.setBloom`.
Scatter must be finite in [0,1], maximum iterations an integer in [1,16], and
clamp finite in (0,65504]. Invalid settings leave the previous filter unchanged.
Settings are copied, render-thread only, with no callbacks or retained script
references. Graphics owns generated shaders/targets; targets allocate on the next
build. Both GLSL and WGSL implementations exist; backend runtime validation status
is recorded by the relevant test run, not implied by source availability.

### 资源纹理内部共享

`ShaderImageInput::contentOwner` 可携带不可变文件快照身份。相同快照、尺寸、格式、mip/层数及
完整采样器配置复用 GPU 图像；无身份则独立上传。调用者必须保证该身份的字节永不修改。
GPU 仅持有弱身份，shader 共同拥有图像；释放一个 shader 不影响其他持有者，新文件快照不复用旧内容。
新图像在内部按约 64 MiB 暂存批次提交（单个大图像可超过阈值）。同步 Result 失败保留原 shader，
脚本无需 begin/end 上传批次。
