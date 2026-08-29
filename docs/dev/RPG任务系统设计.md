# RPG 模块设计：任务 / 目标追踪器

日期：2026-08-18
状态：已实现（`src/modules/rpg/`：Quest.h/.cpp、Tracker.h/.cpp、QuestSystem.h/.cpp、QuestTypes.h；测试 `test/rpg_quest.cpp`）

相关：[RPG系统设计.md](./RPG系统设计.md)（属性 / 效果 / 状态 / 技能 / 结算）、[模块设计.md](./模块设计.md)

## 目标

在现有 RPG 模块内提供一套**通用目标追踪器**：任务、日常、成就、教程共用同一套「条目 + 目标 + 进度」，用 `tags` 和奖励数据区分用途。引擎不解释击杀、拾取、对话，只匹配字符串并计数。

覆盖：

1. **定义（QuestDefinition）** — 数据驱动的条目模板（策略、前置、目标、奖励、标签、extra）。
2. **注册表（QuestRegistry）** — 进程级 JSON / C++ 注册，与 `EffectRegistry` / `SkillRegistry` 同构。
3. **追踪器（Tracker）** — `rpg.newTracker()` 得到的运行时实例；玩家日志、成就、教程可各建一份。
4. **行为（QuestSystem）** — `notify` / `activate` / `claim` / `reset` / `abandon` / `fail` 与事件入队。

与既有子系统同一套设计语言：字符串 id、JSON 定义、纯数据 + `*System` 静态行为、`eve.RPG()` facade 绑定。参考 [模块设计.md](./模块设计.md)「类绑定原则」与 [RPG系统设计.md](./RPG系统设计.md)。

## 为什么做成通用追踪器，而不是「任务日志」单开

- 成就 / 教程 / 日常与主线的差别是 **何时开始听、何时结束、标签是什么**，不是另一套进度模型。
- 进度不挂在 `RPGActor` 上：成就和教程不必绑战斗角色；需要关联时把 actor id 写进 `extra` / 事件即可。
- `notify` 只报事实（`("kill","slime",1)`），条目自己决定听不听。游戏不必维护「哪些任务在追踪史莱姆」。

## 可插拔 / 数据扩展点

v1 **不**提供 C++ `registerObjectiveType` 回调：匹配规则已是自由字符串 `(topic, target)`。特殊判定由游戏决定是否调用 `notify`。

| 扩展点 | 谁来填 | 覆盖场景 |
|--------|--------|----------|
| `QuestDefinition`（数据） | C++ 或 JSON | 新条目无需改引擎 |
| `startPolicy` / `completePolicy` | JSON | 接任务 vs 成就自动听；交任务 vs 成就自动完成 |
| `requires` | JSON | 任务链（本 Tracker 上 AND） |
| `tags` / `extra` | JSON | `main` / `daily` / `achievement` / `tutorial`；标题、NPC、图标 |
| `rewards[].type/id` | JSON | `item` / `attribute` / 游戏自定义；引擎**不发奖** |
| 事件 poll | 脚本或 C++ | UI、发奖、对话衔接 |

## 架构

```text
QuestRegistry（进程级定义）
        │
        ▼
   QuestSystem（静态行为）
        │
        ▼
    Tracker（运行时实例）── 事件队列（本 Tracker 上 poll）
```

| 名字 | 职责 |
|------|------|
| `QuestDefinition` | JSON 模板。靠 `tags` 区分用途，引擎不解释 tag。 |
| `QuestObjective` | 条目内一条目标：听哪个 `notify`、要多少次。 |
| `QuestRegistry` | `registerQuest` / `loadFromJson` / `find` / `clear`。 |
| `Tracker` | 一份进度 + 本实例事件队列。不是 ECS 实体。 |
| `QuestSystem` | 状态机与匹配；Tracker 成员方法转发到这里。 |

**边界**

- 不碰 `RPGActor`、背包、对话。击杀 / 拾取 / 对话结束由游戏 `notify`。
- 奖励只是定义数据；完成事件里可读，引擎不改属性、不塞物品。
- 事件挂在每个 Tracker 上，不进入 `rpg.getCastEvent*` 那组全局缓存。
- v1 **不**走 `rpg.update(dt)`：无限时失败，进度只由 `notify` 驱动。
- 不暗中持有 live Tracker 列表。Registry 变更后由游戏调用 `tracker.syncAuto()`。

文件（均在 `src/modules/rpg/`）：

