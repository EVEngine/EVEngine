# EVEngine SWDB 参考实现分支（P1 确定性世界查询 + P2 事务 / 多 Agent 冲突检测）

> 分支名：`swdb-p1-deterministic-query`（含 P1+P2 两个切片，命名保留 P1 主语义）
>
> 对照蓝本：EVEngine 上游 `docs/dev/SemanticWorldDatabase架构设计.md`（v0.6.1，2026-10-24）
> 该文档自述：统一世界版本、Schema 注册、混合查询规划、向量投影、世界 MVCC、
> 语义冲突规则与依赖驱动的原子发布，**均为"待设计和实施"能力，P0–P4 全部拟议**。
> 本分支把其中 P1（确定性世界查询）与 P2（事务与多 Agent 竖切）两个切片做成
> **可独立运行、可自动化验收的参考实现**，供上游 C++/ECS 侧移植时对照语义。

---

## 0. 为什么需要这个分支

对上游 v0.6.1 的源码侦查结论：作者 12 天从 v0.5.0 冲到 v0.6.1（PR #673），
此前判定缺失的"元内核"里三块已被作者自己补上：

| 原判定缺失内核 | 上游现状（v0.6.1） |
| --- | --- |
| ④ 世界内 Agent 运行时 | 已实现：`src/modules/npc_ai/NpcAiModule.cpp` 脚本面 `ScriptNpcAiWorld`（registerBehavior / createAgent / signal / remember / tick / snapshot），示例 `examples/companion-tactics/`；另有 `src/modules/agent/`（训练/推理/replay）与 `src/modules/gpuagents/` |
| ② 架构行为图 + 任务上下文 | 已实现：`eve_architecture_impact` / `eve_task_context`（PR #530） |
| ②③ Semantic World Database（SWDB） | **仍为纯设计文档**：`docs/dev/SemanticWorldDatabase架构设计.md` 与 `ProjectSemanticIndex设计.md` 无 src 实现 |

→ 分支补丁点锁定 **SWDB**，先做 P1（确定性世界查询），再做 P2（事务 / 冲突检测）。

---

## 1. 目录结构

```
tools/swdb-proto/
├── swdb.py          # P1：Schema/字段契约、稳定 ID、权威世界 + 只读快照、三类投影索引、
│                    #     水位与强一致检查、查询规划器（预算/分页/降级）、MCP 契约桩
├── swdb_tx.py       # P2：事务（read_set CAS / write_set / 范围谓词 phantom）、约束注册表、
│                    #     条件 undo、审计日志、TransactionManager、MCP eve_world_commit
└── tests/
    ├── test_swdb.py    # P1 验收：22 用例
    └── test_swdb_p2.py # P2 验收：14 用例
```

纯 Python 标准库，零依赖；可在无 Vulkan / 无引擎环境直接验证。

## 2. 验证方式

```powershell
cd tools/swdb-proto
python -m unittest discover -s tests
# Ran 36 tests ... OK   （P1 22 + P2 14）
```

---

## 3. P1 — 确定性世界查询（对照文档 §2 / §3 / §4 / §5 / §9）

**P1 验收点（文档 §11）**：同版本查询与权威扫描一致；删除、移动、索引落后和裁剪后
降级行为可验证；结构化查询不依赖向量。

| 能力 | 实现 | 文档对照 |
| --- | --- | --- |
| 权威状态分层 | `World`（权威写入，revision 递增）→ `WorldSnapshot`（绑定 revision 的只读视图，多次查询一致） | §2：ECS 权威 / 投影只读快照，不扫裸指针 |
| 稳定 Entity ID | `PersistentId(domain:seq)`；删除留 tombstone（含删除 revision）；ID 永不复用；`allocate_pending_id()` 供事务预占 | §3 |
| Schema / 字段契约 | `SchemaRegistry` + `FieldDef`：readonly（如 hp）与 derived（如 displayName）拒绝写入；`coerce` 类型校验 | §4 |
| 混合索引 + 水位 | `AttributeIndex` / `SpatialIndex`（格网桶）/ `VectorIndex`（可缺席）；`IndexWatermark` 披露版本与 lag | §5 |
| 强一致 vs 最终一致 | `strong_consistent(snapshot_rev, required)` **只检查查询实际依赖的索引集合**；落后 → 返回 `stalled:` reasons，不返回陈旧结果；`eventual` 允许返回但披露水位 | §5 / §9 |
| 查询规划 | 属性索引起点 → 向量候选（弱一致，字符串统一转 `PersistentId`）→ 空间起点 → 权威快照后过滤；预算、截断、分页 token | §5 / §9 |
| MCP 契约桩 | `mcp_eve_world_query`（similarTo 且向量缺席 → `vector-index-unavailable`）、`mcp_eve_world_snapshot` | §9 |

