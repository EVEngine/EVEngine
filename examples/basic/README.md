# Basic — 一步一步的 2D 入门

[English](README.en.md)

默认只画一个矩形。先掌握窗口和绘制，再加载图片、加入移动与物理，最后加入粒子和火焰。

## 运行与选课

```sh
eve run examples/basic
# 源码构建也可用：make basic
```

SDK 用户从 SDK 目录运行 `bin/eve run share/eve/examples/basic`；Windows 使用 `bin\eve.exe`。
打开该副本的 `config.nut`，把 `tutorialStep = 1` 改为下面的编号，保存后**关闭窗口并重新运行**。
模块在启动时选择，因此切换课程需要重启；同一课程的脚本参数可以保存后热重载。
`main.nut` 只负责选课，完整课程在 `steps/` 下，不需要复制或覆盖文件。

## 学习顺序

1. **矩形** — [`01-rectangle.nut`](steps/01-rectangle.nut)。学习 `eve_init`、`eve_render`、清屏和像素坐标。修改位置、尺寸和颜色。
2. **图片** — [`02-image.nut`](steps/02-image.nut)。把矩形换成随示例提供的 `assets/box.png`。在初始化中加载纹理，每帧只绘制。
3. **移动** — [`03-movement.nut`](steps/03-movement.nut)。按住方向键移动，方块不会离开窗口。用 `speed * dt` 将每秒速度换算成每帧位移。
4. **物理** — [`04-physics.nut`](steps/04-physics.nut)。用重力代替键盘移动：方块落在地板上。物理拥有位置，绘制读取它；物体位置是中心，图片位置是左上角。
5. **粒子** — [`05-particles.nut`](steps/05-particles.nut)。在落地方块上加入跟随的火花。加载 emitter，先更新物理，再设置发射位置、更新和绘制粒子。
6. **火焰** — [`06-fire.nut`](steps/06-fire.nut)。保留火花，再加入火焰。修改 [`particles/fire.json`](particles/fire.json) 的颜色、速度、寿命和发射率，观察资产自动重载。

`config.modules` 随课程启用：第 1–2 步只绘制，第 3 步加入键盘，第 4 步加入物理，第 5–6 步再加入粒子。
`persist` 和初始化中的空值检查保留纹理、物理世界与 emitter，避免脚本热重载时重复创建。
热重载保留位置，不会让已经落地的方块重新下落；重新启动可重看落下过程。
图片为本示例原创的 16×16 RGBA 棋盘方块，无外部资产依赖。

## 继续学习

同目录的 `ui_demo.nut` 和 `ui_component.nut` 是独立的进阶 UI 变体。在副本中用它们替换 `main.nut`；
同时删除 `config.nut` 中的 `modules` 字段，恢复这些变体所需的模块实例。
地图请看 [Building Tilemap](../building-tilemap/README.md)，小游戏请看 [Game Tutorial](../game-tutorial/README.md)，
调试器、控制台与 AI 面板请看 [DevLab](../devlab/README.md)。
