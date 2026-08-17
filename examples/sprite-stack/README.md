# Sprite Stacking

伪 3D sprite-stacking 示例：把程序化生成的三维几何体（圆柱 / 球 / 圆锥 / 方块）
在 CPU 上沿指定轴切成多层 RGBA 图，再以「叠片」方式画进 3D 前向渲染通道。

运行：

```bash
eve run examples/sprite-stack
```

操作：

| 键 | 作用 |
|----|------|
| `A` / `D` | 绕 Y 轴旋转叠片堆（伪 3D 视差） |
| `W` / `S` | 拉近 / 拉远相机 |
| `H` | 切换 vertical（朝向相机的竖直切片）/ horizontal（水平俯视切片）模式 |
| `1`..`4` | 切换圆柱 / 球 / 圆锥 / 方块 |
| `M` | 切换程序化几何体 / `assets/rock.obj`（`sliceModel` 模型文件切片） |
| `Q` / `E` | 调整切片厚度 |
| `R` | 重新切片 |

技术要点：

- 切片：`spritestack.slicePrimitive(...)`（或 `sliceModel` 对 3D 模型文件切片）
  把网格切成 `layerCount` 个薄片，每个薄片正交投影成一张 RGBA 图。
- 渲染：`SpriteStack3D` 用 alpha 混合 + 深度测试的 hair 管线绘制切片，
  `vertical` 模式为朝向相机的 billboard，`horizontal` 模式为水平四边形；
  每帧按相机距离由远到近排序。
- 调用时机：`gfx.render3D()` 之后、`present()` 之前调用 `stack.render(gfx)`，
  切片即与场景内其他 3D 网格正确遮挡。
- 高级功能：`setShadowEnabled` 投影假阴影、`setOutline` 风格化描边；另外三个
  共享纹理的小石头通过 `SpriteStackBatch` 合批成单次 draw。
