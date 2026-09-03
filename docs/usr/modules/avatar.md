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

需要让装备栏自动驱动形象时，先为物品注册视觉定义，再绑定唯一权威的
`EquipmentSet`。Avatar 的 `update()` / `sync()` 会检测槽位签名变化并重建投影：

```squirrel
local av = avatar.newImageAvatar();
av.defineEquipmentVisual2D("armor.red", "body", "armor", redArmorTexture, 5);
av.bindEquipment(equipment);
// equipment.equipFromBag(...) 后，下一次 avatar.update/sync 自动显示 redArmorTexture。
```

3D 使用同一套槽位协议。空骨骼名表示跟随 Avatar 根变换的模块部件；填写
`rightHand` 等 humanoid semantic 或真实骨名时作为骨骼附件跟随动画：

```squirrel
av3d.defineEquipmentVisual3D("sword.iron", "weapon", swordRenderable,
                             "rightHand", 0.0, 0.0, 0.0);
av3d.bindEquipment(equipment);
```

会随身体多个骨骼变形的衣服不要使用刚性附件，而应注册为共享 Pose 的 Skinned Part。
身体、内衣、衬衣和外衣各自保留 Mesh 与 Material，但每帧读取同一个 Avatar Pose：

```squirrel
// 0 是身体的稳定材质槽；bodySkin 是针对 bodyMesh 和同一 Skeleton 创建的 AnimSkin。
av3d.bindSkinnedPart(0, "body", bodyMesh, bodyMaterial, bodySkin);

// 同一装备槽中的不同衬衣可以复用 partIndex=1；同时可见的不同槽不得占用同一 partIndex。
av3d.defineEquipmentSkinnedVisual3D("shirt.linen", "shirt", 1, "shirt",
                                    shirtMesh, shirtMaterial, shirtSkin);
av3d.setEquipmentVisualLayer("shirt.linen", "shirt", "shirt", 0);
```

Skinned Part 优先走 GPU joint/weight stream 和每 Mesh 独立 matrix palette；后端无法建立
GPU skinning stream 时会回退到 CPU 顶点蒙皮。`unbindSkinnedPart(partIndex)` 清除常驻
部件及其材质槽。`getSkinnedPartUpdateMode(partIndex)` 返回 `0=Unavailable`、`1=Gpu`、
`2=Cpu`，使降级和骨架/顶点不兼容可被编辑器与自动化观测。武器、帽子等刚性物件仍
使用 `defineEquipmentVisual3D`。

装备状态始终由 `EquipmentSet` 拥有；Avatar 只保存物品到视觉资源的定义和可重建
投影，不会反向修改装备。纹理和 3D Renderable 均为 borrowed，解绑或销毁 Avatar
不会销毁它们。2D 对应稳定 layer/attachment；3D 对应模块 root part 或共享动画 Pose
上的骨骼附件。大量同屏角色可在游戏侧进一步把稳定组合缓存为合并网格/atlas。

内置穿戴层级按由内到外排列：`body(0) → underwear(100) → shirt(200) →
outerwear(300) → cape(400)`，武器为 `weapon(500)`。项目可用
`defineEquipmentLayer(name, order, parent)` 增加裙装、护甲内衬、头发前后片等层；
`setEquipmentVisualLayer(item, slot, layer, withinOrder)` 记录每个视觉所在层和层内顺序。
正常透明贴图依靠顺序叠加；当外层完全覆盖内层时，再调用
`addEquipmentLayerOcclusion("outerwear", "shirt")` 明确隐藏关系，避免无意义 overdraw
和 3D 穿模。`getEquipmentRenderStack*` 返回当前实际可见的由内到外渲染栈，便于编辑器、
存档证据和批处理系统检查层次关系。

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
  `getParameterCount/getParameterName`、`defineParameter`、`getParameterDefault`、
  `getParameterMinimum`、`getParameterMaximum`、`update/sync/release`。参数元数据只描述
  inspector 建议范围，不会钳制运行时值，因此同一 Avatar 数据模型可用于开发工具和游戏内捏脸。
- 图层：`addLayer(name, texture, z)`、`setLayerTexture`、`setLayerVisible`、
  `setLayerOffset`、`setLayerColor`、`setLayerZ`、`setLayerSize`、
  `getLayerCount/getLayerName/hasLayer`、`getLayerRenderable(name)`。
- 表情：`defineExpression(name, spec)`、`applyExpression(name)`、`removeExpression(name)`；
  `getExpressionCount/getExpressionName` 提供稳定排序的项目表情列表；
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
  `registerMotion`、`unregisterMotion`、`getMotionCount`、`getMotionName`、`getMotionClip`、
  `setMotionBlendTime`；`setMotion(name)` 会播放已注册 Clip，
  或向绑定的状态机发送同名 trigger。`setApplyRootMotion/getApplyRootMotion` 控制
  根骨 X/Z 位移是否累加到 Avatar，`getRootMotionDeltaX/getRootMotionDeltaZ`
  可供角色控制器自行消费。绑定 `AnimSkin` 后，`update(dt)` 会计算世界 Pose、
  CPU skinning 并原位更新绑定 Mesh。
- 程序化骨骼：`setFootIKSolver`、`getFootIKSolver`、`setDynamicBoneSolver`、
  `getDynamicBoneSolver`、`setAnimConstraintStack`、`getAnimConstraintStack` 接入 animation
  模块创建的 solver。Avatar 不拥有这些对象；它们及其 Skeleton 必须覆盖 Avatar 的绑定
  生命周期。每帧顺序为基础动画、约束栈/Foot IK、Dynamic Bone、世界 Pose 与蒙皮。
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
- 多部件蒙皮：`bindSkinnedPart` / `unbindSkinnedPart` 管理常驻身体部件；
  `defineEquipmentSkinnedVisual3D` 注册随共享 Pose 变形的装备 Mesh/Material 槽。
- 装备投影：`defineEquipmentVisual2D`、`defineEquipmentVisual3D`、
  `defineEquipmentSkinnedVisual3D` 注册物品视觉，
  `bindEquipment` / `unbindEquipment` 管理非拥有绑定，`syncEquipmentAppearance` 可强制
  检查，`getEquipmentVisualItem(slot)` 查询当前投影。返回状态整数对应
  `Applied=0`、`Unchanged=1`、`Removed=2`、`Rejected=3`。
  `defineEquipmentLayer`、`addEquipmentLayerOcclusion`、`setEquipmentVisualLayer` 定义层级与
  遮挡；`getEquipmentRenderStackCount`、`getEquipmentRenderStackItem`、
  `getEquipmentRenderStackLayer` 查询最终稳定排序的可见栈。
- 动画联动：`bindTween(tween)` / `unbindTween()` / `getBoundTween()`。

## 生命周期

- 头像实例由脚本持有（`newImageAvatar()` 等返回）；模块级 `update/render` 遍历全部实例。
- Image 层的 `Texture` 引用由 Graphics 持有，传给 `addLayer` 的纹理需保持有效。
- VRoid 路径绑定的是 `Model3D` 数据 + `Renderable3D`；`bakeMorphs` 把 morph 权重
  烘焙进网格，热重载后需重新绑定。
