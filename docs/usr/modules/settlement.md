# Settlement

`settlement` 是 RPG、战棋、RTS、卡牌与其他玩法共享的确定性结算协议。模块只拥有计算顺序、结果解释和原子提交机制；生命、护盾、装甲、棋盘单位、卡牌与效果实例仍由对应玩法模块持有。

## 统一流程

一次结算使用 `SettlementRequest`、一个领域 `ISettlementPolicy` 和可选的 `GameEventLog`。固定计算阶段依次为校验、判定、来源修正、目标减免、护甲/护盾、限制、应用候选、触发准备和事件准备；领域状态随后提交并追加事件。领域 policy 在 `prepareApply` 中制作候选写入，流水线全部成功后才提交；后续事件写入失败会回滚领域状态。

判定结果使用 `SettlementDisposition` 区分 Applied、NoOp、Immune、Resisted、PartiallyApplied、Blocked 和 InvalidTarget，避免把免疫、完全吸收与普通零数值混为一谈。调用方可把命名 RNG 已产生的决定放入 `SettlementRequest::decisions`；Settlement 只校验并记录 stream、sequence、sample、threshold 与 accepted，不读取或推进任何权威随机流。

效果驱散由状态所有者 `EffectContainer::dispel` 执行：按 category tag、强度和最大数量筛选，按优先级与创建顺序稳定移除，并通过已有 snapshot/remove/restore 一次性提交。带 `effect:undispellable` 标签的实例不会被移除；失败不会留下部分驱散。

控制递减直接复用带 stack 的 `resist_percent` 规则；临时控制或元素免疫则使用有限 duration 的 `decision + immune` effect。窗口过期由 EffectContainer 正常生命周期处理，不维护第二份计时器或免疫表。

## 触发链

领域 policy 可在现有 `prepareTrigger` 阶段返回拥有型派生 `SettlementRequest`，例如以实际 `result.applied` 生成吸血、反伤或过量治疗转护盾。`settleChain` 通过调用方提供的 request executor 按稳定广度顺序处理派生请求，并限制最大深度和总结算数；executor 为每个请求构造对应 policy 和规则快照。每个派生请求携带 trigger key 与 ancestry path，重复进入同一 key 会产生结构化冲突而不是继续递归。

当前链策略是显式的 commit-each：父结算成功后才运行子结算，后续子项失败不会撤销此前已经发布的领域事实。返回值复用 `SettlementBatchItemResult`，因此失败位置和此前成功结果都不会丢失。需要全有或全无的静态多目标操作应继续使用 `settleAtomic`。

声明式规则可在 Trigger 阶段使用 `lifesteal` 或 `reflect` 操作，数值表示相对于实际 `applied` 的比例。它们生成普通拥有型请求，继续接受同一套规则、限制、事件和回放处理，而不是绕过 Settlement 直接修改生命值。

shield break、death、kill 也复用 `SettlementResult::derived`，以 magnitude 为零、trigger key 分别为
`shield_break`、`death`、`kill` 的请求表达，并按此顺序稳定生成。它们只在状态跨越边沿时出现：已经死亡的
目标不会再次产生 death/kill。Combat/RTS 可直接审计这些请求；RPG Battle 会执行派生链并发布同名事件；
Card 用 death 请求保证周期伤害只在 alive-to-dead 边沿报告一次死亡。

RPG Battle 已使用 request executor 接入真实链路径。每个根请求或派生请求都会根据自己的 source/target 重新投影双方活动 status、补齐当前生命比例 context，并构造对应 `RPGSettlementAdapter`；不会错误复用根目标的 effect 快照。链中每项成功结果继续发布原有 `VitalsEvent` 与 `BattleEvent`。

## Trace

`SettlementRequest::trace` 提供 Off、Summary 和 Full 三档。Off 不保存阶段数组；Summary 保存阶段、状态和 before/after，但不构造规则 details；Full 保留规则来源、操作数、条件摘要等现有完整信息。三档复用 `SettlementResult::stages`，没有第二套 Trace 对象模型。

RPG `Battle` 已通过 `RPGSettlementAdapter` 将伤害和治疗接入该流程，并在成功提交后继续发布原有 `VitalsEvent`。RTS 的武器、投射物和技能伤害统一汇入 `combat::DamageRuntime`；目标效果倍率、规则减免、护盾吸收和生命提交都在 Settlement 阶段完成，RTS 只根据成功结果提交其拥有的护盾余额与冷却。Card 的即时 Damage/Heal/Shield 以及周期 Damage/Heal/Shield 全部通过 `CardEffectExecutor` 的 Settlement 管线；Damage/Heal 仅在 `period == 0` 时即时执行，周期效果不会在 apply 时多结算一次。Tactics 在 `useAbility` 保持只拥有行动合法性和资源的边界，由 `TacticsSettlementRuntime` 接收已提交的 `AbilityReceipt`，并针对游戏拥有的生命、状态或棋盘对象 policy 执行统一结算。Vehicle 使用 `VehicleSettlementAdapter`。

