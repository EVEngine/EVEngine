# Shader Live Preview — 实时 GLSL 热重载预览

该示例用一份自定义 GLSL 片元着色器渲染由球体、立方体、圆柱体和地面组成的 3D 预览场景，
并开启引擎的资产热重载：保存 `shaders/preview.frag` 即会重新编译并事务式替换管线，
无需重启引擎或重新构建。窗口为 960×540，标题 `EVEngine Live Shader Preview`。

## 运行

```bash
make run/<platform>-debug GAME=examples/shader-live-preview
```

也可构建后进入示例目录直接启动引擎：

```bash
cd examples/shader-live-preview && ../../build/linux-debug/src/engine/eve run
```

Windows 上引擎可执行文件为 `build/win32-debug/src/engine/eve.exe`。

## 演示内容

- 场景搭建：`eve.Camera3D()` 的 `setEye/setTarget/setUp/setFov/setAmbient/setActive`、
  `gfx.setDirectionalLight`，以及 `gfx.newMeshSphere(48, 24)`、`gfx.newMeshCube(1.0)`、
  `gfx.newMeshCylinder(48, 1, true)` 创建的四个 `eve.Renderable3D()` 模型。
- 模型设置：`setMesh`、`setShader`、`setPosition`、`setScale`、`setTint`、`setRoughness`、
  `setCastShadow`、`setReceiveShadow`。
- 自定义着色器：`gfx.loadMeshShaderSpv("", "shaders/preview.frag.spv")`（顶点源码留空），
  片元阶段以**提交的 SPIR-V** 加载——运行期 GLSL 编译需要 `glslc`，Windows 上不可用；
  改完 `shaders/preview.frag` 用 `glslc -o shaders/preview.frag.spv shaders/preview.frag` 重新生成。
  随后 `declareFloat("time")` 声明推送常量，`eve_update` 中每帧 `sendFloat("time", previewTime)`。
- 片元入口 `main()`：读取 `Frame` UBO 的 `lightDirIntensity` 与 `ambient`，以推送常量 `pc.data[0]`
  作为时间，混合 `vTint`、漫反射、边缘光，以及随时间摆动的色带与 UV 网格线。
- 渲染：`gfx.clear()` + `gfx.render3D()`；`eve_update(dt)` 让前三个模型按 `setYaw(previewTime * ...)` 自转。

## 热重载

`config.nut` 中 `hotReload = true`。启动时引擎打印 `hot-reload: watching N path(s)`，
通过 `hot.watchTree(".")` 监视示例目录整棵树；非 `.nut` 文件变更后调用脚本钩子
`eve_asset_reload(path)`，`main.nut` 用 `path.find("preview.frag")` 过滤，只处理该文件。

保存 `shaders/preview.frag` 后的流程：`fs.readText("shaders/preview.frag")` 重新读取源码，
再调用 `gfx.replaceShaderFromGlsl(previewShader, "", source)` 事务式替换管线
（空顶点源码表示使用该着色器类型的后端默认顶点着色器）。
编译成功打印 `[shader-preview] reload applied`；失败打印 `[shader-preview] <诊断信息>`，
保留上一份可用管线，并把状态写入 `reloadMessage`。
实时编译需要 `glslc`；没有它（Windows、未装 Vulkan SDK）时热重载会回退到已提交的
`shaders/preview.frag.spv`，示例照常运行，只是保存 `.frag` 不会立刻生效。
直接编辑 `main()` 中的配色、`bands` 频率或 `grid` 阈值即可立刻看到差别。

## 操作

本示例未注册任何键盘或鼠标输入，没有交互按键。`previewTime` 自动累加驱动动画与推送常量，
改完着色器保存即可观察热重载效果。

## 相关文件

- `config.nut`：窗口尺寸/标题、`modules = ["gfx", "win", "fs"]` 与 `hotReload` 开关。
- `main.nut`：搭建预览场景、注册 `eve_asset_reload` 热重载钩子、每帧推送 `time` 并驱动自转。
- `shaders/preview.frag`：唯一的着色器源码，也是热重载的编辑目标（`#version 450` 片元着色器）。