```text
QuestTypes.h     — QuestObjective / RewardSpec / QuestRuntime / QuestEvent
Quest.h/.cpp     — QuestDefinition + QuestRegistry（JSON）
Tracker.h/.cpp   — 运行时对象 + 脚本向查询/poll
QuestSystem.h/.cpp
RPG.h/.cpp       — registerQuestsFromJson / newTracker / 绑定
```

## 数据模型

### QuestDefinition（`Quest.h`）

```text
QuestObjective {
  id, topic, target,   // target 空 = 只匹配 topic
  count                // <=0 按 1
}

RewardSpec {
  type, id,            // 自由字符串
  amount               // double；脚本绑定用 float
}

QuestDefinition {
  id,
  startPolicy,         // "manual" | "auto"，缺省 "manual"
  completePolicy,      // "auto" | "claim"，缺省 "auto"
  requires[],          // 本 Tracker 上须已 completed 的条目 id，全部 AND
  objectives[],        // v1 全部 AND；数组顺序仅供 UI
  rewards[],
  tags[],
  extra{string→string}
}
```

JSON 示例：

```json
{
  "id": "quest.hunt",
  "startPolicy": "manual",
  "completePolicy": "claim",
  "requires": ["quest.intro"],
  "tags": ["main"],
  "objectives": [
    { "id": "kill_slime", "topic": "kill", "target": "slime", "count": 10 },
    { "id": "talk_guard", "topic": "talk", "target": "npc.guard", "count": 1 }
  ],
  "rewards": [
    { "type": "item", "id": "potion", "amount": 3 },
    { "type": "attribute", "id": "xp", "amount": 50 }
  ],
  "extra": { "title": "清剿史莱姆", "giver": "npc.guard" }
}
```

成就：同样形状，`startPolicy`/`completePolicy` 均为 `"auto"`，`tags` 含 `"achievement"`。

未知 `startPolicy` / `completePolicy` 字符串回退到缺省值（`manual` / `auto`），不拒绝整条定义。

### 匹配规则

`QuestSystem::notify(Tracker *t, topic, target, amount)`（`amount` 为 `int`）：

1. `t == nullptr` 或 `amount <= 0`：忽略。
2. 只遍历 `state == "active"` 的条目。
3. 条目内每条未完成目标：`topic` 相等，且（`target` 为空 **或** `target` 与 notify 的 target 相等）→ `current = min(current + amount, count)`，`done = current >= count`。
4. 同一条目上多条目标可听同一 `(topic, target)`，各自加进度。
5. 一次 notify 可命中多条条目。

`objectives` 为空：`activate` 成功后立刻视为目标已满（进入 `ready` 或 `completed`，见状态机）。用于「纯开关」条目（例如只靠接任务/对话标记）。

同一条目内 `objectives[].id` 必须唯一；重复 id 的定义不注册。

### 运行时（`Tracker`）

```text
ObjectiveRuntime { id, current, count, done }

QuestRuntime {
  id,
  state,               // locked | inactive | active | ready | completed | failed
  objectives[]
}

Tracker {
  entries: id → QuestRuntime,   // 每个定义在本 Tracker 上最多一份
  events[]                      // poll 后清空
}
```

`newTracker()`：按**当时** Registry 为每条定义建 Runtime，再按前置与 `startPolicy` 落初始状态（见下）。之后新注册的定义不会自动出现在已有 Tracker 上，必须 `syncAuto()`。

## 状态机

```text
locked → inactive → active → ready → completed
                 ↘ failed          （abandon / fail）
```

`completePolicy == "auto"` 时没有 `ready`：目标满了从 `active` 直接 `completed`。

| 状态 | 含义 | 听 notify |
|------|------|-----------|
| `locked` | `requires` 中有尚未 `completed` 的 id（含未知 id，视为未完成） | 否 |
| `inactive` | 前置已齐，等待 `activate`（仅 `manual`） | 否 |
| `active` | 正在计数 | 是 |
| `ready` | 目标已满，等待 `claim` | 否 |
| `completed` | 终态（直到 `reset`） | 否 |
| `failed` | `abandon` / `fail`（直到 `reset`） | 否 |

**解锁（`requires` 已全部 `completed`）后的落点**

- `startPolicy == "auto"` → 直接 `active`（空目标则继续按 completePolicy 立刻 ready/complete）
- `startPolicy == "manual"` → `inactive`

**转移**

| 操作 | 合法前置状态 | 结果 |
|------|----------------|------|
| `activate(id)` | `inactive` | `active`（空目标则立刻满） |
| 目标全部 `done` | `active` | `auto` → `completed`；`claim` → `ready` |
| `claim(id)` | `ready` | `completed` |
| `abandon(id)` / `fail(id, reason)` | `inactive` / `active` / `ready` | `failed` |
| `reset(id)` | 任意已存在的 Runtime | 进度清零，按**当前** `requires` 重新落入 `locked` / `inactive` / `active` |