RPG 周期 Status 可在效果定义的 `extra["settlement.tick"]` 中写严格 JSON，例如
`{"kind":"damage","resource":"hp","magnitude":4,"tags":["damage:poison"],"context":{"element":"poison"}}`。
`makeStatusTickSettlementRequest` 将 `StatusTickEvent`、调用方已有的稳定 source/target 身份和模拟 tick
投影为标准请求；magnitude 自动乘以事件捕获的 stacks。它不为 RPGActor 创建第二套身份，也不在
StatusSystem 生命周期更新中隐式修改 Vitals。参战角色可直接调用 `Battle::settleStatusTick`：该入口复用
普通行动相同的主体映射、活动 Status 投影、触发链及 Vitals/Battle 事件发布逻辑。

Tactics 的单位目标默认使用 `AbilityReceipt::targetUnit`；cell-only 能力通过
`AbilitySettlementRequest::target` 提供游戏拥有的稳定棋盘/格子主体。范围能力按稳定目标顺序调用
`TacticsSettlementRuntime::settleAtomic`，直接复用通用原子批处理，任一目标提交失败会恢复全部目标和
事件日志，不需要 AOE 管理器或另一套结果类型。
正式组合路径从 `Tactics::useAbility` 返回的 `AbilityReceipt` 开始；结算成功后，游戏所有者可根据结果调用
`defeatUnit`，由 Tactics 原子更新单位和目标状态。Settlement 不读取行动点或棋盘内部状态，Tactics 也不
读取生命 policy 的内部数据。

RPG `Battle::configureSettlementRules`、`combat::DamageRuntime::configureSettlementRules`、`CardEffectAdapter::configureSettlementRules` 与 `TacticsSettlementRuntime::configureSettlementRules` 提供一致的规则配置入口。配置替换是事务式的：新规则校验或安装失败时，运行时继续保留上一份有效规则。

## 多资源和多目标

`SettlementRequest::resource` 是可选的领域资源/通道 key。单值 `settle` 仍是最低成本路径；
多通道显式使用：

- `settleAtomic`：按输入顺序准备并提交通道。任一 prepare/commit/event 失败会反向恢复所有已准备或已提交变更，并恢复 event log 快照。
- `settleIndependent`：每个通道独立提交，返回保留输入 index、`Status` 和可选 `SettlementResult` 的逐项结果。

原子通道的 policy 必须是非空借用对象。同一 target、同一 `resource` 的多个分段必须复用同一个
policy，并按输入顺序计算，因此物理、火焰等分段可以共同写入 HP。若同一 target/resource 被交给
不同 policy，接口会明确返回所有权冲突；不同资源或不同目标仍可使用各自 policy。

## Buff 与效果规则

`SettlementRuleSet` 是活动 Buff/效果在单次结算中的拥有型投影。效果容器仍负责持续时间、层数、刷新和过期；执行结算前，玩法 adapter 把当前实例投影为 `SettlementRule`：

- `RuleFilter` 按结算 kind、必需标签和排除标签选择规则；
- `priority` 与 `id` 提供稳定顺序；
- `stacks` 和 `valuePerExtraStack` 表达叠层缩放；
- `when` 接受在配置阶段编译的只读条件字符串；空字符串表示无附加条件；
- 操作支持加算、乘算、固定/百分比减免、护盾吸收、结算上限和暴击标记；
- 规则来源、层数和最终操作数进入 stage details，供 UI、日志和回放解释。

先调用 `configure` 完整校验配置，再调用 `install` 把不可变快照装入一条 `SettlementPipeline`。失败的配置替换不会改变旧规则。规则不保存 `EffectInstance*`，因此容器更新、移除、restore 或热重载不会留下悬空引用；下一次结算重新投影即可。

