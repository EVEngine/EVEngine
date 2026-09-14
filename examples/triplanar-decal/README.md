# Triplanar Decal（三维投射贴花）

对照 Bilibili/UE5 教程「三维投射贴花 Decal 使其不再拉伸变形」：
单轴平面投射在贴花体积侧面会拉出竖纹；改为局部空间三平面（YZ/XZ/XY）
按 `|nLocal|^sharpness` 混合后，贴花可包住墙角/立面且保持纹理比例。

## 技术要点

1. **Screen-space box decal**：用 G-buffer 深度还原世界坐标，裁切到贴花体积。
2. **Planar**：`local.xy` 单轴采样；掠射面用 `dot(n, forward) < 0.1` 剔除（避免竖纹）。
3. **Triplanar**：在贴花局部空间对三轴投影采样，用法线权重混合；仅剔除真正背面。
4. **`blendSharpness`**：指数越大，转角处平面切换越硬。

## 运行

```sh
make run/linux-debug GAME=examples/triplanar-decal
# 或
cd examples/triplanar-decal && ../../build/linux-debug/src/engine/eve run
```

## 操作

| 键 | 作用 |
| --- | --- |
| `T` | 切换 planar / triplanar |
| `[` / `]` | 降低 / 提高 blend sharpness |
| `O` | 开关自动环绕 |
| `A` / `D` | 偏航 |
| `W` / `S` | 俯仰 |
| `R` | 重置视角 |

## 文件

- `main.nut` — 墙角场景 + Decal API（过程化网格贴图）
- `assets/grid_decal.png` — 可选离线网格贴花资源
- `scripts/gen_grid_decal.py` — 重新生成网格贴图
- `capture_settings.nut` — `capture_root.nut` 截图覆盖项示例