**开发中修掉的两个真语义 bug（勿回退）**：
1. 强一致原本检查全部索引 → 改为**只检查查询依赖的索引集合**（required={attribute,spatial[,vector]}），否则移动实体会让纯空间查询误 stalled；
2. 向量候选字符串 → 统一转 `PersistentId` 再参与后过滤。

---

## 4. P2 — 事务 / 多 Agent 冲突检测（对照文档 §6 / §7 / §13.8）

**P2 验收点（文档 §11）**：并发可复现合并 / 拒绝 / 重试；运行时写入参与检测；
owner 失败无部分发布。

| 能力 | 实现 | 文档对照 |
| --- | --- | --- |
| 事务工作单元 | `Transaction`：tx_id + actor + base_revision + read_set（读到时的字段版本）+ write_set（期望旧版本, 新值）+ 命令列表；`read/read_range/write/create/delete` | §6 / §13.8 |
| MVCC / CAS | 提交时 dry-run：**read_set 全部字段**（含纯读依赖与 MCP readSet 声明）的当前版本必须等于读到时的版本，否则 `Conflict(version-mismatch)`（附 current/expected 与 retryHint）；`write` 以本事务首次 read 的版本为前置条件 | §6：`commit(baseRevision, readSet, commands, preconditions=[FieldVersion(...)])` |
| Phantom | 范围谓词记录 base_revision；base 之后落入范围内的新对象使旧事务 `Conflict(phantom)` | §6 |
| 领域约束 | `ConstraintRegistry`：唯一性覆盖世界已有 + 事务内 pending create 之间 + write_set key 更新；引用覆盖 create/write 正向与 delete 反向；owner 验证可注入失败 | §7 |
| 原子性 | `commit` = dry-run 全通过 → 原子应用（creates→writes→deletes）→ revision 一次性 +1 → 字段版本统一更新 → tombstone；owner 失败时**世界零变化**（无部分发布） | §6 / §7 |
| 隐藏写者检测 | 运行时直写（`World.set_field`）同样递增字段版本 → 事务 CAS 自然失败，无法绕过冲突检测 | §6 |
| 条件 undo | `undo()` 生成**带当前前置条件的新事务**：逆向 write 写回事务应用前的旧值；撤销 create（删除存活实体）；按删除前快照重建已删实体（restore）；undo 本身也可能冲突 | §6：undo 不是整世界回滚 |
| 审计 | `AuditLog`（committed/conflict/owner-rejected/aborted 条目，含 at_revision） | §13.8 |
| MCP 契约 | `mcp_eve_world_commit`（JSON-RPC：readSet 声明期望版本，commands set/create/delete；冲突 → `{ok:false, conflict}` 并补审计） | §6 commit() 语义示意 |

**已知简化（诚实边界）**：
- phantom 检测用 `field_version(pid,"position") > base_rev` 推断"创建于 base 之后"；
  无 position 字段的实体跳过 phantom 检查；
- `Transaction._apply` 直接访问 `World` 内部结构（原型单线程模拟多 Agent，应用阶段
  无其他事务插入，结果等价原子）；C++ 侧应由世界 owner 安全点 / ECS 批处理协调；
- restore 数据可能撞 unique 约束（属设计语义：undo 也可能被约束拒绝）；
- 直接 `Transaction.commit()` 遇 Conflict 不自动写审计（MCP 路径才补审计条目）。

---

## 5. MCP JSON-RPC 契约示例

### 查询（P1）

```json
{
  "tool": "eve_world_query",
  "params": {
    "snapshot": 42,
    "type": "Door",
    "within": {"center": [5, 5], "radius": 20},
    "where": {"interactable": true},
    "similarTo": "废弃矿场的门",
    "limit": 20,
    "consistency": "eventual"
  }
}
```

