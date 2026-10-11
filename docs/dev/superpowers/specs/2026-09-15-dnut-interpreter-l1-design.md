# dnut_interpreter 模块设计（.dnut 序列语言与解释器下沉到 L1）

> 状态：**设计定案，未实施**；日期：2026-09-15
>
> **修订 v2**：归属从「塞进 `action` 模块」改为**新建 L1 模块 `dnut_interpreter`**，
> 且 `.dnut` 前端（词法/语法/编译/lint）随之下沉。v1 把序列运行时放进 `action`，
> 理由是成本（`action` 已是 L1、已永远编入、`rpg` 已依赖它、已有同族组件），
> 但 `action` 的契约是**单个动作的执行生命周期**（`src/modules/action/Action.h:5-13`、
> `ActionPhase` 于 `Action.h:38`），与「场景级序列解释器」在**粒度、控制方向、脚本面**
> 三处都不匹配；且 `action` 是 `LIB EVAction`、manifest 无 SCRIPT
> （`cmake/module_manifest/core_foundation.cmake:145-146`），装不下「脚本可扩展的语言」。
>
> **目标**：让引擎拥有**唯一的作者序列语言** `.dnut` 及其**唯一的解释器 owner**——
> 语言前端、步骤类型注册表、跨帧游标与分支/调用/等待/命令派发全部归 L1；
> `dialogue`（L6）与 `rpg`（L1）降为消费者，各自只注册自己的**块类型**与**步骤类型**；
> 表现层（sprite / Spine / 3D skeletal / Live2D / 地图 / 相机 / 音频）通过注册命令
> handler 接入，不进入语言与运行时。
>
> **涉及改动**：新增 `src/modules/dnut_interpreter/`；
> `cmake/module_manifest/core_foundation.cmake` 新增一条 L1 声明；
> `src/modules/dialogue/` 大面积迁移（§7 逐文件清单）；
> `cmake/module_manifest/orchestration.cmake:238` 给 `dialogue` 加依赖；
> `src/modules/rpg/StoryEvent.{h,cpp}` 接入；8 个测试文件、3 份模块文档。

## 1. 背景与现状

### 1.1 `.dnut` 今天是什么（2026-09-15 代码核对）

`.dnut` 已经在，但**一个扩展名下有两套互不相识的前端和两个入口**：

| 方言 | 解析器 | 产物 | 入口 | 依赖 |
|---|---|---|---|---|
| `pool { when … speaker: "…" }` | `dialogue/DnutParser.cpp`（真词法分析器 + 递归下降，`tokenize()` 于 `DnutParser.cpp:101-217`） | `DataValue` 根表（`DataValue` 是 `eve::Value` 的别名，`Dialogue.h:34`） | `Dialogue::loadPoolsFromDnut` / `loadPoolsFromDnutFile`（`Dialogue.h:138-140`） | `dialogue/Dialogue.h`（L6） |
| `conversation … node … when … -> …` | `dialogue/ConversationCompiler.cpp:85-177`（逐行 `words()` 切分，无词法器） | `ConversationAsset`（`Conversation.h:40-75`） | `DialogueFlow::loadFromDnut` / `loadFromDnutFile`（`DialogueFlow.h:100-102`） | `dialogue/Conversation.h`（L6） |

- **「两方言可共存于同一文件」只有一侧成立。** `docs/usr/modules/dialogue.md:169`
  如此声明，`ConversationCompiler.cpp:129-131` 也确实显式跳过 pool 语法；但
  `parseDnut` 遇到顶层 `conversation` 块会在 `DnutParser.cpp:414-415`
  （`expectIdent("pool")`）直接失败。今天 `examples/dialogue/pools.dnut` 与
  `examples/rpg-classic/data/village-dialogue.dnut` 是两份不同方言的文件，
  靠两个不同入口分别加载。
- **conversation 方言的词表是编译期闭集。** 节点种类 `line|branch|choice|call|command|wait|end`
  硬编码在 `ConversationCompiler.cpp:61-72`；属性名硬编码在
  `ConversationCompiler.cpp:147-155`（`next/speaker/text/pool/i18n/voice/target/return/result`）。
  没有任何区块或步骤的注册入口。
- **作者侧已经有一份手写反射。** `ConversationAuthoring.h` 的
  `ConversationDocument::getFieldCount/getFieldName/getFieldKind/getField/setField`
  按节点种类列出可编辑字段——这是「步骤类型描述符」的事实前身，但它是第二份真相源，
  与 `ConversationCompiler.cpp:147-155` 的属性表各自维护。
- **控制流与领域焊在同一个类型里。** `ConversationAsset::Node` 同时装
  `Branch/Choice/Call/Command/Wait/End`（控制流）与 `speaker/text/pool/i18nKey/voice/expression`
  （对话领域），以及 `PaymentSpec`/`StateMutation`/`CommandRequestKind`（对话事务语义）。

### 1.2 执行与消费现状

