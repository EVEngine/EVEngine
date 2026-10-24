# 图形渲染模块

[English](../../en/usr/modules/graphics.md)

游戏 `config.nut` 支持布尔配置 `vsync`，默认 `true`；设置 `vsync = false` 请求关闭垂直同步。
启动时会在普通窗口或编辑器宿主创建窗口前应用，修改配置后重启游戏生效。
`gfx.getVSyncCount()` 返回当前请求值（关闭为 0，开启为 1）；`gfx.setVSyncCount(0)` / `gfx.setVSyncCount(1)`
可在运行时更新请求。Vulkan 开启时优先 Mailbox、其次 FIFO；关闭时优先 Immediate，
不支持时按原有后端策略回退 Mailbox/FIFO，所以关闭不保证所有平台都能解除显示同步。

`Mesh.getLocalBounds()` 返回标准 Result，其 value 为 `[minX,minY,minZ,maxX,maxY,maxZ]`。
结果由 CPU 顶点计算，CPU morph baking 会更新边界；GPU 蒙皮和着色器位移不包含在内。
无有限顶点时返回 NotFound。
`eve.destroyRenderable3D(entityId,generation)` 在 render/owner thread 销毁当前世界的指定实体，
不存在或 generation 过期返回 NotFound，不销毁 mesh。成功后不得再访问该实体脚本引用。
场景链接保留完整世界矩阵，直接 TRS setter 切回本地模式；链接下一次推送重新更新派生矩阵。

**脚本入口：** `eve.Graphics()`

清屏、2D 图元、纹理、Canvas、摄像机和 3D 渲染。Camera2D/Camera3D 提供屏幕与世界坐标换算，供 2D/3D 拾取使用；形状命中测试见 [Math](math.md)，物理体查询见 [Physics](physics.md)。

高级光照 / AO / GI / AA / 体积雾等见 [高级渲染与屏幕空间效果](graphics/rendering-effects.md)。实时雾气专册见 [graphics_fog](graphics_fog.md)。

## 当前源码的能力边界

当前源码提供下列接口；实际可用性取决于构建和设备。此表不是发布版本或画面验收报告，
未完成项保持显式 Unsupported / 文档边界，不得静默假实现。

| 主题 | 当前接口 | 已知边界（勿当作完成） |
|------|------------|------------------------|
| 光照模式 | `RenderControl.setLightingMode("forwardPlus"\|"hybrid")`；桌面 preset 可切 Hybrid；半透明仍 Forward+ | ClassicScenes 像素对比与 FrameGraph 录制仍待补；Lavapipe/CI 默认 ForwardPlus |
| 体积雾 / 体积光 | `Volumetric`、[实时雾气](graphics_fog.md)（MAC / ray march / froxel） | 可选：Froxel 绑真实 CSM shadow map、物体空间 AO / RTAO |
| 显示器 HDR | Vulkan `setDisplayOutputMode`（`sdr`/`auto`/`hdr10`/`scrgb`）；无 HDR 表面软回退 SDR | WebGPU 仅 `sdr`/`auto`；真 HDR 显示器端到端验收仍待做 |
| 反射探针 | 采集、Registry 调度、sky backup、发布后 `applyToCamera` | **逐像素天空/大气 capture pass**未完成；Vulkan GGX/irradiance **compute filter** 在缺嵌入 SPIR-V 时返回 Unsupported，且**不得**把未 filter 的 staging 当已发布反射 |
| 抗锯齿 | FXAA 等经 `AntiAliasing` / RenderControl `"aa"` | 硬件 MSAA + 完整多 Pass SMAA LUT + TAA 仍为可选后期 |
| 其他后端 | 部分资源 Shader / 实例绘制路径仅 Vulkan | 非 Vulkan 返回结构化 `Unsupported`，无静默 CPU 假路径 |

