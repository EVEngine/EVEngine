# Mesh Modifier Lab（网格修饰图实验室）

把同一份源网格（`prototype.cylinder1` 圆柱，高 4.0、细分 32）用 **13 种不同方式**变形后
并排渲染，用来对比“类型化修饰图（`MeshModifierGraph`）”“可交互形变会话
（`MeshDeformationSession`）”和“程序化变形节点”三条路径的覆盖面与结果。没有输入，
启动即静态展示，适合当作 API 对照表逐个查。

## 运行

```bash
cd examples/mesh-modifier-lab
../../build/linux-debug/src/engine/eve run
```

或：

```bash
make run/<platform>-debug GAME=examples/mesh-modifier-lab
```

## 13 个变体

| 变体 | 用的节点 / API |
|---|---|
| 源网格 | `mesh.input`（未变形基准） |
| Bend + Twist | `deform.bend`（Y 轴 28°）→ `deform.twist`（Y 轴 150°） |
| Noise + Spherify | `deform.noise`（幅度 0.28、seed 17）→ `deform.spherify` |
| FFD | `deform.ffd`（自由变形控制点） |
| Cut Plane | `mesh.cutPlane`（沿 Y 切平并封盖） |
| Spline Path | `mesh.subdivide` → `deform.splinePath`（Catmull-Rom 路径，按弧长缩放截面） |
| Sculpt | `MeshDeformationSession`：inflate 笔刷 + 方向笔刷 + 塑性冲击 |
| Effector | `deform.effector`（两点密度场） |
| Interactive Surface | `applySurfaceContact` + `recoverSurface`，以及 `configureColliderRefresh` / `updateColliderRefresh` / `requestColliderRefresh` 的 interval 与 manual 两种碰撞体替换路径 |
| Slime | `applySlimeImpulse` + `stepSlime`（弹簧-阻尼回弹） |
| Mesh Fit | `deform.transform` 作为目标面 → `deform.meshFit` 双向贴合 |
| Vertex Editor | `selectVerticesBox` → `moveSelectedVertices` / `manipulateSelectedVertices` |
| Sound React | `deform.soundReact`（电平 / 阈值 / 频率驱动变形） |

图编译同时验证了融合优化：控制台会打印编译后的段数与融合的操作数
（`getCompiledSegmentCount()` / `getFusedOperationCount()`）。

成功标记：

- 控制台打印 `MESH_MODIFIER_LAB_PASS variants=13 ...`
- 当前目录写出 `mesh-modifier-lab.png`

## 文件

| 路径 | 说明 |
|---|---|
| `main.nut` | 全部逻辑：源网格、13 个变体、相机、截图 |
| `config.nut` | 引擎 `config`：1280×720、`hotReload = true` |
| `mesh-modifier-lab.png` | 期望产出的画面（示例自带参考图） |

## 契约

本示例是可运行示例（`main.nut` + `config.nut`），参与 `scripts/smoke_examples.sh` 的
帧循环契约：CI 在 Linux 上无头启动它，要求至少 2 秒仍然存活且日志中没有错误标记。
