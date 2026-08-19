# RPG Quest Tracker Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 `eve.RPG` 内实现通用目标追踪器（`QuestRegistry` + `Tracker` + `QuestSystem`）：JSON 定义、`notify` 计数、前置链、`startPolicy`/`completePolicy`、手动 `reset`。

**Architecture:** 与 Effect/Skill 同构。定义进程级；`Tracker` 是独立运行时对象（非 ECS）；`QuestSystem` 静态方法改 Tracker；事件挂在每个 Tracker 上，脚本 `pollEvents()` 后再读。不绑 `RPGActor` / Inventory，不发奖，不走 `rpg.update(dt)`。

**Tech Stack:** C++20、Poco JSON（`JsonHelpers.h` + `DataModule::decodeJson`）、simplesquirrel、zeroerr。

**Spec:** [docs/dev/RPG任务系统设计.md](../../RPG任务系统设计.md)

## Global Constraints

- 字符串 id / policy / topic / target / tag / reward type，不用编译期枚举。
- 未知 `startPolicy` → `"manual"`；未知 `completePolicy` → `"auto"`。
- `objectives[].count <= 0` → `1`。v1 目标全部 AND。
- `notify` 的 `amount` 为 `int`；`amount <= 0` 忽略。奖励 `amount` 为 `double`，脚本绑定 `float`。
- 脚本浮点禁止直接绑 C++ `double`（Squirrel 未开 `SQUSEDOUBLE`）。
- `create_module(EVRPG rpg)` 会扫 `src/modules/rpg/*.cpp`；新增 `.cpp` 后若链接缺符号，对当前 build 目录重新 cmake configure 一次。
- **不要自动 git commit**，除非用户明确要求。
- Build / 测（本机 Windows Debug）：

```powershell
cmake --build build/win32-debug --config Debug --target unit_test -j 8
.\build\win32-debug\test\Debug\unit_test.exe --testcase="^rpg\.quest"
```

Linux 等价：`make debug JOBS=4` 然后 `./build/linux-debug/test/unit_test --testcase='^rpg.quest'`，或 `make test/linux-debug FILTER=rpg.quest`。

---

## File Structure

| File | Role |
|------|------|
| `src/modules/rpg/QuestTypes.h` | `QuestObjective` / `RewardSpec` / `ObjectiveRuntime` / `QuestRuntime` / `QuestEvent` |
| `src/modules/rpg/Quest.h` / `Quest.cpp` | `QuestDefinition` + `QuestRegistry` |
| `src/modules/rpg/Tracker.h` / `Tracker.cpp` | 运行时对象、查询、事件 snapshot |
| `src/modules/rpg/QuestSystem.h` / `QuestSystem.cpp` | 状态机 + `notify` |
| `src/modules/rpg/RPG.h` / `RPG.cpp` | `newTracker` / JSON facade / Squirrel 绑定 |
| `test/rpg_quest.cpp` | 全部 `rpg.quest.*` 用例 |
| `test/CMakeLists.txt` | 把 `rpg_quest.cpp` 加入 `file(GLOB all_test_cpp ...)` |
| `docs/usr/modules/rpg.md` | 用户文档 |
| `docs/dev/README.md` / `docs/dev/测试覆盖.md` | 索引 |

---

### Task 1: QuestTypes + QuestRegistry（JSON / 环检测）

**Files:**
- Create: `src/modules/rpg/QuestTypes.h`
- Create: `src/modules/rpg/Quest.h`
- Create: `src/modules/rpg/Quest.cpp`
- Create: `test/rpg_quest.cpp`
- Modify: `test/CMakeLists.txt`（在 `rpg.cpp` 后插入 `rpg_quest.cpp`）

**Interfaces:**
- Consumes: `rpg/JsonHelpers.h`、`data/DataModule.h`、`data/JsonDocument.h`（与 `Skill.cpp` 相同）
- Produces:
  - `struct QuestObjective { std::string id, topic, target; int count = 1; }`
  - `struct RewardSpec { std::string type, id; double amount = 0.0; }`
  - `struct ObjectiveRuntime { std::string id; int current = 0; int count = 1; bool done = false; }`
  - `struct QuestRuntime { std::string id; std::string state; std::vector<ObjectiveRuntime> objectives; }`
  - `struct QuestEvent { std::string entryId, objectiveId, action, topic, target, reason; int amount = 0; }`
  - `struct QuestDefinition`（字段见下方头文件）
  - `static void QuestRegistry::registerQuest(const QuestDefinition &def)`
  - `static const QuestDefinition *QuestRegistry::find(const std::string &id)`
  - `static bool QuestRegistry::remove(const std::string &id)`
  - `static void QuestRegistry::clear()`
  - `static int QuestRegistry::count()`
  - `static std::vector<std::string> QuestRegistry::ids()`
  - `static int QuestRegistry::loadFromJson(const std::string &json, std::string *error = nullptr)`

- [ ] **Step 1: 把 `rpg_quest.cpp` 登记进测试并写失败用例**

在 `test/CMakeLists.txt` 的 `rpg.cpp` 下一行加入 `rpg_quest.cpp`。

创建 `test/rpg_quest.cpp`：

