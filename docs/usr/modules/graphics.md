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

Pcg 风格的逐层距离裁剪使用 `Camera3D.setLayerCullDistance(layer, distance)` 和
`Camera3D.getLayerCullDistance(layer)`；阴影 caster 使用
`Camera3D.setShadowLayerCullDistance(layer, distance)` 和
`Camera3D.getShadowLayerCullDistance(layer)`。层编号范围是 0..31，距离 0 表示沿用普通远裁面或阴影
级联范围。`Renderable3D.setLayer(layer)` 与 `Renderable3D.getLayer()` 设置和读取对象层。

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
`setMeshLodCullDistance(distance)` 设置整条 LOD 链的末级剔除距离，零关闭；
`setMeshLodRendererState(index, skinQuality, shadowMode, receiveShadows, motionMode, skinnedMotionVectors,
lightProbeUsage, reflectionProbeUsage)` 配置逐级渲染策略，`getMeshLodRendererState(index, field)` 按 0..6
读取。设置接口使用结构化 Result，并拒绝 Unity 枚举中不存在的空洞数值。
`setMeshLodFadeWidth` 与 `setMeshLodFadePolicy` 配置距离或显式时间驱动的 LOD 渐变；时间模式必须由
`advanceMeshLodTransition(distance, dt)` 推进。主渲染和离屏 Canvas 都同时绘制相邻层，alpha 权重互补；
`getMeshLodSecondaryLevelAtDistance` 和 `getMeshLodSecondaryWeightAtDistance` 可检查当前混合状态。
逐级 `shadowMode=2` 使用无面剔除的双面阴影管线；`shadowMode=1/3` 使用背面剔除管线。普通非 Pcg
Renderable3D 保留原有双面阴影默认值。Vulkan 与 WebGPU 对 opaque、alpha-cutout 和 skinned caster
使用相同策略。
`getMeshLodCullDistance()` 返回当前值。主相机、阴影和反射捕获共享同一选择结果。

逐级 `lightProbeUsage=2` 使用 Light Probe Proxy Volume。规则探针网格可以在 X/Y/Z 采用不同且任意数量的
坐标层；渲染时只选择包含物体的当前网格单元，并在片元世界坐标上对该单元的 2/4/8 个角点执行线性、
双线性或三线性二阶球谐插值。缺少角点的非规则探针集合继续使用有界的距离权重回退，不会被误判为规则网格。

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

### GroomInstance 毛发 / 发片

`newGroomInstance()` 创建运行时 groom 绘制体（C++ 侧完成 bake / rebuild；可失败的 Result API 仍仅限 C++）。脚本侧用 LOD、着色与只读计数器驱动绘制：

- LOD / 几何：`setForcedLod` / `getForcedLod`（`-1` 恢复自动）、`setScreenSize` / `getScreenSize`、`setWidthScale` / `getWidthScale`、`setSideHint`、`getActiveLodIndex` / `getActiveRepresentation`（`0=Strands`、`1=Cards`、`2=Meshes`、`3=None`）
- Cluster 剔除：`setClusterCullingEnabled` / `isClusterCullingEnabled`、`getClusterCount`、`getVisibleCurveCount`
- Marschner 近似：`setMarschnerLobes(r, tt, trt)`、`getMarschnerR` / `getMarschnerTT` / `getMarschnerTRT`
- 分析型自阴影：`setSelfShadow(strength, bias, rootAo)`、`getSelfShadowStrength` / `getSelfShadowBias` / `getRootAoStrength`
- 只读：`getCurveCount` / `getPointCount` / `getGroupCount`、`getMesh` / `getShader` / `getTexture`、`isGuideSimulationEnabled`、`draw`

默认 mid LOD 走 `HairCards` 几何；guide 仿真的 enable/update 仍为 C++-only。

