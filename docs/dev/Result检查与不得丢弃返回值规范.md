# Result 检查与不得丢弃返回值规范

日期：2026-08-26
状态：已确认的架构要求

## 目标

对于可能失败或其返回值影响正确性的 API，同时提供两层保护：

- 编译期：通过 `[[nodiscard]]` 提醒调用方不得直接丢弃返回值。
- Debug 运行期：跟踪 Result 是否在生命周期结束前被观察；未检查即析构时触发 `EV_ASSERT`。

Release 不保留运行期检查成本，但 `[[nodiscard]]` 始终生效。

## Result 类型要求

公共 Result 类型本身标记 `[[nodiscard]]`：

```cpp
template<class T>
class [[nodiscard("Result must be checked or explicitly ignored")]] Result;
```

建议 Debug-only 状态：

```cpp
#if EVENGINE_ENABLE_ASSERTS
mutable bool observed_ = false;
bool mustObserve_ = true;
#endif
```

析构规则：

```cpp
~Result() {
#if EVENGINE_ENABLE_ASSERTS
    EV_ASSERT(!mustObserve_ || observed_,
              "Result destroyed without checking; inspect it or call ignore() explicitly");
#endif
}
```

不能只在 `value()` 中标记 checked；调用方检查失败状态同样属于有效观察。

## 哪些操作算作检查

以下 API 必须标记 Result 已观察：

- `ok()` / `accepted()` / `status()`
- `operator bool()`，若提供，必须为 `explicit`
- `value()` / `valueOr()` / `takeValue()`
- `error()` / `diagnostics()` / `code()`
- `match()` / `andThen()` / `orElse()` 等组合操作
- `throwIfError()`，若保留 Result→Exception 桥
- `ignore()` / `discard()` 显式放弃

仅执行以下操作不算检查：

- 构造、移动和赋值
- 日志打印 Result 对象地址
- 获取与状态无关的调试标签
- 将 Result 存入容器但不消费

访问器可以通过统一私有函数标记：

```cpp
void observe() const noexcept {
#if EVENGINE_ENABLE_ASSERTS
    observed_ = true;
#endif
}
```

## 显式忽略

确实不关心结果时，调用方必须写明意图：

```cpp
operation().ignore();
```

`ignore()` 算作检查，但名称必须体现这是主动放弃，而不是成功断言。建议 Debug 下允许附带理由：

```cpp
operation().ignore("best-effort telemetry cleanup");
```

不得用 `(void)operation()` 规避规范；`[[nodiscard]]` 对显式 void cast 的诊断由编译器决定，Debug 析构检查应继续捕获这种情况。

对于必须成功的调用，提供语义明确的 helper：

```cpp
operation().expect("failed to initialize gameplay schema");
```

`expect()` 检查 Result，并在失败时抛出/断言；不得让调用方以 `ignore()` 冒充“必然成功”。

## 移动与复制

推荐 Result 为 move-only：

```cpp
Result(const Result&) = delete;
Result& operator=(const Result&) = delete;
```

移动构造/赋值必须转移检查责任，并解除 moved-from 对象的析构检查：

```cpp
Result(Result&& other) noexcept {
    // move payload
#if EVENGINE_ENABLE_ASSERTS
    observed_ = other.observed_;
    mustObserve_ = other.mustObserve_;
    other.mustObserve_ = false;
#endif
}
```

如果未来确实需要可复制 Result，不能给每个副本独立 `observed_`，否则会产生误报；应让 Debug 副本共享 observation token，或显式 `cloneValue()` 生成不带检查责任的普通数据。默认不要为便利开放复制。

移动赋值前必须处理目标对象尚未消费的责任；Debug 下若目标 `mustObserve_ && !observed_`，应立即断言，不能用另一个 Result 覆盖并静默丢失。

## API 返回值分级

### A：强制检查 Result

所有可能产生业务失败、部分修改风险或资源/事务状态变化的 API：

- create/load/save/import/build/compile
- transaction prepare/commit/rollback/compensate
- registry insert/replace/remove/restore
- resource reserve/debit/credit/transfer
- action/order/effect apply、cancel、transition
- procgen build/upload/publish
- physics object creation、backend migration
- schema validation 和 snapshot restore
- editor command、document write、hot reload