`reset` **不级联**：不会把依赖本条目的其它条目打回 `locked`。日常刷新不会拆主线；需要回滚时由游戏对依赖条目自行 `abandon`/`reset`。

某条目进入 `completed` 后，对同一 Tracker 调用内部解锁：所有仍为 `locked` 且前置已齐的条目按上表落点；其中 `auto` 的会 `activate`（可再触发链式完成）。`syncAuto()` 做同样的事，并补建 Registry 里尚不在本 Tracker 上的条目。

## 前置任务链

- `requires` 只在**本 Tracker** 上解释，不看其它 Tracker，不看全局。
- 全部 AND。空数组 = 无前置。
- 自依赖或环（A→B→A，含已在 Registry 中的边）：不写入。单条 `registerQuest` 若与已有表成环则拒绝这一条；`loadFromJson` 一批里环上的**新定义全部拒绝**（已在表中且本批未覆盖的旧定义不动）。返回值不含被拒条数。检测范围 = 本批将写入的定义 ∪ 已有 Registry。
- 前置 id 尚未注册：允许（加载顺序不定），运行时当未完成，条目保持 `locked`。
- `canActivate` / `activate` 在前置未齐时失败，原因 `"locked"`。

## API

无重载；浮点脚本绑定用 `float`（Squirrel 未开 `SQUSEDOUBLE`，C++ `double` 不能直接 `addFunc`，见 [RPG系统设计.md](./RPG系统设计.md) 脚本绑定一节）。

### `eve.RPG()`

| 方法 | 说明 |
|------|------|
| `registerQuestsFromJson(json)` | 返回成功注册条数 |
| `clearQuestDefinitions()` | |
| `getQuestDefinitionCount()` | |
| `newTracker()` | 新 Tracker，并按当时 Registry 建条目 |

C++ 另有 `QuestRegistry::registerQuest`。

### `Tracker`（脚本） / `QuestSystem`（C++）

写操作返回 `bool`（未知 id 或非法状态为 `false`，不抛）。`fail` 的 `reason` 写入事件。

```text
syncAuto()
activate(id) / canActivate(id) / canActivateReason(id)
notify(topic, target, amount)
claim(id)
reset(id)
abandon(id)
fail(id, reason)
```

`canActivateReason`：`""`（可以）/ `unknown` / `locked` / `alreadyActive` / `alreadyCompleted` / `failed` / `ready`（已可交，不必再 activate）。

查询：

```text
getCount() / getId(index) / getState(id)
hasTag(id, tag) / getExtra(id, key)          // extra 读定义
getObjectiveCount(id) / getObjectiveId(id, i)
getObjectiveCurrent(id, i) / getObjectiveCountRequired(id, i) / isObjectiveDone(id, i)
getRewardCount(id) / getRewardType(id, i) / getRewardId(id, i) / getRewardAmount(id, i)
```

未知 `id`：写操作 `false`；`getState` 返回 `""`；计数类返回 `0`；字符串查询返回 `""`。

奖励 API 读的是 **定义**，不是发放结果。`getRewardAmount` 脚本侧为 `float`。

## 事件

每个 Tracker 自带队列；`poll` 风格与 `getCastEvent*` 相同（`getEventCount` / 按 index 取字段）。`pollEvents` 把当前队列拷出并清空（C++）；脚本按 index 读的是「上次 `poll` 之后、或约定为调用 `pollEvents()` / 首次读前需 poll」——与 Status 对齐的做法是：**写操作当场入队，脚本用 `getEventCount` 读的是当前队列，并提供 `clearEvents()`**。为减少漏清，规定：

- C++：`QuestSystem::pollEvents(Tracker&, vector<QuestEvent>& out)` 移动并清空。
- 脚本：`tracker.pollEvents()` 把队列快照到 Tracker 内部「可读缓存」并清空实时队列（与 `RPG::update` 刷新 tick 缓存同模式）；随后 `getEventCount` / `getEventAction(i)` 等读这份缓存。
- 无 `rpg.update` 驱动，因此脚本必须在处理完 `notify`/`activate`/… 之后自己 `pollEvents()`。

`QuestEvent` 字段：`entryId`、`objectiveId`、`action`、`topic`、`target`、`amount`、`reason`。无关字段为空/`0`。