```squirrel
local groom = gfx.newGroomInstance();
groom.setScreenSize(0.4);
groom.setForcedLod(1); // Cards
groom.setMarschnerLobes(1.0, 0.45, 0.25);
groom.setSelfShadow(0.35, 0.25, 0.3);
groom.draw();
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
`setRenderableTextureFromImageData(renderable, imageData, repeatU, repeatV)` 则一次完成纹理上传和
Renderable/Material 绑定，避免脚本在两个绑定调用之间传递可空的借用句柄；返回纹理由 Graphics 持有。

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
- `hasUniform()`、`isEnabled()`、`isMorphDirty()`、`newGroomInstance()`、`newHairShader()`、`newMeshCylinder()`、`newMeshShader()`、`newMeshShaderVF()`、`newMeshSphere()`、`newQuad()`、`newShader()`
- `GroomInstance`：`setForcedLod`、`getForcedLod`、`setScreenSize`、`getScreenSize`、`setWidthScale`、`getWidthScale`、`setSideHint`、`setClusterCullingEnabled`、`isClusterCullingEnabled`、`getActiveLodIndex`、`getActiveRepresentation`、`getClusterCount`、`getVisibleCurveCount`、`setMarschnerLobes`、`getMarschnerR`、`getMarschnerTT`、`getMarschnerTRT`、`setSelfShadow`、`getSelfShadowStrength`、`getSelfShadowBias`、`getRootAoStrength`、`getCurveCount`、`getPointCount`、`getGroupCount`、`isGuideSimulationEnabled`
- `newShaderFromSpvFile()`、`replaceShaderFromGlsl()`、`replaceShaderFromWgsl()`、`newTexture()`、`newTextureWithSampler()`、`setRenderableTextureFromImageData()`、`updateTextureFromImageData()`、`setTextureSampler()`、`getMaxAnisotropy()`、`newVolumetric()`、`newAmbientOcclusion()`、`newGlobalIllumination()`、`newAntiAliasing()`、`setMsaaSamples()`、`getMsaaSamples()`、`present()`、`render3D()`、`reset()`、`screenToRay()`、`screenToWorldX()`、`screenToWorldY()`
- `sendFloat()`、`sendVec2()`、`sendVec3()`、`sendVec4()`、`setActive()`、`setAmbient()`、`setBackgroundColor()`、`setCamera()`
- `setCanvas()`、`setCastOcclusion()`、`setCastShadow()`、`setCloudShadows()`、`setColor()`、`setDirection()`、`setDirectionalLight()`、`setEnabled()`、`setEnvIntensity()`、`setEnvMap()`
- `setEye()`、`setFov()`、`setMesh()`、`getMesh()`、`setMeshLod()`、`clearMeshLod()`、`getMeshLodCount()`、`getMeshLodLevelAtDistance()`、`setMetallic()`、`setMorphWeight()`、`setNormalTexture()`、`setPackedNormalMask()`、`setHeightTexture()`、`setPosition()`、`setRadius()`
- `setReceiveLight()`、`setCustomLightProbe()`、`setCustomLightProbeCoefficient()`、`clearCustomLightProbe()`、`setReceiveShadow()`、`setRotation()`、`setRoughness()`、`setScale()`、`setShader()`、`setHair()`、`getHair()`、`setShadowBias()`、`setShadowStrength()`
- `setTarget()`、`setTexCellBomb()`、`getTexCellBombScale()`、`getTexCellBombStrength()`、`getTexCellBombRotation()`、`setParallax()`、`getParallaxScale()`、`getParallaxMinLayers()`、`getParallaxMaxLayers()`、`setTexture()`、`setTint()`、`setType()`、`setUp()`、`setViewport()`、`setVisible()`、`setVolumetric()`、`setVolumetricIntensity()`、`setYaw()`
- `setZoom()`、`worldToScreenX()`、`worldToScreenY()`、`Texture.getMipmapCount()`
- 字体：`newFont()`、`setFont()`、`getFont()`、`drawText()`、`print()`、`getAscent()`、`getBaseline()`、`hasGlyph()`
- `AlphaMask`：`newAlphaMask()`、`setThreshold()`、`getThreshold()`、`setSoftness()`、`getSoftness()`、`setInverted()`、`getInverted()`
- `MapFog`：`newMapFog()`、`update()`、`setTime()`、`getTime()`、`setCloudTexture()`、`getCloudTexture()`、`setMaskTexture()`、`getMaskTexture()`、`setCloudTiling()`、`setCloudSpeed()`、`setDistort()`、`setDistortFix()`、`setFogColor()`、`setFogAlpha()`、`setEdgeSoftness()`、`setShadowEnabled()`、`setShadow()`、`setSelectStrength()`、`setDissolveScale()`、`setCloudMix()`、`setCloudDensity()`、`getCloudTileA()`、`getCloudTileB()`、`getFogAlpha()`、`getShadowEnabled()`、`makeCloudTexture()`、`draw()`（大地图迷雾：双层云 + mask R/G/B）
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
### HDR、景深与反射探针绑定

下列绑定用于 HDR 图像、反射探针采集/注册、天空环境、曝光、Bloom、Gaussian 景深（DOF）与反射链质量控制。
`Camera3D.setDepthOfField(focusDistance, maxBlurPx, focusRange)` 配置最终 HDR resolve 上的可分离 Gaussian 景深；
`maxBlurPx <= 0` 关闭效果。焦点平面用视空间距离表示；`getDofFocusDistance` / `getDofMaxBlur` /
`getDofFocusRange` 读取当前参数。HD-2D 缩微感可配合 `hd2d.Hd2dLook` 一键写入（见 [HD2D](hd2d.md)）。
Cutout/masked 精灵需写入深度，DOF 才能正确对焦角色。

`applyConfiguredToCamera`、`applyToCamera`、`clearReflectionProbe`、`configureInfluence`、`filterAndPublish`、`getActiveCubemap`、`getAdaptiveFaceBudget`、`getAdaptiveFilterSamples`、`getBloomIntensity`、`getBloomThreshold`、`getCaptureClusteredLighting`、`getCaptureFarDistance`、`getCaptureLodDistanceScale`、`getCaptureMask`、`getCaptureTransparent`、`getCenterX`、`getCenterY`、`getCenterZ`、`getCount`、`getDofFocusDistance`、`getDofFocusRange`、`getDofMaxBlur`、`getEnvProbeCenterX`、`getEnvProbeCenterY`、`getEnvProbeCenterZ`、`getEnvProbeExtentX`、`getEnvProbeExtentY`、`getEnvProbeExtentZ`、`getEnvironmentLighting`、`getEnvironmentLightingIntensity`、`getExposure`、`getFaceCanvas`、`getGpuBudgetMs`、`getInfluenceBlendDistance`、`getInfluenceExtentX`、`getInfluenceExtentY`、`getInfluenceExtentZ`、`getInfluenceIntensity`、`getInfluencePriority`、`getLastCandidateCount`、`getLastCapturedFaceCount`、`getLastFilterSampleCount`、`getLastPublishedCount`、`getLastSelectedCount`、`getMaxRoughness`、`getPendingFaceCount`、`getPostProcessQuality`、`getPublishedRevision`、`getReflectionCaptureEnabled`、`getReflectionCaptureMask`、`getReflectionProbeCount`、`getReflectionQuality`、`getRefreshInterval`、`getResolution`、`getResolutionScale`、`getRevision`、`getSelectionHysteresis`、`getSkyB`、`getSkyFaceColor`、`getSkyFaceTexture`、`getSkyFaceTextureScale`、`getSkyG`、`getSkyIntensity`、`getSkyR`、`getSmoothedGpuDurationMs`、`getStagedRevision`、`getStagingCubemap`、`getThickness`、`getTotalCapturedFaceCount`、`getUpdateMode`、`hasEnvProbe`、`isAutoExposure`、`isCaptureComplete`、`isCapturePending`、`isRecaptureQueued`、`newHDRImageData`、`newReflectionProbeCapture`、`newReflectionProbeRegistry`、`queueCapture`、`queueCaptureAABB`、`remove`、`reportGpuDurationMs`、`requestCapture`、`setAutoExposure`、`setBloom`、`setCaptureClusteredLighting`、`setCaptureLodDistanceScale`、`setCaptureMask`、`setCaptureTransparent`、`setDepthOfField`、`setEnvironmentLighting`、`setExposure`、`setGpuBudgetMs`、`setMaxRoughness`、`setReflectionCaptureEnabled`、`setReflectionCaptureMask`、`setReflectionProbe`、`setReflectionQuality`、`setRefreshInterval`、`setResolutionScale`、`setSelectionHysteresis`、`setSkyColor`、`setSkyFaceColor`、`setSkyFaceTexture`、`setSkyFaceTextureScale`、`setUpdateMode`、`stageCapturedFaces`、`tick`、`tickAdaptive`、`updateCamera`。

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

### Native vegetation wind reference (C++)

`VegetationWindState` holds caller-owned direction, strength, published `phase` and private-source-equivalent
`updatePhase`. Normally callers use the checked operations rather than editing either phase directly.
`advanceVegetationWind(state, forward, strength, dt)` follows Pcg WindManager's
smoothed globals, downward direction component and phase advance; dt is explicit.
The phase subtracts 100 once when greater than 100, matching the source rather than
silently applying a different modulo rule. Invalid or overflowing results preserve state.

`evaluateVegetationWind(input, state)` implements PW_GeneralWind distance attenuation,
main/branch/leaf flex, object scale/orientation compensation and source volume normalization.
`VegetationWindInput` includes position, world offset, camera, object transform, width/height,
flex/frequency, maximum distance, injected sin(time/4) and sin(time), and enabled/billboard/alphaTest
switches. The root's source 0/0 normalization resolves to zero displacement. A singular transform,
invalid dimensions/globals or unrepresentable result returns a diagnostic. Double intermediates
and float output define tolerance-based behavior, not GPU bit equality.

These functions provide the CPU reference. The same deformation is now implemented in grass
vertex shaders, with typed state/profile and checked Squirrel operations. No wall clock, callbacks, scene references
or global state are retained. Caller serializes access to the single mutable state owner.

`VegetationWindProfile` separates material controls from vertex inputs and defaults to disabled.
`packVegetationWind(state, profile, seconds)` returns a validated 14-float snapshot intended for
slots 18–31 of the grass shader: direction, strength, phase, signed distance, flex, frequency,
sin(seconds/4) and sin(seconds). Zero distance encodes disabled wind; negative distance selects
the source billboard branch. Disabling alphaTest zeros leaf flex without changing alpha discard.
The calculation is pure and validates disabled profiles too. Grass shaders reserve slots 18–31:
`windGlobals` vec4 at 18, `windPhaseDistance` vec2 at 22, `windFlex` vec3 at 24,
`windFrequency` vec3 at 27, and `windSineTime` vec2 at 30. Defaults disable motion.
`applyGrassWind(shader, state, profile, seconds)` checks the complete wind schema and packet
before publishing any uniform bytes. Invalid input/schema leaves the prior shader storage intact.
It performs no GPU allocation and retains no references. Call on the Graphics owner thread outside
draw/capture callbacks. Re-baking resets shader defaults; reapply the caller-owned snapshot afterward.
The terrain-stamping example uses this API with `terrainGrass.getShader()` and an explicit clock.

`GrassFoliageSettings` selects the static PW/Pcg foliage surface profile. Its script fields are
`baseR`, `baseG`, `baseB`, `alphaCutoff`, `normalStrength`, `renderDistance`, `fadeRange`,
`hardRenderDistance`, `density`, `snowMinimumHeight`, `snowFadeDistance`, `snowProgress`,
`snowR`, `snowG`, and `snowB`.
The profile consumes albedo, tangent normal and packed metallic/occlusion/thickness/smoothness maps;
the renderer applies distance alpha fade, height snow, native directional/ambient lighting and shadows.

`TerrainDetailOverwriteSettings` ports Pcg's terrain detail override without merging its three controls:
`pcgDetailDistance` starts the shader fade, `pcgFadeoutDistance` is the fade span,
`unityDetailDistance` is the outer hard cutoff, and `unityDetailDensity` deterministically thins instances.
`detailResolutionPerPatch` is classified with Pcg's exact 2/4/8/16/32 quality mapping and all other values
fall back to VeryLow64. Apply it with `GrassField.setTerrainDetailOverwrite(settings)`; the checked Result
value is the quality ordinal (VeryLow64=0 through Ultra2=5). The call requires an uploaded foliage profile,
copies its settings, publishes all shader values atomically, and retains no references. The getters
`getTerrainDetailHardDistance()` and `getTerrainDetailDensity()` expose the applied runtime state.

The vertex path deforms the camera-facing card in world-relative coordinates, uses the effective
instance up scale for the source width/height compensation, and converts back to object space.
World/view positions follow deformation while the root varying remains unchanged. Both GLSL and
WGSL contain the formula. Vulkan runtime screenshots verified time-dependent movement and exact
restoration after disabling wind. Native Dawn/WebGPU runtime also accepted and executed the WGSL;
fixed two- and four-second captures differ, while repeated disabled captures are byte-identical.
Lighting/material output still differs across the backends, so this establishes deformation behavior
rather than cross-backend pixel identity. The terrain-detail adapter also transports deterministic
healthy/dry RGB coloring; PBR foliage and the remaining original material effects remain outside this path.


### Squirrel wind controls

`eve.VegetationWindState()` owns `strength` and `phase` fields, `setDirection(x,y,z)`,
`getDirectionX()`, `getDirectionY()` and `getDirectionZ()`. `eve.VegetationWindProfile()` exposes
`maximumDistance`, `enabled`, `billboard`, `alphaTest`, `setFlex(x,y,z)` and
`setFrequency(x,y,z)`. Configuration is mutable; checked operations validate complete snapshots.

`eve.advanceVegetationWind(state,x,y,z,strength,dt)` updates the state from a forward direction
and explicit nonnegative dt, returning the standard Result table. Invalid values preserve state.
`eve.initializeVegetationWind(state,x,y,z,strength)` implements Pcg's distinct
`InstantWindApply` path: direction and clamped strength change immediately, and phase becomes
`pow(strength*0.5+0.5,3)*0.1`. It preserves `updatePhase`, matching Pcg's private `m_windTime`;
the next advance resumes that accumulator and republishes it. It does not reuse smoothing or read a hidden clock.
`eve.applyGrassWind(shader,state,profile,seconds)` applies the snapshot without advancing it,
returning a Result table and preserving shader storage on invalid values/schema. Squirrel uses its
native float clock argument. Null or wrong object types fail VM argument conversion with a type
exception before entering the operation. Caller owns state/profile; Graphics owns the grass shader.

```squirrel
local windState = eve.VegetationWindState();
local windProfile = eve.VegetationWindProfile();
windProfile.enabled = true;
assert(eve.initializeVegetationWind(windState,1.0,0.0,0.2,0.5).ok);
// dt and seconds are injected by the caller's update loop.
assert(eve.advanceVegetationWind(windState,1.0,0.0,0.2,0.5,dt).ok);
assert(eve.applyGrassWind(field.getShader(),windState,windProfile,seconds).ok);
```

### Tree prototype vertex wind

`gfx.newTreeWindShader()` creates a backend-native Mesh3D vertex shader and reuses the engine's
default PBR fragment stage. Vulkan consumes embedded SPIR-V; WebGPU consumes matching WGSL.
The generic `newMeshShaderFromSpv` and `newMeshShaderFromWgsl` contract treats either empty stage
as the corresponding default Mesh3D stage.

`eve.TreeWindProfile()` owns `bendFactor` and `setDimensions(width,height)`. Dimensions are the
unscaled source prototype crown width and height. `eve.applyTreeWind(shader,state,vegetation,tree,seconds)`
validates the complete 16-float schema before publishing wind values and dimensions. Pcg bend
scales all three flex channels. Invalid input or shader layout preserves all prior parameter bytes.

The vertex shader keeps the prototype origin fixed and applies PW main-stem, branch and leaf motion
in world-relative coordinates. Graphics owns the shader; state/profile objects remain caller-owned.
No wall clock or scene link is retained. Call on the Graphics owner thread outside draw callbacks.

### Pcg 水面系统控制器

`eve.WaterSystemSettings()` 配置 `refreshRate`、`infiniteMode`、`autoRefresh`、
`ignoreSceneConditions`，并通过 `setAutoUpdateMode(0|1)` 选择 Interval 或 SceneConditions。
`eve.WaterSceneConditions()` 提供 `sunIntensity`、`hour`、`minute`、`sunAvailable`、
`timeAvailable`，以及 `setSunColor(r,g,b)`、`setSunDirection(x,y,z)`。调用者提交快照，
因此控制器不会搜索全局场景，也不会持有相机、太阳或玩家指针。

`eve.WaterSystemState()` 暴露权威 `seaLevel`、`positionX`、`positionY`、`positionZ`、
`refreshRemaining`、`sceneCheckFrames`、`refreshRevision` 和 `initialized` 状态。
先调用 `initializeWaterSystem(state,settings,scene,initialFrames)`；随后每帧调用
`advanceWaterSystem(state,settings,scene,playerX,playerZ,dt,nextFrames)`。返回的 Result 值为
本帧是否应重新生成反射。无限模式把 X/Z 跟随输入位置并始终把 Y 锁定为 seaLevel。
Interval 模式保留 Pcg 的严格小于零触发和 refreshRate 重置；SceneConditions 比较太阳颜色、
强度、方向与可选时间，并使用调用者注入的 nextFrames 取代隐藏随机数。
`updateWaterSeaLevel(state,level,regenerate)` 原子更新高度，并在要求重建时递增 refreshRevision。
所有失败都保留原状态；dt、刷新间隔、场景值和延迟会完整验证。
### Pcg WindManager 音量控制

`eve.VegetationWindAudioState()` 与
`eve.advanceVegetationWindAudio(state,windStrength,transitionTime,dt,enabled,clipAvailable)` 移植
WindManager 的风声控制器。状态拥有 currentWindSpeed、anchorVolume、volume、processing 和 playing；
函数只计算确定性的音量状态，不跨模块持有 Audio Source。脚本应创建循环、listener-relative Source，
每帧把 state.volume 写入 Source。实现保留源码行为：currentWindSpeed 不更新，非零风速每帧重启处理；
上升在目标下方 0.05 内吸附，下降比较不对称而通常渐近目标。dt 与 transitionTime 均显式注入。

### Pcg 程序化水面网格

`eve.WaterMeshSettings()` 提供 `sizeX`、`sizeY`、`sizeZ`、`densityX`、`densityY`、`height`，
并通过 `setType(0|1)` 选择 Plane 或 Circle。`eve.calculateWaterMeshTriangles(settings)` 在分配前返回
Pcg 算法的三角形数量；`water.createProceduralMesh(settings)` 验证并上传网格，返回顶点数量。
Plane 按 `round(density/40*size)+1` 建立规则格点。Circle 使用同心六边环，环数取
`round(densityX/40*sizeX/2)`，并保留 Pcg 的环缝合索引、UV 缩放与非对称 X/Z 密度行为。
无效、过小或超过单轴 4096 点的输入返回诊断且保留 Water 当前网格。Custom 对应调用者直接提供
EVEngine Mesh，不复制 Unity MeshFilter 的资源引用模式。

### PWS 水波方向

`water.setWaveDirectionAngle(degrees)` 返回标准 Result，并设置 PWS_WaterSystem 的 `directionAngle`，并将任意有限角度归一化到
[0,360)；`water.getWaveDirectionAngle()` 返回当前角度。Vulkan GLSL 与 WebGPU WGSL 都按该角度旋转
顶点位移和片元细浪坐标。实现把角度编码在 128 字节水参数块的 ripple-count 小数部分；整数涟漪数量保持
不变，避免超过 Vulkan 保证支持的最小 push-constant 容量。

### Pcg 水深色带

`eve.WaterDepthGradient()` 分别拥有颜色键和透明度键。`addColorStop(time,r,g,b)` 与
`addAlphaStop(time,a)` 返回 Result；`clear()` 清空两组键。`water.setDepthGradient(gradient,resolution)`
生成 `resolution × resolution` 的 RGBA8 Clamp 色带，并严格按 Pcg 使用 `x/resolution` 采样，
返回像素数量。可通过 `getDepthGradientTexture()` 将同一纹理设置给使用 Water shader 的
Renderable3D normal slot；直接调用 Water.draw 时会自动绑定。`clearDepthGradient()` 恢复深浅双色模式。
无键、重复时间或 2..4096 外的分辨率失败并保留当前纹理。

### Pcg 平面水反射规划

`eve.WaterPlanarReflectionSettings()` 移植 `PcgPlanarReflections.PlanarReflectionSettings`：反射开关、
天空跳过、Full/Half/Third/Quarter 分辨率倍率、clip-plane 偏移、反射层、阴影、统一或逐层渲染距离和
基础纹理分辨率。`setResolutionMultiplier(0..3)` 选择倍率，`setLayerDistance(layer,distance)` 设置 32 层距离。
`eve.WaterPlanarReflectionInput()` 通过 `setCameraPosition`、`setCameraForward` 及 `waterPlaneY`、`renderScale`、
`orthographic`、`reflectionOrPreviewCamera` 提供不可变帧快照。

调用 `buildWaterPlanarReflectionPlan(output,settings,input)` 后，output 包含镜像相机位置/方向、世界裁剪平面、
反射矩阵、排除水层 4 的 culling mask、32 层距离、捕获分辨率和 `shouldRender`。Third 精确保留 Pcg 的
0.33 倍率及整数截断。规划器纯计算且不持有相机、纹理或场景节点；调用者用结果配置现有离屏捕获，再将纹理
传给 `Water.drawWithPlanarReflection`。非法枚举、非有限值、负距离或无效分辨率返回 Result，保持 output 不变。

设置字段为 `disableSkyboxReflections`、`clipPlaneOffset`、`reflectLayers`、`shadows`、
`enableRenderDistance`、`enablePerLayerDistances`、`customRenderDistance` 和 `textureResolution`。
结果字段为 `textureWidth`、`textureHeight`、`cullingMask`、`renderShadows`；读取镜像数据使用
`getCameraX()`、`getCameraY()`、`getCameraZ()`、`getForwardY()`、`getLayerDistance(layer)` 与
`getReflectionMatrix(column,row)`。

### Pcg 水下效果状态机

`UnderwaterColorGradient.addStop(time,r,g,b)` 建立深度或时段颜色渐变，`clear()` 清空键。
`WaterUnderwaterSettings` 提供 `enabled`、`supportFog`、`supportPostFx`、`enableTransitionFx`、
`useCaustics`、`hdrp`、`overrideFogColor`、`fogDepth`、`fogDistance`、`hdrpFogDistance`、
`nearFogDistance`、`fogDensity`、`playbackVolume`、`causticSize`、`framesPerSecond`、
`causticTextureCount`、`overrideFogCurve`、`anisotropyShallow`、`anisotropyDeep`，并用
`setFogColorMultiplier`、`setOverrideFogMultiplier` 写入颜色乘数。

`WaterUnderwaterInput` 的 `cameraY`、`seaLevel`、`timeOfDay`、`causticTicks`、`hasMainLight` 和
`setMainLightColor` 是不可变帧输入。调用
`advanceWaterUnderwaterEffects(state,output,settings,input,depthGradient,timeGradient,postExposureCurve,postColorGradient)` 后，
`WaterUnderwaterState` 保存 `initialized`、`isUnderwater`、`causticFrame`；output 发布
`entered`、`exited`、`playSubmergeDown`、`playSubmergeUp`、`loopAudio`、`particles`、`horizon`、
`surfaceVfx`、`postFx`、`transitionFx`、`caustics`、`causticSize`、`depth01`、`fogStart`、
`fogEnd`、`hdrpBaseHeight`、`hdrpMeanFreePath`、`hdrpProbeDimmer`、`hdrpAnisotropy`、
`hdrpDepthExtent`，颜色由 `getFogR()`、`getFogG()`、`getFogB()` 读取。

相机位于海面或以下视为水下。普通路径按深度渐变、主光颜色和非负分量偏移生成雾；override 路径按
timeOfDay 渐变和乘数生成雾。HDRP 输出精确保留 Pcg 的深度插值常量。音频、粒子、Volumetric、灯光
cookie 与后处理仍由各 provider 消费该快照；状态机不持有跨模块对象。所有输入先完整验证，失败时 state
与 output 均保持不变。

`WaterSurfaceFogSnapshot` 保存离水后恢复的 `density`、`start`、`end`、`height`、`heightFalloff`，
颜色通过 `setColor(r,g,b)` 设置。`applyWaterUnderwaterFog(volumetric,output,surface)` 在完整验证后，把水下
`fogColor`、`fogDensity`、`fogStart`、`fogEnd`、`fogHeight` 写入借用的 `Volumetric`；离水时恢复 surface。
EVEngine 的距离雾要求非负起点，因此 Pcg 默认 `nearFogDistance=-4` 在 provider 边界钳制到 0。
`Volumetric.projectDirectionalCookie(graphics,depth,cookie,worldSize,intensity)` 从硬件深度重建世界位置，
沿主光方向建立投影平面并重复采样 cookie；参数只在本次渲染调用中借用，不跨帧保存纹理指针。

`UnderwaterScalarCurve.addKey(time,value)` 与 `clear()` 定义时段曝光曲线。设置中的
`timeDrivenPostFx`、`constantPostExposure`、`setConstantPostColor(r,g,b)` 选择 Pcg 的天气时段模式或
固定模式。输出的 `postExposure` 和 `getPostR()`、`getPostG()`、`getPostB()` 是本帧后处理值；
`WaterSurfacePostFxSnapshot` 用 `exposureEv` 和 `setColor(r,g,b)` 保存表面状态。
`applyWaterUnderwaterPostFx(graphics,camera,output,surface)` 将水下曝光写入借用的 Camera3D，并把 RGB
颜色过滤写入 Graphics 的最终 HDR 曝光阶段；离水时原子恢复 snapshot。Vulkan SPIR-V 与 WebGPU WGSL
使用相同的线性颜色乘法。
Pcg 内置 transition prefab 的 Y 尺寸 0.05 和 `blendDistance=0.025` 分别对应设置中的
`transitionHalfHeight=0.025` 与 `transitionBlendDistance=0.025`。水下距海面不超过半高时
`transitionWeight=1`，随后在 0.025 米内衰减到 0；`transitionVignette=0.25` 和
`transitionLensDistortion=0.252` 来自原始 Post Processing asset。输出发布同名 vignette/lens 值，
`WaterSurfacePostFxSnapshot.vignette` 与 `lensDistortion` 保存表面状态；适配器将它们接入 Vulkan/WebGPU
最终 HDR 合成的径向暗角和 UV 畸变。该 profile 启用的 Lift/Gamma/Gain trackball 先按 Unity Post Processing v2 的
`ColorToLift`、`ColorToInverseGamma`、`ColorToGain` 转为 HDR 系数，再按 `transitionWeight` 从
中性值混合；`setTransitionLift`、`setTransitionInverseGamma`、`setTransitionGain` 可覆盖资源值，
`WaterSurfacePostFxSnapshot.setLiftGammaGain(...)` 保存离水后的恢复状态。profile 的黑色 Color Filter 同样按
过渡权重从白色混合，并与既有水下时段颜色相乘；`setTransitionColorFilter(r,g,b)` 可覆盖该资源值。
脚本可用 `getTransitionLiftR()`、`getTransitionInverseGammaR()`、`getTransitionGainR()` 和
`getTransitionColorFilterR()` 读取代表性的红通道结果以诊断当前混合状态。
`transitionVignetteSmoothness=0.8`、`transitionLensScale=1.02`（surface snapshot 中对应
`vignetteSmoothness`、`lensScale`）保留 profile 的其余启用值；混合时分别从
Unity 的中性值 0.2 和 1.0 过渡。最终 pass 使用 Post Processing v2 的 classic vignette 幂函数以及正镜头
畸变的 theta/sigma/tangent 采样公式，而非简单二次 UV 偏移。
`WaterSurfaceMaterialSnapshot` 用 `setColor(r,g,b)` 和 `alpha` 保存水面 tint。
`applyWaterUnderwaterMaterial(renderable,output,surface)` 对应 Pcg `UpdateUnderwaterMaterial`：水下把
`getUnderwaterMaterialR()`、`getUnderwaterMaterialG()`、`getUnderwaterMaterialB()` 发布的颜色写入专用水面
Renderable3D，离水时恢复 snapshot。有主光时颜色来自 `mainLightColor`，没有主光时使用 Pcg 的
`#CFCFCF` 回退值；适配器同步借用对象且在验证完成前不修改 tint。
`applyWaterUnderwaterHorizon(horizon,output)` 同步借用 Renderable3D，并严格按状态机的 `horizon` 输出控制
可见性；HDRP 路径和离水状态都会隐藏该网格。`getVisible()` 可读取最终可见状态。

