# 3D 基础图形绘制示例

这个示例直接使用长期 `Primitive3D` 脚本 API，展示：

- 忽略深度的 X/Y/Z 轴线；
- 保持屏幕像素节奏并持续流动的虚线；
- 参与场景深度测试的线框球和虚线 AABB；
- `Primitive3D` proxy 的生命周期和 stale 检查。

Linux Debug：

```sh
cd examples/primitive-drawing
../../build/linux-debug/src/engine/eve run
```

Windows Debug：

```powershell
make run/win32-debug GAME=examples/primitive-drawing
```

当前 Squirrel 长期对象入口提供 line、sphere、AABB。圆盘、弧、OBB、网格、胶囊、圆柱、
圆锥、箭头和视锥目前通过 C++ `PrimitiveSceneCanvas3D` 使用。
