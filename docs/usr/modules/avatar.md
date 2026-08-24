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

- `getName()`：模块名（"Avatar"）。
- `newImageAvatar()`：新建 Image 分层立绘。
- `newLive2DAvatar()`：新建 Live2D 头像（需已注册真实后端，见 `getLive2DBackendName`）。
- `newVroidAvatar()`：新建 VRoid（3D）头像。
- `update(dt)` / `sync()` / `render(gfx)`：驱动全部实例。
- `getAvatarCount()`、`getLive2DBackendName()` / `hasLive2DBackend()`；内置 `null`
  后端只保存状态且不渲染，因此 `hasLive2DBackend()` 返回 false。

### `AvatarInstance`

- 通用：`getKind()`、`setPosition/getX/getY`、`setScale/getScaleX/getScaleY`、
  `setVisible/isVisible`、`setLayer/getLayer`、`setExpression/getExpression`、
  `setMotion/getMotion`、`setParameter/getParameter/hasParameter`、
  `getParameterCount/getParameterName`、`update/sync/release`。
- 图层：`addLayer(name, texture, z)`、`setLayerTexture`、`setLayerVisible`、
  `setLayerOffset`、`setLayerColor`、`setLayerZ`、`setLayerSize`、
  `getLayerCount/getLayerName/hasLayer`、`getLayerRenderable(name)`。
- 表情：`defineExpression(name, spec)`、`applyExpression(name)`；
  `transitionExpression(name, duration)` 对数值/布尔图层及 morph 通道做平滑过渡。

`getLayerRenderable(name)` 返回该层原生的 `Renderable2D`。兼容的图层 setter
仍可使用；需要完整 Sprite2D 能力时可直接调用 `setPosition`、`getX`、`getY`、
`setRotation`、`getRotation`、`setScale`、`setSize`、`setColor`、`setLayer`、
`getLayer`、`setVisible`、`isVisible`、`setTexture`、`setQuad`、
`setReceiveLight`、`getReceiveLight`、
`setCastOcclusion`、`getCastOcclusion`、`setBlendMode`、`getBlendMode`；混合模式可选
alpha、additive 或 opaque。Avatar 同步只传播角色整体变换、显隐和
相对层级，不会覆盖层上的旋转、Quad、Shader、Canvas、Camera 或光照设置。
- Live2D：`loadLive2DModel(path)`、`getLive2DBackendName`、`hasLive2DBackend`。
  真实插件可接收 Avatar transform、visibility、layer，并通过共享 2D draw queue
  输出 drawable；`hasLive2DBackend` 只在后端 `isRuntimeAvailable()` 时为 true。
- VRoid/3D：`loadVroidModelPath(path)`、`bindVroidModelData(data)`、
  `loadMorphNamesFromModel`、`setMesh`、`setTexture`、`setPosition3D`、
  `setRotation3D`、`setScale3D`、`getRenderable3D`、`getBoundMesh`、
  `getVroidModelPath`、`bakeMorphs`。
- 3D 动画：`bindAnimPlayer`、`bindAnimStateMachine`、`bindAnimSkin`、
  `registerMotion`、`setMotionBlendTime`；`setMotion(name)` 会播放已注册 Clip，
  或向绑定的状态机发送同名 trigger。`setApplyRootMotion/getApplyRootMotion` 控制
  根骨 X/Z 位移是否累加到 Avatar，`getRootMotionDeltaX/getRootMotionDeltaZ`
  可供角色控制器自行消费。绑定 `AnimSkin` 后，`update(dt)` 会计算世界 Pose、
  CPU skinning 并原位更新绑定 Mesh。
- 动画分层：`bindAnimLayerMixer(mixer)` 让 Avatar 直接消费通用 Override/Additive
  分层 Pose，并继续执行根运动、LookAt、蒙皮和骨骼附件。每帧事件可通过
  `getAnimationEventCount()`、`getAnimationEventLayer()`、`getAnimationEventName()`、
  `getAnimationEventPayload()` 读取。
- VRM 运行时语义：`mapHumanoidBone` / `autoMapHumanoidBones` 把标准 humanoid
  名称映射到当前动画骨架；`mapViseme` / `setViseme` 把口型语义映射到真实 mesh
  morph，并在切换口型时清零上一个通道。当前不解析 VRM 扩展元数据；导入器重做前可由
  游戏脚本或加载插件提供映射。`getHumanoidBoneName(semantic)` 返回已映射骨骼名。
- VRM LookAt：`setLookAtTarget(x, y, z)` 设置世界空间注视点，
  `setLookAtWeight(weight)` 控制影响强度，`clearLookAtTarget()` 关闭注视；运行时会限制
  头部偏航和俯仰，并叠加到当前动画 Pose。
- 骨骼附件：`attachToBone(name, semanticOrBone, renderable, ox, oy, oz)` 让现有
  `Renderable3D` 每帧跟随骨骼世界位置和旋转；`detachAttachment` 只解除绑定，不销毁
  外部对象，`getAttachmentCount` 返回当前附件数。
- 场景：`linkSceneNode(scene, nodeId)` 把 Image Avatar 的透明变换锚点或 3D Avatar
  的 `Renderable3D` 链接到 Scene 当前 Host；`isSceneLinked()` 查询状态。Image
  图层会继承节点的平移、旋转、缩放和显隐；3D Avatar 链接后由 Scene 驱动世界变换。
- 动画联动：`bindTween(tween)` / `unbindTween()` / `getBoundTween()`。

## 生命周期

- 头像实例由脚本持有（`newImageAvatar()` 等返回）；模块级 `update/render` 遍历全部实例。
- Image 层的 `Texture` 引用由 Graphics 持有，传给 `addLayer` 的纹理需保持有效。
- VRoid 路径绑定的是 `Model3D` 数据 + `Renderable3D`；`bakeMorphs` 把 morph 权重
  烘焙进网格，热重载后需重新绑定。