`WaterUnderwaterDisableTriggerState` 与 `WaterUnderwaterTriggerEvent` 对应 Pcg
`DisableUnderwaterFXTrigger`。事件的 `sensorTag`、`visitorTag`、`entered`、`exited` 从 `World3D` 的
begin/end trigger 缓冲复制 Sensor/Visitor Shape Tag，
再调用 `advanceWaterUnderwaterDisableTrigger(state,event,expectedSensorTag,expectedVisitorTag)`；匹配的 enter
把 `effectsEnabled` 设为 false，匹配的 exit 恢复为 true，其他标签不改变状态。每个事件必须恰有一个方向，
非法标签或方向在发布前失败。将结果写入 `WaterUnderwaterSettings.enabled` 后再推进水下状态机。

Pcg 自动景深跟焦计算使用 `DepthOfFieldFocusSettings/Input/State/Output` 和
`evaluateDepthOfFieldFocus(state, output, settings, input)`。应用层负责用 World3D 或其他权威场景查询提供
中心射线命中距离或目标距离；计算器复现 FollowScreen、FollowTarget、FixedOffset、玩家遮挡回退到最大距离
以及 `deltaSeconds * response` 平滑，并输出 focusDistance、aperture 和 focalLength。状态由调用者持有。
字段 `tracking`、`interactWithPlayer`、`focusOffset` 控制模式与偏移；输入字段 `hasRayHit`、
`hitPlayer`、`rayHitDistance`、`hasTarget`、`targetDistance` 描述查询结果。状态字段
`maximumExceeded` 记录玩家遮挡或最大距离回退，输出字段 `active` 表示效果是否启用。

