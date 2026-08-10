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

在 `eve_render()` 开始调用 `clear()`，随后按背景、地图、角色、粒子、UI 的顺序提交。纯色占位使用 `drawSolidRect()`；已有 Texture 使用 `drawTexturedRect()`。正常主循环由引擎负责 present。

### 渲染带光照的 3D 对象

初始化时创建 mesh、shader 和 renderable，设置 camera、ambient 和 directional light；每帧只更新 transform/material 参数，最后调用 `render3D()`。阴影开关、bias 和 strength 应逐场景调节。

### 屏幕空间体积光（尘雾光柱）与体积雾

`vol <- gfx.newVolumetric()`。`setQuality("low"|"medium"|"high")` 控制采样与 `resolutionFor`。

- **screenspace**：`beginOcclusionMap` → `drawOccluders2D` → `scatter`；或 `applyFromScene`
- **raymarch**：`setMode("raymarch")` + `setCamera` + 线性深度 → `rayMarch`
- **fog**：`setMode("fog")` + `setFogHeight*` / `setFogStart`/`End` + 线性深度 → `applyFog`（雾色 alpha 叠加场景）

细节见 [`体积光模块设计.md`](../体积光模块设计.md)。

### 2D 屏幕拾取

`Camera2D` 用 `setPosition` / `setZoom` 控制视口中心与缩放；`screenToWorldX/Y(screenX, screenY, viewW, viewH)` 把鼠标像素换成世界坐标（`viewW/H` 通常取 `gfx.getWidth/Height`），再交给 Math 的 `pointIn*` 或 Physics 的 `testPoint` / `queryAABB`。

### 3D 屏幕拾取

`Camera3D.screenToRay(screenX, screenY, viewW, viewH)` 写入眼点与单位方向，用 `getScreenRayOrigin*` / `getScreenRayDir*` 读取，再对包围球/盒调用 Math 的 `raycastSphere` / `raycastBox`。

## 常见问题

- 忘记每帧 `clear()`，保留未定义的旧帧内容。
- 每帧编译 shader 或上传纹理。
- 2D/UI/3D 提交顺序错误导致覆盖。
- 拾取时 `viewW/H` 与实际渲染 drawable 不一致，射线会偏。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `bakeMeshMorph()`、`clear()`、`clearMorphWeights()`、`declareFloat()`、`declareMatrix()`、`declareVec2()`、`declareVec3()`、`declareVec4()`
- `drawSolidRect()`、`drawTexturedRect()`、`drawOcclusionSolid()`、`drawOcclusionTexture()`、`getCastShadow()`、`getCastOcclusion()`、`getDirX()`、`getDirY()`、`getDirZ()`、`getHeight()`、`getMorphCount()`
- `getMorphName()`、`getMorphWeight()`、`getName()`、`getRadius()`、`getScreenRayDirX()`、`getScreenRayDirY()`、`getScreenRayDirZ()`、`getScreenRayOriginX()`
- `getScreenRayOriginY()`、`getScreenRayOriginZ()`、`getShader()`、`getShadowBias()`、`getShadowStrength()`、`getType()`、`getUniformIndex()`、`getVertexCount()`
- `getVolumetric()`、`getVolumetricIntensity()`、`getWidth()`、`getX()`、`getY()`、`getYaw()`、`getZ()`、`getZoom()`、`hasMorph()`、`hasMorphData()`
- `hasUniform()`、`isEnabled()`、`isMorphDirty()`、`newMeshCylinder()`、`newMeshShader()`、`newMeshSphere()`、`newQuad()`、`newShader()`
- `newShaderFromSpvFile()`、`newTexture()`、`newVolumetric()`、`present()`、`render3D()`、`reset()`、`screenToRay()`、`screenToWorldX()`、`screenToWorldY()`
- `sendFloat()`、`sendVec2()`、`sendVec3()`、`sendVec4()`、`setActive()`、`setAmbient()`、`setBackgroundColor()`、`setCamera()`
- `setCanvas()`、`setCastOcclusion()`、`setCastShadow()`、`setColor()`、`setDirection()`、`setDirectionalLight()`、`setEnabled()`、`setEnvIntensity()`、`setEnvMap()`
- `setEye()`、`setFov()`、`setMesh()`、`setMetallic()`、`setMorphWeight()`、`setNormalTexture()`、`setPosition()`、`setRadius()`
- `setReceiveLight()`、`setReceiveShadow()`、`setRotation()`、`setRoughness()`、`setScale()`、`setShader()`、`setShadowBias()`、`setShadowStrength()`
- `setTarget()`、`setTexture()`、`setTint()`、`setType()`、`setUp()`、`setViewport()`、`setVisible()`、`setVolumetric()`、`setVolumetricIntensity()`、`setYaw()`
- `setZoom()`、`worldToScreenX()`、`worldToScreenY()`
- `Volumetric`：`setQuality`、`setMode`、`scatter`、`applyFromScene`、`rayMarch`、`applyFog`、`setFogHeight`、`setFogStart`、`setFogEnd`、`setCamera`、`setLightDirection`、`setDensity` 等

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/graphics/`](../../../src/modules/graphics/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `graphics`、`Camera2D`、`Camera3D`。