```cpp
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "rpg/Quest.h"

using namespace eve::rpg;

TEST_CASE("rpg.quest.registryCRUD") {
    QuestRegistry::clear();
    QuestDefinition def;
    def.id = "quest.intro";
    def.tags = {"main"};
    def.extra["title"] = "Intro";
    QuestRegistry::registerQuest(def);
    CHECK_EQ(QuestRegistry::count(), 1);
    const QuestDefinition *found = QuestRegistry::find("quest.intro");
    REQUIRE(found != nullptr);
    CHECK(found->hasTag("main"));
    CHECK_EQ(found->getExtra("title", ""), std::string("Intro"));
    CHECK_EQ(found->startPolicy, std::string("manual"));
    CHECK_EQ(found->completePolicy, std::string("auto"));
    CHECK(QuestRegistry::remove("quest.intro"));
    CHECK_EQ(QuestRegistry::count(), 0);
    QuestRegistry::clear();
}

TEST_CASE("rpg.quest.loadFromJsonDefaultsAndRewards") {
    QuestRegistry::clear();
    int n = QuestRegistry::loadFromJson(R"([
      {
        "id": "quest.hunt",
        "startPolicy": "manual",
        "completePolicy": "claim",
        "requires": ["quest.intro"],
        "tags": ["main"],
        "objectives": [
          {"id": "kill_slime", "topic": "kill", "target": "slime", "count": 10},
          {"id": "any_kill", "topic": "kill", "count": 0}
        ],
        "rewards": [
          {"type": "item", "id": "potion", "amount": 3},
          {"type": "attribute", "id": "xp", "amount": 50}
        ],
        "extra": {"title": "Hunt"}
      }
    ])");
    CHECK_EQ(n, 1);
    const QuestDefinition *d = QuestRegistry::find("quest.hunt");
    REQUIRE(d != nullptr);
    CHECK_EQ(d->startPolicy, std::string("manual"));
    CHECK_EQ(d->completePolicy, std::string("claim"));
    REQUIRE(d->requires.size() == 1);
    CHECK_EQ(d->requires[0], std::string("quest.intro"));
    REQUIRE(d->objectives.size() == 2);
    CHECK_EQ(d->objectives[1].count, 1);  // count<=0 → 1
    CHECK(d->objectives[1].target.empty());
    REQUIRE(d->rewards.size() == 2);
    CHECK_EQ(d->rewards[0].type, std::string("item"));
    CHECK_EQ(d->rewards[1].amount, 50.0);
    CHECK_EQ(d->getExtra("title", ""), std::string("Hunt"));
    QuestRegistry::clear();
}

TEST_CASE("rpg.quest.loadFromJsonRejectsBadAndCycles") {
    QuestRegistry::clear();
    int n = QuestRegistry::loadFromJson(R"([
      {"startPolicy": "auto"},
      {
        "id": "dup.obj",
        "objectives": [
          {"id": "a", "topic": "kill", "count": 1},
          {"id": "a", "topic": "talk", "count": 1}
        ]
      },
      {"id": "cyc.a", "requires": ["cyc.b"]},
      {"id": "cyc.b", "requires": ["cyc.a"]},
      {"id": "ok.solo", "startPolicy": "weird", "completePolicy": "nope"}
    ])");
    CHECK_EQ(n, 1);
    CHECK(QuestRegistry::find("dup.obj") == nullptr);
    CHECK(QuestRegistry::find("cyc.a") == nullptr);
    CHECK(QuestRegistry::find("cyc.b") == nullptr);
    const QuestDefinition *ok = QuestRegistry::find("ok.solo");
    REQUIRE(ok != nullptr);
    CHECK_EQ(ok->startPolicy, std::string("manual"));
    CHECK_EQ(ok->completePolicy, std::string("auto"));
    QuestRegistry::clear();
}
```

- [ ] **Step 2: 编译确认失败**

Run:

```powershell
cmake --build build/win32-debug --config Debug --target unit_test -j 8
```

Expected: 编译失败，找不到 `rpg/Quest.h` 或 `QuestRegistry`。

- [ ] **Step 3: 实现类型与注册表**

创建 `src/modules/rpg/QuestTypes.h`：

```cpp
#pragma once

#include <string>
#include <vector>

namespace eve::rpg {

struct QuestObjective {
    std::string id;
    std::string topic;
    std::string target;
    int count = 1;
};

struct RewardSpec {
    std::string type;
    std::string id;
    double amount = 0.0;
};

struct ObjectiveRuntime {
    std::string id;
    int current = 0;
    int count = 1;
    bool done = false;
};

struct QuestRuntime {
    std::string id;
    std::string state;  // locked | inactive | active | ready | completed | failed
    std::vector<ObjectiveRuntime> objectives;
};

struct QuestEvent {
    std::string entryId;
    std::string objectiveId;
    std::string action;
    std::string topic;
    std::string target;
    int amount = 0;
    std::string reason;
};

}  // namespace eve::rpg
```

创建 `src/modules/rpg/Quest.h`：

```cpp
#pragma once

#include "rpg/QuestTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::rpg {

struct QuestDefinition {
    std::string id;
    std::string startPolicy = "manual";
    std::string completePolicy = "auto";
    std::vector<std::string> requires;
    std::vector<QuestObjective> objectives;
    std::vector<RewardSpec> rewards;
    std::vector<std::string> tags;
    std::unordered_map<std::string, std::string> extra;

    bool hasTag(const std::string &tag) const;
    std::string getExtra(const std::string &key, const std::string &fallback = {}) const;
};

class QuestRegistry {
public:
    static void registerQuest(const QuestDefinition &def);
    static const QuestDefinition *find(const std::string &id);
    static bool remove(const std::string &id);
    static void clear();
    static int count();
    static std::vector<std::string> ids();
    static int loadFromJson(const std::string &json, std::string *error = nullptr);
};

}  // namespace eve::rpg
```

创建 `src/modules/rpg/Quest.cpp`，解析风格抄 `src/modules/rpg/Skill.cpp` 的 `loadFromJson` / `parseSkillObject`。要点：