`gfx.newDepthOfField()` 创建跨后端景深 compositor。`DepthOfField.apply(source, linearDepth,
focusDistance, focusRange, maximumBlurPixels, nearPlane, farPlane)` 返回借用的内部 HDR 结果纹理；
`applyTo` 写入调用者提供的 Canvas。`linearDepth` 使用 GBuffer `getDepthTexture()` 的归一化线性深度。
焦距附近保持清晰，离焦程度按世界距离差增长，并以 `maximumBlurPixels` 限制采样半径。
活动 Camera3D 可调用 `setDepthOfField(focusDistance, focusRange, maximumBlurPixels)`，把镜头状态接入
最终场景合成；`clearDepthOfField()` 关闭效果。启用时应同时在 RenderControl 开启 G-buffer。

### Pcg 反射探针距离裁剪

`ReflectionProbeRegistry.setDistanceCullingEnabled(true)` 启用 Pcg 风格的硬距离资格；
`setMaxRenderDistance(distance)` 设置正且有限的相机到探针中心距离。每次 `updateCamera(camera)` 更新
所有探针的资格，`getLastDistanceCulledCount()` 返回被拒绝数量。距离外探针不会消耗 `tick()` 的捕获和
过滤预算；重新进入范围会排队一次完整 recapture。距离判断使用严格的 `distance >= maximum` 淘汰，
对应 Pcg 的 `distance < maximum` 启用规则。注册表仍负责八槽稳定选择、优先级和滞回。

