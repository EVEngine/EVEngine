# RPG 对战平衡组件

`rpg_balance` 是 `rpg` 所属的原生卫星模块，向脚本提供 `eve.RPGBalanceSuite()`。它消费正式 RPG 注册表中的职业、技能、特质和状态定义；每场战斗从构筑生成独立角色，通过正式的装备属性、法力/冷却、Battle 伤害与状态结算运行。先加载游戏定义，再配置测试集。

```squirrel
local suite = eve.RPGBalanceSuite();
local configured = suite.configure(balanceSuiteSpec());
if (!configured.ok) throw configured.diagnostics[0].message;
local targets = suite.targets().value;
local report = suite.measure({"warrior.power": 1.8, "mage.power": 6.0,
    "ranger.power": 1.5, "mage.weapon": 14.0}, [101, 127], 0.5, 32);
if (!report.ok) throw report.diagnostics[0].message;
```

配置格式为 `evengine.rpg.balance.suite` 版本 1。`builds` 给出构筑 ID、职业 ID、基础属性、已学技能和装备槽属性；`strategies` 给出技能标签优先级、治疗血量阈值和保留法力比例；`scenarios` 给出血量/法力倍率与每秒法力回复；`bindings` 把候选参数投影到某构筑属性或装备槽；`matchups` 用有向边指定目标胜分区间。详见 [完整可运行示例](../../../examples/rpg-balance/README.md) 的 `suite.nut`。

`measure` 返回 `evengine.rpg.balance.report` 版本 1。`matrix[i][j]` 是构筑 `i` 对 `j` 的胜分，胜场 1、平局 0.5；顺序交换后的同一 seed 均计入，每对只模拟一次并填互补项。对角线填 0.5 作为展示值，未进行同构筑自博弈。`cells` 给出每个无序构筑对、独立双方策略组合及场景的胜负平局、对战轮数、法力消耗、技能使用和首场动作轨迹。`metrics` 只投影目标边，可直接作为 `BalanceSearch` 评估器返回值。报告的区间是有关联 seed 的描述性 Wilson 近似，不能解释为独立样本置信结论。

调用方应把职业/技能/效果定义视为一次搜索期间不可变的权威数据。搜索器保存候选参数，不会写回定义；可先用独立验证 seed 比较搜索前后矩阵。版本 1 接受即时回合技能，蓄力技能在配置时失败。策略是确定性规则，无法替玩家学习新连招；将玩家实测或 AI 策略加入测试集后才可把胜分解释为目标群体的预测。每次 `measure` 同步、耗时约随构筑对数量、双方策略数、场景数、seed 数与轮数相乘；建议离线使用。

Battle 的随机流目前使用 32 位种子；输入允许 64 位整数，但会折叠到 32 位，因此不同的 64 位输入可能对应同一随机流。为了严格区分训练和验证流，示例使用不同的较小整数 seed。

## module-interface

- capability：消费 `agent` 搜索器以及 `rpg` 的注册表和战斗/结算系统；不提供全局能力。依赖缺失时模块不装载。
- ECS/Link：每次评估私有 `ecs::Table` 与事件作用域；只在本次调用持有 `RPGActor*`，退出即销毁；不产生跨帧 Link。构筑原始值由 suite 独有，角色属性由 RPG 系统持有，报告为独立值。
- 事件：只读取私有战斗、技能和状态事件；不会观察或发射运行中世界的事件。状态推进用调用方传入的 `dt`，每场 seed 注入 Battle 独立随机流。
- script：`RPGBalanceSuite.configure`、`targets`、`measure`；同步、单 RPG owner 线程；禁止在 `measure` 期间重入和改写注册表/回调。
- protocol：没有新 MCP 工具，也没有持久化写协议；版本化输入/报告可导出 JSON，未知字段与版本拒绝。
- 裁剪：`rpg_balance` 为 L2 模块，依赖 `agent` 与 `rpg`。独立 `test/balance` 的 `EVE_BALANCE_SCRIPT=OFF` 构建和 7 个用例验证搜索核心无需 RPG 链接；独立 `test/rpg-balance` 构建、12 个用例和真实模块注册验证启用路径。未声明 L3/L4 裁剪。
