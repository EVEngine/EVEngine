# Composable Rebellion — 引擎只存事实、玩法语义全在脚本的叛乱示例

本示例演示"通用能力 + 项目语义"的分层：引擎侧的 `eve.*` 模块只保存中性的属性、
归属、授权、指令、事件等事实，完全不认识"忠诚""叛乱"这类领域概念。全部政治含义
写在 `simulation.nut` 里，由一次拖欠军饷的薪资事件推动将领带着基地与军队倒向边疆阵营。

## 运行

```bash
make run/<platform>-debug GAME=examples/composable-rebellion
```

也可以进入示例目录直接启动引擎：

```bash
cd examples/composable-rebellion && ../../build/linux-debug/src/engine/eve run
```

Windows 下可执行文件为 `build/win32-debug/src/engine/eve.exe`。

## 演示内容

- 模块装配：`reset_demo` 创建 `eve.Attributes`、`eve.Social`、`eve.Orders`、`eve.Tags`、`eve.Effects`、
  `eve.GameEvent`、`eve.Transaction`、`eve.StatePatch`、`eve.Definitions`、`eve.Authority`、`eve.Production`、
  `eve.PolicyRegistryModule`、`eve.Sensing`、`eve.Steering`、`eve.Decision`——引擎侧没有叛乱专用类型。
- 事实与授权：`setBase` 写入 `administration` 80、`loyalty` 48、`ambition` 82 与 `production_speed` 1.0；
  `setOwner` / `assign` / `setRelation` 建立 `general.arden`、`base.north`、`army.first` 的归属和
  `officer.vela` 对将领的 `support` 关系；`Authority.grant` 授予 `govern_base` / `command_army`，
  `PolicyRegistryModule.select` 在 `administration` 频道选中 `governor_bonus`。
- 生产与 AI：`Production.setSlotCount` / `enqueue` 在同一基地混放 `build_unit`(`tank.medium`) 与
  `issue_decree`(`decree.tax_reform`)，`advance` 按 `production_speed` 最终值推进；
  `Sensing.circle` 查到最近敌军后交给 `Decision.setState` / `addTransition` / `trigger` / `choose`，
  `Steering.arrive` 求朝向速度，`Decision.sample("threat", ...)` 采样 8×8 威胁网格。
- 状态效果与判定：`Effects.apply` 施加 `salary_unpaid`，`addTag("politics")` 打标签、payload 写入
  `loyalty_delta`，再用 `addModifier` 叠加 `loyalty` 负修正；`evaluate_rebellion` 按
  `getFinal("loyalty")`、`ambition`、`relation(..., "support")` 三重阈值判定，并由
  `Transaction.create` / `stage` / `markValid` / `validate` / `commit` 提交三步 `set_owner` 计划。
- 原子切换与投影：`StatePatch.newBatch` + `setExpected(..., "faction.frontier", "faction.crown")`
  校验后一次 `commit`，冲突则 `plan.fail("ownership_conflict")`；成功后 `Orders.newQueueOwned` /
  `append` 下发 `secure_assets`，`GameEvent.append` 记录 `rebellion_started`，
  `Social.setOwner` / `setRelation` 只作为查询投影同步。

## 观察方式

启动时 `eve_init` 调用 `run_rebellion_scenario()`，只打印一行
`rebellion=... base_owner=... production=...`：`rebellion` 为 `true` 且 `base_owner` 为
`faction.frontier`，说明事务提交与状态补丁都成功；`production` 不再是初始的 `1.0`，因为
`refresh_governor_bonus` 用 `addModifier("governor.production", "production_speed", ...,`
`"multiply", ...)` 挂了州长加成。任一步失败时会经 `decision_result_ok` 打印
`decision <operation> failed: <status.summary>` 便于定位；`eve_render` 用 `gfx.drawSolidRect` 画
左右两块，叛乱成功后左块（王权）红通道由 0.65 降至 0.18、右块（边疆）绿通道由 0.20 升至 0.72。

## 相关文件

- `main.nut`：入口——设置背景色、调用 `run_rebellion_scenario()`、打印结果并渲染两个色块。
- `simulation.nut`：全部玩法逻辑——模块装配、生产与 AI、状态效果、叛乱事务与状态补丁。
- `config.nut`：窗口 960×540、标题 `Composable Gameplay: Rebellion`、`debug` 与 `hotReload` 均为 `true`。
- `README.md`：本说明。
