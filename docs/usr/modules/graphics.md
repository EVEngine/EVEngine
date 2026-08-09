# 图形渲染模块

**脚本入口：** `eve.Graphics()`

清屏、2D 图元、纹理、Canvas、摄像机和 3D 渲染。

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

## 常见问题

- 忘记每帧 `clear()`，保留未定义的旧帧内容。
- 每帧编译 shader 或上传纹理。
- 2D/UI/3D 提交顺序错误导致覆盖。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `clear()`、`declareFloat()`、`declareMatrix()`、`declareVec2()`、`declareVec3()`、`declareVec4()`、`drawSolidRect()`、`drawTexturedRect()`
- `getCastShadow()`、`getDirX()`、`getDirY()`、`getDirZ()`、`getHeight()`、`getName()`、`getRadius()`、`getShader()`
- `getShadowBias()`、`getShadowStrength()`、`getType()`、`getUniformIndex()`、`getWidth()`、`getX()`、`getY()`、`getYaw()`
- `getZ()`、`hasUniform()`、`isEnabled()`、`newMeshShader()`、`newMeshSphere()`、`newQuad()`、`newShader()`、`newShaderFromSpvFile()`
- `newTexture()`、`present()`、`render3D()`、`reset()`、`sendFloat()`、`sendVec2()`、`sendVec3()`、`sendVec4()`
- `setActive()`、`setAmbient()`、`setBackgroundColor()`、`setCamera()`、`setCanvas()`、`setCastShadow()`、`setColor()`、`setDirection()`
- `setDirectionalLight()`、`setEnabled()`、`setEnvIntensity()`、`setEnvMap()`、`setEye()`、`setFov()`、`setMesh()`、`setMetallic()`
- `setNormalTexture()`、`setPosition()`、`setRadius()`、`setReceiveLight()`、`setReceiveShadow()`、`setRotation()`、`setRoughness()`、`setScale()`
- `setShader()`、`setShadowBias()`、`setShadowStrength()`、`setTarget()`、`setTexture()`、`setTint()`、`setType()`、`setUp()`
- `setViewport()`、`setVisible()`、`setYaw()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/graphics/`](../../../src/modules/graphics/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `graphics`。