返回公共 Result，具有 `[[nodiscard]]` 和 Debug 析构检查。

### B：不得丢弃的非 Result 值

一些 API 不返回 Result，但返回值决定对象身份或后续正确性，也应标记 `[[nodiscard]]`：

- `EntityHandle`、ResourceHandle、Subscription
- UUID/PersistentId 生成
- transaction/order/effect ID
- lock/guard/token
- ownership transfer 返回的 unique handle
- begin/end 配对 API 返回的 scope object

例如：

```cpp
[[nodiscard("keep the handle or explicitly destroy the entity")]]
ecs::EntityHandle spawn(...);
```

这类普通值不默认加入 Debug `observed_`，除非它本身代表必须消费的一次性 token。`Subscription` 等 RAII 类型可以定义自己的未使用规则。

### C：建议检查，但允许自然忽略

- `remove()` 返回是否存在
- cache hit/miss
- best-effort notify
- count、revision、statistics
- 普通 optional 查询

按风险选择函数级 `[[nodiscard]]`，不使用 Result 析构断言。若忽略是常见且正确的用法，不应添加噪声警告。

### D：可安全丢弃

- setter 返回 `void`
- fire-and-forget 且语义明确的 enqueue/notify
- 纯日志和 telemetry
- 明确命名为 `try...BestEffort` 且失败由内部记录的操作

Fire-and-forget API 必须在名称和文档中明确；不得把一个本来需要处理失败的 Result API 包装成静默 void 作为默认入口。

## Result 与异步 API

异步任务的“启动结果”和“完成结果”是两次不同责任：

```cpp
[[nodiscard]] Result<TaskHandle> startImport(...);
[[nodiscard]] Result<Artifact> TaskHandle::await();
```

如果 TaskHandle 被销毁时任务仍在运行，必须由其 own/cancel/detach 语义决定，不通过普通 Result 的 observed flag 代替任务生命周期管理。

Future/Promise 若承载 Result，最终消费端仍需检查 Result；中间转发移动检查责任。

## Result 与脚本绑定

C++ Debug flag 不能依赖 Squirrel GC 及时析构，因此脚本侧需要结构化投影：

```text
{ status, value, diagnostics }
```

绑定层在完成投影时即视为已经消费 C++ Result。脚本 API 通过以下方式继续约束：

- 关键 API 返回 Result object/table，不同时提供默认静默 bool 版本。
- Debug 工具可记录创建但未读取 status/value/diagnostics 的脚本 Result；这是后续增强，不作为首批 C++ Result 落地的阻塞项。
- `ignore()` 在脚本侧同样显式可见。

### 公共 Squirrel 投影契约

`src/engine/common/SquirrelBinding.h` 是唯一的 Result/Status/Diagnostic 与
`eve::Value` Squirrel adapter。所有 checked binding 使用同一张表，不再返回
`value/null + lastError`：

```text
Result<T> = {
  ok: bool,
  code: string,
  hasValue: bool,
  checked: true,
  ignored: bool,
  ignoreReason: string,
  status: { ok, code, summary, diagnostics, diagnosticCount },
  diagnostics: [{ code, severity, message, path, source, details }],
  value: Value | null
}
```

`eve.result.ignore(result, reason)` 要求非空理由，并把 `ignored`、
`ignoreReason` 写入该投影；它不把失败改写为成功。绑定在形成投影时已经显式
消费 C++ Result，脚本调用方仍必须检查 `ok`，或者明确调用该 helper。诊断的
`path` 是稳定字段路径/参数路径，`source` 是 producer/binding 来源，不得从
人类可读的 `message` 推导分支。

`eve::Value` 的唯一 Squirrel 双向转换同样位于 common。它只接受
Null/Bool/Int64/finite Double/String/Array/Object，限制最大深度和元素数，拒绝
循环容器、非字符串 table key、非有限数字及不支持的 Squirrel object，并在
诊断中保留完整路径和 source。反向 push 失败时恢复 VM stack，不留下半成品。