- 匿名命名空间静态 `std::unordered_map<std::string, QuestDefinition> table()`。
- `hasTag` 用 `std::find`；`getExtra` 找不到返回 fallback。
- `normalizeStart`：仅 `"auto"` 保留，其余（含空、未知）→ `"manual"`。
- `normalizeComplete`：仅 `"claim"` 保留，其余 → `"auto"`。
- 每条 objective：`count <= 0` 则 `count = 1`；`id` 为空或同一条目内 id 重复 → 整条定义拒绝。
- 环检测：图的边是 `id → requires[i]`；**未在表中的前置 id 不算环**。自依赖是环。
- `registerQuest`：拷贝表、写入候选、若 `def.id` 落在环上则恢复旧值且不增加 count；否则规范化后写入。
- `loadFromJson`：先 parse 出所有合法候选到 `proposed`（在现有 table 副本上覆盖），收集本批 `candidateIds`；对每个 candidate 若在 `proposed` 上成环则拒绝；其余写入真正的 `table()`。缺 `id` 的 JSON 对象跳过。支持顶层数组或单对象（与 Skill 相同）。
- `ids()` 返回当前 table 的全部 key（顺序不保证；Tracker 自己记插入序）。

环检测函数（写入 `Quest.cpp` 匿名命名空间）：

```cpp
bool idOnCycle(const std::unordered_map<std::string, QuestDefinition> &table,
               const std::string &start) {
    std::unordered_set<std::string> visiting;
    std::unordered_set<std::string> visited;
    std::function<bool(const std::string &)> dfs = [&](const std::string &id) -> bool {
        if (visiting.count(id)) return true;
        if (visited.count(id)) return false;
        auto it = table.find(id);
        if (it == table.end()) return false;
        visiting.insert(id);
        for (const auto &req : it->second.requires) {
            if (dfs(req)) return true;
        }
        visiting.erase(id);
        visited.insert(id);
        return false;
    };
    return dfs(start);
}
```

JSON 字段读取：`id`/`startPolicy`/`completePolicy` 用 `asString`；`requires`/`tags` 用 `asStringArray`；`extra` 用 `asStringMap`；`objectives`/`rewards` 手写数组循环（与 Skill `costs` 相同），`count`/`amount` 用 `asInt` / `asDouble`。

- [ ] **Step 4: 跑 Task 1 测试**

```powershell
cmake --build build/win32-debug --config Debug --target unit_test -j 8
.\build\win32-debug\test\Debug\unit_test.exe --testcase="^rpg\.quest"
```

Expected: 三条 `rpg.quest.registry*` / `loadFromJson*` 全 PASS。若 LNK 缺符号，先 `cmake -S . -B build/win32-debug` 再编。

---

### Task 2: Tracker 初始状态 + `syncAuto`

**Files:**
- Create: `src/modules/rpg/Tracker.h`
- Create: `src/modules/rpg/Tracker.cpp`
- Create: `src/modules/rpg/QuestSystem.h`
- Create: `src/modules/rpg/QuestSystem.cpp`
- Modify: `test/rpg_quest.cpp`

**Interfaces:**
- Consumes: `QuestRegistry::find` / `ids` / `QuestDefinition`
- Produces:
  - `class Tracker` 构造时调用 `QuestSystem::syncAuto(this)`
  - `void Tracker::syncAuto()`
  - `int Tracker::getCount() const`
  - `std::string Tracker::getId(int index) const`（插入序，越界 `""`）
  - `std::string Tracker::getState(const std::string &id) const`（未知 `""`）
  - `int Tracker::getObjectiveCount(const std::string &id) const`
  - `std::string Tracker::getObjectiveId(const std::string &id, int index) const`
  - `int Tracker::getObjectiveCurrent(const std::string &id, int index) const`
  - `int Tracker::getObjectiveCountRequired(const std::string &id, int index) const`
  - `bool Tracker::isObjectiveDone(const std::string &id, int index) const`
  - `bool Tracker::hasTag(const std::string &id, const std::string &tag) const`（读定义）
  - `std::string Tracker::getExtra(const std::string &id, const std::string &key) const`
  - `int Tracker::getRewardCount(const std::string &id) const`
  - `std::string Tracker::getRewardType(const std::string &id, int i) const`
  - `std::string Tracker::getRewardId(const std::string &id, int i) const`
  - `double Tracker::getRewardAmount(const std::string &id, int i) const`
  - `void QuestSystem::syncAuto(Tracker *t)`
  - `std::string QuestSystem::unlockedState(const QuestDefinition &def, const Tracker &t)` — 前置未齐返回 `"locked"`；齐了则 auto→稍后由 sync 建成 `"active"` 的起点标记，manual→`"inactive"`。实现上：`requiresMet` 为 false → locked；true 且 startPolicy auto → `active`；否则 `inactive`。
  - Tracker 数据（供 QuestSystem 读写）：`std::unordered_map<std::string, QuestRuntime> entries`、`std::vector<std::string> order`、`std::vector<QuestEvent> pending`、`std::vector<QuestEvent> polled`

前置满足：`requires` 里每个 id 在 **本 Tracker** 上 `state == "completed"`。未出现的 id（含未注册）视为未完成。

本任务 **还不** 处理空目标立刻完成（Task 3）。auto 条目先停在 `active`，即使 objectives 为空。

- [ ] **Step 1: 写失败测试**

追加到 `test/rpg_quest.cpp`，并 `#include "rpg/Tracker.h"`：

