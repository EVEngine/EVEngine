# Interior Mapping（室内映射）

实现知乎专栏 [Interior Mapping室内映射技术拆解](https://zhuanlan.zhihu.com/p/2071667769171513389)
中的预投影 2D Atlas 室内映射：在无真实室内几何的情况下，透过窗户看到有透视深度的房间。

## 技术要点

1. **切线空间射线–AABB 求交**：每个窗户 tile 视为单位房间盒，用视线与远平面交点采样。
2. **伪透视缩放**：`scale = B / (B + depthNorm)`，对预投影贴图做相似三角形校正。
3. **房间图集**：运行时生成 4×4 正交预投影房间 Atlas，支持顺序 / 随机选房。
4. **前景遮挡**：文章中的深度图 Ray March 在此用**过程化家具 AABB** 代替（同算法：步进 + 二分求精），便于示例自包含、不依赖额外 set-1 深度贴图。
5. **窗框 + 掠射菲涅尔**：浅角时偏反射，掩盖预投影极限。

## 运行

```sh
make run/linux-debug GAME=examples/interior-mapping
# 或
cd examples/interior-mapping && ../../build/linux-debug/src/engine/eve run
```

## 操作

| 键 | 作用 |
| --- | --- |
| `O` | 开关自动环绕 |
| `A` / `D` | 偏航 |
| `W` / `S` | 俯仰 |
| `[` / `]` | 房间深度 |
| `-` / `=` | 透视强度 |
| `F` | 开关过程化家具遮挡 |
| `R` | 开关随机房间 |
| `1` / `2` / `3` | 浅 / 默认 / 深预设 |

## 文件

- `shaders/interior_mapping.frag` — Mesh3D 自定义片元着色器
- `main.nut` — 场景、Atlas 生成、参数绑定
- `config.nut` — 窗口配置