| action | 何时 |
|--------|------|
| `activate` | 进入 `active` |
| `progress` | 某目标 `current` 增加 |
| `ready` | `claim` 策略下目标全部完成 |
| `complete` | 进入 `completed` |
| `fail` | `abandon`（reason=`abandon`）或 `fail`（reason=调用方字符串） |
| `reset` | `reset` |
| `reject` | `activate` / `claim` / `abandon` / `fail` 被拒绝；`reason` 同 `canActivateReason` 一类 |

一次 `notify` 的顺序：对每个命中的目标先 `progress`；该条目若因此满目标，紧接着 `ready` 或 `complete`；所有条目处理完后跑解锁/`auto` 激活，可能再产生 `activate`，以及空目标条目的 `ready`/`complete`。

## 失败与边界

- `claim` 仅当 `state==ready`；`completePolicy==auto` 的条目永不进 `ready`，`claim` → `reject`。
- 已 `completed` / `failed` / `locked` / `inactive` / `ready` 不听 `notify`。
- `abandon`/`fail` 对 `completed` 或已 `failed` 或未知 id：`reject`。
- `reset` 对未知 id：`false`，无事件。
- 不在 v1 做生命周期 C++ hook；UI 用 poll 事件。

## 脚本用法（示意）

```squirrel
local rpg = eve.RPG();
rpg.registerQuestsFromJson(questJson);
local log = rpg.newTracker();     // 玩家任务
local ach = rpg.newTracker();     // 成就（定义里 startPolicy=auto）

log.activate("quest.hunt");
log.notify("kill", "slime", 1);
ach.notify("kill", "slime", 1);   // 两份 Tracker 互不影响

log.pollEvents();
for (local i = 0; i < log.getEventCount(); i++) {
    if (log.getEventAction(i) == "ready") {
        log.claim(log.getEventEntryId(i));
        // 游戏读 getReward* 后自己发奖
    }
}
```

跨天后：`log.reset("daily.hunt")`。日历不在引擎内。

## 测试

`test/rpg.cpp` 或拆出 `test/rpg_quest.cpp`（须在 `test/CMakeLists.txt` 登记）。zeroerr，`TEST_CASE("rpg.quest....")`。每个用例 `QuestRegistry::clear()`。不依赖 `rpg.update(dt)`。

**注册表：** JSON 缺省 policy、`count<=0`→1、`extra`/`tags`/`rewards`；缺 `id`、重复 objective id、循环 `requires` 不注册；`clear` 后 `find` 为空。

**状态机：** manual 前置未齐 `locked` → 齐后 `inactive` → `activate`；auto 在 `newTracker` 时前置已齐则 `active`，否则 `locked`，前置完成后 `syncAuto` 激活；`completePolicy=auto` 满目标无 `ready`；`claim` 策略下未 ready 的 `claim` 产生 `reject`；`abandon`/`fail` → `failed` 且不再听 notify；`reset` 按当前 requires 回落且不级联。

**notify：** 只推进 `active`；`amount<=0` 忽略；空 `target` 吃该 topic 全部；`current` 封顶；一次 notify 可多条目；同一次内 `progress` 后接 `ready`/`complete`，再 `syncAuto` 可能 `activate`。

**前置：** A 完成后 B(`requires:[A]`, auto) 被激活；`activate(B)` 在 A 未完成时失败，reason `locked`；未注册的前置 id 视为未完成。

**Facade：** `newTracker` + JSON + `notify`/`claim` + `pollEvents` 跑通 manual+claim；两个 Tracker 事件队列不串。

不测：存档、引擎发奖、背包/对话自动挂钩、`rpg.update` 副作用。

## 非目标（v1）

- 存档序列化（与属性/技能一样留给统一存档）
- 日历 / 自动刷新（只有手动 `reset`）
- 引擎代发物品或经验
- 目标之间的 OR、可选目标、强制顺序步骤
- 终身计数账本（条目中心：只给当前 `active` 的目标加分）
- `rpg.update(dt)` 驱动的限时失败
- C++ 自定义目标类型 / 生命周期 hook
- 与 `event` 模块打通（独立 poll，与 tick/cast 现状一致）

## 实现注意

- Tracker 用 `new Tracker()` 交给 Squirrel 托管，风格同 `SettlementContext`。
- `QuestRegistry` 静态表；测试必须 `clear()`。
- JSON 解析走现有 `json_helpers` + Poco，与 `SkillRegistry::loadFromJson` 相同。
- 用户文档：实现后更新 `docs/usr/modules/rpg.md`（入口、示例、API 快查）。