`getDistanceCullingEnabled()` 与 `getMaxRenderDistance()` 返回当前配置，便于运行时面板和存档系统读取。

Camera3D 的物理镜头与裁剪读取接口为 setPhysicalLens、getAperture、getFocalLength、getNearClip 和 getFarClip。物理参数保存在相机权威组件中，供照片模式和后处理共同读取。

### Pcg PhotoMode Water 权威配置

PcgWaterPhotoModeAuthority 通过 setAuthority 管理唯一 Water 字段租约。它接收反射开关、额外距离、分辨率、LOD bias、Underwater fog color、density 与 distance 七个字段。applyToPlanarSettings 将值写入 WaterPlanarReflectionSettings；其中 lodBias 同时缩放实际 capture layer culling distance。applyToUnderwaterSettings 将颜色和雾参数写入 WaterUnderwaterSettings，水下求值会直接使用照片模式颜色。

### Pcg PhotoMode Grass 权威配置

`GrassField.setPhotoModeAuthority(true)` 将当前 GPU 草地场注册为照片模式 Grass 域唯一所有者；关闭或销毁字段时撤销。`m_globalGrassDensity` 控制 shader 的确定性实例密度丢弃，`m_globalGrassDistance` 与 `m_cameraCellDistance` 缩放硬裁剪、淡出和渲染距离，`m_cameraCellSubdivision` 以二次幂调整 cell 覆盖范围。`getPhotoModeDensity`、`getPhotoModeDistance`、`getPhotoModeCellDistance` 和 `getPhotoModeCellSubdivision` 返回当前倍率。重新上传 foliage 或调用 `setTerrainDetailOverwrite` 后会再次应用这些倍率。