本轮已迁移的 canonical Result binding 包括 Procgen 的
`buildArtifact/publishArtifact`，Definitions/PolicyRegistry 的
registry mutation/resolve/generation API，Orders 的 append/replace/interrupt/
transition/update，Effects 的 apply/remove/update，以及 Transaction 的
stage/create API。Procgen 不再提供 `buildArtifactChecked`、
`publishArtifactChecked`、`lastError()` 或带 `Owned` 后缀的公共兼容入口；C++
侧使用带领域 tag 的 `Result<...HandleRef>`，Squirrel 侧保留易读的基础方法名，
并以 Result 投影返回由 handle 支持的 owned proxy。这里的 `Owned` 是对象所有权
语义，不是第二套方法命名。其他模块仍真实存在的历史兼容入口由各自模块文档
单独说明，本段不改变那些模块的 API 状态。

## 宏与注解建议

优先直接使用标准 `[[nodiscard("reason")]]`。如果需要统一跨编译器兼容，可定义：

```cpp
#define EV_NODISCARD [[nodiscard]]
#define EV_NODISCARD_MSG(reason) [[nodiscard(reason)]]
```

不要使用一个宏同时隐藏 `[[nodiscard]]` 和完全不同的运行时行为；Result 自身负责 Debug observation，函数注解只负责 API 意图。

## 测试清单

- [ ] 直接丢弃 Result 产生编译器 warning（CI 增加独立 compile-fail/diagnostic fixture，不污染正常 build）。
- [ ] 保存到局部变量但未读取，Debug 析构断言。
- [ ] `ok/status/value/error/diagnostics` 均标记 observed。
- [ ] `ignore()` 不断言；失败的 `expect()` 会报告原始 diagnostics。
- [ ] 移动后仅目标承担检查责任，moved-from 不断言。
- [ ] 移动赋值覆盖未检查目标时断言。
- [ ] 从函数多层返回/转发后最终消费者承担责任。
- [ ] Release 构建不包含 observation 字段和分支。
- [ ] 绑定层投影后 C++ Result 不产生未检查断言。

## 迁移顺序

1. 实现公共 move-only `Result<T>` 与 `Result<void>`，先加测试。
2. 迁移新 API 和 Transaction/Schema/Editor 等高风险写操作。
3. 迁移 Procgen build、snapshot restore、registry mutation。
4. 迁移 Effects/Orders/Resources/Settlement。
5. 审计现有非 Result 返回值，为 handle、token、Subscription 添加函数级 `[[nodiscard]]`。
6. 最后逐步淘汰 `bool/nullptr + lastError`。

## EVEngine 现有 API 的 `[[nodiscard]]` 决策

以下结论来自 2026-08-26 对 `src/engine`、`src/modules` 公开头文件的审计。原则是标记“丢弃会隐藏失败、丢失控制权或使操作没有意义”的返回值；普通 getter 不一刀切，避免警告疲劳。

### 必须标记：所有 Result 类型

Result 类型本身使用 class-level `[[nodiscard]]`，因此所有返回以下类型的 API 自动受约束：

- 未来公共 `Result<T>` / `Result<void>`
- 迁移后的 `EditorResult<T>`
- `presentation::WriteResult`
- `statepatch::PatchResult` 或其公共 Result 替代
- ConditionResult、SettlementResult、TransferResult、CompileResult 等包含状态/诊断的结果

现有 Editor API 全部属于这一类，包括但不限于：

- `EditorTransactionService::begin/append/commit/rollback/undo/redo`
- `EditorCommandService::register*/execute/plan/executePlan`
- `EditorSession::execute*/retainPlan/saveDocument/autosaveDocument`
- `EditorDocumentService::open/edit/requestSave/executeSave/close`
- `EditorAssetDatabase::publish/find/query`
- `GraphDocument::createNode/deleteNode/connect/disconnect`
- `MaterialGraphCompileService::compile/compileAsync/result/publishPreview`
- `IEditAuthority::preflight/commit/compensate`

即使某个 Result 当前总是成功，也不得在 call site 关闭检查；若 API 确实无失败语义，应改为 `void` 或直接返回普通值，而不是保留虚假的 Result。

### 必须标记：事务、异步、订阅和生命周期 token