```cpp
TEST_CASE("rpg.quest.trackerInitialStatesAndSyncAuto") {
    QuestRegistry::clear();
    QuestRegistry::loadFromJson(R"([
      {"id":"q.intro","startPolicy":"manual"},
      {"id":"q.auto","startPolicy":"auto"},
      {"id":"q.locked","startPolicy":"auto","requires":["q.missing"]},
      {"id":"q.chain","startPolicy":"manual","requires":["q.intro"]}
    ])");

    Tracker log;
    CHECK_EQ(log.getCount(), 4);
    CHECK_EQ(log.getState("q.intro"), std::string("inactive"));
    CHECK_EQ(log.getState("q.auto"), std::string("active"));
    CHECK_EQ(log.getState("q.locked"), std::string("locked"));
    CHECK_EQ(log.getState("q.chain"), std::string("locked"));
    CHECK_EQ(log.getState("nope"), std::string(""));

    QuestRegistry::loadFromJson(R"([{"id":"q.late","startPolicy":"auto"}])");
    CHECK_EQ(log.getCount(), 4);
    log.syncAuto();
    CHECK_EQ(log.getCount(), 5);
    CHECK_EQ(log.getState("q.late"), std::string("active"));
    CHECK(log.hasTag("q.intro", "x") == false);

    QuestRegistry::clear();
}
```

- [ ] **Step 2: 编译确认失败**

Run: 同 Global Constraints 的 `--testcase="^rpg\.quest.tracker"`  
Expected: `Tracker` 未定义。

- [ ] **Step 3: 实现 Tracker + `QuestSystem::syncAuto`**

`QuestSystem.h`：

```cpp
#pragma once

#include "rpg/QuestTypes.h"

#include <string>
#include <vector>

namespace eve::rpg {

class Tracker;
struct QuestDefinition;

class QuestSystem {
public:
    static void syncAuto(Tracker *t);
    static bool requiresMet(const Tracker &t, const QuestDefinition &def);
    static std::string stateForUnlocked(const QuestDefinition &def);
};

}  // namespace eve::rpg
```

`stateForUnlocked`：`def.startPolicy == "auto"` → `"active"` 否则 `"inactive"`。

`syncAuto` 逻辑：

1. `t == nullptr` 直接返回。
2. 对 `QuestRegistry::ids()` 里每个 id，若 `t->entries` 没有：按定义建 `QuestRuntime`（`objectives` 从定义拷贝 `id/count`，`current=0`，`done=false`），`state` 先设 `"locked"`，`order.push_back(id)`。
3. 重复扫描 entries（最多 `entries.size()` 轮，或直到一轮没有变化）：若 `requiresMet` 且当前为 `locked`，设为 `stateForUnlocked(def)`。不要把 `inactive`/`failed`/`completed` 改掉。定义已删除的条目跳过。

`Tracker` 构造函数：`QuestSystem::syncAuto(this)`。

查询方法：找不到 entry 时 getState 返回 `""`，计数 0，字符串 `""`，bool false。index 越界同样。`hasTag`/`getExtra`/`getReward*` 走 `QuestRegistry::find(id)`，与运行时 state 无关。

`Tracker.cpp` 里成员方法 `syncAuto` 调 `QuestSystem::syncAuto(this)`。

- [ ] **Step 4: 跑测试**

```powershell
.\build\win32-debug\test\Debug\unit_test.exe --testcase="^rpg\.quest"
```

Expected: Task 1+2 全 PASS。

---

### Task 3: `activate` / `notify` / 空目标立刻满（auto 完成）

**Files:**
- Modify: `src/modules/rpg/QuestSystem.h`
- Modify: `src/modules/rpg/QuestSystem.cpp`
- Modify: `src/modules/rpg/Tracker.h`
- Modify: `src/modules/rpg/Tracker.cpp`
- Modify: `test/rpg_quest.cpp`

**Interfaces:**
- Consumes: Task 2 的 Tracker / `syncAuto` / `requiresMet`
- Produces:
  - `bool QuestSystem::activate(Tracker *t, const std::string &id, std::string *reason = nullptr)`
  - `bool QuestSystem::canActivate(Tracker *t, const std::string &id, std::string *reason = nullptr)`
  - `std::string QuestSystem::canActivateReason(Tracker *t, const std::string &id)`
  - `void QuestSystem::notify(Tracker *t, const std::string &topic, const std::string &target, int amount)`
  - `void QuestSystem::pushEvent(Tracker *t, QuestEvent ev)`（内部）
  - Tracker 转发：`activate` / `canActivate` / `canActivateReason` / `notify`
  - 内部：`void QuestSystem::onObjectivesFull(Tracker *t, QuestRuntime &rt)` — 本任务：`completePolicy != "claim"` 则 `state="completed"` 并 `syncAuto`；`claim` 则 `state="ready"`（claim API 在 Task 4 才测，但分支必须写对）

`canActivateReason`：`unknown` / `locked` / `alreadyActive` / `alreadyCompleted` / `failed` / `ready` / `""`（仅 `inactive` 时可激活）。

`activate` 仅当 reason 为空：`state="active"`，push `action="activate"`。若此时全部 objective 已 `done`（含空列表），立刻 `onObjectivesFull`。

`notify`：`t==nullptr` 或 `amount<=0` 返回。只处理 `state=="active"`。匹配：`obj.topic == topic` 且（`obj.target.empty()` 或 `obj.target == target`）且 `!obj.done`。命中则 `current = min(current+amount, count)`，`done = current>=count`，push `progress`（`entryId/objectiveId/topic/target/amount`）。该条目全部 done 则 `onObjectivesFull`。先处理完本轮所有 active 条目的 progress，再对因此 completed 的条目调用 `syncAuto`（可在 `onObjectivesFull` 末尾调 `syncAuto`，auto 链可能再次 activate；空目标激活在 Task 4 测链）。

本任务 `onObjectivesFull`：`claim` → `ready` + 事件留到 Task 4 再断言；`auto` → `completed` + 暂不强制事件测试（事件在 Task 4 一次性覆盖）。为了 Task 4 简单，**本任务就入队事件**：`ready` 或 `complete`。`syncAuto` 新激活的 auto 条目若空目标，会再进 `onObjectivesFull`——Task 3 测试不用空目标 auto。

