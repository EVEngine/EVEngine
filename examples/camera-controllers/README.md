# CAMERA — 3D Camera Controllers

Demonstrates `eve.Camera` / `eve.CameraController`: gameplay rigs selected by a
priority director, obstruction recovery, additive impulses, and a camera timeline with
event markers, in addition to the basic smoothed view behaviors.

Run it like any example:

```
make run/win32-debug GAME=examples/camera-controllers
```

## Behaviors

- **follow** — 自动追踪核心玩家（第三人称跟随）。镜头挂在 `target + offset`，始终看向目标。
- **orbit** — 自动盘旋。绕 `target` 公转，`azimuth` 按 `orbitSpeed` 自动旋转。
- **topdown** — 俯视。从 `target` 正上方 `radius` 高处往下拍。
- **firstperson** — 第一人称。`eye` 位于 `target`，用 `yaw` / `pitch` 控制朝向。
- **cinematic** — 过场 / 自动切换视角。在一组命名 `View` 之间平滑插值。

The center obstacle also exercises camera collision. Cinematic mode plays a looped
timeline that cuts between rigs and emits `camera.beat` markers.

## Controls

- **1–5** 或左侧面板按钮切换五种行为。
- **方向键**：firstperson 下调整朝向。
- **空格**：orbit 下切换自动盘旋开 / 关。

## API quick start

```squirrel
local cam  = eve.Camera3D();
local ctrl = eve.CameraController();

ctrl.setCamera(cam);
ctrl.setMode("follow");          // follow / orbit / topdown / firstperson / cinematic
ctrl.setTarget(px, py, pz);      // 要追踪/观察的对象
ctrl.setOffset(0.0, 2.6, 6.0);   // follow：镜头相对目标的偏移
ctrl.setSmooth(6.0);             // 平滑移动（指数阻尼）
ctrl.setOrbitSpeed(20.0);        // orbit：自动盘旋速度

// cinematic：定义一组命名视角，自动循环切换
ctrl.addView("front", 0, 3, 12, 0, 1, 0);
ctrl.addView("side",  14, 3, 0,  0, 1, 0);
ctrl.playSequence(3.0);          // 每 3 秒切到下一个视角

// 每帧驱动
eve_update = function(dt) {
    ctrl.update(dt);
};
```
