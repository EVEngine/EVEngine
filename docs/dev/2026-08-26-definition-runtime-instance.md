# Definition 与 Runtime Instance 契约

本切片固定 Definition 与 Runtime Instance 的边界，适用于本次接入的
Card、Effects、RPG Skill 与 Vehicle；Weapon 暂不接入，等待资源代理完成。

## 所有权与身份

`definitions::DefinitionRegistry` 是 Definition JSON、schema version 和
generation 的唯一权威。`DefinitionRef` 只表达稳定的 `namespace:name`，不
携带 generation；`definition::DefinitionHandle` 表达某一具体 registry incarnation，
因此旧 handle 在 replace/remove 后必须被视为 stale。

每个 runtime instance 必须携带一个 `definition::InstanceIdentity`：

```text
instanceId             PersistentId (UUID)
definition             DefinitionRef / LogicalId
definitionGeneration   Generation
```

instance UUID 在重载和 rebuild 中保持不变；DefinitionRef 也保持不变；只有
generation 在成功切换到新 Definition 后前进。运行时状态由玩法 adapter 以
强类型结构拥有，不能退化为 common 层的大字段表。`RuntimeInstance<State>`
只提供状态候选值的事务式切换，不拥有 registry，也不延长 registry 生命周期。

## Reload policy

adapter 必须先从 registry 取得当前 generation-qualified handle，解析出完整的
强类型 defaults，再交给 common runtime 执行策略。策略语义固定如下：

| Policy | active instance | inactive instance | state 结果 |
| --- | --- | --- | --- |
| `KeepInstanceValues` | 成功 | 成功 | 保留全部 runtime values，仅更新 generation |
| `ReapplyDefaults` | 成功 | 成功 | 用新 Definition 的完整 typed defaults 替换 |
| `RebuildInstance` | 成功（须提供 rebuild callback） | 成功 | callback 返回完整 typed state，失败则不改变旧状态 |
| `RejectWhileActive` | `Conflict`，不改变任何字段 | 成功 | 保留 values，仅更新 generation |

generation 不得倒退。相同 generation 是 `NoOp`；不同逻辑 DefinitionRef
是 `Conflict`；旧 generation 是 `StaleHandle`。所有解析、默认值构造和
rebuild 都在提交前完成，提交点只交换一个完整状态并更新 identity，因而
失败不会留下半更新的 observable state。

## 两个实际消费者

- `card::CardDefinitionRuntime` 解析 name/kind/cost/attack/health/tint/tags
  为 `CardRuntimeState`，并通过 `CardData::DefinitionBinding` 投影统一身份。
  `currentHealth` 是实例值，`RebuildInstance` 会在新 maxHealth 上进行安全
  clamp；旧 `CardData::Identity::definitionId` 只是兼容投影。
- `effects::EffectDefinitionRuntime` 解析 stack、duration、magnitude、
  payload、tags 和 `EffectPolicy` 为 `EffectRuntimeState`，并投影到
  `EffectInstance::definitionIdentity` 与 typed `EffectInstance::policy`。
  `remaining` 与 stack count 的 rebuild 规则由 effects adapter 负责。

- `rpg::SkillDefinitionRuntime` 通过 `SkillRegistry::definitionRegistry()` 读取
  `rpg.skill:<name>`，只拥有 `cooldownRemaining` 与 `learned` 两个实例状态，
  并把兼容投影写入 `RPGActor::Skills`。旧 `SkillRegistry` API 保留，但注册、
  替换、删除都向 common registry 转发。
- `vehicle::VehicleDefinitionRuntime` 读取 `vehicle:<name>`，把 typed 状态
  投影到 `VehicleEntity` 的 Identity/Definition/DefinitionBinding/Motion/Health
  组件；Definition 是不可变 projection，`DefinitionBinding` 的
  `InstanceIdentity` 与 generation 是 stale 检查权威。旧 `Vehicle` JSON 注册
  API 继续提供兼容入口并向 common registry 发布有效 typed definition。

这两个 state 结构故意不同，common 层只约束 identity、generation 和 reload
事务，不试图统一卡牌属性与效果生命周期。

## ECS、snapshot 与兼容 API

`CardData` 和 `EffectInstance` 仍由各自领域/ECS 或 container 所有；adapter
只接受借用目标并在 `applyTo` 中先构造完整候选值，再一次性投影。不能保存
目标裸指针跨帧或跨任务。跨域连接使用 `DefinitionBinding` 或
`definitionIdentity`，并保留 owner、generation stale 检查和重建语义。

Definitions registry 的 `snapshot`/`restoreSnapshot` 已包含 schema、版本、
generation、tombstone 和 payload 的事务边界。runtime instance 使用公共
`common/definitions/RuntimeSnapshot.h`，通过 `SnapshotEnvelope` 保存 schema、
version、content hash、revision/tick、`InstanceIdentity` 三字段及 domain-owned
typed state；Card/Effects 与 Skill/Vehicle 均可复用同一 envelope 形状，而不
引入公共字段表。恢复先校验 envelope/hash、payload metadata、instance UUID、
DefinitionRef 和 generation，再解码候选 state，最后一次性提交；任一步失败都
不改变 observable state。过期 handle 只能走显式 reload/rebuild，不能静默按
LogicalId 读取并假装仍是旧 incarnation。

原有 `registerDefinition`、`replaceDefinition`、`resolve`、`handle` 等 API
继续提供单向兼容投影，内部转发到 `Result`/common registry；新代码使用
checked API，不再以 `lastError` 或 `nullptr` 作为主要失败通道。

## 验证边界

`test/definition_runtime.cpp` 覆盖四种 policy、generation stale、atomic
failure，以及 Card/Effects 两个真实 typed consumer 的解析和投影；
`test/definition_runtime_skill_vehicle.cpp` 覆盖 Skill/Vehicle adapter 的
generation-qualified identity、active reject、typed rebuild、实体投影、
snapshot hash/restore 与 stale snapshot rejection。
本切片不修改 physics、procgen、editor、particles、animation；这些模块的
Definition/Runtime consumer 仍是明确的后续跨写集缺口。
