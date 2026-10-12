# Interactive Shallow Water（GPU 浅水方程交互水面）

用 **Saint-Venant / 浅水方程** 在 GPU（`gpgpu` compute）上求解可交互水面：高度 `h` 与
水平动量 `(hu, hv)` 在笛卡尔格子上守恒推进。人物腿部用 **Capsule 碰撞体** 采样进格子，
产生排水、侧向涌浪与拖曳动量。无 GPU 时自动回退到与内核 1:1 的 CPU 参考求解器。

## 运行

```bash
cd examples/interactive-shallow-water
../../build/linux-debug/src/engine/eve run
```

或：

```bash
make run/<platform>-debug GAME=examples/interactive-shallow-water
```

## 操作

| 输入 | 作用 |
|---|---|
| `A` / `D` / `W` / `S` | 移动角色（双腿 capsule 跟随） |
| 鼠标左键 | 在水面点击处制造冲击波 |
| `Space` | 暂停 / 继续 |
| `R` | 重置水深与角色位置 |
| `G` | 打印当前是否走 GPU 路径 |
| `F` | 水面贴图：场通道（R/G/B）↔ 着色水色 |

## 文件

| 路径 | 说明 |
|---|---|
| `main.nut` | 场景装配、腿部碰撞体同步、网格上传与输入 |
| `config.nut` | 窗口与 `fluids` / `gpgpu` 模块裁剪 |

## 技术要点

- API：`eve.Fluids().newShallowWater(resX, resZ, cellSize)` → `ShallowWater`
- 每帧 `setColliderCapsule` 更新左右腿，再 `step(dt)`
- `buildSurfaceMesh(visualHeightGain)` 产出 positions/normals/uvs/indices/colors，经
  `gfx.updateMeshVertices` 上传；顶点色与场纹理共用通道语义
- 场纹理（`copyFieldToImage(image, velocityScale, depthContrast)`，RGBA8，格子分辨率）：
  - **R** = 水深（`depthContrast>0` 时为 crest heat：静止偏暗、浪峰偏红；否则 `h/maxDepth`）
  - **G / B** = 水平速度 `vx` / `vz`（0.5 为静止，按 `velocityScale` 映射）
  - **A** = 速率 `|v| / velocityScale`（泡沫 / 扰动掩码）
- 示例默认把场纹理贴在水面上（无光照），另有 UV 图例板；`F` 可切到着色反照率 + 法线
- 着色：`copyShadedAlbedoToImage`（深度起伏 + 流向色偏 + 泡沫）与
  `copyNormalToImage`（自由面斜率法线）每帧上传
- 单元测试见 `test/fluids_shallow_water.cpp`
