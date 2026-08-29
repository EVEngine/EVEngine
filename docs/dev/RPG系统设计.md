# RPG 模块设计：属性 / 效果 / 状态 / 技能 / 结算

日期：2026-08-08
状态：已实现（`src/modules/rpg/`）

任务 / 目标追踪器（第六子系统）见单独文档 [RPG任务系统设计.md](./RPG任务系统设计.md)（已实现，2026-08-18）。

新增子系统：
- **成长（LevelSystem）** — 等级 / 经验 / 升级事件（`RPGActor::Progression`）。
- **当前资源（VitalsSystem）** — 受伤 / 治疗 / 死亡 / 复活（`RPGActor::Vitals`，上限取同名最终属性）。
- **装备加成与掉落（EquipmentSystem / LootSystem）** — 把已装备物品（`inventory::EquipmentSet`）的注册属性加成同步到 actor，掉落表按概率随机入包（`inventory::Bag`）。
- **特征（Trait / TraitSystem）** — 参数倍率（落属性）、元素/状态耐性、暴击/命中/闪避等特殊参数、攻击附加元素/状态；`RPGActor::Traits`。
- **回合制战斗（BattleSystem / Battle）** — 伤害公式 DSL（`a.atk * 4 - b.def * 2`）、命中/暴击/元素耐性结算、行动顺序、胜败判定。**阵营是任意整数 id（多派系），伤害目标资源名任意（`damageType` 即资源名，`XHeal` 后缀为治疗）**；`setPlayerSide`/`getWinnerSide` 支持非默认玩家侧。
- **职业（Class / ClassSystem）** — 职业定义、职业基础特征、升级学技能（`RPGActor::ClassInfo`）。
- **全局状态（GameState）** — 开关 / 变量 / 独立变量（RPG Maker 的 `$gameSwitches` / `$gameVariables`）。

## 目标

提供一套**高度灵活、可定制化**的 RPG 游戏基础框架，覆盖 5 个互相配合的子系统（任务系统为后续增量，不在本文件展开）：

1. **属性（Attribute）** — 任意数值资源（生命、法力、攻击力……），支持基础值 + 多重修改器叠加。
2. **效果（Effect）** — 数据驱动的"定义/模板"：一段效果会做什么（改属性、持续多久、如何叠加）。
3. **状态（Status）** — 效果被施加到具体单位后的运行时实例（buff/debuff，含到期/叠层/周期 tick）。
4. **技能（Skill）** — 消耗 + 冷却 + 读条 + 命中后授予效果的通用释放流程。
5. **结算（Settlement）** — 伤害/治疗/命中判定等"多阶段计算"的可插拔流水线。

与引擎既有模块（`map`/`particles`/`scene`）保持同一套设计语言：ECS 组件为数据、`*System`
静态类为行为、`Module` facade 做脚本绑定；参考 [模块设计.md](./模块设计.md) 与
[游戏模型设计.md](./游戏模型设计.md)。

## 为什么不用固定枚举

沿用仓库既定的类绑定原则（见 [模块设计.md](./模块设计.md) "类绑定原则" 一节）：属性名、
效果 id、技能 id、修改器/结算阶段的运算方式全部是**字符串**，而不是编译期枚举。
好处：

- 新增一个属性/效果/技能，不需要改引擎头文件、不需要重新编译。
- 数据可以完全放进 JSON 由策划配置（`EffectRegistry::loadFromJson` /
  `SkillRegistry::loadFromJson`），也可以由 C++ 插件代码注册。
- 序列化/存档天然是字符串 key，方便调试和跨版本兼容（未知字符串直接忽略，而不是编译失败）。

## 可插拔扩展点一览