- [ ] **Step 1: 写失败测试**

```cpp
TEST_CASE("rpg.quest.activateAndNotify") {
    QuestRegistry::clear();
    QuestRegistry::loadFromJson(R"([
      {"id":"q.lock","startPolicy":"manual","requires":["nope"]},
      {"id":"q.hunt","startPolicy":"manual","completePolicy":"auto",
       "objectives":[
         {"id":"k","topic":"kill","target":"slime","count":2},
         {"id":"any","topic":"kill","count":3}
       ]},
      {"id":"q.flag","startPolicy":"manual","completePolicy":"auto","objectives":[]}
    ])");
    Tracker log;
    CHECK_EQ(log.canActivateReason("q.lock"), std::string("locked"));
    CHECK(!log.activate("q.lock"));
    CHECK_EQ(log.getState("q.lock"), std::string("locked"));

    CHECK(log.canActivate("q.hunt"));
    CHECK(log.activate("q.hunt"));
    CHECK_EQ(log.getState("q.hunt"), std::string("active"));
    CHECK_EQ(log.canActivateReason("q.hunt"), std::string("alreadyActive"));

    log.notify("talk", "npc", 1);
    CHECK_EQ(log.getObjectiveCurrent("q.hunt", 0), 0);

    log.notify("kill", "slime", 1);
    CHECK_EQ(log.getObjectiveCurrent("q.hunt", 0), 1);
    CHECK_EQ(log.getObjectiveCurrent("q.hunt", 1), 1);  // target 空，吃所有 kill
    log.notify("kill", "goblin", 5);
    CHECK_EQ(log.getObjectiveCurrent("q.hunt", 0), 1);  // slime 不涨
    CHECK_EQ(log.getObjectiveCurrent("q.hunt", 1), 3);  // 封顶 3
    CHECK(log.isObjectiveDone("q.hunt", 1));

    log.notify("kill", "slime", 0);
    CHECK_EQ(log.getObjectiveCurrent("q.hunt", 0), 1);
    log.notify("kill", "slime", 1);
    CHECK(log.isObjectiveDone("q.hunt", 0));
    CHECK_EQ(log.getState("q.hunt"), std::string("completed"));

    CHECK(log.activate("q.flag"));
    CHECK_EQ(log.getState("q.flag"), std::string("completed"));

    QuestRegistry::clear();
}
```

- [ ] **Step 2: 编译确认失败**

Expected: `activate` / `notify` 不是 `Tracker` 的成员。

- [ ] **Step 3: 实现 activate/notify/onObjectivesFull**

在 `QuestSystem.h` 增加上述 public 方法。Tracker 对应薄转发。

`pushEvent`：`t->pending.push_back(std::move(ev))`。

匹配与封顶按上面规则。`activate` 失败时仍 push `reject`（`reason` = canActivateReason），返回 false（Task 4 会测 reject；本任务不读事件也可以入队）。

空 `objectives`：`std::all_of` 空范围是 true → activate 后立刻 `onObjectivesFull`。事件顺序：先 `activate` 再 `complete`。

- [ ] **Step 4: 跑测试**

Expected: `rpg.quest.activateAndNotify` PASS，旧用例仍 PASS。

---

### Task 4: `claim`、事件 poll、前置链自动激活、双 Tracker 隔离

**Files:**
- Modify: `src/modules/rpg/QuestSystem.h`
- Modify: `src/modules/rpg/QuestSystem.cpp`
- Modify: `src/modules/rpg/Tracker.h`
- Modify: `src/modules/rpg/Tracker.cpp`
- Modify: `test/rpg_quest.cpp`

**Interfaces:**
- Consumes: Task 3 的 `pending` 队列与 `onObjectivesFull`
- Produces:
  - `bool QuestSystem::claim(Tracker *t, const std::string &id, std::string *reason = nullptr)`
  - `void QuestSystem::pollEvents(Tracker *t, std::vector<QuestEvent> &out)`
  - `void Tracker::pollEvents()` — `polled = std::move(pending); pending.clear();`
  - `int Tracker::getEventCount() const` 读 `polled`
  - `std::string Tracker::getEventEntryId(int i) const`
  - `std::string Tracker::getEventObjectiveId(int i) const`
  - `std::string Tracker::getEventAction(int i) const`
  - `std::string Tracker::getEventTopic(int i) const`
  - `std::string Tracker::getEventTarget(int i) const`
  - `int Tracker::getEventAmount(int i) const`
  - `std::string Tracker::getEventReason(int i) const`
  - `syncAuto` 在把某条从 `locked` 推到 `active` 时：push `activate`；若 objectives 已全部 done（空列表），再 `onObjectivesFull`

`claim`：仅 `state=="ready"` 成功 → `completed`，事件 `complete`，然后 `syncAuto`。否则 `reject`，reason：未知 `"unknown"`，其它用当前 `state`（`active`/`inactive`/`locked`/`completed`/`failed`）。

C++ `QuestSystem::pollEvents`：若 `t` 空则 `out.clear()`；否则 `t->pollEvents(); out = t->polled`。

一次 `notify` 顺序（必须测）：每个命中目标一条 `progress`；该条目满则紧跟 `ready` 或 `complete`；然后 `syncAuto` 可能 `activate` 下一条。

- [ ] **Step 1: 写失败测试**