API 细节：光照模式见 [rendering-effects](graphics/rendering-effects.md)；HDR present 与探针绑定见下文「显示器 HDR」「HDR、景深与反射探针绑定」。设计进度见 [`HDR与反射链AAA升级.md`](../../dev/HDR与反射链AAA升级.md)。

`gfx.setSceneToneMapping("none" | "aces" | "filmic")` 返回标准 Result；
`getSceneToneMapping()` 读取当前模式。默认仍为 `aces`。Vulkan 的 `filmic`
使用固定 Rec.709/AP1 电影式曲线、蓝色修正和色域扩展，仅影响最终显示，
不会把线性 HDR 反射或离屏纹理提前映射到 SDR。不支持切换的后端返回
Unsupported 并保留原模式。曝光、暗角和其他镜头效果仍由各自组件控制。

`gfx.setScenePhotographicVignette(intensity)` 返回 Result，强度范围 `[0,1]`，
默认 `0` 关闭；`getScenePhotographicVignette()` 读取当前值。Vulkan 在最终
色调映射前应用余弦四次方摄影暗角，不改变线性 HDR 捕获。非法参数保留旧值；
未支持此效果的后端只接受 `0`。此选项与原来的过渡暗角独立。

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
sprite.setNormalTexture(normalMap); // optional; enables GPU lit2d with Light2D
sprite.setQuad(quad);
sprite.setPosition(400, 270);
sprite.setScale(2.0, 2.0);
sprite.setRotation(30.0); // degree, rotate around rect center
sprite.setAnchor(0.25, 0.75); // normalized rotation pivot
sprite.setFlip(true, false);  // mirror UV without negative scale
sprite.setReceiveLight(true);
sprite.setBlend("alpha"); // alpha | premultiplied | additive | multiply