- `presentation::IPropertyModel::subscribe`
- `rx::Observable::subscribe` 的全部重载
- 后续所有 GameEvent、Capability、Asset reload listener 的 Subscription 返回值
- `EditorTaskService::submit` 返回的 TaskId/TaskHandle
- `EditorTransactionService::begin` 返回的 TransactionId
- `EditorSession::retainPlan` 返回的 PlanId
- Future/Promise、Job、Task、CancellationToken、Lock/Guard、Scope 返回值
- 任何 `begin...()` 返回且要求 `end/commit/cancel` 的 token

丢弃 Subscription 会立即析构并退订，通常是逻辑错误；丢弃 Task/Transaction token 会失去取消、等待、提交或关联能力。对于明确支持 detached 的任务，应提供命名清晰的 `detach()` 或 `submitDetached()`，不允许靠丢弃 handle 表达 detached。

### 必须标记：创建、克隆和所有权转移

所有创建独立运行时对象、module-owned 对象或资源的工厂返回值：

- ECS `create()`/spawn API 及其 `ecs::EntityHandle`
- Editor 的 `newGizmo/newSession/newWorkspace/new*Target/new*Tool`
- Procgen C++ 的 `newParamsHandle/newGridHandle/newPointSetHandle/beginSystemHandle/`
  `buildMeshHandle/generate*Handle`；Squirrel 对应的基础方法名返回 Result 投影
  和 handle-backed owned proxy
- Physics 的 `newWorld/newBody/newShape/newJoint/newCloth` 等工厂
- Effects `newContainer`、Orders `newQueue`、Production `newQueue`
- Authority `newStore`、Definitions `newRegistry`、GameEvent `newStream`
- Card/Weapon/Vehicle/RPG 的 runtime entity factory
- Graphics/Image/Audio/Model 的资源创建和 clone API

中期应把易悬空的裸指针返回改成 strong handle、owner-aware borrowed handle 或 Result；迁移前先加函数级 `[[nodiscard]]`。

例外：明确命名为 `registerAndForget`、生命周期完全由 owner 管理且创建本身就是完整副作用的 API，可以返回 `void`；不要保留可丢弃裸指针。

### 必须标记：创建稳定 ID 或后续操作凭证

- `OrderQueue::append/replace/interrupt`
- `EffectContainer::apply`
- `WorkQueue::enqueue` 及同类任务创建 API
- `Transaction::create`/`Ledger::create` 返回的 plan/transaction ID 或对象
- Authority grant、Definition registration、Policy registration 返回的新 handle
- Decal、UI callback、observer hook 等返回 registration ID 的 API
- UUID/PersistentId、ArtifactId、DocumentId、EventId 生成 API

若业务确实允许 fire-and-forget，应显式写 `ignore()`，或另设返回 `void` 的 `enqueueDetached/emit` API。不能让同一个函数的 ID 在部分场景关键、部分场景静默丢弃而没有意图表达。

### 必须标记或迁移为 Result：加载、保存、恢复和编译

当前大量 API 使用 `bool/int + error/lastError`，应先函数级 `[[nodiscard]]`，再迁移到 Result：

- `ReloadSession::begin/commit/abort/restore`
- `ScenarioRecorder::begin/end/beginReplay/stageFrame`
- `ConversationAsset::validate`、`ConversationRunner::start/runUntilBlocked/advance/select/resumeCommand/restoreState`
- `RpgState/CardState/AnimStateMachine::restore*`
- Authority、Definitions、PolicyRegistry、Production、Sensing、Transaction、GameEvent、StatePatch 的 `restore*`
- I18n、Spine、Map/TileLayer、Voxel、ParticleEmitter 的 `load/reload/applyConfig`
- Filesystem write/copy/create/remove 和 Procgen `writeGridJson`
- Graphics `saveFramePng`、readback/capture 启动
- shader/pipeline/virtual geometry/fluid binding 的 `create/build/compile`
- network `start/submit/send/init`

其中表示“成功数量”的 `int loadFromJson()` 也必须标记；长期应返回 `Result<LoadSummary>`，避免 `0` 同时表示空输入和失败。

### 必须标记或迁移为 Result：有原子性要求的状态修改

