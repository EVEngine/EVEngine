# 3D 相机控制器（CameraController）

**脚本入口：** `eve.CameraController()`

在 `Camera3D` 之上提供可复用的视角行为：跟随、环绕、俯视、第一人称与过场
（命名视角序列）。`Camera3D` 本身只描述"看哪里"；本模块负责"怎么移动"。

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
- 跟随目标：`setTarget(x,y,z)` / `getTargetX()` / `getTargetY()` / `getTargetZ()`、
  `setOffset(x,y,z)`、`setLookAhead(v)`。
- 模式：`setMode("follow"|"orbit"|"topdown"|"firstperson"|"cinematic")` / `getMode()`。
- 环绕：`setRadius`、`setAzimuth`、`setElevation`、`setOrbitSpeed`、`setYaw`、`setPitch`。
- 平滑：`setSmooth(k)`、`setMaxSpeed(v)`、`snap()`（立即到位）。
- 过场：`addView(name, ex,ey,ez, tx,ty,tz)`、`switchTo(name)`、
  `playSequence(seconds)`、`stopSequence()`、`isPlaying()`。
- `update(dt)`：每帧驱动（放到 `eve_update`）。

## 生命周期

- `Camera3D` 由脚本创建并 `setActive(true)` 才会被 `RenderSystem3D` 采用；
  控制器只改其 eye/target，不负责渲染。
- 示例：[`examples/camera-controllers`](../../../examples/camera-controllers/main.nut)。