- 执行器 `ConversationRunner`（`Conversation.h:78-228`，实现于 `Conversation.cpp`）
  已经有完整的跨帧游标：`runUntilBlocked`（`Conversation.cpp:167-272`，含
  `Conversation.cpp:168` 的 10000 步预算护栏）、`advance`（`:398`）、`select`（`:407`）、
  `selectRouteForTransaction`（`:424`）、`resumeCommand` 双重载（`:373`、`:394`）、
  `captureState`/`restoreState`（`:296`/`:310`，帧内用 `captureFrame`，`:283-292`）。
- 消费面**全部在 dialogue 模块内**：`ConversationImporter`（Yarn/Twee）、
  `ConversationToolchain`（跨资产 lint 与重命名）、`ConversationAuthoring`
  （`ConversationDocument`）、`ConversationPersistence`（存档 JSON + 版本迁移）、
  `ConversationLocalization`、`editing/DialogueGraph`、`editor/EditorDialoguePreview`、
  `DialogueFlow`。模块外只有测试与 `examples/`（`test/dialogue_*.cpp` 共 17 个，
  其中 8 个直接包含迁移中的头文件；另有 `test/rpg_classic*.cpp`、
  `examples/rpg-classic/main.nut`）；`src/` 下唯一的跨模块引用是
  `src/modules/i18n/editing/LocalizationRuntime.cpp` 引用 `DialogueVoice.h`（与本次无关）。

### 1.3 通用机制其实已经齐了，缺的是一个 owner

| 已有能力 | 归属 | 层 | 提供什么 |
|---|---|---|---|
| 具名命令描述符 + handler 路由 + 校验 | `action::ActionNotifyRegistry` | L1 | `type + displayName + category + shape(Instant/State) + requiredPayloadFields`；`registerDescriptor`/`registerHandler`/`validate`/`dispatch`；`withBuiltins()` 已含 `presentation:vfx`/`presentation:audio`/`presentation:camera`（`ActionNotifyRegistry.cpp:21-34`） |
| 表现引用不绑后端 | `action::ActionTimeline` | L1 | 8 条语义轨道、Notify/StateEnter/StateExit、确定性 `sample(previous,current)`；"resources are referenced by URI/type payloads and resolved by downstream adapters"（`ActionTimeline.h:99-101`） |
| 分支条件 AST | `decision::Condition` | L0 | `All/Any/Not/Compare/HasTag/HasAttribute/HasResource/StateEquals/AuthorityCheck/PolicyCall`（`Condition.h:20`） |
| 脚本可注册策略 | `policyregistry::PolicyRegistry` | L0 | `ImplementationKind { Builtin, Script, Batch }` + generation 失效 |
| 宿主中立命令/观测 | `common::IGameplayControlProvider` | L0 | action descriptor 带 `parameterSchema`、command 带 `expectedRevision`、Capability 多 provider |
| 热重载 | `filesystem::HotReload` + `Runtime::reload(ScriptId)` + 目录原子替换范式 | — | `HotReload.h:19-33`、`Runtime.h:357`、`StoryEvent.h:62` |

### 1.4 rpg 侧只有平铺数据，且够不着 dialogue

- `enum class StoryEventStepKind { Dialogue, Message, Wait, Move, Camera }`
  （`StoryEvent.h:15`）是编译期闭集，1–128 步线性序列，固定字段
  `{reference, actorId, x, y, duration}`（`StoryEvent.h:18-25`）；`StoryEvent.cpp:33-58`
  的 `parseKind`/`kindName` 是唯一的种类真相源。没有分支、循环、标签、公共事件、
  脚本命令、开关/变量指令、移动路线、动画/音频/画面特效指令，也没有命令注册入口。
- 执行逻辑在宿主脚本里：`examples/rpg-classic/main.nut:714-753` 用
  `getStepKind()` 比较 + if/else 链逐条驱动。
- **决定性约束：`rpg` 够不着 `dialogue`。** `rpg` 是 **LAYER 1**
  （`core_foundation.cmake:102-103`），`dialogue` 是 **LAYER 6**
  （`orchestration.cmake:238-241`）。L1 依赖 L6 会被
  `scripts/module_depgraph.py --check-layers` 判失败。这就是 `StoryEvent` 为何是闭集枚举。

## 2. 问题定义

缺的不是「语言基础设施」，而是三件绑定在一起的事：

1. **一个 owner 负责「作者序列语言 + 跨帧游标 + 场景级控制流」。**
   `ActionTimeline` 是**单个动作内部**的采样器（无跨帧游标、无分支、无挂起）；
   `common::IGameplayControlProvider` 是**单条命令**的提交/观测协议（无序列）；
   `TransitionRunner` 有完整控制流但在 L6 且与对话领域焊死。
2. **`.dnut` 前端的分裂要收口。** 同一扩展名两套词法、两个入口、两种产物，
   且「共处一文件」只有半侧成立；新增步骤类型必须改 `ConversationCompiler.cpp:61-72`
   的闭集。没有统一前端，就不存在「可扩展的语言」。