### Pcg PhotoMode Graphics 权威配置

`PcgGraphicsPhotoModeAuthority` 绑定 Graphics、RenderControl 与 Camera3D 后，以显式租约接收 LOD bias、anti-aliasing、shadow distance、shadow resolution 和 shadow cascades。LOD bias 直接缩放所有 Renderable3D 网格 LOD 的选择距离；AA 枚举切换真实 FXAA、SMAA、TAA 或关闭合成；阴影距离写入 Camera3D 每层 caster 裁剪。完整分辨率和级联设置保存在同一状态中，供阴影资源配置使用。

状态读取方法为 getAuthority、getReflectionEnabled、getReflectionDistance、getReflectionResolution、getReflectionLodBias、getFogDensity、getFogDistance、getFogRed、getFogGreen 和 getFogBlue。WaterPlanarReflectionSettings 也公开 lodBias。

### Pcg PhotoMode 全局阴影倍率

`PcgGraphicsPhotoModeAuthority` 同时接受 Lighting 域的 `m_globalShadowDistanceMultiplier`。实际 32 层 shadow caster 裁剪距离为 `m_shadowDistance * multiplier`，基础距离与倍率分别保存，任一字段变化都会重新投影。
### GPU skin influence limit

The native `Graphics::setMesh3DSkinInfluenceLimit` draw state selects the strongest 1, 2, or 4
vertex influences for subsequent skinned mesh draws. Retained weights are
renormalized in the forward, G-buffer, and shadow vertex paths. `Renderable3D`
applies Pcg LOD `SkinQuality` per selected level; `Auto` resolves to the engine
default of four influences.

### Pcg ReflectionMasker

`eve.WaterReflectionMasker()` 根据玩家在当前地形中的归一化 X/Z 位置控制平面反射。
`configure(channel,min,max,enabled)` 的 channel 为 0 R、1 G、2 B、3 A、4 RGBA，阈值为闭区间；
RGBA 模式任一通道命中即启用。`setMask(imageData)` 会复制像素，不跨帧保留图片指针，`clearMask()`
清除遮罩。`evaluate(playerX,playerZ,terrainX,terrainZ,width,depth,reflectionSettings)` 返回 0 Unchanged、
1 Enabled 或 2 Disabled，并在状态变化时同步 `WaterPlanarReflectionSettings.enabled`。
`getEnabled()`、`getHeightFeaturesEnabled()`、`getSampleX()` 和 `getSampleY()` 返回最近状态与采样坐标。
