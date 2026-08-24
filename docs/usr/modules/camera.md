# 3D 相机控制器（CameraController）

**脚本入口：** `eve.CameraController()`

在 `Camera3D` 之上提供可复用的视角行为：跟随、环绕、俯视、第一人称与过场。
模块还提供优先级 Camera Rig、遮挡修正、构图、Impulse Modifier，以及可发送
`Event` 的 Camera Timeline。`Camera3D` 本身只描述“看哪里”；本模块负责“怎么移动”。

## 基本用法

```squirrel
cam <- eve.Camera3D();
cam.setEye(0.0, 3.0, 8.0);
cam.setTarget(0.0, 1.0, 0.0);
cam.setFov(50.0);
cam.setAmbient(0.3, 0.32, 0.34);
gfx.setDirectionalLight(-0.4, 0.9, 0.35, 1.9, 1.6, 1.3);

ctrl <- eve.CameraController();
ctrl.setCamera(cam);
ctrl.setMode("orbit");
ctrl.setTarget(0.0, 1.0, 0.0);
ctrl.setRadius(8.0);
ctrl.setOrbitSpeed(0.6);
ctrl.snap();

function eve_update(dt) { ctrl.update(dt); }
```

## Rig 与 Director

Rig 是一份可复用的相机参数快照，Director 会选中优先级最高的 enabled Rig：

```squirrel
ctrl.setMode("follow");
ctrl.setOffset(0.0, 2.8, 7.0);
ctrl.setFov(55.0);
ctrl.addRig("gameplay", "follow", 10);

ctrl.setMode("orbit");
ctrl.setRadius(5.0);
ctrl.setElevation(20.0);
ctrl.setFov(42.0);
ctrl.addRig("boss", "orbit", 30);

ctrl.setRigEnabled("boss", false);      // gameplay 自动接管
ctrl.activateRig("boss", 0.6);          // 显式平滑切换
ctrl.saveRigState("boss");              // 保存当前参数回 preset
```

`addRig(name, mode, priority)` 创建时保存当前的 target、offset、lookAhead、构图、
orbit/first-person 参数、FOV、平滑和限速。`removeRig` 删除，`getActiveRig` 返回当前
Rig。`setRigPriority` / `setRigEnabled` 可在游戏状态变化时驱动 Director。

## 构图、目标和遮挡

- `setTargetNode(nodeRef)` 直接跟随稳定的 `SceneNodeRef`；传 `null` 后可重新使用
  `setTarget(x,y,z)`。Scene 的 transform 应先于 camera 更新。
- `setComposition(x,y)` 设置归一化横纵构图偏移；范围会限制在 `-0.9..0.9`。
- `setDeadZone(radius)` 让目标在世界空间死区内移动时不推动镜头。
- `setFov(degrees)` / `getFov()` 管理镜头 FOV，输入限制为 `1..179` 度。
- `addInput(yawDeltaDeg,pitchDeltaDeg,zoomDelta)` 接收与设备无关的输入增量，统一
  驱动 orbit / first-person；调用方可从鼠标、手柄或触摸模块提供 delta。
- `setCollisionEnabled` 开启球形探针；`setCollisionRadius` 设置探针半径，
  `setCollisionRecovery` 设置遮挡解除后的恢复阻尼。
- 若 Physics/Box3D 模块存在，探针会自动查询所有存活的 `World3D`，无需逐个注册；
  `setCollisionMask(mask)` 过滤 category bits，`setCollisionIgnoredBody(id)` 排除玩家刚体，
  `getCollisionBodyId()` 返回当前命中的动态刚体。Physics 不存在时 camera 仍可独立使用。
- `addCollisionBox(minX,minY,minZ,maxX,maxY,maxZ)` 注册静态遮挡体，
  `clearCollisionBoxes` 清空；`isObstructed()` 查询当前状态。

碰撞缩进会立即发生，恢复采用阻尼，避免相机穿墙或解除遮挡时弹跳。

## Modifier / Impulse

`addImpulse(positionAmplitude, rotationAmplitude, duration, seed)` 叠加一个确定性、
随时间衰减的位置与旋转冲击，适合命中、爆炸和落地反馈；`clearImpulses()` 立即清除。
`addFovImpulse(degrees,duration)` 叠加冲刺/受击常用的 FOV 脉冲。多个 modifier
可以同时叠加，不会改变 Rig 的基础参数。

