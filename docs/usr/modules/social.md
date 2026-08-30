# 社会关系图模块

**脚本入口：** `eve.Social()`

Social 保存四类事实：实体 owner、实体 controller、主体在目标上的角色指派，以及
`source -> target` 的具名数值关系。它不拥有游戏实体，只保存稳定 ID；删除实体时调用
`removeEntity`，以同时清理两端索引。完整组合示例见 `examples/composable-rebellion`。

## 所有权、角色和关系

```squirrel
local social = eve.Social();
social.setOwner("base:north", "faction:crown");
social.setController("army:first", "general:arden");
social.assign("general:arden", "governor", "base:north");
social.setRelation("officer:vela", "general:arden", "support", 0.8);

local owner = social.ownerOf("base:north");
local support = social.relation("officer:vela", "general:arden", "support");
```

字符串 ID 与整数 ID 属于不同命名空间；需要整数实体 ID 时使用 `*Int` API，
`idFromInt` 可取得其规范字符串形式，不能直接用 `"7"` 替代整数 7。

## API 快查

| 分类 | API | 说明 |
|---|---|---|
| 模块 | `getName()` / `clear()` | 查询名称或清空全部事实、索引和事件。 |
| Owner | `setOwner()` / `setOwnerInt()` | 设置实体 owner。 |
| Owner | `ownerOf()` / `ownerOfInt()` | 查询 owner。 |
| Owner | `ownedCount(owner)` / `ownedAt(owner,i)` | 按稳定顺序枚举 owner 拥有的实体。 |
| Controller | `setController()` / `setControllerInt()` | 设置实体 controller。 |
| Controller | `controllerOf()` / `controllerOfInt()` | 查询 controller。 |
| Controller | `controlledCount()` / `controlledAt()` | 枚举 controller 控制的实体。 |
| Assignment | `assign()` / `assignInt()` / `unassign()` | 添加或删除 `(assignee, role, target)` 指派。 |
| Assignment | `isAssigned()` / `assigneeCount()` / `assigneeAt()` | 测试或枚举目标上的角色成员。 |
| Assignment | `targetCount()` / `targetAt()` | 枚举某主体以指定 role 指向的目标。 |
| Relation | `setRelation()` / `setRelationInt()` | 设置具名有向关系值。 |
| Relation | `addRelation()` / `removeRelation()` / `hasRelation()` / `relation()` | 累加、删除、测试或读取关系。 |
| Relation | `relationSourceCount()` / `relationSourceAt()` | 按 target、type 和阈值反查 source。 |
| Relation | `relationTargetCount()` / `relationTargetAt()` | 按 source、type 和阈值查询 target。 |
| Link | `link()` | 写入通用的具名实体 Link。 |
| 清理 | `removeEntity()` / `removeEntityInt()` | 清除该 ID 作为任一关系端点的事实。 |
| ID | `idFromInt()` | 将整数域 ID 转成无碰撞的规范 ID。 |
| Event | `eventCount()` / `clearEvents()` | 查询或清除变更事件。 |
| Event | `eventSequence()` / `eventAction()` / `eventType()` | 事件顺序、动作和关系/指派类型。 |
| Event | `eventSource()` / `eventTarget()` | 事件两端稳定 ID。 |
| Event | `eventOldValue()` / `eventNewValue()` | 关系修改前后的数值。 |

## 所有权与一致性

- Social 只权威拥有关系事实，不拥有 ID 指向的实体；实体销毁者负责调用 `removeEntity`。
- 所有枚举结果只保证在下一次 Social 修改前有效，不要边枚举边增删关系。
- 同一事实只在 Social 中维护；不要再建立一份无法同步的 owner/controller 表。
- 关系方向有意义，`relation(a,b,type)` 不隐含 `relation(b,a,type)`。
