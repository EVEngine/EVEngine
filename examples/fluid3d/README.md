# Fluid3D Lab（3D 体积流体实验室）

交互式 3D 体积流体 / 颗粒实验室：一个可运行项目里覆盖 19 个场景模式（`mode` 0–18），
外加 70 多个 opt-in 数值验收脚本。窗口标题 `EVEngine | Fluid3D Lab`，启动后默认**暂停**。

演示的能力：体积流体求解器（SPH 类）的材质 / 相态、表面重建与各向异性、颗粒发射器与
生命周期、碰撞体与接触抓取、热耦合、泡沫池、移动 SDF 障碍、重叠查询、网格化输出，
以及相机与渲染设置（折射 / 反射 / 粒子 impostor / 正交相机）等。

## 运行

```bash
cd examples/fluid3d
../../build/linux-debug/src/engine/eve run
```

或从仓库根目录：

```bash
make run/<platform>-debug GAME=examples/fluid3d      # <platform> = win32 / linux / macosx
```

## 操作

| 按键 | 作用 |
|---|---|
| `Space` | 暂停 / 继续（启动时是暂停状态） |
| `R` | 按当前模式重建场景 |
| `1`–`9` | Water / Viscous / Smoke / Granular / Jet / Mesh / Moving / Multiphase / Wheel |
| `0` | Mixing（左侧加热、右侧冷却，颜色跟随黏度） |
| `T` | Thermal（热耦合与热色） |
| `S` | Solid attachment（固体附着，跟随运动碰撞体） |
| `F` | Foam（泡沫池与合成耗时） |
| `D` | SDF（只做变换更新的移动 SDF 障碍） |
| `Q` | Query（重叠触发器，红 / 黄计数） |
| `M` | Maze（迷宫；模式内用 `A` / `D` 调角速度） |
| `B` | Bucket（水桶） |
| `W` | Bottle（威士忌瓶；按住 `D` 倾倒） |
| `K` | Wake（卡门涡街尾流） |

HUD 会实时打印粒子数、掉帧丢弃的秒数以及 Sim / Surface / Upload 三个阶段的耗时；
求解失败时窗口内显示错误提示，控制台打印 `VOLUME_FLUID_STOPPED: <原因>`。

## 文件

| 路径 | 说明 |
|---|---|
| `main.nut` | 入口：模式注册、按键分发、每帧求解与表面上传 |
| `config.nut` | 引擎 `config`：960×720、`hotReload = true` |
| `maze-support.nut` | 迷宫模式的几何与状态 |
| `bucket-support.nut` | 水桶模式 |
| `bottle-support.nut` | 威士忌瓶模式（姿态更新的 SDF） |
| `karman-support.nut` | 卡门涡街尾流模式 |
| `wave-support.nut` | 波浪发生器 |
| `*-check.nut` | opt-in 数值验收脚本（见下） |
| `assets/` | OBJ 网格（发射环、酒瓶等） |
| `fonts/` | `DejaVuSans-Bold.ttf` 与授权说明 |
| `eve_snapshot.json` | 示例快照 |

## opt-in 数值验收脚本

`*-check.nut` 都是**可选**探针，不会被 `main.nut` 自动加载；在运行中的示例里按需加载并
调用对应函数，例如：

```squirrel
dofile("thermal-check.nut"); verifyVolumeFluidThermalContact();
dofile("soak-check.nut");    beginVolumeFluidSoak();
```

它们用固定的步数与数值容差验证求解结果（粒子数、位移下限、表面能量等），失败时直接
`throw`。完整的脚本清单、每个探针覆盖的 API 与判定条件见
[`docs/usr/fluid3d.md`](../../docs/usr/fluid3d.md)。

## 契约

本示例是可运行示例（`main.nut` + `config.nut`），参与 `scripts/smoke_examples.sh` 的
帧循环契约：CI 在 Linux 上无头启动它，要求至少 2 秒仍然存活且日志中没有错误标记。
