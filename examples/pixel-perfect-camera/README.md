# 完美像素正交相机

演示 `Camera3D.setPixelOrthographic` 与 `Camera2D.applyPixelPerfect`：

- 参考分辨率 `320×180`，`pixelsPerUnit=16`，在 `1280×720` 上得到 **4×** 整数缩放。
- 左侧 / 场景：纯正交俯视的方块棋盘（`CameraController` 模式 `ortho`）。
- 右侧 HUD：2D 像素面板经 `worldToScreen*` 映射，十字与 1px 色块在整数缩放下保持锐利。

## 运行

```sh
make run/linux-debug GAME=examples/pixel-perfect-camera
# 或
cd examples/pixel-perfect-camera
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json ALSOFT_DRIVERS=null \
  xvfb-run -a ../../build/linux-debug/src/engine/eve run --debug --mcp-port=7531 .
```

## 操作

- `A` / `D`：机位左右挪 0.3 世界单位，随后 `snapToPixelGrid`
- `S`：手动像素吸附
- `R`：复位

截图请用引擎 MCP `eve_screenshot`，不要用 Xvfb 帧缓冲。