3. **`.dnut` 必须对 rpg 可用。** rpg 在 L1，前端与运行时必须在 L1 或更低，
   否则 rpg 只能继续写第二套闭集枚举。

因此本次工作有三半，缺一不可：

1. **新建**：L1 模块 `dnut_interpreter`，拥有 `.dnut` 前端 + 资产模型 + 步骤类型注册表 + 运行时。
2. **迁移**：`dialogue` 的 conversation 方言与执行器下沉/改写为它的消费者，
   领域语义（payment / stateMutations / i18n / voice / 台词池 / UX）留在 dialogue。
3. **接入**：`rpg` 剧情事件改为该运行时上的注册步骤类型；两个消费者各自只注册词表。

## 3. 设计原则

1. **一个事实一个 owner。** `.dnut` 的语法、资产模型、序列游标、命令派发归
   `dnut_interpreter`；条件归 `decision::Condition`；命令词表归
   `action::ActionNotifyRegistry`；持久游标/完成事实/开关变量归 `rpg::GameState`
   （`StoryEvent.h:61-65` 已确立该契约）；命令的实际执行归各适配器。
2. **语言不懂领域。** `line` / `pool` / `story:move` / `story:animation` 都是**注册项**，
   不是编译器或运行时里的 `switch` 分支。`dialogue` 注册 `line` 步骤与 `pool` 块；
   `rpg` 注册 `story:*` 步骤。
3. **命令只发意图，不绑表现后端。** payload 一律 URI/type 字符串，沿用
   `ActionTimeline.h:99-101` 的既定原则。sprite / Spine / 3D skeletal / Live2D
   的差别落在 handler 实现上，换后端不改脚本。
4. **运行时不断言品类。** 它只推进游标、求条件、派发步骤；宿主边界靠
   `GameState` 游标 + 适配器注册，因此俯视 tile RPG / HD2D / 3D 动作共用同一份脚本。
5. **保语义搬迁。** 挂起/恢复、事务化选择、状态捕获三处逐字保语义（§5.5），
   不顺手重构；`.dnut` 既有源码的**接受集不变**（§5.9）。
6. **可重载。** 沿用已证范式：严格校验 → 原子替换目录 → 活动会话持定义副本 →
   事件内存档后重放未确认步骤。

## 4. 总体架构

```text
L1  dnut_interpreter（新模块）
    语言前端    DnutLexer            统一的 .dnut 词法器（今天的 tokenizer 升格）
                DnutBlockScanner     顶层块切分：pool{…} / conversation…endconversation
                DnutCompiler         conversation 块 → SequenceAsset，按注册表校验步骤类型
                DnutLint             可达性 / 重复 id / 跨资产引用 / 迁移校验
    资产模型    SequenceAsset / SequenceNode{id,type,next,payload,routes}
                SequenceRoute{label,condition,target,payload}
                SequenceDocument     可编辑文档（取代手写反射的 ConversationDocument）
    步骤词表    StepKindRegistry     descriptor + handler + payloadSchema + validate
                BlockKindRegistry    （后续增量）块类型的注册入口
    运行时      SequenceRuntime      跨帧游标 + 调用栈 + 挂起/恢复 + 状态捕获 + 预算护栏
                SequenceCommandRequest / Response / Handler
    持久化      SequenceSaveMigrations  资产 id + version + node 迁移（与语言无关）

L0  decision::Condition     条件求值（不变）
    policyregistry          脚本策略（不变）
    common::Value/StateValue 值类型（不变）

L6  dialogue（消费者）
    注册：步骤 `line`、块 `pool`
    保留：台词池/Dialogue、PaymentSpec、StateMutation、i18n、voice、UX、文本渲染、
          Yarn/Twee 导入、存档 JSON 编解码
    删除：ConversationAsset / ConversationRunner（被 L1 资产模型与运行时取代）

L1  rpg（消费者）
    注册：`story:dialogue` / `story:message` / `story:wait` / `story:move` / `story:camera`
          （后续）`story:animation` / `story:expression` / `story:wait-for-motion`
    保留：GameState（持久游标与完成事实的唯一权威）、StoryEventCatalogue 的内容目录契约

适配器（各自层）注册命令 handler
    avatar / animation / map / audio / ui / camera …
```

## 5. 详细设计

### 5.1 归属判据：逐文件/逐类型的迁移表

`dialogue` 现有 33 个文件按归属分类（依据：`Conversation*`/`Dnut*` 的 `#include` 图与类型用法）：

