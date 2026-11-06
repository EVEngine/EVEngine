# Roster：小队编制、岗哨占用与补员请求

**入口：** `eve.Roster()`，槽位 `roster`

## 这篇解决什么

- **问题**：小队成员表、岗哨座位占用、岗哨/队员被干掉后的**空缺 → 延迟补员请求**（不拥有血量、寻路、生产队列或行为树）。
- **入口**：`registerPost` / `createSquad` 建编制；`releaseSubject` 或 `releasePost` / `removeMember` 产生空缺；`setPolicy` + `step` 产出 `spawnRequest`；游戏 `ackSpawnRequest` 后自行生成主体并 `assignPost` / `addMember`。
- **不要**：在巡逻状态机或 `npc_ai` 叶任务里直接 `new` 单位；不要把动作岗哨挂到 RTS Building 补员短根上。
- **相关**：几何槽位租约见 [npc_ai SmartObject](npc_ai.md)；关系/角色指派见 [social](social.md)；实际生产队列见 [production](production.md)；RTS 建筑投送补员见 [rts](rts.md)。

## 快速开始

```squirrel
function roster_value(result) {
    if (!result.ok) throw result.status.summary;
    return result.value;
}

local roster = eve.Roster();
roster_value(roster.registerPost("gate.north", 2, "sentry"));
roster_value(roster.createSquad("patrol.east", 3, "patrol"));
roster_value(roster.assignPost("gate.north", "guard:a", ""));
roster_value(roster.addMember("patrol.east", "guard:a", "lead"));
roster_value(roster.setPolicy("gate-refill", "unit.sentry", 2, 4, "gate.north", "", "sentry"));
roster_value(roster.setFallback("gate-refill", "unit.sentry", "unit.militia"));

// 岗哨被干掉：从所有编制中移除并记空缺
roster_value(roster.releaseSubject("guard:a", 10));
roster_value(roster.step(12)); // delayTicks=2 → 在 tick 12 生成请求
local req = roster_value(roster.spawnRequestAt(0));
// 游戏侧：按 req.preferredDef 生成新 subject，再：
roster_value(roster.ackSpawnRequest(req.requestId));
roster_value(roster.assignPost(req.postId, "guard:new", req.role));
roster_value(roster.addMember("patrol.east", "guard:new", "lead"));
```

## 权威边界

| 事实 | Owner |
|---|---|
| 岗哨容量 / 占用 / 角色 | `roster` |
| 小队成员 / 共享 intent | `roster` |
| 空缺队列与 spawn 请求 | `roster` |
| 主体生成与定义表 | 游戏 + `production` / `definitions` |
| 巡逻叶任务与导航 | `npc_ai` + 导航 provider |
| 掩体/椅子等世界交互租约 | `npc_ai` SmartObject（可另记 vacancy） |

`step(tick)` 只把到期空缺变成 `SpawnRequest`；**不会**创建实体。预算耗尽时丢弃匹配空缺且不入队。`failSpawnRequest` 在配置了 fallback 时入队替代定义。

完整竖切见 `examples/post-patrol-reinforce`。
