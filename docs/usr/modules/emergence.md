# 涌现式规则（Emergence）

**脚本入口：** `eve.Emergence()`

按事实变更唤醒条件规则，用于剧情、任务与经济等跨系统的涌现触发。
上万条规则时也只评估订阅了变更键的规则，不做全表扫描。

## 基本用法

```squirrel
local emergence = eve.Emergence();
local engine = emergence.newEngine().value;

engine.replaceCatalogueJson(@"
{
  ""schema"": ""eve.emergence.rules"",
  ""version"": 1,
  ""rules"": [{
    ""id"": ""treasury.rich"",
    ""condition"": {""kind"": ""compare"", ""key"": ""gold"", ""operator"": ""ge"", ""expected"": 1000},
    ""actions"": [{""kind"": ""quest.notify"", ""args"": {""topic"": ""rich"", ""target"": """", ""amount"": 1}}]
  }]
}");

engine.setResource("gold", 1000);
local n = engine.drain(tick).value;
for (local i = 0; i < engine.activationCount(); i++) {
    print(engine.activationRuleId(i));
}
```

## 对象关系与调用时机

`RuleEngine` 持有 `FactStore`（规则可见事实）和规则目录。世界系统在状态变化时
写入 fact（或镜像经济/任务进度），每仿真步调用一次 `drain(tick)`。
内置动作：`fact.set`、`economy.credit` / `economy.debit`（经 `IEconomy`）。
其它动作进入激活日志，并由可选的 `IEmergenceActionHandler` 消费。

## 目标导向指南

### 让剧情在条件满足时出现

把剧情前置写成 `condition`，动作为 `story.begin`，由脚本或 handler 调用
`StoryEventSession::begin`。默认 `fireMode=rising`，条件持续为真不会重复触发。

### 与任务系统联动

动作 `quest.notify` / `quest.activate`；C++ 侧实现 `IEmergenceActionHandler` 并
`cap::addListener`，或在脚本里读 `activationRuleId` 后调用 `QuestSystem::notify`。

### 与经济系统联动

经济入账后把余额镜像到 `setResource("gold", …)`；规则内也可直接发
`economy.credit`（需已 `provide<IEconomy>`）。

## 常见问题

- 每帧全量扫规则：不要。只在 fact 变更后 `drain`。
- `policy_call` 无 dependencies：注册失败——无法建立索引。
- 在 handler 里再次 `drain` 同一引擎：禁止（PreconditionViolation）。

## API 快查

下列方法名来自当前 Squirrel 绑定。

- `getName`、`newEngine`
- `ownership`、`isStale`、`release`、`ruleCount`
- `replaceCatalogueJson`、`setValue`、`setTag`、`setResource`、`setState`
- `drain`、`activationCount`、`activationRuleId`、`clearActivations`、`lastDrainEvaluations`
- `snapshotJson`、`restoreJson`

**源码：** [`src/modules/emergence/`](../../../src/modules/emergence/)
**设计：** [`docs/dev/涌现式规则触发框架.md`](../../dev/涌现式规则触发框架.md)
**相关测试：** `test/emergence_rule_engine.cpp`、`test/emergence_perf.cpp`