EveScript 可把完整规则文档写成一个字符串，交给现有玩法对象的
`configureSettlementRulesJson(json)`：`rpg.Battle`、`rts` 和 `card.CardData` 使用完全相同的
`settlement.rules` version 1 格式。C++ 侧也可直接调用 `SettlementRuleSet::configureJson`。
该入口先严格解析整份文档并编译所有 `when`，全部成功后才替换旧配置；未知字段、未知版本或任一条件
编译失败都会保留上一份规则。Tactics 的 settlement policy 由游戏侧 C++ 持有，因此当前通过同一个
`configureJson` 解析规则后调用 `TacticsSettlementRuntime::configureSettlementRules`，不暴露无法提供
权威 policy 的脚本假接口。

最小文档如下：

```json
{
  "schema": "settlement.rules",
  "version": 1,
  "conditionLanguageVersion": 1,
  "rules": []
}
```

### Effect payload 投影

通用 `EffectContainer` 中的实例可在 payload 的 `settlement.rule` 字段携带一条规则。
该字段可以是 JSON object，也可以是内容为 JSON object 的字符串（便于复用 RPG
`EffectDefinition::extra`）：

```json
{
  "stage": "target_mitigation",
  "operation": "resist_percent",
  "value": 0.1,
  "value_per_extra_stack": 0.05,
  "kinds": ["damage"],
  "required_tags": ["element:fire"],
  "excluded_tags": ["damage:true"],
  "when": "context.target_hp_ratio < 0.5"
}
```

- `stage` 支持 `source_modifiers`、`target_mitigation`、`armor_shield`、`clamp`；
- `operation` 使用 `RuleOperation` 的 snake_case 拼写；
- `value` 缺省时使用 effect magnitude；
- priority、stack count 和 source 直接来自当前 `EffectInstance`；
- `kinds`、`required_tags`、`excluded_tags` 均为可选字符串数组。

`projectEffectRules` 每次从容器制作拥有型 `SettlementRuleSet`。RPG 在攻击时分别投影
source/target status；Card 在即时和周期结算前投影当前 active effects。RTS 继续复用
`RTSEffectAdapter` 的拥有型 multiplier/additive 投影，Tactics 继续由游戏所有者通过
`AbilitySettlementRequest::context` 传入状态，两者都不新建第二个 effect store。

### 条件表达式

`configure` 会把每条非空 `when` 一次性编译为规则集内部的紧凑程序；结算热路径不会重新解析字符串。当前语法支持：

- `true`、`false`、有限数值和双引号字符串；
- `&&`、`||`、`!`，以及 `==`、`!=`、`<`、`<=`、`>`、`>=`；
- `+`、`-`、`*`、`/` 和一元负号；
- `min`、`max`、`clamp`、`abs`；
- 请求字段 `kind`、`magnitude`、`tick`；
- `context.<name>` 及嵌套字段，例如 `context.hit.location`；字段类型由它在表达式中的布尔、数值或字符串用法静态推导；
- `has_tag("tag.name")`，查询本次请求的标签快照。

示例：

```cpp
SettlementRule execute;
execute.id = "execute.low-health";
execute.stage = StageKind::SourceModifiers;
execute.operation = RuleOperation::Multiply;
execute.value = 1.5;
execute.when = "kind == \"damage\" && context.target_hp_ratio < 0.3 && has_tag(\"class.assassin\")";
```

条件没有赋值、循环、随机数或任意脚本回调。编译会拒绝未知字段、类型不匹配、常量除零、过长源码、过深嵌套和过多节点，并提供配置路径、行列号、期望类型和实际类型。运行时缺失字段、动态除零或非有限结果会使整次结算失败，且不会提交领域状态。短路逻辑不会读取未执行分支的字段。

当前领域统一使用 `context` 快照，不建立字段注册表。RPG Battle 提供
`source_hp_ratio`、`target_hp_ratio`、`element`、`critical`；Combat/RTS 伤害路径提供
`damage_type`、`poise_damage`、`incoming_damage_multiplier`、`available_shield`、
`target_health_ratio`、`target_poise_ratio`；Tactics 保留调用方传入的拥有型 context。

## 确定性与扩展边界

流水线只使用请求提供的 `SimulationTick`，不读取墙钟或全局随机数。需要暴击掷骰等随机行为时，玩法 policy 必须从自己的命名 RNG stream 得到结果，并把决定作为拥有型 context 数据传入。自定义 C++ 阶段保持同步、无跨帧借用；脚本和数据驱动配置应先解码、校验为强类型规则，再一次性替换有效配置。

## 摘要与回放记录

`SettlementRuleSet::canonicalJson` 按实际执行顺序编码规则，并把集合语义的 filter 排序去重；
`digest` 使用调用方注入的 `SnapshotHashProvider` 生成 `ContentId`。系统不内置第二套哈希算法，
也不把配置文件的书写顺序误认为规则语义。