```cpp
TEST_CASE("rpg.quest.claimEventsAndChain") {
    QuestRegistry::clear();
    QuestRegistry::loadFromJson(R"([
      {"id":"a","startPolicy":"manual","completePolicy":"claim",
       "objectives":[{"id":"k","topic":"kill","target":"slime","count":1}]},
      {"id":"b","startPolicy":"auto","completePolicy":"auto","requires":["a"],
       "objectives":[{"id":"t","topic":"talk","target":"npc","count":1}]}
    ])");
    Tracker log;
    CHECK_EQ(log.getState("b"), std::string("locked"));
    CHECK(log.activate("a"));
    log.notify("kill", "slime", 1);
    CHECK_EQ(log.getState("a"), std::string("ready"));
    CHECK(!log.claim("nope"));
    CHECK(!log.claim("b"));
    CHECK(log.claim("a"));
    CHECK_EQ(log.getState("a"), std::string("completed"));
    CHECK_EQ(log.getState("b"), std::string("active"));

    log.pollEvents();
    bool sawProgress = false, sawReady = false, sawComplete = false, sawActivateB = false;
    for (int i = 0; i < log.getEventCount(); ++i) {
        auto act = log.getEventAction(i);
        auto id = log.getEventEntryId(i);
        if (act == "progress" && id == "a") sawProgress = true;
        if (act == "ready" && id == "a") sawReady = true;
        if (act == "complete" && id == "a") sawComplete = true;
        if (act == "activate" && id == "b") sawActivateB = true;
    }
    CHECK(sawProgress);
    CHECK(sawReady);
    CHECK(sawComplete);
    CHECK(sawActivateB);

    Tracker ach;
    QuestRegistry::loadFromJson(R"([
      {"id":"ach.kill","startPolicy":"auto","completePolicy":"auto","tags":["achievement"],
       "objectives":[{"id":"k","topic":"kill","target":"slime","count":1}]}
    ])");
    ach.syncAuto();
    log.notify("kill", "slime", 1);
    ach.notify("kill", "slime", 1);
    CHECK_EQ(log.getState("ach.kill"), std::string(""));  // log 未 sync 这条也可；隔离重点是事件
    CHECK_EQ(ach.getState("ach.kill"), std::string("completed"));
    log.pollEvents();
    ach.pollEvents();
    bool logCompletedAch = false;
    for (int i = 0; i < log.getEventCount(); ++i)
        if (log.getEventEntryId(i) == "ach.kill") logCompletedAch = true;
    CHECK(!logCompletedAch);
    CHECK(ach.getEventCount() >= 1);

    QuestRegistry::clear();
}
```

说明：第二个 Tracker `ach` 在 `loadFromJson` 之后 `syncAuto` 才有 `ach.kill`。`log` 若不 sync 则没有该条目，`getState` 为 `""`。事件队列互不串。

- [ ] **Step 2: 编译确认失败**

Expected: `claim` / `pollEvents` / `getEventAction` 缺失。

- [ ] **Step 3: 实现 claim、poll、syncAuto 激活事件**

`onObjectivesFull`：`claim` 策略 → `ready` + 事件 `ready`；否则 `completed` + 事件 `complete` + `QuestSystem::syncAuto(t)`。

`claim` 成功后同样 `syncAuto`。

改 `syncAuto`：从 `locked` 变为 `active` 时 `pushEvent(activate)`；若该 runtime 的 objectives 全 done，调用 `onObjectivesFull`（注意避免 `syncAuto` ↔ `onObjectivesFull` 无限递归：`onObjectivesFull` 在已是 `completed`/`ready` 时必须直接 return。进入时若 state 已是 ready/completed/failed 则 no-op）。

- [ ] **Step 4: 跑测试**

Expected: `rpg.quest.claimEventsAndChain` PASS。

---

### Task 5: `reset` / `abandon` / `fail`（不级联）

**Files:**
- Modify: `src/modules/rpg/QuestSystem.h`
- Modify: `src/modules/rpg/QuestSystem.cpp`
- Modify: `src/modules/rpg/Tracker.h`
- Modify: `src/modules/rpg/Tracker.cpp`
- Modify: `test/rpg_quest.cpp`

**Interfaces:**
- Consumes: Task 4 状态机
- Produces:
  - `bool QuestSystem::reset(Tracker *t, const std::string &id)`
  - `bool QuestSystem::abandon(Tracker *t, const std::string &id)`
  - `bool QuestSystem::fail(Tracker *t, const std::string &id, const std::string &reason)`
  - Tracker 同名转发

`reset`：未知 id → false，无事件。否则：objectives `current=0` `done=false`；按**当前定义**的 `requiresMet` 落入 `locked` 或 `stateForUnlocked`；push `reset`。**不**修改其它条目。若 reset 后落到 `active` 且空目标，再 `onObjectivesFull`。

`abandon` / `fail`：允许从 `inactive` / `active` / `ready` 进入 `failed`。`completed` / `failed` / `locked` / 未知 → false + `reject`。成功：`failed`；abandon 的事件 `action=fail` `reason=abandon`；`fail` 的 reason 为调用方字符串。

- [ ] **Step 1: 写失败测试**

