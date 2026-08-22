# Virtual Geometry — 虚拟几何体（Nanite 风格）

演示 `eve.VirtualGeometry()`：把程序化 icosphere 预处理成层级 cluster DAG，
每帧 GPU 驱动 cluster 剔除（视锥 + 屏幕空间误差 LOD），再软件光栅化进 visibility buffer。
架构见 [`docs/dev/VIRTUAL_GEOMETRY.md`](../../docs/dev/VIRTUAL_GEOMETRY.md)。

## 运行

```bash
make run/<platform>-debug GAME=examples/virtualgeometry
```

## 注意

- **仅 Vulkan 后端**，且为计算演示：每帧输出 cluster / 三角形 / LOD 统计到 stdout，
  屏幕显示背景色。要把结果画出来，需把 `resolve()` 返回的 RGBA `ByteData`
  包成 `ImageData` 纹理后绘制（见 `demo.nut` 文件头注释）。