| 今天的文件/类型 | 归属 | 理由 |
|---|---|---|
| `DnutParser` 的 `tokenize()`（`DnutParser.cpp:101-217`） | **L1** | `.dnut` 词法，两种方言共用 |
| `ConversationCompiler`（语句解析、`nodeKind`、`attributes`、`lintConversations`） | **L1** | conversation 方言的语法与图校验；节点种类改由注册表解析 |
| `ConversationAsset` / `Node` / `ConversationRoute`（`Conversation.h:22-75`） | **L1** | 通用资产模型；领域字段改入 `payload` |
| `ConversationAsset::validate`（`Conversation.cpp:23-65`） | **L1 + dialogue** | 通用图校验（id/入口/引用）归 L1；`payment`/`stateMutations` 的「仅 command/choice 可用」规则归 dialogue 的步骤校验 |
| `ConversationRunner`（`Conversation.h:78-228`、`Conversation.cpp:67-459`） | **L1** | 纯控制流 + 游标（§5.5 三处逐字保语义） |
| `ConversationRunner::CommandResult/Event/CommandHandler` | **L1** | 通用命令与事件类型 |
| `CommandRequest`/`CommandResponse`/`CommandRequestKind`（`DialogueState.h:23-58`） | **L1（通用部分）+ dialogue（领域字段）** | `name/arguments/bindings/locals` 通用；`kind/payment/stateMutations` 是对话事务语义，留 dialogue |
| `toCanonicalValue`/`toDialogueStateValue`（`DialogueState.h:65,72`） | **dialogue** | 对话侧 legacy 值转换，L1 不引用 |
| `ConversationPersistence` 的 `ConversationSaveMigrations` | **L1** | 只依赖「资产 id + version + 节点 id 映射」，与领域无关 |
| `ConversationPersistence` 的 StateValue↔JSON | **dialogue** | 对话存档格式 |
| `ConversationToolchain`（跨资产 lint、`renameConversationAsset/Node`） | **L1** | 全是对资产图的操作 |
| `ConversationAuthoring`（`ConversationDocument`） | **L1** | 手写字段反射正是步骤描述符的 `payloadSchema`；保留即第二真相源 |
| `ConversationImporter`（Yarn / Twee） | **dialogue** | 外部对话格式到 `line` 步骤的映射是对话领域知识；产物是 L1 资产 |
| `ConversationLocalization` / `ConversationText` / `DialoguePayment` / `DialogueState` / `Dialogue.{h,cpp}` / `DialogueUX` / `DialogueVoice` / `editing/DialogueGraph` / `editor/EditorDialoguePreview` | **dialogue** | 领域载荷、本地化、表现与作者 UI |
| `DialogueFlow`（`DialogueFlow.h:33-232`） | **dialogue** | 脚本门面；内部改为持有 L1 运行时并注册 `line` 步骤 |

### 5.2 资产模型

```text
SequenceAsset {
  id, version, entry, parameters[],
  nodes[]: SequenceNode { id, type, next, payload, routes[] }
}
SequenceRoute { label, condition, target, payload }
```

- `type` 是**字符串**步骤类型名（`line` / `branch` / `choice` / `call` / `command` / `wait` /
  `end` / `story:move` / …），不再是闭集枚举。
- `payload` 是 `eve::Value::Object`：步骤类型自己的字段，由该类型的 descriptor 校验与解释。
  `line` 的 `speaker/text/pool/i18n/voice/expression`、`command` 的 `arguments/payment/stateMutations`、
  `choice` 的每条路由 `payment/stateMutations` 都进 payload。
- `condition` 沿用 `eve::Value`（今天 `ConversationRoute::condition` 即如此，`Conversation.h:25`）。

**为什么不让 `ConversationAsset` 留在 dialogue 与 L1 资产并存。** 并存就是同一份内容的两份
表示，正是本次要消除的「第二套控制流」；而且运行时的 `dispatch` 必须把 payload 交给**领域
注册的 handler**，L1 不可能知道 dialogue 的强类型结构，所以运行时一侧本来就必须接受弱类型
payload。既然运行时已经弱类型，资产再强类型只会多一次同步。dialogue 侧改为**无状态的
payload 编解码**（`decodeLinePayload(node)` 返回强类型视图），不持有第二份资产。

**拒绝的替代方案**：L1 定义资产**接口**（`ISequenceAsset` + 视图），dialogue 保留具体强类型资产
实现它。好处是 dialogue 侧改动小；坏处是每个领域字段都要走一次虚函数取值，且「步骤类型
的字段表」这个通用事实仍留在 dialogue，`ConversationDocument` 的手写反射也留着。故不采用。

### 5.3 步骤类型与块类型注册表

复用 `ActionNotifyRegistry` 已证明的形态（`ActionNotifyRegistry.h:19-71`），不新造机制：

```text
StepKindDescriptor {
  type, displayName, category,
  shape(Instant | State | Await),      // Await = 阻塞直到宿主 ack
  payloadSchema,                       // 字段表：同时供编译器严格校验与作者 UI 反射
  validate(payload) -> Result<void>    // 领域不变量（如 payment 只允许出现在 command/choice）
}
registerStep(type, descriptor, handler) / unregisterStep(type)
validate(node) / dispatch(node, context)
```

