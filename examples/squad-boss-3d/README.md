# Squad Boss 3D / 四人协同打 BOSS

3D 动作小队示例：玩家 + 三名 AI 队友围攻 **COLOSSUS**。把三套现成能力叠在同一场战斗里：

| 系统 | 本例用法 |
| --- | --- |
| **EQS** (`sensing.queryPositions`) | 队友支援站位与危险圈躲避：圆环候选 → 宿主按支援带/间距/威胁再评分 |
| **GAS** (`Action` / `Attributes` / `Effects` / `Combat`) | 玩家 Slash / Breaker / Rally：时间线能力、能量、攻击修正、暴露易伤、原生伤害结算 |
| **多队友协同** (`npc_ai` + 预约/指挥) | Bastion / Lumen / Vesper：效用评分、治疗预约互斥、1–4 指挥、原生状态机 |

```sh
make run/linux-debug GAME=examples/squad-boss-3d
# 或
eve run examples/squad-boss-3d
```

## 操作

| 输入 | 行为 |
| --- | --- |
| WASD / 方向键 | 移动玩家（接管后关闭自动演示） |
| J / K / L | GAS：Slash / Breaker / Rally |
| 1 / 2 / 3 / 4 | 指挥：均衡 / 集火 / 保护 / 撤退 |
| P | 切换自动演示 |
| V | 运行隔离契约测试 |
| R | 重置整场 |

启动即自动绕场输出伤害；玩家倒地或击杀 BOSS 后按 R 重开。

## 角色

- **YOU**：GAS 近战套件，Attribute.Energy / Attack，Rally = `State.Empowered`。
- **BASTION**：护盾优先，也会治疗与远程攻击。
- **LUMEN**：治疗优先；与 Bastion 共享治疗预约。
- **VESPER**：输出优先，空闲时协助治疗。
- **COLOSSUS**：预告范围砸地；队友用 EQS 选逃生点，没有自动无敌。

## 权威与边界

- `SquadBossSimulation` 拥有血量、法力、护盾、危险圈、预约与指挥；HUD / `squadObservation` 只读。
- 玩家攻击走 `AbilityRuntime` 准入与阶段；命中写入 `CombatRuntime.applyDamage`，再投影回模拟血量。
- 队友治疗/护盾仍由宿主执行（`CombatRuntime` 无治疗入口），攻击 BOSS 同样走原生伤害。
- EQS 的脚本便利入口是 World2D；本例把 `(x,y)` 映射到竞技场 XZ，不声称导航网格或遮挡 LOS。
- 这是示例级组合，不声称 UE5 GAS 网络预测、正式导航或联机权威。

## 文件

- `eqs.nut` — sensing 圆环查询与二次评分
- `planner.nut` / `behavior.nut` — 效用 AI 与原生状态机
- `simulation.nut` — 唯一战斗权威 + GAS 适配
- `presentation.nut` — 程序化 3D 机体与 HUD
- `verification.nut` — 预约、EQS、躲避、撤退、GAS 路径
- `data/*.json` — 版本化 ability timeline

## 验证

```sh
MIN_RUN_SECONDS=2 RUN_SECONDS=8 bash scripts/smoke_examples.sh squad-boss-3d
python3 scripts/check_examples.py
```

游戏内按 `V` 打印 `SQUAD_BOSS_VERIFY_PASSED`。字体见 `assets/fonts/NOTICE.txt`（DejaVu）。
