# dnut 序列语言与解释器（模块 `dnut_interpreter`）

日期：2026-09-15
层：**L1**
源码：`src/modules/dnut_interpreter/`
清单：`cmake/module_manifest/core_foundation.cmake` 的 `dnut_interpreter` 声明

## 1. 这个模块拥有什么

`.dnut` 是引擎的**作者序列语言**。本模块是它的**唯一 owner**：

| 事实 | 唯一 owner | 说明 |
| --- | --- | --- |
| `.dnut` 词法 | `DnutLexer` | 所有方言共用；不变量是「同一文件只有一个词法器」 |
| 编译产物模型 | `SequenceAsset` / `SequenceNode` / `SequenceRoute` | 通用图模型，节点类型是**字符串**而非闭集枚举 |
| 步骤词表与字段表 | `StepKindRegistry` | descriptor 是字段的唯一真相源；编译器与作者 UI 都从它派生 |
| 跨帧执行游标 | `SequenceRuntime` | 调用栈、挂起/恢复、状态捕获、执行预算护栏 |
| `.dnut` → 资产的编译 | `DnutCompiler` | 只懂控制流关键字；步骤合法性交给注册表 |
| 条件求值的**契约** | `SequenceConditionOutcome` | 只定义「通过/不通过 + 稳定原因」；条件系统由消费者适配 |

**不拥有**（明确不在本模块）：任何领域语义。`line`/`pool`/`story:move`/`skill` 都是
**注册项**，不是编译器或运行时里的 `switch` 分支。命令的实际执行、i18n、支付、
存档格式、表现后端都在各自的模块。

## 2. 对外六个面

| 面 | 内容 | 取证 |
| --- | --- | --- |
| 能力入（capability in） | 无。本模块不 `cap::query` 任何接口，也不调用 `getModInst`/`ModuleManager`。消费者直接链接并在自己线程上构造注册表与运行时。 | `grep -n "cap::\|getModInst\|requireModInst" src/modules/dnut_interpreter/*` → 0 命中 |
| 能力出（capability out） | 无 `cap::provide`。对外契约是**类型与函数**：`lexDnut` / `compileDnut` / `SequenceRuntime` / `StepKindRegistry`。 | 同上；`module_boundary_audit.py --capabilities` 中该模块无条目 |
| ECS + Link 数据 | 无。本模块不注册 ECS 组件、不注册 Link 种类、不创建实体。 | `grep -n "SystemContract\|LinkKind\|registerLinkKind"` → 0 命中 |
| 事件 | 唯一出口 `SequenceRuntime::setEventSink`；事件种类 `Started/NodeEntered/Blocked/Stepped/Resumed/Ended/Failed`，同步派发、无锁、不得重入。 | `SequenceRuntime.h` 的 `Event` / `EventSink` |
| 脚本绑定 | **无 SCRIPT 声明**。脚本面由消费者提供：`rpg` 绑定 `eve.rpg.newStorySession(...)` 等。 | manifest 中该模块无 `SCRIPT` |
| 工具协议 | 无 MCP/LSP/DAP 方法。 | — |

**跨模块包含**：公共头只包含 `common/Result.h`、`common/Value.h` 和本模块头。
不包含 `decision`、`dialogue`、`rpg` —— 条件求值通过 `std::function` 契约注入。

## 3. 裁剪声明

- **L1 编译期**：`dnut_interpreter` 的源文件不参与编译时，`dialogue`/`rpg` 的 TU 会
  因找不到头而失败——它们是**硬依赖**（`DEPS`），不是可选依赖。
- **L2 链接期**：`rpg` 的 `RpgDialect.cpp` 调用 `compileDnut`、`SequenceRuntime::start`、
  `StepKindRegistry::registerStep` 等非 inline、非虚函数，产生**未定义符号**，因此
  关掉本模块后 `EVRPG` 无法链接。这是 `R-BOUND-2` 判据表「调 B 的非虚、非 inline
  成员函数」一行。
- **L3/L4**：本模块没有运行时可选性——它要么被编入，要么消费者无法编译。因此
  不声明 `GROUP`（与 `rpg` 一致，只随完整构建进入），也不需要 present/absent 双 profile。
- **符号引用证据**：`rpg/RpgDialect.cpp` 对 `eve::dnut::compileDnut(...)`、
  `eve::dnut::SequenceRuntime::start(...)` 的调用即符号引用；`include` 图不作为判据
  （`R-BOUND-1`）。

## 4. 语言表面

```
document   := block*
block      := 'story' IDENT modifier* '{' statement* '}'
            | <其他方言的顶层块，直接跳过>
modifier   := 'repeatable' | 'version' '=' INT
statement  := step | ifStmt | choiceStmt | 'end'
step       := IDENT (IDENT '=' value)*
ifStmt     := 'if' condition block ['else' block]
choiceStmt := 'choice' '{' ('option' STRING ['when' condition] block)+ '}'
value      := STRING | NUMBER | 'true' | 'false' | IDENT | '-' NUMBER
condition  := 比较 / '&&' / '||' / '!' / '(' ')'
```