- `conversation` 块本身由 L1 编译；`pool` 块在阶段 1 仍由 dialogue 的池解析器消费，
  但**共用 L1 的词法器与顶层块切分**（`DnutBlockScanner`）。块类型的完整注册入口
  （`BlockKindRegistry`）列为后续增量，见 §11。
- `dialogue` 注册 `line`（Await）与 `choice` 的领域校验；`rpg` 注册 `story:*`（§4）。
- 适配器注册命令：`avatar`/`animation`/`map`/`audio`/`ui` 各自实现
  `presentation:*` / `movement:*` 的 handler。
- `ConversationDocument` 的 `getFieldName/getFieldKind` 反射（`ConversationAuthoring.h`）
  由 `payloadSchema` 派生，消除第二份字段表。

### 5.4 控制流集合

**本设计交付**（与今天 `ConversationRunner` 等价，纯搬迁）：

```text
Branch   取第一条条件通过的 route，否则 next          （Conversation.cpp:184-189）
Call     压栈 + 切入被调用资产 + bindings mergeDefaults + 结束帧时弹栈恢复（:204-225）
Command  派发具名命令；Blocked → blocked_ + waitingCommand_；Failed → 失败（:226-268）
Wait     阻塞，等宿主 advance                          （:181-183）
End      栈空则 Ended + stop，否则弹栈回到 returnNode  （:190-203）
```

**后续增量**（不在本设计交付，但 owner 已就位）：
`Loop / BreakLoop / Label + Jump / CallCommonEvent / Parallel`。

### 5.5 必须逐字保语义的四处

1. **双重重载**：`resumeCommand(StateValue)`（`Conversation.cpp:373`）与
   `resumeCommand(eve::Value)`（`Conversation.cpp:394`）。L1 运行时**两个都保留**，
   不做「只留 canonical 版本 + dialogue 加转换壳」的改写。理由：`common/StateValue.h`
   是 L0 且 L1 已有直接使用先例（`src/modules/rpg/RpgState.h:3`）；保留双载 = 零转换
   = 零行为变化，引入转换壳反而制造一条需要单独证明等价的新路径。
   `toCanonicalValue` / `toDialogueStateValue`（`DialogueState.h:65,72`）留在 dialogue，不被 L1 引用。
2. **事务化选择**：`selectRouteForTransaction`（`Conversation.cpp:424-459`）与
   `DialogueFlow.cpp` 的支付/状态事务包装。运行时的 `select` 必须保持
   「选中即提交、失败保持游标」的现状语义（`:445-455` 的 capture → enter → restore 序列），
   事务包装留在 dialogue。
3. **调用栈重建**：`captureState` 存的是 `{asset id, asset version, node, bindings, locals}`
   （`Conversation.cpp:283-292`、`:296-308`），即**稳定引用而非指针**。L1 必须延续这个形态
   （含 version 校验，`:327-329`、`:346-353`），不得简化成裸 id。
4. **`advance` 对 choice 的拒绝**：`advance` 在 choice 节点必须返回
   "select a choice route instead"（`Conversation.cpp:401-402`），不能被通用化掉。

另外 `runUntilBlocked` 的 10000 步执行预算（`Conversation.cpp:168`）必须保留——
它是脚本内容写错死循环时的唯一护栏，属于通用行为。

### 5.6 命令请求与响应

通用层（L1）——值类型与今天逐字一致，不引入新转换：

```text
SequenceCommandRequest  { name, arguments, bindings, locals }   // 全部 eve::Value，同今天 CommandRequest
SequenceCommandResponse { Status(Completed|Blocked|Failed), value, error }
SequenceCommandHandler  = std::function<SequenceCommandResponse(const SequenceCommandRequest&)>
```

帧内 bindings/locals 继续用 `eve::StateValue`（今天 `ConversationRunner::Frame` 就是这样），
与 canonical `eve::Value` 的边界保持今天的模样：命令请求/响应走 `eve::Value`
（`CommandRequest` 今天即如此，`DialogueState.h:39-41`），legacy handler 走 `StateValue`
（`Conversation.h:97` 的 `CommandHandler` 今天即如此）。

dialogue 侧保留领域扩展（`CommandRequest` 继续带 `payment` / `stateMutations` / `kind`，
`DialogueState.h:36-46`），由 dialogue 注册的 handler 做适配转换。
**不得**把 `PaymentSpec` / `StateMutation` 提到 L1——它们属于 dialogue 的事务语义。

### 5.7 挂起、事件与条件

- 挂起：`Blocked` → `blocked_ = true` + `waitingCommand_ = true` → 宿主
  `advance()` 或 `resumeCommand(value)` 继续。语义不变。
- 事件：运行时发出通用事件（`Started/NodeEntered/Command/Ended/Failed`）；
  步骤 handler 发出领域事件（`Line/Choice/…`）。`setEventSink` 保持单一出口。