```cpp
TEST_CASE("rpg.quest.resetAbandonFailNoCascade") {
    QuestRegistry::clear();
    QuestRegistry::loadFromJson(R"([
      {"id":"daily","startPolicy":"auto","completePolicy":"auto",
       "objectives":[{"id":"k","topic":"kill","target":"slime","count":1}]},
      {"id":"main","startPolicy":"manual","completePolicy":"auto","requires":["daily"],
       "objectives":[{"id":"t","topic":"talk","target":"npc","count":1}]}
    ])");
    Tracker log;
    log.notify("kill", "slime", 1);
    CHECK_EQ(log.getState("daily"), std::string("completed"));
    CHECK(log.activate("main"));
    CHECK_EQ(log.getState("main"), std::string("active"));

    CHECK(log.reset("daily"));
    CHECK_EQ(log.getState("daily"), std::string("active"));
    CHECK_EQ(log.getObjectiveCurrent("daily", 0), 0);
    CHECK_EQ(log.getState("main"), std::string("active"));  // 不级联

    CHECK(log.abandon("main"));
    CHECK_EQ(log.getState("main"), std::string("failed"));
    log.notify("talk", "npc", 1);
    CHECK_EQ(log.getObjectiveCurrent("main", 0), 0);
    CHECK(!log.activate("main"));
    CHECK_EQ(log.canActivateReason("main"), std::string("failed"));
    CHECK(log.reset("main"));
    // daily 已 reset 且未完成，main 的 requires 不满足 → locked（不是 inactive）
    CHECK_EQ(log.getState("main"), std::string("locked"));

    Tracker t2;
    QuestRegistry::loadFromJson(R"([{"id":"only","startPolicy":"manual"}])");
    t2.syncAuto();
    CHECK(t2.fail("only", "timeout"));
    CHECK_EQ(t2.getState("only"), std::string("failed"));
    t2.pollEvents();
    bool sawTimeout = false;
    for (int i = 0; i < t2.getEventCount(); ++i)
        if (t2.getEventAction(i) == "fail" && t2.getEventReason(i) == "timeout") sawTimeout = true;
    CHECK(sawTimeout);

    QuestRegistry::clear();
}
```

注意：`reset("main")` 时 `daily` 已不是 completed，因此 main 必须落到 `locked` 而不是 `inactive`。测试里不要写互相矛盾的两次 `CHECK_EQ(getState("main"))`——**只保留 `locked` 那一次**。上面第一句 `inactive` 注释掉，实现与断言均为 `locked`。

- [ ] **Step 2: 编译确认失败**

Expected: `reset` / `abandon` / `fail` 缺失。

- [ ] **Step 3: 实现三方法**

按 Interfaces。`reset` 重建 objectives 向量（尺寸与定义一致；若定义目标列表已变，以当前定义为准）。

- [ ] **Step 4: 跑测试**

Expected: `rpg.quest.resetAbandonFailNoCascade` PASS。若 Task 5 测试里 `reset("main")` 后 state 不是 locked，说明 `requiresMet` 误把缺失进度当成完成——未 completed 一律不算满足。

---

### Task 6: `eve.RPG` 绑定、facade 单测、用户文档

**Files:**
- Modify: `src/modules/rpg/RPG.h`
- Modify: `src/modules/rpg/RPG.cpp`
- Modify: `test/rpg_quest.cpp`
- Modify: `docs/usr/modules/rpg.md`
- Modify: `docs/dev/README.md`（功能设计列表增加 [RPG 任务系统](RPG任务系统设计.md)）
- Modify: `docs/dev/测试覆盖.md`（rpg 行备注加上 `test/rpg_quest.cpp`）
- Modify: `docs/dev/RPG任务系统设计.md` 状态行改为「已实现（首版）」——仅当本任务测试通过后

**Interfaces:**
- Consumes: 全部 Tracker / QuestRegistry API
- Produces:
  - `Tracker *RPG::newTracker()` → `new Tracker()`
  - `int RPG::registerQuestsFromJson(const std::string &json)` → `QuestRegistry::loadFromJson(json, nullptr)`
  - `void RPG::clearQuestDefinitions()`
  - `int RPG::getQuestDefinitionCount()`
  - Squirrel：`table.addClass<Tracker>("Tracker", ...)`，方法名与 Tracker 成员一致；`getRewardAmount` 包一层 `float`
  - `RPG::expose(ssq::Class &)` 增加四个 facade 方法

- [ ] **Step 1: 写 facade 失败测试**

```cpp
#include "rpg/RPG.h"

TEST_CASE("rpg.quest.facadeManualClaim") {
    auto *rpg = RPG::create();
    rpg->clearQuestDefinitions();
    int n = rpg->registerQuestsFromJson(R"([
      {"id":"quest.hunt","startPolicy":"manual","completePolicy":"claim",
       "objectives":[{"id":"k","topic":"kill","target":"slime","count":1}],
       "rewards":[{"type":"item","id":"potion","amount":3}]}
    ])");
    CHECK_EQ(n, 1);
    CHECK_EQ(rpg->getQuestDefinitionCount(), 1);

    Tracker *log = rpg->newTracker();
    REQUIRE(log != nullptr);
    CHECK(log->activate("quest.hunt"));
    log->notify("kill", "slime", 1);
    CHECK_EQ(log->getState("quest.hunt"), std::string("ready"));
    CHECK(log->claim("quest.hunt"));
    CHECK_EQ(log->getState("quest.hunt"), std::string("completed"));
    CHECK_EQ(log->getRewardCount("quest.hunt"), 1);
    CHECK_EQ(log->getRewardId("quest.hunt", 0), std::string("potion"));

    log->pollEvents();
    CHECK(log->getEventCount() >= 1);

    rpg->clearQuestDefinitions();
}
```

- [ ] **Step 2: 编译确认失败**

Expected: `RPG::newTracker` 未声明。

- [ ] **Step 3: 绑定与文档**

`RPG.h` 前向声明 `class Tracker;`，加入四个方法；文件头注释改为六套系统并指向 `docs/dev/RPG任务系统设计.md`。

`RPG.cpp`：`#include "rpg/Quest.h"`、`#include "rpg/Tracker.h"`，实现四个方法。

在 `RPG::expose(ssq::Table &table)` 里，仿 `SettlementContext`：

