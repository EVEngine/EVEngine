# RPG 自动数值平衡示例

本例把 `eve.BalanceSearch` 接到 `eve.RPGBalanceSuite`。每次评估都在独立 ECS 表中创建真实 `RPGActor`，通过现有 `ClassSystem`、`EquipmentSystem`、`SkillSystem`、`Battle`、状态效果和结算管线完成对战。定义写在 `definitions.nut`，构筑、策略、场景、参数和设计目标写在 `suite.nut`。

Windows 上先运行无窗口的完整脚本入口；它调用与引擎相同的原生类、注册表和结算代码，适合 CI：

```powershell
./cmake/with-msvc.cmd cmake -S test/rpg-balance -B build/rpg-balance -G Ninja -DEVE_ZEROERR_SOURCE=C:/path/to/zeroerr -DEVE_THIRD_PARTY_SOURCE=C:/path/to/EVEngine/third-party -DEVE_ECS_SOURCE=C:/path/to/ECS.hpp
./cmake/with-msvc.cmd cmake --build build/rpg-balance --target rpg_balance_demo rpg_balance_tests -j4
./build/rpg-balance/rpg_balance_demo.exe build/rpg-balance/evidence.json
./build/rpg-balance/rpg_balance_demo.exe --probe build/rpg-balance/probe.json 1.8 1.0 1.0 14
ctest --test-dir build/rpg-balance --output-on-failure
```

完整引擎构建后也可运行窗口示例：

```powershell
make run/win32-debug GAME=examples/rpg-balance
```

`main.nut` 在控制台显示训练和独立验证损失、四个构筑的对战矩阵、策略/资源场景格数和对战数。窗口用颜色显示矩阵。CPU 入口可把完整搜索报告、前后验证矩阵、逐格胜负/平局、技能使用次数和首场动作轨迹导出为 JSON；候选只修改隔离对战中的属性和装备投影，不改正在运行的角色或注册表。
`--probe` 跳过搜索，用当前 `suite.nut` 的验证 seed 直接测量四个参数，便于检查某组候选的实际对战矩阵；参数顺序为战士 power、法师 power、游侠 power、法师武器 mat。

示例含战士、法师、游侠三职业及法师的第二套构筑。战士、法师、游侠的克制循环通过真实职业特质影响不同伤害类型；剑、法杖等装备经正式属性修饰路径生效。七种技能覆盖伤害、治疗、防御和周期状态，两种策略分别偏好爆发与续航，两种场景改变血量、法力和回复。目标边指定战士克制游侠、法师克制战士、游侠克制法师，以及法师两套构筑之间的关系。每格用相同 seed 的两个出场顺序对战，训练 seed 与验证 seed 分开。

这是离线平衡工具。版本 1 接受即时回合技能；需要蓄力的技能会在配置时被拒绝。策略是固定规则，不会随参数学习更优操作。矩阵评分和平衡损失用于检验设计目标，不能代替玩家体验评测；报告中的区间是有关联样本的描述性近似。重放时应固定游戏定义和程序版本。

接口说明见 [RPG 平衡组件](../../docs/usr/modules/rpg-balance.md) 与 [搜索器](../../docs/usr/modules/agent-balance.md)。