如果返回 bool 表示操作可能被拒绝、冲突或只完成部分工作，必须检查：

- Resource/Economy 的 `debit/reserve/transfer`；`credit` 若返回实际入账/溢出量也必须检查
- Inventory/Container/Card 的 move/remove/transfer/draw/extract
- Orders/Production 的 complete/fail/cancel/pause/resume
- Effects apply/remove 和 Attribute modifier add/remove
- Scene/UI tree reconcile、parent/child 变更、document CAS
- Procgen `commitSystem/applyToLayer/publish`
- Physics/Graphics backend migration、resource replacement、mesh/skin upload
- Hot reload、plugin/module registration 与替换

这些 API 中很多应返回带 reason 的 Result，而不是仅加 `[[nodiscard]]` 后长期保留信息不足的 bool。

### 必须标记：有副作用的“取出/消费”操作

返回值携带被消费的数据，丢弃会造成数据丢失：

- Card Deck `draw/pop`
- Queue/Channel `pop/take/receive/dequeue`
- Inventory `remove/take/split`
- UI `consumeClick/consumeChange`
- GameEvent consumer `next/read/take`
- `GatherNode::extract`
- ownership `release/detach/takeValue`

纯 `peek/find/get` 不在这一规则内，因为它们没有消费副作用。

### 函数级建议标记：安全与边界检查

以下返回值经常在控制流中关键，但忽略有时合法，采用函数级 `[[nodiscard]]`，不增加 Debug observation：

- `isValid()`、handle resolve/`try_get()`
- bounds/index validation
- `canAfford/canCast/can/accepts/hasCapability`
- Physics raycast、shape cast、overlap/query 返回的 hit count 或 query token
- parser 的 `remaining/eof/valid`
- optimistic concurrency 的 revision/hash compare

其中 Physics 查询若 API 设计为“执行查询并把结果保存在 World 内部”，返回的 count 必须标记，因为忽略 count 后读取旧结果风险很高。长期优先改为 `[[nodiscard]] QueryResult`，让结果所有权和本次查询绑定。

### 通常不标记：普通无副作用 getter

默认不标记以下 API：

- `getCount/getWidth/getHeight/getName/getStatus`
- `has/isPlaying/isVisible` 等普通状态查询
- `find/get/at` 的只读查询（除非返回借用锁、必须释放的对象或安全 handle）
- revision、statistics、profiling 和 debug counters
- enum/string name 转换
- snapshot/export 字符串的纯生成函数

丢弃这些返回值通常只是无意义代码，不会隐藏状态修改失败。编译器的 unused expression、静态分析和 code review 足以处理，不应制造全项目 warning 噪声。

### 通常不标记：返回“是否发生变化”的幂等操作

如果 bool 只表示 changed/not-changed，而操作本身总是成功，允许忽略，例如：

- `removeTag`：不存在时保持不变，调用者只要求最终不存在
- `setSelected`：返回是否改变，但最终状态已满足
- cache `erase/invalidate`
- best-effort listener removal

前提是文档明确 bool 表示 `changed`，而不是 `success`。命名或文档含糊时应改成 `ChangeResult` 或返回 `void`，不能让调用者猜测。

### 虚函数和 override 的注解

Result 使用 class-level `[[nodiscard]]`，不会因 override 丢失。对于 bool、pointer、ID 等函数级注解，`[[nodiscard]]` 不应假设会自动从基类传播到所有静态调用类型；公开基类和公开 override 声明均应使用统一 `EV_NODISCARD_MSG`。CI 增加 header lint，检查关键 override 家族的一致性。

### 首批实际标记范围

为控制 warning 数量，第一批只处理：

1. Result/EditorResult/WriteResult/PatchResult 类型。
2. Subscription、TaskId、TransactionId、PlanId 和 scope/token。
3. 所有 `create/new/spawn/build/load/save/restore/commit` 的失败返回值。
4. `bool + error*` 或 `bool + lastError` API。
5. 有副作用的 consume/pop/take/draw/extract。
6. 创建后必须保留的对象/资源/registration ID。

第二批再处理 Resource、Container、Orders、Effects、Scene mutation 和 Physics query；普通 getter 不进入批量标记计划。
