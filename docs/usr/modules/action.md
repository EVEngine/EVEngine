# Action

**脚本入口：** `eve.Action()`（槽位 `action`）

`action` 是玩法行动与 Ability 的协议模块：唯一拥有 `ActionRuntime` 的 phase 状态，以及
`AbilityRuntime` 的 grant / cooldown / activation 链接。它不拥有生命值、棋盘或角色实体。

## Squirrel 快速开始

```squirrel
local action = eve.Action();
local created = action.newRuntime();
assert(created.ok);
local runtime = created.value;
assert(runtime.ownership() == "owned");

local registered = runtime.registerAbilityJson(abilityAssetJson);
assert(registered.ok);
local granted = runtime.grantAbility("fighter:player", "ability:light-attack");
local activated = runtime.activateAbility(granted.value.grantId, 1);
local advanced = runtime.advanceAbilities(2, 0.10);
```

`newRuntime()` 返回脚本 GC 拥有的 `ActionRuntime` 代理；内部同时持有
`ActionRuntime` 与借用它的 `AbilityRuntime`。冷却与 phase 只接受调用者注入的
tick / delta，不读取墙钟。所有易失败方法返回统一 Result 投影。

## 绑定与行为说明

| 方法 | 说明 |
| --- | --- |
| `getName` | 模块名 `"Action"` |
| `newRuntime` | 创建脚本拥有的 `ActionRuntime`（Result + `ownership`） |
| `ownership` | 运行时代理所有权标记，恒为 `"owned"` |
| `registerAbilityJson` / `replaceAbilityJson` | 解码 `eve.action.ability` schema v3 并注册/热替换；v1/v2 显式迁移为历史的超量失败策略 |
| `registerTimelineAbility` / `replaceTimelineAbility` | 与 Combat 同形的 timeline 注册入口 |
| `grantAbility` / `revokeAbility` / `abilityGrant` | grant 生命周期与快照 |
| `activateAbility` / `advanceAbilities` / `cancelAbility` | 激活、推进、取消 |
| `matchingAbilities` | 按 gameplay-event tag 匹配 grant |
| `submitAction` / `advanceAction` / `cancelAction` / `findAction` | 不经 Ability 的直接 ActionRuntime 路径 |
| `executionCount` | 当前未终态 execution 数 |

Ability 生命周期仍由本模块的 `AbilityRuntime`/`ActionRuntime` 唯一拥有。Combat /
RPG / Weapon 等域可以继续在 C++ 侧复用同一协议，也可以通过本脚本面独立编排。

动作定义保存使用 `ActionDefinition::encodeState` / `prepareState`（`action.definition` v1），与 Ability 资产共用字段编解码。Ability 当前写出 v3，完整保留选靶 `countPolicy` 及锥形区域半角；v1/v2 的缺失策略迁移为 `FailIfOutOfRange`，v1 毫秒同时转换为纳秒。新格式拒绝未知结构字段及会丢失维度或溢出为无限范围的几何数据，metadata 仍允许可序列化扩展值。读取不会执行条件、扣费或注册动作，也不授予命名策略/效果的实现权限。执行阶段、计时游标、资源审计和世界绑定须另行联合恢复。

动作运行检查点 `ActionCapture::seal/parse` 使用 `eve.action-runtime / action:runtime` v1，保存完整阶段、计时、已选目标、历史回执、时间线游标和执行 ID 水位。读取不会重新提交动作、扣费或执行效果；需要通过 `PreparedActionRestore` 接回实际领域实体与服务，再由游戏组合根联合发布。资源回执以 `ActionResourceReceiptAudit` 保存历史数字身份，不恢复账户凭据；余额和资源账本仍由实际账户负责。协议细节见地图生成设计 §8.22。