| 系统 | 扩展点 | 谁来注册 | 覆盖场景 |
|------|--------|----------|----------|
| 属性 | `AttributeSystem::registerOp(name, fn)` | C++ | 内置 add/mulAdd/mulMul/override/clampMin/clampMax 之外的自定义运算 |
| 状态/Buff | `EffectDefinition`（数据） | C++ 或 JSON | 新增效果无需改代码 |
| 状态/Buff | `EffectDefinition::extra` / `StatusInstance::props` | C++ 或 JSON | 图标/VFX/脚本钩子等自定义字段无需扩结构体 |
| 状态/Buff | `StatusSystem::registerApplyCondition(name, fn)` | C++ | 免疫/抗性/互斥等"能否施加"判断 |
| 状态/Buff | `StatusSystem::registerStackPolicy(name, fn)` | C++ | 内置 none/refresh/extend/stack 之外的自定义叠加 |
| 状态/Buff | `StatusSystem::registerLifecycleHook(name, fn)` | C++ | 施加/刷新/叠层/移除/到期时的副作用（UI、成就、VFX） |
| 状态/Buff | 周期效果只产生 tick 事件，不直接改属性 | 调用方（脚本/C++） | 让 DOT/HOT 可以完整走结算流水线而非被硬编码为直接扣血 |
| 技能 | `SkillSystem::registerCastCondition(name, fn)` | C++ | 引擎不内置"沉默/眩晕"等互斥概念，由游戏自定义判断逻辑 |
| 技能 | `SkillDefinition::extra`（string→string） | C++ 或 JSON | 任意自定义附加数据（动画名、特效……）无需扩结构体 |
| 结算 | `SettlementPipeline::registerStage(pipeline, stage, priority, fn)` | C++ | 伤害/治疗/命中率等公式完全可换、可插入新阶段 |
| 结算 | `setStageEnabled` / `setStagePriority` | C++ 或脚本 | 不写新原生代码也能开关/重排已注册的内建阶段 |

## 数据模型

### RPGActor（`RPGActor.h`）

ECS 实体，挂 3 个组件：

```text
RPGActor : ecs::Entity
  Attributes { unordered_map<string, AttributeValue> values }
  Statuses   { vector<StatusInstance> active; int nextInstanceId }
  Skills     { unordered_map<string, SkillRuntime> known; CastingState casting }
```

游戏可以直接用 `RPGActor`，也可以仿照 `docs/dev/游戏模型设计.md` 的示例从它派生出
`Player`/`Monster` 等子类，追加自己的组件——三大组件与配套 System 天然复用。

`RPGActor::createActor()` 是推荐的工厂方法：除了创建实体外，还会把它加入
`RPGActor::liveActors()` 跟踪列表，`StatusSystem::update` / `SkillSystem::update`
靠这份列表逐帧推进（ECS `View` 目前不回传 `Entity*`，见
`external/ECS.hpp/docs/API.md`，因此周期 tick / 技能释放事件需要携带 actor 身份时改用
这份显式列表而非 `View`）。`release()` 会让 actor 从该列表中自然消失。

> **坑 / 设计约束：** 这份跟踪列表内部存的是 `ecs::EntityHandle`（id + generation），
> 而不是裸 `RPGActor*`。原因：`ecs::DestroyEntity` 在非 deferred 模式下会立刻
> `reclaim` 槽位，槽位对应的内存会被**下一个** `createActor()` 复用（同地址、
> generation 递增），并且复用时会用 `T{}` 重新构造，把 `flags` 清零。也就是说，
> 一个已销毁 actor 的裸指针在槽位被复用后，`ecs::is_entity_visible(ptr)` 会重新
> 变为 `true`——旧指针"看起来又活了"，导致 `liveActors()` 把同一块内存返回两次
> （一份是新 actor 自己 push 的，一份是上一个已销毁 actor 遗留、从未被清理的
> stale 条目），进而使 `StatusSystem::update` / `SkillSystem::update` 对同一个
> actor 重复跑一遍 tick 逻辑（duration 类 buff 会提前双倍衰减到期、冷却双倍扣减等）。
> 修复方式是用 `ecs::handle_of(actor)` 存 handle、用 `ecs::try_get(handle)` 解析
> ——它会同时校验 id 与 generation，槽位被复用后旧 handle 会正确解析为
> `nullptr` 并被 `liveActors()` 清理掉。**结论：任何跨帧持有 `Entity*` 的场景都应该
> 优先使用 `EntityHandle` 而不是裸指针**，除非能保证在实体销毁的同一帧内完成清理。

### 属性（`AttributeTypes.h` / `AttributeSystem.h`）