// eve_render: clear/draw background first, then submit all live Sprite2D objects
gfx.renderSprites();
```

`Sprite2D` 还提供 size、color、layer、visible、receiveLight、castOcclusion、normalTexture 等属性。
当 `setReceiveLight(true)` 且同时设置了 albedo（`setTexture`）与切线空间法线贴图（`setNormalTexture`）、且未挂自定义 2D shader 时，走 GPU `lit2d` 路径，与 `Light2D`（`point` / `dir` / `spot`，每 canvas 最多 8 盏）和 `Camera2D.setAmbient` 配合；只有 albedo 时则为 CPU 近似调制。旋转精灵时 lit2d 会按屏幕/UV 导数重建切线帧，法线贴图随几何一起旋转。

手电式聚光：`setType("spot")`，用 `setPosition` + `setDirection` 对准光束，`setRadius` 控制射程，`setSpotAngle(degrees)` 为外半角（整锥 ≈ 2×半角），`setSpotSoftness(0..1)` 控制边缘软硬；可用 `getSpotAngle()` / `getSpotSoftness()` 读取。开启 `setVolumetric(true)` 后，`scatterFromSceneLights2D` 会按同一锥角裁剪屏幕空间光柱。Tilemap 需 `layer.setReceiveLight(true)` 才会受光（默认关闭以保持旧图不受影响）；有纹理的格子走 lit2d（缺法线时用平坦法线）。

```squirrel
local flash = eve.Light2D();
flash.setType("spot");
flash.setPosition(heroX, heroY);
flash.setDirection(facingX, facingY); // 光束中心轴
flash.setColor(1.0, 0.95, 0.8, 2.5);
flash.setRadius(220.0);
flash.setSpotAngle(28.0);   // 半角（度）
flash.setSpotSoftness(0.4);
layer.setReceiveLight(true);
```
裁边动画通常由 `SpriteAnim.bindSprite(sprite)` 自动调用 `setFrameLayout`，无需游戏代码逐帧修正偏移。
不再使用时调用 `destroy()`；`renderSprites()` 是聚合提交接口，不要再把同一精灵加入另一条 2D 队列，以免重复绘制。

### 渲染带光照的 3D 对象

初始化时创建 mesh、shader 和 renderable，设置 camera、ambient 和 directional light；每帧只更新 transform/material 参数，最后调用 `render3D()`。阴影开关、bias 和 strength 应逐场景调节。

`Light3D` 支持 `point` / `dir` / `spot`。聚光用 `setType("spot")`、`setSpotAngle` / `setSpotSoftness`（与 2D 锥体语义相同）。实时阴影通过 `setCastShadow(true)` 与 `setShadowMethod("auto"|"none"|"csm"|"perspective"|"cube")` / `getShadowMethod()` 选择技术；有向光走 CSM，聚光走 perspective 本地 atlas。进程级方案用 `gfx.setShadowSchemeDirectionalEnabled` / `setShadowSchemeSpotEnabled` / `setShadowSchemePointEnabled`、`setShadowSchemeMaxSpotCasters` / `getShadowSchemeMaxSpotCasters`，以及本地分页 `setShadowSchemePagingEnabled`、`setShadowSchemeMaxLocalUpdates` / `getShadowSchemeMaxLocalUpdates`、`setShadowSchemeHysteresisBonus`（固定 atlas 槽位按重要性租用，更新预算可时间切片重绘）。动态阴影分辨率有两层：
1. **质量档 / atlas 预算**：`setShadowSchemeDirectionalMapSize` / `setShadowSchemeLocalMapSize`（边长钳制到 256/512/1024/2048/4096，GPU depth array 取两者较大值，变更时在下一帧 ensure 路径重建 atlas）、`getShadowSchemeAtlasMapSize`，以及 CSM 覆盖距离 `setShadowSchemeDirectionalMaxDistance` / `getShadowSchemeDirectionalMaxDistance`（默认 64 m）。
2. **按距离与屏幕覆盖自适应分配**（默认开启）：`setShadowSchemeAdaptiveCascadeResolution`。近距 cascade 与屏幕覆盖大（阴影边缘/轮廓更明显）的 cascade 获得更高有效分辨率，远处 cascade 用更小 viewport；Vulkan 数组层尺寸必须统一，因此物理纹理仍是同一 atlas，采样通过 `cascadeUvScale` 重映射到各 cascade 的左上角子区域。

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

运行期 GLSL→SPIR-V 由引擎自带编译器完成（`graphics/vulkan/GlslCompiler.cpp`，唯一实现）：
Windows 构建在配置时能找到 `VULKAN_SDK` 就链接 shaderc 静态库，发布出去的包不需要外部
`glslc.exe`；没有链接 shaderc 的构建才回退到 PATH 上的 `glslc`。C++ 侧用
`Graphics::supportsRuntimeGlslCompilation()` 查询该能力（WebGPU 固定为 false）；希望完全不依赖
运行期编译时，改用随包提交的 SPIR-V：`gfx.newShaderFromSpvFile` / `gfx.loadMeshShaderSpv`。

自定义网格着色器可用 `Shader.setMeshTexture(slot, texture)` 绑定 0..3 四个可移植纹理槽；
传入 `null` 清除对应槽，越界会抛出脚本异常。纹理由调用方持有，必须存活到解除绑定且
相关绘制结束；该接口只能在 Graphics 所属的渲染线程调用。

### 植被全局字段与细节设置

`gfx.newVegetationField()` 创建脚本拥有的 `VegetationField`，返回带 `ok`、`value` 和
`ownership="owned"` 的 Result 表。字段可以通过 `restore(document)` 原子恢复，并用
`snapshot()` 保存。`field.sample(x, y, z, colorLayer, extrasLayer, motionLayer, vertexLayer)`
在世界坐标采样四个通道；各层编号必须在 0..8，成功结果的 `value` 包含 `color`、
`extras`、`motion` 和 `vertex` 四个四元向量。

`gfx.newVegetationDetails()` 同样返回脚本拥有的 `VegetationDetails` Result，用于保存和恢复
全局颜色、覆盖、湿润及顶点阶段参数。创建和恢复失败不会发布部分状态；调用方必须检查
Result 的 `ok`。

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
mat.setCameraFacing(false);         // true：草卡片绕世界 Y 轴朝向活动相机
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

`mat.copyPbrSurfaceFrom(sourceMaterial)` 返回 Result，通过同一个原生 PbrSurface 校验入口复制
导入材质的扩展 PBR 描述并启用相同渲染路径，适合让程序化服装和导入人物采用一致着色。
不会复制 tint / roughness 等独立字段，也不共享可变材质状态；贴图仍由原 Graphics owner 拥有，
需覆盖目标材质及绘制的生命周期。调用在渲染 / VM 所属线程同步执行，不保留 sourceMaterial 指针。

遮罩材质使用 `setSurfaceMode("masked")`、`setAlphaCutoff()` 和
`setAlphaTechnique("cutoff" | "dither" | "coverage")`。对应查询接口为
`getSurfaceMode()`、`getBlendMode()`、`getDepthWrite()`、`getDoubleSided()`、
`getCameraFacing()`、`getSortPriority()`、`getAlphaCutoff()` 和 `getAlphaTechnique()`。
`setCameraFacing(true)` 为 GPU-driven 草片启用保持底部锚定的圆柱公告板，使顶点仅绕世界 Y 轴
朝向活动相机；关闭时保留实例原始朝向。纹理 Alpha 数据可用
`Texture.setAlphaConvention("straight" | "premultiplied")` 声明，并通过
`Texture.getAlphaConvention()` 查询。

虚拟纹理由材质拥有并在绑定时自动流式驻留（对齐 UE5 RVT）。脚本只编写图层：

```squirrel
material.enableVirtualTexture(width, height, tileSize, borderSize, residentCapacity, maxDecodesPerUpdate);
material.addVirtualTextureLayer(albedo, normal, uvScale, constantWeight, heightContrast);
// 可选带权重图：addVirtualTextureLayerWeighted(albedo, normal, weight, ...)
```

`usesVirtualTexture()` 查询是否已进入 atlas/page-table 采样；`clearVirtualTexture()` 关闭。
页反馈、解码、上传与页表发布不向外暴露，由 `render3D` → `Material.bind` 驱动。
详见 `examples/virtual-texture-blending` 与 `docs/dev/virtual-texture-material-blending.md`。

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

## 绑定与行为说明

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


- `GroomInstance`：`setForcedLod`、`getForcedLod`、`setScreenSize`、`getScreenSize`、`setWidthScale`、`getWidthScale`、`setSideHint`、`setClusterCullingEnabled`、`isClusterCullingEnabled`、`getActiveLodIndex`、`getActiveRepresentation`、`getClusterCount`、`getVisibleCurveCount`、`setMarschnerLobes`、`getMarschnerR`、`getMarschnerTT`、`getMarschnerTRT`、`setSelfShadow`、`getSelfShadowStrength`、`getSelfShadowBias`、`getRootAoStrength`、`getCurveCount`、`getPointCount`、`getGroupCount`、`isGuideSimulationEnabled`
- 字体：`newFont()`、`setFont()`、`getFont()`、`drawText()`、`print()`、`getAscent()`、`getBaseline()`、`hasGlyph()`
- `AlphaMask`：`newAlphaMask()`、`setThreshold()`、`getThreshold()`、`setSoftness()`、`getSoftness()`、`setInverted()`、`getInverted()`
- `MapFog`：`newMapFog()`、`update()`、`setTime()`、`getTime()`、`setCloudTexture()`、`getCloudTexture()`、`setMaskTexture()`、`getMaskTexture()`、`setCloudTiling()`、`setCloudSpeed()`、`setDistort()`、`setDistortFix()`、`setFogColor()`、`setFogAlpha()`、`setEdgeSoftness()`、`setShadowEnabled()`、`setShadow()`、`setSelectStrength()`、`setDissolveScale()`、`setCloudMix()`、`setCloudDensity()`、`getCloudTileA()`、`getCloudTileB()`、`getFogAlpha()`、`getShadowEnabled()`、`makeCloudTexture()`、`draw()`（大地图迷雾：双层云 + mask R/G/B）
- `Sprite2D`：`setPosition()`、`getX()`、`getY()`、`setRotation()`、`getRotation()`、`setScale()`、`getScaleX()`、`getScaleY()`、`setSize()`、`getWidth()`、`getHeight()`、`setTexture()`、`getTexture()`、`setNormalTexture()`、`getNormalTexture()`、`setQuad()`、`getQuad()`、`setColor()`、`setLayer()`、`getLayer()`、`setVisible()`、`getVisible()`、`setReceiveLight()`、`getReceiveLight()`、`setBlend()`、`getBlend()`、`setAnchor()`、`getAnchorX()`、`getAnchorY()`、`setFlip()`、`getFlipX()`、`getFlipY()`、`setFrameLayout()`、`setCastOcclusion()`、`getCastOcclusion()`、`destroy()`
- `Volumetric`：`setQuality`、`setMode`、`scatter`、`applyFromScene`、`rayMarch`、`applyFog`、`setFogHeight`、`setFogStart`、`setFogEnd`、`setCamera`、`setLightDirection`、`setDensity` 等
- `FogVolume`：`setShape/getShape`、`setPosition`、`setSize`、`setExtinction/getExtinction`、`setAlbedo`、`setEmissive`、`setAnisotropy/getAnisotropy`、`setEdgeFalloff/getEdgeFalloff`。`setNoise(amount, scale, seed)` 可加入确定性的世界空间密度变化，`getNoiseAmount/getNoiseScale/getNoiseSeed` 返回当前设置；调用 `Volumetric.setCamera()` 后，通过 `injectFroxelLocalVolume(volume)` 按当前视锥注入 froxel 网格。
- `Volumetric` froxel：`configureFroxelGrid`、`clearFroxelGrid`、
- `AmbientOcclusion`：`setQuality`、`setMode`、`setCamera`、`setRadius`、`setBias`、`setIntensity`、`setPower`、`compute`、`blur`、`applyOverlay`、`applyFromDepth`、`resolutionFor` 等
- `GlobalIllumination`：`setQuality`、`setCamera`、`setRadius`、`setIntensity`、`setLightDirection`、`setLightColor`、`applyFromDepth`、`getSampleCount` 等
- `AntiAliasing`：`setQuality`、`setMode`、`apply`、`applyTo`、`applyCanvas`、`applyCanvasTo`、`suggestScale`、`resolutionFor`、`setFloat`、`getFloat` 等

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。

**源码：** [`src/modules/graphics/`](../../../src/modules/graphics/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `graphics`、`Camera2D`、`Camera3D`。
### HDR、景深与反射探针绑定

详见 [Shader 与资源管理](graphics/shader-resources.md#hdr景深与反射探针绑定)。

### 预编译网格 Shader 与画布快照

详见 [Shader 与资源管理](graphics/shader-resources.md#预编译网格-shader-与画布快照)。

### 自定义网格资源和光栅状态

详见 [Shader 与资源管理](graphics/shader-resources.md#自定义网格资源和光栅状态)。

### 自定义 Mesh Shader 实例资源

详见 [Shader 与资源管理](graphics/shader-resources.md#自定义-mesh-shader-实例资源)。

### 显示器 HDR（swapchain 输出）

详见 [Shader 与资源管理](graphics/shader-resources.md#显示器-hdrswapchain-输出)。

# Gaussian scatter bloom

详见 [Shader 与资源管理](graphics/shader-resources.md#gaussian-scatter-bloom)。

### 资源纹理内部共享

详见 [Shader 与资源管理](graphics/shader-resources.md#资源纹理内部共享)。

### Native vegetation wind reference (C++)

详见 [植被风场](graphics/vegetation-wind.md#native-vegetation-wind-reference-c)。

### Squirrel wind controls

详见 [植被风场](graphics/vegetation-wind.md#squirrel-wind-controls)。

### Tree prototype vertex wind

详见 [植被风场](graphics/vegetation-wind.md#tree-prototype-vertex-wind)。

### Pcg 水面系统控制器

详见 [水面与环境控制](graphics/water-and-environment.md#pcg-水面系统控制器)。

### Pcg WindManager 音量控制

详见 [水面与环境控制](graphics/water-and-environment.md#pcg-windmanager-音量控制)。

### Pcg 程序化水面网格

详见 [水面与环境控制](graphics/water-and-environment.md#pcg-程序化水面网格)。

### PWS 水波方向

详见 [水面与环境控制](graphics/water-and-environment.md#pws-水波方向)。

### Pcg 水深色带

详见 [水面与环境控制](graphics/water-and-environment.md#pcg-水深色带)。

### Pcg 平面水反射规划

详见 [水面与环境控制](graphics/water-and-environment.md#pcg-平面水反射规划)。

### Pcg 水下效果状态机

详见 [水面与环境控制](graphics/water-and-environment.md#pcg-水下效果状态机)。

### Pcg 反射探针距离裁剪

详见 [水面与环境控制](graphics/water-and-environment.md#pcg-反射探针距离裁剪)。

### Pcg PhotoMode Water 权威配置

详见 [水面与环境控制](graphics/water-and-environment.md#pcg-photomode-water-权威配置)。

### Pcg PhotoMode Grass 权威配置

详见 [水面与环境控制](graphics/water-and-environment.md#pcg-photomode-grass-权威配置)。

### Pcg PhotoMode Graphics 权威配置

详见 [水面与环境控制](graphics/water-and-environment.md#pcg-photomode-graphics-权威配置)。

### Pcg PhotoMode 全局阴影倍率

详见 [水面与环境控制](graphics/water-and-environment.md#pcg-photomode-全局阴影倍率)。

### GPU skin influence limit

详见 [水面与环境控制](graphics/water-and-environment.md#gpu-skin-influence-limit)。

### Pcg ReflectionMasker

详见 [水面与环境控制](graphics/water-and-environment.md#pcg-reflectionmasker)。

## 导入植被的原生 PBR

`material.setFoliageTranslucency(r,g,b,intensity,strength,normalDistortion,scattering,direct,ambient,shadow)`
返回 Result；验证完整候选 PBR 状态后才提交，并启用双面背面法线翻转，不替换已导入贴图。
`gfx.configureFoliageWind(material,dirX,dirZ,strength,bending,bendingSpeed,branch,branchSpeed,flutter,flutterSpeed,fadeDistance,bendFactor)`
返回 Result，在渲染线程复用 Graphics-owned 风场纹理，首次调用含纹理上传成本。
先调用 `model3d.prepareFoliageDeformation` 为匹配的静态网格建立变形数据。
`material.setFoliageWindTime(seconds)` 返回 Result，要求已配置风；时间由调用方显式注入，
不读墙钟。参数非法时材质状态不变；贴图借用在 Graphics 资源释放/关闭时失效，
材质必须先停止使用这些资源。调用均为 main/render 线程同步路径，无回调。

### 资源更新与每帧调用成本

建议初始化时直接 `gfx.loadTexture(path)`：脚本返回可直接使用的纹理，失败抛出诊断；C++ 返回 `ResultRef<Texture>`。首次调用完成解码和上传，缓存命中复用；后续尺寸 getter 不执行准备工作。这个简单入口与兼容 `newTextureFromFile` 共用同一缓存，不维护第二套资源。

- `newTextureFromFile(path)` 首次请求排队解码并在消费时上传；已缓存路径直接返回同一资源，文件删除或变化不会隐式重载。更新文件需显式 `reloadTextureFromFile(path)`。首次尺寸查询仍会完成延迟上传；每帧保存并使用已加载纹理。
- `setTextureSampler(texture, desc)` 修改期望配置，相同配置零后端工作；首次消费前合并配置，并复用 Graphics 生命周期内已准备的 sampler。Vulkan 发布改变的描述符仍需要一次批量在途帧等待；setter 本身不等待。持续改变 LOD bias 等配置会产生新 sampler，应避免每帧构造不同描述。释放纹理取消待发布状态。
- `updateMeshVertices` 是显式动态上传：每次调用仍有线性转换/上传成本，内部复用转换数组与 GPU 缓冲容量，不扫描全数组判断是否相同。源数据的 owner 应只在数据变化时调用；无需额外 token/commit。
- Groom 的 `update(dt)` 完整推进模拟；参数与可见性修改只记录变化。视图准备阶段（或直接 `draw` 的准备边界）合并几何刷新并复用同一网格。`getMesh` 等 getter 不触发刷新，返回最后发布结果；`rebuild` 是兼容的显式同步刷新。`Representation::None` 隐藏但保留网格以供再次显示复用。显式可见性掩码属于调用者当前视图，多视图时在各视图绘制前更新，不能跨视图复用最后一次掩码。
- Groom 必须在其 Graphics owner 销毁前析构；析构移除准备回调。网格、纹理与 shader 仍由 Graphics 统一回收，不产生第二套所有权。

## 补充绑定参考

参数与接收者见 [源码生成参考](graphics/api-reference.md)，使用前先确认本章的前置条件与生命周期。

## 集成任务：从资源到一帧

先在 init 解码 CPU 图像并创建 Texture，update 只改角色状态，render 按背景、世界、HUD 顺序提交。窗口 resize 后使用实际视口尺寸，设备重建后重建 GPU 资源。教程使用程序化纹理避免外部文件条件。3D 则先准备 Camera/Light/Renderable，再提交同一场景；曝光、阴影和后处理参数放在创建/设置阶段。昂贵的探针捕获和资源 upload 不混入每实体的 draw。

完整项目：[examples/game-tutorial](../../../examples/game-tutorial/README.md)，
其 [main.nut](../../../examples/game-tutorial/main.nut) 展示实际生命周期。
先验证现有资产可加载，再换成自己的资源。测试失败、取消、资源缺失和旧 revision，
不能只以脚本没有语法错误作为渲染或玩法正确的证据。
详细流程：[专题](graphics/rendering-effects.md)。
### AI 候选遮挡测量

`eve.prepareSceneVisibility(request)` 返回 owning 单次查询，`query.measure(gfx)` 返回结构化 Result。
MCP 可直接调用 `eve_scene_modify(action="visibility", target="object", params={width=256,height=256})`，
或 `params={preview=true}` 测量现有物理摆放候选，无需提交或移动实时场景。
结果包含 visibleFraction、fullPixels、visiblePixels、occluders 的 ObjectId/比例和深度歧义。
只有 Measured 状态可作测量证据；少于四像素为 Unmeasurable，Unsupported 不得替代为包围盒估计。
支持静态无贴图不透明网格，具体范围、预算和 schema 见 [遮挡接口](../../dev/2026-10-11-scene-visibility-validation.md)。

visibility-request v2 支持对象局部区域 target（v1 字符串 target 仍支持），证据为 v2；
区域用实际目标 depth 还原局部表面点筛选，多个区域复用对象 full pass。
参见 [区域协议与验证](../../dev/2026-10-11-scene-regions-validation.md)。

## 投影轮廓重叠

作者文档 v4 支持 projected_overlap(subject, reference, minimum, maximum)，可附 reference 的 local_box region。测量两个对象独立真实投影 mask 的交集/reference 像素数；前后顺序及第三者遮挡不改变该构图指标。visibility-evidence v3 的 projectedOverlaps 返回交集像素、双方像素数及比例；任一 mask 少于 4 像素不可测。关系通过原有 relations/relation_begin/relation_check/relation_commit 正式路径使用，提交重新测量并一次撤销。详见 [契约与验证](../../dev/2026-10-12-scene-projected-overlap-validation.md)。

## 同帧 SceneColor 与实例草坪

`gfx.captureMesh3DSceneColor()` 在 `gfx.render3D()` 后、消费该图像的自定义绘制前调用，返回
`captured` / `scheduled` / `explicitOverride` / `historyReuse` / `unavailable`。
它是渲染线程操作，不调用脚本回调；快照及绑定 18 的纹理由 Graphics 持有，使用期内不向脚本转移所有权。
代价约为一次全分辨率 GPU 颜色复制，应每帧捕获一次供所有草簇共享，不能每簇捕获。
`historyReuse` 明确表示使用历史帧，不能当作同帧证据；当前 Vulkan 同帧复制要求单采样场景通道。

实例资源 shader 允许在 **fragment stage** 使用 `set=0,binding=18` 的普通 `sampler2D`
读取已完成快照；vertex stage 和其他未登记的绑定仍被拒绝。实例矩阵仍在
`set=1,binding=33`，不改变已有 ABI。

`examples/stylized-meadow` 在绘制草之前捕获已受光、含阴影的地表颜色，按草根投影位置的亮度
决定整簇颜色。输入是画面中该位置实际可见的表面；根部被其他物体遮挡时会读取遮挡物的颜色，
并非无限制的离屏地表查询。输出不反馈到快照中。

边界变更仅扩展 graphics 的既有脚本和 shader 资源面；没有 capability、ECS/Link、事件或工具协议变更。
复用现有后端状态枚举，无全局附加状态、无跨模块 include，也未宣称新增裁剪等级。

### 可复用默认草坪 Style

[Stylized Grass](../../../assets/styles/stylized-grass/README.md) 提供三阶草色、CPU 生成的两组四帧遮罩、分组慢速摇曳与地面补色。复制目录并调用 `StylizedGrassStyle`；`examples/stylized-meadow` 使用同一份 style。要求原生 Vulkan 单采样与同帧 SceneColor 支持，接入、成本和实例数据契约见 style 文档。

### CPU 生成的默认草坪（无部署素材）

`gfx.buildStylizedGrassPoints(radius, extent, seed)` 在 CPU 上生成 Bridson 蓝噪声，返回结构化 Result，value 为交错 x/z 数值数组。初始化时调用；固定种子可复现，半径及半边长为正有限数，radius 至少 extent/1000、seed 非负、最多 49,999 点。

`gfx.buildStylizedGrassShader(records, pass)` 是初始化 builder：`pass` 为 `grass` 或 `ground`，records 是内存中的列主序仿射矩阵平坦数组，最多 64 MiB。返回 Result.value 中 Graphics 拥有的 shader 借用句柄；所有输入同步复制，调用后可释放。无调用方指针跨帧保留；限定渲染/VM 线程，不调用脚本回调或持有跨回调锁。非法矩阵、未知 pass、非 Vulkan 后端明确失败。失败不返回部分 style。

@cost 蓝噪声生成、CPU 矢量轮廓栅格化及 GPU 上传远高于普通参数更新，应在初始化阶段完成并跨帧摊销。草 pass 在 CPU 内存生成两种草型、各四帧遮罩，再上传纹理；地面 pass 不生成纹理。shader 已内嵌，无需外部 PNG/RGBA/SPIR-V/草根文件。每帧仍一次地面补色、一次 GPU 草实例绘制与一张同帧场景快照。

脚本封装、根点 ABI、参数和部署要求见 [默认草坪 style](../../../assets/styles/stylized-grass/README.md)。场景高度与剔除回调由脚本初始化时同步执行；没有每帧逐草簇脚本更新。
