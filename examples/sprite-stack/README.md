# SpriteStack 2D 示例

这个示例把程序化几何体切成 RGBA 水平切片，再通过 Graphics 的普通 2D 纹理管线逐层偏移绘制。运行阶段没有 3D 相机、3D 网格、深度缓冲或专用 3D shader。

运行：

```powershell
build\\win32-debug\\src\\engine\\eve.exe run examples\\sprite-stack
```

操作：A / D 旋转叠片，1～4 切换圆柱、球、圆锥、方盒，Q / E 调整层间像素距离。

主物体直接调用 `stack.render(gfx)`；左右两个副本通过 `SpriteStackBatch` 提交，共享同一组切片纹理。