`createSettlementReplayRecord` 将一个请求、规则摘要、完整预期结果和结果摘要写入严格的
`settlement.replay` version 1 JSON。请求包含命名随机决定，因此读取记录不需要访问实时 RNG。
`settlementReplayRequest` 恢复拥有型请求；未知字段、未知版本、错误 ID 和错误类型会返回带路径的失败，
不会修改玩法状态。

`verifySettlementReplayRecord` 先验证记录内预期结果的摘要，再比较当前规则摘要和实际结果。
差异使用 `ruleDigest`、`result.payload.applied`、`result.payload.stages[0].details.rule` 等稳定路径。
事件日志运行时分配的 `eventId` 与 `sequence` 不属于结算语义，因此不会造成回放伪差异；因果、关联、
schema、tick、flags 和 payload 仍会参与比较。

RPG 吸血链可以为父伤害和派生治疗分别保存记录，再从父请求恢复并执行完整链。Combat/RTS 的
`DamageOutcome` 直接保留实际进入共享流水线的 `settlementRequest` 与 `settlementResult`，因此批量单位伤害
不需要由调用方重新拼装审计数据，也不会出现领域结果与回放结果来自两次计算的问题。

Card 的周期效果更新按生命周期顺序返回现有 `SettlementBatchItemResult`，可在恢复效果快照后直接验证
实际执行过的 request/result。Tactics 的运行入口和回放工具共同调用 `makeSettlementRequest`，避免各自
维护一份领域请求到通用请求的字段映射。

## 性能基准

`settlement.benchmark.traceAndIndependentBatchReportP50P95` 默认立即返回，不拖慢日常测试。显式设置
`EVENGINE_SETTLEMENT_BENCHMARK=1` 后运行该用例，会测量 10/100/1000 条已编译条件规则、0/50/100%
命中率、Trace Off/Summary/Full、触发链深度 1/4/8，以及 1000 项 `settleIndependent`，并输出一行
`SETTLEMENT_BENCHMARK_JSON`，供外部基准任务收集 p50/p95。JSON 也记录基准前后的进程峰值驻留字节数；
MSVC Debug 复用仓库已有的 CRT allocation hook，记录各场景每次操作的堆分配数，其他构建明确输出
`null`，不会把不可比的估算值伪装成测量结果。Debug 数字只能比较同一构建、同一机器上的相对成本；
正式预算应使用固定参考机的 Release 构建。当前实现刻意不提供未经数据证明有收益的缓存或 workspace
对象。

当前 Windows MSVC Release 基线中，1000 条规则、50% 命中时 Trace Off p50/p95 约为
112.8/114.8 us，Summary 约为 2336/2377 us，Full 约为 3672/4083 us；10 条规则、50% 命中的
1000 项 Independent 折算每项约 6.52/6.62 us。该结果说明 Trace 保留是当前主要成本，不能据此
给不同硬件设置绝对门槛；CI 应在固定参考机上采样后再确定预算。

## 配置检查与规则遥测

编辑器、命令行工具和热重载代码应调用 `SettlementRuleSet::fromJson` 检查规则文档。该入口不修改现有
规则集，成功时返回已完成条件编译的拥有型规则集，失败时返回与运行时配置完全相同的 schema、字段路径、
行列和类型诊断。因此工具侧不需要实现另一套 JSON 或条件 parser。

通用 `EditorValidationService` 已内建这一检查：宿主以 `subject = "settlement.rules"`、字符串 `value`
提交文档即可获得 `fromJson` 的原始诊断；其他 subject 不受影响。宿主可把返回值直接发布到现有
`EditorDiagnosticService`，不需要 Settlement 专用面板、诊断类型或遥测服务。

需要规则命中率时，把请求 Trace 设为 Summary，然后读取 `SettlementResult::ruleEvaluationCount()` 和
`ruleMatchCount()`；两者只扫描已有 stage/status，不分配、不写全局状态。Trace Off 两者均为零。
耗时统计使用显式 benchmark，不把墙钟数据写进确定性结算结果。

## 裁剪组合

Settlement 沿用引擎现有 module profile，不维护自己的模块开关。Windows Debug 的真实
`eve_profile_smoke` 已覆盖 minimal、2d、3d：minimal 验证 Settlement/Combat 基础组合，2d 与 3d
同时验证 RTS/Tactics 适配；每个组合都运行 provider 存在与缺失探针。RPG/Card 不在这三档现有清单中，
由 full 构建覆盖。这样能保持单一模块清单与启动列表，也避免为 Settlement 再造裁剪框架。