- 条件：`setConditionEvaluator` 继续委托 `eve::decision::ConditionResult`，
  dialogue 侧的 legacy 分支表达式兼容路径（`evaluateRoute`，`Conversation.cpp:140-165`）
  留在 dialogue 的 evaluator 实现里。

### 5.8 热重载与存档迁移

沿用 `StoryEvent` 已证范式（`StoryEvent.h:61-65`、`docs/usr/modules/rpg.md:274-276`）：

- 内容目录严格校验 + 原子替换，失败保留上一份。
- 活动会话持**定义副本**，所以目录热替换不产生悬垂引用。
- 捕获的状态用**稳定 id + version** 引用资产，恢复时按新定义重新解析（§5.5 第 3 点）。
- 存档迁移 `SequenceSaveMigrations` 的规则全是资产级事实
  （`ConversationPersistence.h` 的 `Rule{assetId, fromVersion, currentAssetId, nodes}`），随之下沉。
- 事件中存档 → 读档后重放尚未确认的步骤（`examples/rpg-classic/main.nut:1967-1998`
  已有该行为雏形）。
- 脚本侧热重载沿用 `filesystem::HotReload` + `Runtime::reload(ScriptId)`，本设计不新增重载机制。

### 5.9 语言兼容性契约

前端下沉是**改写**而非移动（今天的 conversation 方言没有词法器，pool 方言有），
因此验收必须钉住「接受集不变」：

- 现有 `.dnut` 源码（`examples/dialogue/pools.dnut`、
  `examples/rpg-classic/data/village-dialogue.dnut` 及测试内联源）必须编译出**等价资产**：
  相同的 `id`/`version`/`entry`/`parameters`、相同的节点 id 与类型、相同的路由与目标、
  相同的诊断（`ConversationDiagnostic` → L1 `DnutDiagnostic`，字段逐字保留：
  severity/path/line/message）。
- **行为改善处需显式声明**：今天是 `parseDnut` 无法读含 `conversation` 块的文件
  （`DnutParser.cpp:414-415`），统一前端后两种方言可在同一文件被同一入口读入。
  这是有意扩大的接受集，必须在文档与测试中写明，不能作为「无声兼容」。
- 语法收紧要禁止：不得因为改写而拒绝今天能编译的源。

## 6. 分层影响与模块声明

- **新建 L1 模块 `dnut_interpreter`。** 声明插入
  `cmake/module_manifest/core_foundation.cmake` 的 `# L1 -- sensing/action protocol`
  段（`:133`）内、`action` 声明块（`:145-147`）之后、`# L2 -- combat resolution`
  （`:148`）之前——`scripts/check_module_manifest.py:216-227` 要求声明必须落在匹配的
  `# L<n>` 段内。
- **声明形态**：

  ```cmake
  # .dnut authored-sequence language, compiler, step-kind registry and cross-frame
  # interpreter. Domain vocabularies (dialogue lines/pools, RPG story steps) are
  # registered by their owning modules; this core never interprets a domain payload.
  eve_declare_module(NAME dnut_interpreter LIB EVDnutInterpreter LAYER 1
                     SCRIPT DnutInterpreter SLOT dnutInterpreter
                     DEPS decision
                     GROUP minimal 2d 3d web)
  ```

  `GROUP` 取值见 §11 待决问题 3；`SCRIPT`/`SLOT` 见 §11 待决问题 4。
  `LIB`+`SCRIPT` 同时使用有先例：`rpg LIB EVRPG LAYER 1 SCRIPT RPG`（`:102`）。
- **依赖合法性**：`dialogue`（L6）→ `dnut_interpreter`（L1）、`rpg`（L1）→
  `dnut_interpreter`（L1）都是合法下行/同层依赖；`scripts/module_depgraph.py:179`
  明确「Same-layer edges (e.g. window -> image) are allowed」，同层先例是
  `rpg`（L1）→ `action`（L1）。`dnut_interpreter` 不依赖 dialogue/rpg，无环。
- **`rpg` 的依赖声明**：`core_foundation.cmake:102-103` 的 `DEPS` 增加 `dnut_interpreter`。
- **`dialogue` 的依赖声明**：`orchestration.cmake:238-241` 的 `DEPS` 增加 `dnut_interpreter`。
  `dialogue` 无 `GROUP`，加依赖不改变任何裁剪档位。
- **裁剪影响**：`dnut_interpreter` 的消费者（`dialogue`/`rpg`）都不声明 `GROUP`，
  因此它不应声明 `GROUP`（§11 待决问题 3）；若声明 `GROUP minimal 2d 3d web`
  会被每个裁剪档无条件编入。裁剪声明按 AGENTS.md 要求给出层号与符号/构建证据。

## 7. 影响面清单

**新增**

- `src/modules/dnut_interpreter/`：`DnutLexer`、`DnutBlockScanner`、`DnutCompiler`、
  `DnutLint`、`SequenceAsset.h`、`StepKindRegistry`、`SequenceRuntime`、`SequenceDocument`、
  `SequenceSaveMigrations`。
