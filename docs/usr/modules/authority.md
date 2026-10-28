# 权限规则模块

**脚本入口：** `eve.Authority()`

Authority 按 `(actor, scope, capability)` 仲裁 allow/deny 规则。规则带 source、priority
和可选持续时间；高优先级胜出，同优先级冲突时 deny 胜出。规则字段可写 `"*"` 作为该维度的
通配符。Store 保留规则变化和过期事件（可设容量），可用 explain 获取可审计的最终决策。

## 授权、拒绝和解释

```squirrel
local result = eve.Authority().newStore();
if (!result.ok) throw result.status.summary;
local auth = result.value;
local grant = auth.grant("general:7", "base:2", "govern",
                         "role:general", 5, 0.0);
local deny = auth.deny("general:7", "base:2", "govern",
                       "policy:emergency", 10, 3.0);
if (!grant.ok || !deny.ok) throw "authority rule rejected";

local decision = auth.explain("general:7", "base:2", "govern");
if (!decision.isAllowed()) print(decision.getWinningRuleId() + " denied\n");
auth.update(dt);
```

duration 为 0 表示不自动过期。`can` 适合只取布尔结果，`explain` 用于 UI、日志和审计。
规则变化后不要缓存旧 decision；下一次查询重新计算胜者。

通配示例：`auth.grant("*", "base:2", "inspect", "role:visitor", 1, 0.0)` 让任意 actor
在该基地上获得 `inspect`；精确 deny 在同优先级下仍可覆盖它。

## 绑定与行为说明

| 对象 | API | 说明 |
|---|---|---|
| `Authority` | `getName()` / `newStore()` | 查询模块名或创建模块拥有的 Store Result。 |
| 拥有型 Store | `ownership()` / `ownerEpoch()` / `handle()` / `isStale()` / `release()` | 查询世代生命周期或释放 Store。 |
| Store | `grant()` / `deny()` | 添加 allow/deny 规则并返回稳定 rule ID Result。 |
| Store | `revoke(id,reason)` | 按稳定 ID 撤销；缺失时返回 NotFound Result。 |
| Store | `revokeBySource` / `revokeByActor` / `revokeByScope` / `revokeByCapability` | 按维度批量撤销并返回删除数量。 |
| Store | `can(actor,scope,capability)` | 返回当前最终允许结果。 |
| Store | `explain(actor,scope,capability)` | 返回带胜出规则的即时 Decision。 |
| Store | `find(id)` | 按稳定 ID 借用规则。 |
| Store | `query(actor,scope,capability)` / `queryAt(i)` | 按可选条件筛选规则并读取结果。 |
| Store | `queryActor()` / `queryScope()` / `queryCapability()` | 单维度查询便利入口。 |
| Store | `update(dt)` | 推进有期限规则并产生 expired 事件。 |
| Store | `setEventCapacity(n)` / `eventCapacity()` | 设置或查询事件保留上限；`0` 表示不限制。 |
| Store | `eventCount()` / `eventAt(i)` / `clearEvents()` | 消费规则生命周期事件。 |
| Store | `snapshotJson()` / `restoreJson(json)` | 确定性快照或事务性恢复 Result。 |
| Rule | `getId()` / `getActor()` / `getScope()` / `getCapability()` | 规则身份和匹配三元组。 |
| Rule | `getEffect()` / `getSource()` / `getPriority()` / `getRemaining()` | allow/deny、来源、优先级和剩余秒数。 |
| Decision | `isAllowed()` / `getWinningRuleId()` / `getReason()` | 最终结论、胜出规则和解释。 |
| Event | `getSequence()` / `getKind()` / `getRuleId()` | 事件顺序、类型和规则 ID。 |
| Event | `getActor()` / `getScope()` / `getCapability()` / `getSource()` | 事件规则维度和来源。 |

## 生命周期与安全边界

- Store 权威拥有规则，但不拥有 actor 或 scope 指向的实体；实体销毁时用
  `revokeByActor` / `revokeByScope` / `revokeBySource` 清理。
- 规则字段 `"*"` 只在**规则侧**通配；查询参数 `"*"` 只匹配字面量或通配规则，不会反过来扩展查询。
- `revokeBy*` 批量接口按字段**精确相等**匹配，不会误删通配规则，除非字段本身就是 `"*"`。
- Rule、Decision、Event 和 query 结果是即时借用；修改、update、restore 或 release 后重查。
- Authority 只回答权限，不执行操作；消费者在执行前应再次检查，避免检查与使用分离。
- `update(dt)` 使用注入模拟时间；相同规则输入和 dt 序列产生相同过期及事件顺序。
- `setEventCapacity(n)`（n>0）在超出时丢弃最旧事件；序列号继续递增，不回绕。
