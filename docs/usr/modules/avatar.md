# Avatar 分层渲染

**脚本入口：** `eve.Avatar()`

人物 2D/3D 分层表现库：Image 图层叠加为基线，Live2D / VRoid 为可插拔后端。适合
立绘、剧情演出、角色换装与表情切换；与 `dialogue`（口型/表情/动作转发）和
`animation`（`bindTween` 绑定补间）配合使用。

## 基本用法

```squirrel
avatar <- eve.Avatar();
local av = avatar.newImageAvatar();
av.addLayer("body", null, 0);      // 纹理可为 null → 纯色层
av.addLayer("face", null, 1);
av.setLayerSize("body", 140.0, 280.0);
av.setLayerColor("body", 0.85, 0.55, 0.40, 1.0);
av.setLayerColor("face", 0.96, 0.86, 0.78, 1.0);
av.setPosition(120.0, 300.0);
av.setLayer(20);

av.defineExpression("shy", "blush=1");   // 表达式 = 图层可见性/参数切换
av.applyExpression("shy");
```

## 目标导向指南

### 做一个可换表情、可换装的立绘

分层 + 命名表达式即可：把身体、脸、腮红、嘴各放一层，用 `defineExpression`
定义"表情 = 一组图层可见性"，`applyExpression` 切换；换装就是 `setLayerTexture`
换纹理或 `setLayerColor` 换色。

### 接入对话口型与动作

`dlg.bindAvatar("alice", av)` 后，`dlg.setExpression` / `dlg.setMotion` 会自动
转发到 Avatar；开启口型后 `dlg` 把打字机包络写入 `setLipSyncParameter` 指定的
参数，Avatar 侧可映射到嘴部图层 alpha。每帧调用 `avatar.update(dt)` 与
`avatar.render(gfx)`。

## API 快查

### `Avatar`（模块）

- `newImageAvatar()`：新建 Image 分层立绘。
- `newLive2DAvatar()`：新建 Live2D 头像（需已注册后端，见 `getLive2DBackendName`）。
- `newVroidAvatar()`：新建 VRoid（3D）头像。
- `update(dt)` / `sync()` / `render(gfx)`：驱动全部实例。
- `getAvatarCount()`、`getLive2DBackendName()` / `hasLive2DBackend()`。

### `AvatarInstance`

- 通用：`getKind()`、`setPosition/getX/getY`、`setScale/getScaleX/getScaleY`、
  `setVisible/isVisible`、`setLayer/getLayer`、`setExpression/getExpression`、
  `setMotion/getMotion`、`setParameter/getParameter/hasParameter`、
  `getParameterCount/getParameterName`、`update/sync/release`。
- 图层：`addLayer(name, texture, z)`、`setLayerTexture`、`setLayerVisible`、
  `setLayerOffset`、`setLayerColor`、`setLayerZ`、`setLayerSize`、
  `getLayerCount/getLayerName/hasLayer`。
- 表情：`defineExpression(name, spec)`、`applyExpression(name)`。
- Live2D：`loadLive2DModel(path)`、`getLive2DBackendName`、`hasLive2DBackend`。
- VRoid/3D：`loadVroidModelPath(path)`、`bindVroidModelData(data)`、
  `loadMorphNamesFromModel`、`setMesh`、`setTexture`、`setPosition3D`、
  `setRotation3D`、`setScale3D`、`getRenderable3D`、`getBoundMesh`、
  `getVroidModelPath`、`bakeMorphs`。
- 动画联动：`bindTween(tween)` / `unbindTween()` / `getBoundTween()`。

## 生命周期

- 头像实例由脚本持有（`newImageAvatar()` 等返回）；模块级 `update/render` 遍历全部实例。
- Image 层的 `Texture` 引用由 Graphics 持有，传给 `addLayer` 的纹理需保持有效。
- VRoid 路径绑定的是 `Model3D` 数据 + `Renderable3D`；`bakeMorphs` 把 morph 权重
  烘焙进网格，热重载后需重新绑定。