```text
AttributeModifier { id, source, op, value, priority }
AttributeValue    { base, modifiers[], cached, dirty }
```

计算规则（`computeAttributeValue`，纯函数、不依赖 ECS，可独立单测）：

1. `add` 全部求和后加到 base 上
2. `mulAdd` 全部求和后作为一次百分比加成：`result *= (1 + sum)`
3. `mulMul` 逐条独立相乘：`result *= (1 + value)`
4. 其余按 `priority` 升序依次处理：`override`（直接赋值）、`clampMin`/`clampMax`、
   或自定义 op（`AttributeSystem::registerOp` 注册的 `f(current, value)`）

### 效果 / 状态 / Buff（`Effect.h` / `StatusTypes.h` / `StatusSystem.h`）

```text
EffectDefinition {
  id, durationPolicy(instant|duration|infinite), duration, period,
  stackPolicy(none|refresh|extend|stack|<custom>), maxStacks,
  modifiers[], tags[], extra{}
}
StatusInstance {
  instanceId, effectId, source, stacks, remaining, periodAccum,
  appliedModifiers[], props{}
}
StatusChangeEvent {
  actor, instanceId, effectId, source,
  action(apply|refresh|extend|stack|remove|expire|reject), stacks, reason
}
```

ECS 组件 `RPGActor::Statuses` 即运行时 Buff 列表。脚本/游戏侧可用 `applyBuff` /
`hasBuff` / `removeBuff*` 等与 `applyEffect` / `hasEffect` / `removeStatus*` 等价的别名。

- `durationPolicy == "instant"`：立即把 `modifiers[].value` 当增量写入属性 base，
  不产生状态实例（`StatusSystem::apply` 返回 `0`），并产生 `action="apply"` 变更事件。
- `period <= 0`（duration/infinite 的普通增益/减益）：`apply` 时把 modifiers 转成
  `AttributeSystem` 修改器写入，`remove`/到期时精确撤销。
- `period > 0`（周期效果，如 DOT/HOT）：**不**自动改属性，`StatusSystem::update`
  每满一个周期产生一个 `StatusTickEvent`（携带 actor / effectId / stacks），
  由上层决定如何处理（直接改值，或走 `Settlement` 完整结算护甲/抗性/暴击）。
- `stackPolicy`："none" 拒绝重复施加；"refresh" 重置剩余时间；"extend" 累加剩余时间；
  "stack" 增加层数（受 `maxStacks` 限制）并按新层数重新写入修改器（`value * stacks`）；
  其它字符串走 `registerStackPolicy` 自定义策略（未注册则按 "none" 拒绝）。
- `registerApplyCondition`：施加前全部条件 AND；任一失败返回 -1 并产生
  `action="reject"`（`reason` 为条件名或条件写入的说明）。
- `registerLifecycleHook`：每次 `StatusChangeEvent` 入队后同步回调，适合 UI/成就/VFX。
- `extra`（定义侧）与 `props`（实例侧）互补：模板数据进 JSON；运行时 UI 覆盖等写 props。

### 技能（`Skill.h` / `SkillTypes.h` / `SkillSystem.h`）

```text
SkillDefinition { id, cooldown, castTime, targetType, costs[], grantedEffects[], tags[], extra{} }
```

`SkillSystem::beginCast`：`canCast` 通过后立即扣消耗、进入冷却；`castTime<=0` 立即结算
（对目标施加 `grantedEffects` + 产生 `SkillCastEvent`），否则进入读条，由 `update` 在
读条结束时结算。`targetType` 是自由字符串，引擎不解释含义——目标选取完全由脚本/游戏
逻辑负责，`SkillSystem` 只处理冷却/消耗/读条与授予效果这条通用流水线。

### 结算（`Settlement.h`）

`SettlementContext` 是一个通用读写数据袋（`values: string→double` + `tags` +
`source`/`target`/`cancelled`）。`SettlementPipeline` 按名字管理若干条流水线，每条
由若干按 `priority` 升序执行的具名阶段组成；任一阶段可置 `cancelled=true` 提前终止
（例如"闪避判定"取消后续伤害计算）。阶段本身是 `std::function`，因此**注册新阶段是
C++ 专属扩展点**；脚本可以读写 `SettlementContext`、调用 `run()`，也可以
enable/disable、调整已注册阶段的优先级，但不能新增原生阶段逻辑。