- `test/dnut_interpreter_sequence.cpp`（控制流 + 挂起/恢复 + 捕获重建 + 预算护栏 + 注册表校验）、
  `test/dnut_interpreter_language.cpp`（词法/块切分/编译/lint/方言共处 + 诊断保真）。
  测试按 AGENTS.md 独立成文件，不进共享 test main。

**迁移（dialogue 内）**

- `DnutParser` 的 `tokenize()`（`DnutParser.cpp:101-217`）→ L1 `DnutLexer`；`parseDnut` 保留在
  `dialogue`，改为消费 L1 词法与顶层块切分。
- `ConversationCompiler.{h,cpp}`、`Conversation.{h,cpp}`、`ConversationToolchain.{h,cpp}`、
  `ConversationAuthoring.{h,cpp}`、`ConversationPersistence` 的迁移部分 → L1。
- `ConversationImporter.{h,cpp}`、`ConversationLocalization.{h,cpp}`、`ConversationText.{h,cpp}`、
  `DialogueFlow.{h,cpp}`、`dialogue/editing/DialogueGraph*`、`dialogue/editor/*` → 改为消费 L1 资产模型。
- `DialoguePayment.{h,cpp}`、`DialogueState.{h,cpp}`、`Dialogue.{h,cpp}`、`DialogueUX.*`、
  `DialogueVoice.*` → 原地不动（领域语义）。

**测试**：`test/dialogue_conversation.cpp`、`test/dialogue_compiler.cpp`、
`test/dialogue_proc.cpp`、`test/dialogue_toolchain.cpp`、`test/dialogue_authoring.cpp`、
`test/dialogue_persistence.cpp`、`test/dialogue_localization.cpp`、`test/dialogue_importers.cpp`
跟随类型变化；`test/rpg_classic.cpp:242`、`test/rpg_classic_playthrough.cpp:130` 直接使用
`compileDnutConversations`，需改为 L1 入口。

**文档**：新增 `docs/dev/dnut_interpreter 序列语言与解释器.md`（模块 owner 文档，
六面边界与裁剪声明写在这里）；更新 `docs/usr/modules/dialogue.md:168-180,275`（`.dnut` 语法
与 `loadPoolsFromDnut` 的归属指针）、`docs/dev/对话与Avatar模块设计.md:220`（`.dnut` 内容格式的
owner 变更）、`docs/dev/战斗系统与动作编辑器.md`（`action` 仍是单动作管线，序列语言不在其中）、
`docs/usr/MODULES.md:75`（若新模块有用户文档则登记）。

**示例**：`examples/dialogue/main.nut:133-135`（`loadPoolsFromDnutFile` 热重载路径）、
`examples/rpg-classic/main.nut:439-442,573`（`dialogueFlow.loadFromDnut`）在阶段 1 保持可用，
但内部路径改为 L1 前端。

## 8. 分阶段落地

1. **模块与语言（阶段 1，一个 PR）**：新建 `dnut_interpreter`（词法 + 块切分 + conversation
   编译/lint + 资产模型 + 步骤类型注册表 + 运行时 + `SequenceDocument` + 迁移），
   并**在同一个 PR 内**把 `dialogue` 全部消费者切过去（§7）。
   验收 = 现有 dialogue/rpg 测试全绿 + §5.9 的接受集契约。
   AGENTS.md 的「接口变化一个 PR」要求不留中间破坏 CI 的 commit，所以阶段 1 不能只做一半。
2. **rpg 接入（阶段 2，一个 PR）**：`StoryEvent` 改用通用运行时；注册五个既有步骤类型；
   保持 `eve.rpg.story-events` v1 的线格式与游标语义完全兼容；宿主脚本
   `examples/rpg-classic/main.nut:714-753` 的 if/else 链退役。
3. **补表现命令（阶段 3）**：`story:animation`、`story:expression`、`story:wait-for-motion`、
   移动路线（基于既有 `map::Pathfinder` + 一个按格移动控制器；
   `docs/dev/2.5D-tilemap-developer-evaluation.md:61` 已记录该缺口）。
4. **补控制流与脚本扩展面（阶段 4）**：`Loop / Label+Jump / CallCommonEvent / Parallel`，
   块类型注册入口 `BlockKindRegistry`，脚本侧注册步骤/块的能力，以及 lint 与 i18n 抽取的扩展。

阶段 1、2 是「不改内容格式」的架构收敛；阶段 3、4 是可交付能力，各自独立 PR。

## 9. 验收与门禁

阶段 1 的机械验收：

```sh
python3 scripts/check_module_manifest.py          # 新声明落在 L1 段内、目录存在
python3 scripts/module_depgraph.py --check-layers # L1 不出现对 L6 的依赖
ARCHITECTURE_BASE=HEAD make check/architecture-contracts
make check/quality-metadata
git clang-format                                   # 只格式化改动行
```

行为保持验收（必须覆盖 §5.5 的四处）：