## Timeline 与 Event

```squirrel
local cameraEvents = eve.Event();
ctrl.setEventSink(cameraEvents);
ctrl.clearTimeline();
ctrl.addTimelineCut(0.0, "gameplay", 0.0);
ctrl.addTimelineEvent(1.2, "camera.dialogue", "intro");
ctrl.addTimelineFloat(0.0, "fov", 48.0);
ctrl.addTimelineFloat(2.0, "fov", 36.0);
ctrl.addTimelineCut(2.0, "boss", 0.75);
ctrl.playTimeline(false);

// 既可从共享 Event 队列消费，也可直接从 controller 的 FIFO 队列逐个消费 marker。
local marker = ctrl.consumeTimelineEvent();
if (marker != "") print(marker + ": " + ctrl.getTimelineEventData() + "\n");
```

- `playTimeline(loop)` / `pauseTimeline()` / `stopTimeline()` 控制播放。
- `seekTimeline(time, fireEvents)` 定位；`getTimelineTime()` 和
  `isTimelinePlaying()` 查询状态。
- `addTimelineCut(time, rigName, blendTime)` 添加切镜点。
- `addTimelineEvent(time, name, data)` 添加 marker。若设置了 `Event` sink，marker
  同时进入全局事件队列；否则仍可通过 `consumeTimelineEvent()` 读取。单帧跨过多个
  marker 时不会覆盖，可用 `getPendingTimelineEventCount()` 查询积压数。
- `addTimelineFloat(time, property, value)` 添加线性属性关键帧；property 支持 `fov`、
  `radius`、`smooth`、`compositionX`、`compositionY`。

## Camera Asset 与调试快照

`serializeAsset()` 将全部 Rig、cut、marker 和 float track 输出为带 `version` 的 JSON；
`deserializeAsset(json)` 校验后替换当前配置。游戏可配合 Filesystem/Data 模块把字符串
保存为 `.camera.json`。`getRigCount()`、`getTimelineDuration()`、
`getPendingTimelineEventCount()`、`getCollisionBodyId()` 可供编辑器 inspector、测试和
运行时 HUD 生成轻量调试快照。

## 目标导向指南

### 第三人称跟随玩家

`setMode("follow")` + `setTarget(x, y, z)`（每帧更新为玩家坐标）+
`setOffset(0, 2.6, 6.0)` + `setSmooth(6.0)`；玩家高速移动时 `setLookAhead`
让镜头略微前探。

### 过场动画

`setMode("cinematic")` 前用 `addView("front", ...)` 等注册多个命名视角，
`playSequence(3.0)` 在视角间平滑切换，`stopSequence()` / `isPlaying()` 控制。

## API 快查

### `CameraController`

- 绑定：`setCamera(cam)` / `getCamera()`。
- 跟随目标：`setTarget(x,y,z)` / `setTargetNode(nodeRef)` / `getTargetX()` /
  `getTargetY()` / `getTargetZ()`、`setOffset(x,y,z)`、`setLookAhead(x,y,z)`。
- 模式：`setMode("follow"|"orbit"|"topdown"|"firstperson"|"cinematic")` / `getMode()`。
- 环绕：`setRadius`、`setAzimuth`、`setElevation`、`setOrbitSpeed`、`setYaw`、`setPitch`。
- 平滑：`setSmooth(k)` 同时设置两路阻尼；也可用 `setPositionSmooth(k)` /
  `setTargetSmooth(k)` 独立控制机位与朝向跟随，再配合 `setMaxSpeed(v)`、`snap()`。
- 过场（兼容 API）：`addView(name, ex,ey,ez, tx,ty,tz)`、`switchTo(name, blendTime)`、
  `playSequence(seconds)`、`stopSequence()`、`isPlaying()`。
- `update(dt)`：每帧驱动（放到 `eve_update`）。

## 生命周期

- `Camera3D` 由脚本创建时默认 active，也可用 `setActive` 控制是否被渲染器采用；
  控制器只改其 eye/target，不负责渲染。
- 示例：[`examples/camera-controllers`](../../../examples/camera-controllers/main.nut)。