响应含 `ids / revision / tick / indexWatermarks / truncated / pageToken / reasons`；
向量 provider 缺席且请求 similarTo 时返回 `{ok:false, error:"vector-index-unavailable"}`。

### 提交（P2）

```json
{
  "tool": "eve_world_commit",
  "params": {
    "actor": "agent-A",
    "baseRevision": 42,
    "readSet": [{"pid": "dungeon:1", "field": "locked", "version": 7}],
    "commands": [
      {"op": "set", "pid": "dungeon:1", "field": "locked", "value": true},
      {"op": "create", "type": "Chest", "domain": "dungeon", "fields": {"gold": 10}},
      {"op": "delete", "pid": "dungeon:5"}
    ],
    "preconditions": [{"kind": "field-version", "pid": "dungeon:1",
                       "field": "locked", "version": 7}]
  }
}
```

冲突响应：`{ok:false, conflict:{kind, detail, retryHint, actor}}`，kind ∈
`version-mismatch | phantom | unique-violation | dangling-reference |
owner-rejected | entity-missing`。

---

## 6. 移植到上游 C++/ECS 的要点（建议 PR 形态）

建议落点 `src/modules/swdb/`，按上游模块惯例（模块 + Script 面 + MCP 工具注册）：

| Python 参考 | C++/ECS 对应 |
| --- | --- |
| `SchemaRegistry / FieldDef` | `swdb_schema.h/.cpp`：编译期或数据驱动 Schema 表，字段契约（readonly/derived/owner） |
| `World`（revision + field_versions） | ECS 世界 + 全局 `revision` 计数 + per-field 版本位；写入统一走 owner API |
| `WorldSnapshot` | 一致性只读快照：可复用上游事件系统 + 快照缓存，**不扫并发裸指针** |
| `AttributeIndex / SpatialIndex / VectorIndex` | 投影组件 + 水位记录；向量投影可迟到可重建 |
| `QueryPlanner` | 查询管线：起点选择（属性/空间/向量）→ 权威后过滤 → 预算/分页 |
| `TransactionManager / Transaction` | 工作单元：MCP 进来的修改意图先转事务，dry-run 验证，原子应用 |
| `Conflict` | 结构化错误：返回给 Agent 的 JSON-RPC error，Agent 据此重新规划而非盲重试 |
| `AuditLog` | 复用上游 `eve_architecture_impact` / `eve_task_context` 的因果与审计基础设施 |

依赖关系：P1 无外部依赖；P2 依赖 P1 的字段级版本（`World.field_version`），
建议按 P0（Schema+ID+查询）→ P1（确定性查询）→ P2（事务）顺序合入。

---

## 7. 诚实边界清单（上游合入前需补齐）

> **Review 修复记录（repro_review.py，13 项 / 15 断言，均已修复并转正为
> `tests/test_swdb_review.py` 回归测试）**：快照深拷贝隔离（双向不可见）、
> 完整 read_set CAS（纯读依赖参与版本校验）、write 字段契约（readonly/derived/
> 类型 coerce，事务不可绕过 Schema）、create 应用前校验（无效 create → SchemaError，
> 世界与审计零变化）、唯一性约束覆盖事务内 create 之间与 write_set 更新、
> 删除反向引用检查、type-only 查询回退全量权威扫描、分页 token 绑定快照
> revision（陈旧 token 拒绝）。

1. **并发模型**：原型是单线程模拟多 Agent 提交；真实并发（多线程 / 多进程 / 多进程内
   多线程）的锁与提交临界区未做压力验证。
2. **向量投影**：P1/P2 均未实现真实 embedding 后端，只定义了 `VectorIndex.provider`
   接口与缺席降级；上游接入需自带向量检索服务。
3. **持久化**：无磁盘持久化；`AuditLog` 在内存。文档也注明"内存原子性 ≠ 磁盘耐久性"。
4. **查询优化**：未做选择率估计 / 索引成本模型；格网桶 cell=8.0 为硬编码，未按
   数据分布自适应。
5. **P3+（文档 §11 其余切片）**：世界 MVCC 快照 GC、跨域依赖发布、意图风险分级、
   规划器与事务的组合验证，均未实现。