## 脚本绑定（`eve.RPG`）

`rpg <- eve.RPG()`：

- Actor：`rpg.newActor()` → `RPGActor`
- 定义注册（进程级）：`registerEffectsFromJson`/`registerSkillsFromJson` +
  `clear*Definitions`/`get*DefinitionCount`
- 帧调度：`rpg.update(dt)` 驱动 `StatusSystem`/`SkillSystem`，并刷新本帧事件缓存
- 事件轮询（push/poll 风格，与 `event` 模块一致）：
  `getTickEventCount/Actor/InstanceId/EffectId/Source/Stacks`、
  `getStatusChangeEventCount/Actor/InstanceId/EffectId/Source/Action/Stacks/Reason`、
  `getCastEventCount/Caster/Target/SkillId`
- 结算：`newSettlementContext()` → `SettlementContext`，`runSettlement(pipeline, ctx)`，
  以及 stage 的 enable/priority/remove 内省接口

`RPGActor` 自身暴露一层薄的便捷方法（转发到各 `*System`），与 `TileLayer`/
`ParticleEmitter` 的绑定风格一致：`actor.setBaseAttribute(...)`、
`actor.applyEffect(...)`、`actor.beginCastSkill(...)` 等，详见 `RPG.cpp::expose`。

> **坑 / 设计约束：** `AttributeSystem` 内部全用 C++ `double` 计算，但
> `setBaseAttribute`/`getBaseAttribute`/`modifyBaseAttribute`/`getFinalAttribute`/
> `addAttributeModifier`（以及 `SettlementContext::get`/`set`）**不能**直接
> `addFunc(&RPGActor::xxx)` 绑给 Squirrel——`simplesquirrel` 只有在 squirrel 以
> `SQUSEDOUBLE` 编译（`SQFloat` 为 64 位）时才认识 `double`，本项目的 squirrel
> 没开这个宏（`SQFloat` 是 32 位 `float`），于是这些方法的 `double` 参数/返回值
> 会落到 `simplesquirrel` 给 `INSTANCE`/`USERDATA` 准备的通用模板上，脚本调用
> 直接抛 `Type error bad cast expected: INSTANCE got: FLOAT`（同样的坑
> `mouse/Mouse.cpp`、`timer/Timer.h` 也写了注释提醒过）。`RPG.cpp::expose` 的
> 修法是给这几个方法包一层 lambda，脚本侧一律用 `float`，进 C++ 前再转
> `double`（见该文件 `actor.addFunc("setBaseAttribute", ...)` 附近的注释）。
> **结论：任何要暴露给 Squirrel 的新方法，只要参数/返回值涉及浮点数，一律用
> `float` 或包一层转换 lambda，不要指望 `double` 能直接绑定。**

## 测试

`test/rpg.cpp`：属性纯函数计算（各 op / 自定义 op / 优先级）、`AttributeSystem` 的
增删查与缓存失效、`EffectRegistry` 的注册与 JSON 加载（含 `extra`）、`StatusSystem` 的
instant/duration/infinite/周期 tick/四种叠加策略、施加条件拒绝、自定义 stackPolicy、
生命周期钩子与变更事件、Buff API 别名、`SkillSystem` 的
学习/冷却/消耗/读条/取消/自定义施法条件、`SettlementPipeline` 的优先级执行/取消/
开关/内省，以及一个贯穿 `RPG` facade 的端到端用例（JSON 注册技能与效果 →
释放技能 → 命中目标产生持续伤害 tick 事件；以及 facade 侧 status change 事件轮询）。

## 非目标（v1）

- 不内置任何具体战斗数值公式（暴击率、护甲减伤公式等）——这些通过
  `SettlementPipeline` 由游戏自己注册。
- 不内置目标选取/寻路/范围判定——`targetType` 只是透传字符串。
- 不做脚本侧动态注册结算阶段（`std::function` 无法跨 C++/Squirrel），仅支持脚本
  开关/重排已注册阶段。
- 不做存档序列化（属性/状态/技能运行时状态的落盘格式留待存档系统统一设计）。