- `resumeCommand`：`Blocked` → `advance`/`resumeCommand` → 继续的两条路径行为不变。
- `selectRouteForTransaction`：成功提交与失败保持游标两种情况不变；
  `advance` 在 choice 上仍拒绝。
- `captureState`/`restoreState`：含 0 层与多层调用栈、bindings/locals 保真、
  asset version 不匹配时明确失败。
- 执行预算护栏：构造超长/自环内容必须仍以预算耗尽失败，不挂死。
- 语言接受集：§5.9 的等价性用例（既有源 → 等价资产 + 等价诊断）。

契约条目：本设计改变的是「谁拥有 `.dnut` 的语法、序列游标与命令派发」，属可执行的
`state-owner` 与 `api-lifetime` 范围，按需在 `scripts/architecture_contracts.json` 补齐条目。

> **门禁陷阱（已核对，勿踩）：** AGENTS.md 要求新模块补 `module-interface` catalogue 条目，
> 但 `scripts/check_architecture_contracts.py:47-58` 的 `RULES` 里没有 `module-interface`
> （实际为 `api-shape`/`link`/`state-owner`/`ecs-system`/`time-rng`/`api-lifetime`/`persistence`/
> `optional-capability`/`backend-contract`/`debt-metadata` 十项），写这种条目会被判
> `rule must be one of …` 直接失败（`docs/dev/2026-09-15-模块边界审查台账.md:199`
> 也确认该机制尚未存在）。本设计**不**往 `architecture_contracts.json` 写非法 rule；
> 新模块的六面边界写进它的 owner 文档，并用可执行的 `state-owner`/`api-lifetime` 条目表达。

## 10. 非目标

- 不改变 `eve.rpg.story-events` / `eve.dialogue.*` 的既有线格式与存档兼容性。
- 不把 `PaymentSpec` / `StateMutation` / i18n / voice / 台词池等 dialogue 事务语义提到 L1。
- 不做脚本侧动态注册原生控制流（`std::function` 无法跨 C++/Squirrel）；脚本侧仍只可注册
  策略与命令 handler，控制流由 C++ 拥有。
- 阶段 1、2 不新增任何面向玩家的能力，只做架构收敛。
- 不把 `action::ActionTimeline` 的语义轨道采样并入本模块（动作内采样器与场景级序列是
  两个粒度，各自保留）。

## 11. 待决问题（实施前需定）

1. **payload 的值类型**：建议统一用 canonical `eve::Value::Object`（与 `CommandRequest` 一致）。
   代价是 `Node::arguments` 今天存 `StateValue`，payload 化后需要在边界转换；
   必须用 §5.5 的双重重载与等价测试证明零行为变化。
2. **`pool` 块的处理深度**：本设计取「阶段 1 共用 L1 词法与顶层块切分，池语法解析仍归
   dialogue」；完整方案是 `BlockKindRegistry` 让块类型成为注册项（阶段 4）。
   若实施中发现共享词法不足以表达池语法的行敏感属性（`parseAttrs` 用 `cur().line` 判定，
   `DnutParser.cpp:336-337`），需要在阶段 1 就引入 `BlockKindRegistry`。
3. **`GROUP` 取值**：推荐不声明 `GROUP`（与 `rpg`/`dialogue` 一致，只随完整构建进入）；
   备选 `minimal 2d 3d web`（每档都编入）。以 `scripts/profile_matrix.py` 输出取证后定。
4. **`SCRIPT` 面的范围**：推荐阶段 1 就声明 `SCRIPT DnutInterpreter SLOT dnutInterpreter`
   并只暴露只读/装载绑定，步骤与块的脚本侧注册留到阶段 4；若认为这构成未使用的门面，
   可先 LIB-only，但后续必须补一次「加 SCRIPT」的接口变更。
5. **命名空间与类型名**：模块名 `dnut_interpreter` 按惯例命名空间同名
   （`eve::dnut_interpreter`），但较长。候选 `eve::dnut`。需与 `eve::EventSequence`
   （`common/EventSequence.h`，事件流内序号，123 处引用）在阅读上明确区分——
   本设计不使用 `EventSequence`/`Sequence` 作为模块名。

## 12. ADR 信号

本设计触及持久架构面：**模块新增**（L1 `dnut_interpreter`）、**owner 变更**
（`.dnut` 语法、资产模型、序列游标与命令派发的 owner 从 L6 移到 L1）、
**公共契约变更**（`ConversationAsset`/`ConversationRunner` 被取代）、
**依赖方向变更**（`dialogue` → `dnut_interpreter`、`rpg` → `dnut_interpreter`）、
**源码真相源迁移**（语言前端与作者反射从 dialogue 迁到 dnut_interpreter）。
实施完成后应补一条 ADR，并在 baseline 同步时回答：
`action` 是否仍是「单动作生命周期」的唯一 owner（本设计回答：是），
以及 `dnut_interpreter` 是否同时承担了「语言」与「运行时」两个职责而需要再切分（待验证）。
