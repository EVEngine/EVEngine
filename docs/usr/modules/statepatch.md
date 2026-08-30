# 原子状态补丁模块

**脚本入口：** `eve.StatePatch()`

StatePatch 是以 `(subject, key)` 为地址的 JSON 值仓库。修改先写入 batch，再一次性
`commit`；任一操作无效或 expected value 冲突时整个批次不产生可见修改。它适合
权威玩法状态、编辑器事务和需要确定性快照的轻量事实表。

## 原子写入和冲突检测

```squirrel
local storeResult = eve.StatePatch().newStore();
if (!storeResult.ok) throw storeResult.status.summary;
local store = storeResult.value;

local batchResult = store.newBatch();
if (!batchResult.ok) throw batchResult.status.summary;
local batch = batchResult.value;
batch.set("actor:1", "rank", "3");
batch.set("actor:1", "alive", "true");
if (!store.commit(batch)) {
    local r = batch.result();
    for (local i = 0; i < r.errorCount(); ++i)
        print(r.errorAt(i).getMessage() + "\n");
}
```

`set` 的值必须是完整 JSON 文本，字符串要写成 `"\"mage\""`。用
`setExpected(subject, key, newJson, expectedJson)` 或 `removeExpected(...)` 做乐观
并发控制；expected 不匹配时 `commit` 返回 false，仓库保持不变。

## 查询、同步和快照

```squirrel
local n = store.queryDirty();
for (local i = 0; i < n; ++i)
    sendPatch(store.dirtySubjectAt(i), store.dirtyKeyAt(i));
store.clearDirty();

local snapshot = store.snapshotJson();
local copyResult = eve.StatePatch().newStore();
local restored = copyResult.value.restoreJson(snapshot);
if (!restored.ok) print(restored.status.summary + "\n");
```

`restoreJson` 先完整解析和校验，再替换可观察状态；失败不会留下部分恢复结果。
恢复成功会使旧 batch 句柄失效，恢复后必须重新调用 `newBatch()`。

## API 快查

### 模块、Store 与 Batch

| API | 说明 |
|---|---|
| `getName()` / `newStore()` | 返回模块名或创建模块拥有的 Store Result。 |
| `ownership()` / `ownerEpoch()` / `handle()` / `isStale()` / `release()` | 查询拥有型 Store/Batch 的身份、世代和生命周期。 |
| `newBatch()` | 创建当前 Store 拥有的空批次，返回 Result。 |
| `set(subject,key,json)` / `remove(subject,key)` | 追加无条件操作，返回 Result。 |
| `setExpected(subject,key,json,expected)` / `removeExpected(subject,key,expected)` | 追加带前置值检查的操作。 |
| `size()` / `clear()` / `result()` | 查询/清空批次，或读取最近提交结果。 |
| `commit(batch)` | 原子校验并提交；成功后 revision 至多推进一次。 |
| `has(subject,key)` / `get(subject,key)` / `valueRevision(subject,key)` | 查询值和最后修改 revision。缺失值的 `get` 返回空串。 |
| `revision()` | 当前仓库全局 revision。 |
| `querySubjects()` / `queryKeys(subject)` / `queryAt(i)` | 以词典序建立查询结果并按索引读取。后一次 query 会覆盖前一次结果。 |
| `queryDirty()` / `dirtySubjectAt(i)` / `dirtyKeyAt(i)` / `clearDirty()` | 枚举和确认脏键。 |
| `eventCount()` / `eventAt(i)` / `clearEvents()` | 枚举和清空已提交的 change event。 |
| `snapshotJson()` / `restoreJson(json)` | 导出确定性快照，或事务性恢复；恢复返回 Result。 |

### Result、错误和事件

| API | 说明 |
|---|---|
| `isSuccess()` / `getChangedCount()` | 最近 commit 是否成功以及实际变化数。 |
| `getRevisionBefore()` / `getRevisionAfter()` | commit 前后的全局 revision。 |
| `errorCount()` / `errorAt(i)` | 枚举提交错误。 |
| `getOperationIndex()` / `getSubject()` / `getKey()` | 错误对应的操作和地址。 |
| `getCode()` / `getMessage()` | 稳定错误码和诊断文本。 |
| `getSequence()` / `getRevision()` | ChangeEvent 的单调序号与 commit revision。 |
| `getOldJson()` / `getNewJson()` / `isRemoved()` | 变化前后 JSON，以及是否为删除。 |

## 生命周期与一致性

- Store 由 StatePatch 模块拥有；Batch 由 Store 拥有，二者都不是脚本自行删除的裸指针。
- `Effect` 等其他领域对象不会自动镜像到 Store；每个事实仍应只有一个权威 owner。
- `queryAt`、`eventAt`、`result` 返回的对象只用于即时观察，不跨恢复、释放或下一次结构修改保存。
- 快照包含 schema/version；只把 `snapshotJson()` 产物交给 `restoreJson()`，不要手工拼接持久化格式。