```cpp
auto tracker = table.addClass<Tracker>(
    "Tracker", std::function<Tracker *()>([]() { return new Tracker(); }), true);
tracker.addFunc("syncAuto", &Tracker::syncAuto);
tracker.addFunc("activate", &Tracker::activate);
tracker.addFunc("canActivate", &Tracker::canActivate);
tracker.addFunc("canActivateReason", &Tracker::canActivateReason);
tracker.addFunc("notify", &Tracker::notify);
tracker.addFunc("claim", &Tracker::claim);
tracker.addFunc("reset", &Tracker::reset);
tracker.addFunc("abandon", &Tracker::abandon);
tracker.addFunc("fail", &Tracker::fail);
tracker.addFunc("pollEvents", &Tracker::pollEvents);
tracker.addFunc("getCount", &Tracker::getCount);
tracker.addFunc("getId", &Tracker::getId);
tracker.addFunc("getState", &Tracker::getState);
tracker.addFunc("hasTag", &Tracker::hasTag);
tracker.addFunc("getExtra", &Tracker::getExtra);
tracker.addFunc("getObjectiveCount", &Tracker::getObjectiveCount);
tracker.addFunc("getObjectiveId", &Tracker::getObjectiveId);
tracker.addFunc("getObjectiveCurrent", &Tracker::getObjectiveCurrent);
tracker.addFunc("getObjectiveCountRequired", &Tracker::getObjectiveCountRequired);
tracker.addFunc("isObjectiveDone", &Tracker::isObjectiveDone);
tracker.addFunc("getRewardCount", &Tracker::getRewardCount);
tracker.addFunc("getRewardType", &Tracker::getRewardType);
tracker.addFunc("getRewardId", &Tracker::getRewardId);
tracker.addFunc("getRewardAmount",
                [](Tracker *t, const std::string &id, int index) -> float {
                    return t ? float(t->getRewardAmount(id, index)) : 0.f;
                });
tracker.addFunc("getEventCount", &Tracker::getEventCount);
tracker.addFunc("getEventEntryId", &Tracker::getEventEntryId);
tracker.addFunc("getEventObjectiveId", &Tracker::getEventObjectiveId);
tracker.addFunc("getEventAction", &Tracker::getEventAction);
tracker.addFunc("getEventTopic", &Tracker::getEventTopic);
tracker.addFunc("getEventTarget", &Tracker::getEventTarget);
tracker.addFunc("getEventAmount", &Tracker::getEventAmount);
tracker.addFunc("getEventReason", &Tracker::getEventReason);
```

`expose(ssq::Class &cls)` 增加 `newTracker` / `registerQuestsFromJson` / `clearQuestDefinitions` / `getQuestDefinitionCount`。

`Tracker::activate` 脚本侧只有 `id` 一个参数（不要 `reason` 指针）。C++ `Tracker::activate(const std::string &id)` 内部调 `QuestSystem::activate(this, id, nullptr)`。`fail(id, reason)` 两个 string。`notify(topic, target, amount)` amount 用 `int`。

更新 `docs/usr/modules/rpg.md`：

- 开头补一句：组合属性、效果、状态、技能、结算与**目标追踪器**。
- 「对象关系」补：`QuestRegistry` 是定义；`rpg.newTracker()` 是进度；`notify` 推进；完成后 `pollEvents`。
- 新小节「追踪任务与成就」：

```squirrel
local rpg = eve.RPG();
rpg.registerQuestsFromJson(questJson);
local log = rpg.newTracker();
log.activate("quest.hunt");
log.notify("kill", "slime", 1);
log.pollEvents();
```

- API 快查按字母序插入 `abandon`、`activate`、`claim`、`newTracker`、`notify`、`pollEvents`、`registerQuestsFromJson`、`reset`、`syncAuto` 等本任务绑定的名字。
- 常见问题补：`completePolicy=claim` 时满进度是 `ready`，要 `claim`；两个 Tracker 要各自 `notify`；Registry 变更后旧 Tracker 要 `syncAuto`。

`docs/dev/README.md` 在 `[RPG 系统](RPG系统设计.md)` 下加 `- [RPG 任务系统](RPG任务系统设计.md)`。

`docs/dev/测试覆盖.md` rpg 行备注改为含 `test/rpg_quest.cpp`。

- [ ] **Step 4: 跑全套 rpg.quest 与既有 rpg 回归**

```powershell
cmake --build build/win32-debug --config Debug --target unit_test -j 8
.\build\win32-debug\test\Debug\unit_test.exe --testcase="^rpg\.quest"
.\build\win32-debug\test\Debug\unit_test.exe --testcase="^rpg\."
```

Expected: 所有 `rpg.quest.*` 与原有 `rpg.*`（含 `rpg.simulation.*`）PASS。

通过后再把 `docs/dev/RPG任务系统设计.md` 状态改为：`已实现（首版，src/modules/rpg/）`。

---

## Self-Review

**Spec coverage**

| Spec 节 | Task |
|---------|------|
| QuestDefinition / JSON / extra/tags/rewards | 1 |
| 环 requires、缺 id、count<=0、未知 policy | 1 |
| Tracker 独立实例、newTracker 初始状态、syncAuto 补定义 | 2 |
| notify 匹配、封顶、只听 active、空目标 | 3 |
| startPolicy / activate / canActivateReason | 3 |
| completePolicy claim/auto、事件顺序、前置链、双 Tracker | 4 |
| reset 不级联、abandon/fail | 5 |
| eve.RPG 绑定、usr 文档、不发奖 | 6 |
| 不绑 Actor/背包、无 update(dt)、无存档 | 全局未做即满足 |

**类型名一致性：** `getObjectiveCountRequired`、`syncAuto`、`pollEvents`、`getEventEntryId`、状态字符串 `locked|inactive|active|ready|completed|failed` 贯穿全文。
