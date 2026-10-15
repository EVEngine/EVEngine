# Mesh Impact Lab（网格冲击形变）

把 Box3D 的**碰撞命中事件**接到网格形变会话上：飞行弹丸撞上静态目标，按命中点的法向
冲量对目标网格做一次性塑性冲击，重建并重新上传网格后立刻可见。整个示例没有任何输入，
启动即自动演示，用于验证“物理命中 → 网格损伤 → GPU 重新上传”这条链路。

## 运行

```bash
cd examples/mesh-impact-lab
../../build/linux-debug/src/engine/eve run
```

或：

```bash
make run/<platform>-debug GAME=examples/mesh-impact-lab
```

## 演示内容

1. `prototype.cylinder1` 配方生成目标网格，再经 `mesh.subdivide` + `mesh.weld` 修饰图
   加密（细分 2 级、焊接容差 `1e-4`），作为可形变的目标。
2. `procgen.newMeshDeformationSession()` 持有该网格；`applyImpact()` 用命中点与法向
   冲量做塑性凹陷，`currentMeshResult()` 取回快照并 `procgen.uploadMesh()` 上传。
3. 静态目标体（2.8×4.4×2.8）开启命中事件，动态球体弹丸在 X 方向以 12 m/s 射出。
4. 命中冲量超过 `0.05` 时应用损伤，并隐藏弹丸；随后在第 45 帧之后保存截图。

成功标记：

- 控制台打印 `MESH_IMPACT_LAB_PASS impulse=... colliderPolicy=deferred impact=bounded`
- 当前目录写出 `mesh-impact-lab.png`

## 文件

| 路径 | 说明 |
|---|---|
| `main.nut` | 全部逻辑：配方、修饰图、物理世界、命中消费与截图 |
| `config.nut` | 引擎 `config`：1100×700、`hotReload = true` |
| `mesh-impact-lab.png` | 期望产出的画面（示例自带参考图） |

## 契约

本示例是可运行示例（`main.nut` + `config.nut`），参与 `scripts/smoke_examples.sh` 的
帧循环契约：CI 在 Linux 上无头启动它，要求至少 2 秒仍然存活且日志中没有错误标记。