- `story` 之外的顶层块（`pool`、`conversation`）被**跳过**，因此一个文件可以同时
  容纳其他方言而不被本编译器拒绝。
- 控制流关键字只有 `if` / `choice` / `call` / `wait` / `end`；它们分别下降到
  `branch` / `choice` / `call` / `wait` / `end` 节点。**步骤类型不是关键字**：
  `mark`、`skill`、`story:move` 一样由注册表声明。
- 条件编译为 `eve::Value` 对象：`{var,op,value}`、`{all:[…]}`、`{any:[…]}`、`{not:…}`，
  `op ∈ eq|ne|gt|lt|ge|le`。**求值器由消费者注入**：语言核心不知道 `switch.x` 是什么。

### 4.1 编译错误恢复

单个语句失败时，编译器同步到下一个可能的语句起点继续，因此一次编译能报出多个
错误（例如一个块里两处拼写错误会得到 2 条诊断）。**含错误的文档不发布资产**：
`RpgStoryCatalogue::replaceFromDnutStrict` 在 `hasErrors()` 时保留上一份目录。

## 5. 步骤注册表契约

```cpp
StepKindDescriptor {
  type, displayName, category,
  shape(Instant | Await),
  fields[{name, type(String|Number|Integer|Boolean|Any), required}],
  validate(node) -> Result<void>      // 跨字段的领域不变量
}
```

- `Instant`：必须绑定 handler；`validate` 会拒绝「instant 但没有 handler」的声明。
- `Await`：可以**不绑定 handler**，此时运行时挂起、宿主呈现后用
  `SequenceRuntime::advance()` 确认；绑定了 handler 的 `Await` 可以返回 `Blocked`
  并在之后用 `resumeStep(value)` 继续。
- 字段校验是**严格**的：未声明的字段被拒绝。这是刻意的——拼错的字段名不会静默
  变成一个永远为空的属性。
- `validate` 是跨字段规则（「`learn` 与 `forget` 恰有其一」）的落点，因此这类
  错误在**编译内容时**报出，而不是等玩家走到那一步。

### 5.1 `StepContext::host`

语言核心不理解消费者的领域对象。消费者在运行时上装一个不透明指针
（`setHostContext`），核心原样转发给 handler。`rpg` 用它传 `RpgStoryBinding*`。
这保持了「核心零领域依赖」，代价是一个需要文档说明的 `void*`。

## 6. 所有权、生命周期与线程

- `SequenceAsset` / `StepKindRegistry` 归调用者所有。`SequenceRuntime` 持有
  **借用**指针，调用者必须保证它们在运行时存活期内有效。
- `RpgStorySession` 在 `begin` 时**快照整份目录**，因此热替换不会让活动会话悬垂。
- `captureState` 存的是**稳定引用**（`asset id` + `version` + `node id` + bindings +
  locals），不是指针。`restoreState` 通过资产解析器重新解析，并在 `version` 不匹配时
  明确失败。
- 所有方法都是**owner 线程专属**，不做同步；事件回调同步派发，且不得重入运行时。
- 执行预算 `kExecutionBudget = 10000` 是内容写出死循环时的唯一护栏：超预算失败而不是挂死。

## 7. 已知债务（显式登记）

| 债务 | owner | 原因 | 移除条件 |
| --- | --- | --- | --- |
| `dialogue` 的 `pool` / `conversation` 两套前端尚未迁到本模块 | dialogue | 迁移触及 15 个文件 + 8 个测试文件，与「新增 RPG 方言能力」混在一次交付里失败模式不可分离 | 设计文档 `docs/dev/superpowers/specs/2026-09-15-dnut-interpreter-l1-design.md` §8 阶段 1 的剩余部分完成 |
| 顶层块切分（`DnutBlockScanner`）尚未抽成显式类型，目前是编译器内的跳过逻辑 | dnut_interpreter | 只有 `story` 一种块需要真正解析，抽象尚无第二个消费者 | dialogue 迁移开始时随迁移落地 |
| `SequenceDocument`（作者字段反射）尚未落地 | dnut_interpreter | 当前无作者 UI 消费者；字段表已由 `payloadSchema` 唯一拥有，反射可按需派生 | 编辑器接入 `.dnut` 作者视图时 |
| `rpg::StoryEvent`（JSON 闭集）与 `.dnut` 故事并存 | rpg | 既有 JSON 路径有 4 个测试文件与线格式兼容要求；本次交付只新增能力，不做格式替换 | 设计文档 §8 阶段 2 完成时 |

债务没有第二真相源：`.dnut` 的语法、模型与游标只有本模块一份实现；
`StoryEvent` 是**另一种内容格式**，不是同一事实的第二份表示。

## 8. 验收

```sh
python3 scripts/check_module_manifest.py
python3 scripts/module_depgraph.py --check --check-layers
build/win32-debug/test/unit_test.exe "dnut.*"
build/win32-debug/test/unit_test.exe "rpg.dnut.*"
```
