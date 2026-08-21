# 状态热更新设计（State Hot Reload）

> 状态：PR1（公共层：`StateValue` + `IStateProvider` + Snapshot v2）、
> PR2（dialogue / AnimStateMachine 的 `IStateProvider` 接入）、
> PR3（`ReloadSession` + `eve.dev` API + `load.nut` 三阶段协议 + 脚本测试）、
> PR2b（动画内容热更：`AnimClip::adopt` + `AnimClipRegistry` + "animclip"
> `IAssetReloader`）、PR4（rpg / card 接入 `IStateProvider` + 状态根严格校验）
> 均已实施；日期：2026-08-21
> 目标：把当前"资源可热更、脚本可重载、状态不可迁移"的现状，升级为
> **快照 → 重载 → 恢复** 的三阶段状态热更新：脚本/资源变化后，运行中的
> 状态机与脚本状态被序列化保存、在新定义下恢复，而不是复位或悬垂。
> 涉及改动：`src/engine/common/StateValue.h`、`src/engine/common/IStateProvider.h`、
> `src/engine/devtools/Snapshot.*`（v2 格式 + 原生状态段）、`src/engine/devtools/ReloadSession.*`、
> `src/scripts/load.nut`（三阶段重载协议）、`src/modules/{dialogue,animation,rpg,card}` 接入，
> 新增 `test/{state_value,reload_session}.cpp` 等。

## 1. 背景与现状

现状盘点（2026-08-21 代码核对）：

- **资源热更：可用。** `filesystem/HotReload` 通过 `IAssetReloader` 能力分发到
  image / model / font / sound / texture / particle / tilemap / cache 8 类 reloader，
  本地 watcher 与远程 `eve dev` 同步共用一条管线。
- **脚本热更：只换定义。** `load.nut` 对 `.nut` 重新 `dofile` 并调 `eve_reload()`。
  Squirrel 中旧 class 实例仍绑定旧 class，重新定义不会迁移运行中对象。
- **状态热更：缺失。** `AnimStateMachine`（currentState_/参数表/混合进度）、
  `Dialogue`（Phase/变量/rngState_）等运行时状态没有序列化接口；devtools `Snapshot`
  只能对 Squirrel 顶层表做 JSON 快照（函数丢弃、instance 还原成 table、引擎对象按
  "无状态"跳过），且未接入重载流程。
- 设计方向已有铺垫：`docs/dev/游戏模型设计.md` 明确"游戏=有限自动机、对象=小状态机，
  维护核心状态即可实现代码热更新"；`docs/dev/模块编排与裁剪架构.md` 的能力机制
  （`eve::cap::provide/query/addListener`）为跨模块解耦提供了现成底座。

## 2. 设计原则

1. **状态是数据，逻辑是代码。** 一切参与热更的状态必须可序列化（标量 / table /
   array），函数、userdata、class 实例不得直接进入状态根。
2. **只换定义，迁移状态。** 热更不追求"原地替换函数"，而是
   `capture → reload → restore`：代码换新，状态按名字/字段迁入新定义。
3. **按名字引用，不按指针。** 状态里引用资源/定义（动画 clip、任务 id、状态名）
   一律存名字；恢复时按新定义重新解析，杜绝悬垂指针与旧 class 绑定。
4. **能力接入，不破坏分层。** 原生状态机的序列化通过 `common/` 里的能力接口暴露，
   各模块自行实现；filesystem 继续只管资源分发，编排放在 devtools + 脚本层。
5. **可回滚。** 每次重载前落盘一份完整快照；恢复失败时降级为默认值或回退上一份快照。

## 3. 总体架构

```text
脚本层 load.nut
  soft_reload_scripts() ── 三阶段协议
    ① capture   eve.dev.captureState()         脚本状态根 + 全部 IStateProvider
    ② reload    dofile .nut + hot.tryReload()  现有资源/脚本链路不变
    ③ restore   eve.dev.restoreState()         校验/合并/迁移后恢复

devtools（C++）
  ReloadSession：会话缓冲、落盘回滚点、状态校验
  Snapshot v2：  { version, roots, native:{ dialogue, anim, ... } }

common（能力接口）
  StateValue    —— 可序列化状态值（JSON 兼容，无新依赖）
  IStateProvider—— capture/restore/reset，各模块 addListener 注册

各模块（provider 实现）
  dialogue / animation / rpg / card …… 自行实现 captureState/restoreState
```

## 4. 详细设计

### 4.1 `common/StateValue`：可序列化状态值

`common/`（LAYER -1，仅 std）新增轻量 JSON 兼容变体，作为能力接口的交换格式，
不依赖 `data`/Poco，保持公共层纯净：

```cpp
// common/StateValue.h
namespace eve {
class StateValue {
public:
    enum class Type { Null, Bool, Int, Float, String, Array, Object };

    static StateValue null();
    static StateValue boolean(bool);
    static StateValue integer(int64_t);
    static StateValue number(double);
    static StateValue string(std::string);
    static StateValue array();                       // push_back 追加
    static StateValue object();                      // set(key, value)

    Type type() const;
    // 路径访问：get("obj.field[2]") / set(path, v)，供校验与合并使用
    const StateValue* get(const std::string& dottedPath) const;
    void set(const std::string& dottedPath, StateValue v);
    // 以 defaults 补齐缺失字段（浅层合并），返回是否发生补齐
    bool mergeDefaults(const StateValue& defaults);
};
}
```

devtools `Snapshot` 现有的 `sqToVar` / `pushVar` 泛化到 `StateValue`，JSON 持久化
继续用 Poco（devtools 已依赖 poco）。同一套类型同时服务脚本根序列化与原生状态序列化。

### 4.2 `common/IStateProvider`：原生状态机接入

```cpp
// common/IStateProvider.h
namespace eve::caps {
class EVENGINE_API IStateProvider {
public:
    static constexpr const char* capabilityName = "IStateProvider";
    virtual ~IStateProvider() = default;

    /** 稳定标识："dialogue" / "anim" / "rpg.casting" / "card" ... */
    virtual const char* stateKind() const = 0;
    /** 序列化当前运行时状态；失败返回 false 并置错误信息。 */
    virtual bool captureState(StateValue& out) = 0;
    /** 把状态恢复到本模块新定义上；字段缺失/非法时返回 false。 */
    virtual bool restoreState(const StateValue& in, std::string* err = nullptr) = 0;
    /** 恢复失败时的降级路径：回到模块默认状态。 */
    virtual bool resetToDefaults() = 0;
};
}
```

注册沿用能力机制：单例服务用 `eve::cap::provide<IStateProvider>(&impl)`，多实例
（如每个 AnimStateMachine）由模块持有 registry、provider 遍历。ReloadSession 用
`cap::forEach` / `cap::query` 收集，不 include 任何业务模块——裁剪不受影响。

### 4.3 脚本状态根与三阶段重载协议

**状态根约定**：脚本顶层可序列化数据放在固定根下，沿用 `Snapshot::resolveRoots`
的启发式（`eve_state` / `gameState` / `state`），并允许脚本显式登记：

```squirrel
eve.dev.markStateRoot("game");   // 把根表 slot "game" 登记为状态根
```

**load.nut 三阶段改造**（`soft_reload_scripts`）：

```squirrel
function soft_reload_scripts() {
    // ① 可选钩子：固化瞬态数据
    if ("eve_before_reload" in getroottable()) eve_before_reload();

    // ② 快照：脚本状态根 + 全部 IStateProvider → 内存缓冲并落盘 eve_snapshot.json
    local err = eve.dev.beginStateReload();
    if (err != "") { print("state reload: capture failed: " + err + "\n"); return; }

    // ③ 重载：重新 dofile（旧状态以 buffer 为准，新脚本可自由重建默认值）
    foreach (p in watched_scripts) {
        if (!file_exists(p)) continue;
        try { dofile(p); }
        catch (e) { print("hot-reload script failed: " + p + ": " + e + "\n"); }
    }

    // ④ 恢复：旧值优先、新脚本新增字段保留；原生 provider 恢复/降级
    err = eve.dev.commitStateReload();
    if (err != "") print("state reload: restore failed: " + err + "\n");

    // ⑤ 可选迁移钩子：从恢复后的状态重建 class 实例
    if ("eve_after_reload" in getroottable()) {
        try { eve_after_reload(); } catch (e) { print("eve_after_reload: " + e + "\n"); }
    }

    if ("eve_reload" in getroottable()) eve_reload();
}
```

新增脚本钩子约定：

- `eve_before_reload()`（可选）：脚本自己整理/固化状态（如落盘临时数据）。
- `eve_after_reload()`（可选）：脚本级迁移，使用 `migrate_instance(old, newClass)`。
- `eve_reload()`：保留现有语义（重绑定 UI/场景引用等），在恢复完成后调用。

实现说明（与早期草案的差异）：**不做状态根摘除/挂回**。旧状态在
`beginStateReload` 时已整体进入会话缓冲，`dofile` 期间新脚本自由重建/改写状态根，
`commitStateReload` 以"旧值优先、新字段保留"（`mergeDefaults`）的方式恢复；
这既避免摘除导致脚本顶层读状态根报错，也不需要迁移 helper 与 dofile 抢时序。
原生状态机（AnimStateMachine / Dialogue）若未被脚本重建，其 C++ 对象原样保留，
provider 恢复只是把参数/阶段写回；若被重建，则按名字校验并恢复。

### 4.4 脚本类实例迁移

Squirrel 的 class 实例在重新 `dofile` 后仍绑定旧 class，因此提供显式迁移原语：

```squirrel
// 按字段名把旧实例数据拷入新 class 的实例；新字段保持默认值。
migrate_instance(oldInst, NewClass) → newInst
// 批量迁移：container 里的实体/任务实例全部换新 class。
remap_instances(container, NewClass) → container
```

两者以 `load.nut` 脚本函数提供（Squirrel 的 `foreach` 对普通 class 实例不可枚举，
故实现为：table 全量字段拷贝；class 实例按 `getclass(old)` 声明的字段拷贝，
新 class 缺失字段保持默认值；闭包/方法跳过）。`eve_after_reload()` 里典型用法：
从恢复后的状态根（table）重建实例。

约定优先级：**状态优先写成 table + 自由函数**（对话 generator、ECS 组件已是此形态），
class 实例迁移是兜底而非首选。状态根校验会拒绝携带闭包/class 的字段，防止
"闭包捕获旧函数"这类不可迁移状态静默混入。

### 4.5 动画状态机接入（重点）

`AnimStateMachine` 的 clip 是裸指针，是热更最容易悬垂的地方。分两步解决，
当前已落地第二步：

1. **资源侧：动画内容热更（PR2b，已实施）。** `AnimClip::adopt` 原地换内容；
   `AnimClipRegistry`（弱引用、path→clip，`~AnimClip` 自动注销）登记
   `newClipFromEvaFile` 创建的每个 clip；animation 注册 `IAssetReloader`
   （kind `"animclip"`，`.eva` 文件变化 → 重新导入 → adopt 进已登记实例）。
   持有 clip 指针的状态机/脚本永远指向有效对象，内容自动刷新。
   **所有权说明**：ssq 的 `addFunc` 返回值（`pushByPtr`）不设 release hook，
   脚本 GC 不会 `delete` AnimClip——因此无需把 AnimClip 改成引用计数对象，
   弱注册表 + adopt 即可安全实现身份稳定。
2. **状态侧：状态机按名字保存运行态（已实施）。** `AnimStateMachine` 实现
   `captureState/restoreState`（参数表、当前/下一状态、混合进度、started），
   构造时登记到模块级 registry、析构注销，animation 模块注册
   `IStateProvider`（stateKind `"anim"`）遍历 registry：

```cpp
// capture：{ "currentState": "...", "nextState": "...", "stateTime": ...,
//            "floats": {...}, "bools": {...}, "triggers": {...}, "blend": {...} }
// restore：恢复参数表与当前状态名；目标状态机不存在该状态时返回 false，
//          由重载会话降级到 resetToDefaults()（回到 entry）。
```

这样状态机被脚本重建后能以"同一状态名、同一参数"继续播放；`.eva` 源文件变化时
"animclip" reloader 通过 adopt 保持 clip 实例身份并刷新内容（与状态恢复正交）。

### 4.6 其他模块接入

| 模块 | stateKind | capture 内容 | restore 要点 |
| --- | --- | --- | --- |
| dialogue | `"dialogue"` | Phase、当前台词/选项 id、变量区、rngState_ | 按台词 id 重解析；变量按作用域合并 |
| rpg | `"rpg"` | 每个 actor 的 known 技能冷却 + CastingState（active/skillId/remaining/total） | 技能定义按 id 重查；target 指针不序列化（恢复为 null）；未知技能丢弃 |
| card | `"card"` | 每张卡的 phase（结构态） | 按 id 匹配恢复；Hovered/Dragging 归一到 Hand，拖拽内部状态丢弃 |

`Dialogue` 现有 `resetDialogue` 可作为 `resetToDefaults` 的底座；`rpg` 的
Effect/Skill 已是 JSON 定义（`loadFromJson`），天然适合"定义重载 + 状态保留"。

### 4.7 重载会话与回滚（ReloadSession）

`devtools` 新增 `ReloadSession`，主持一次状态重载的完整事务：

1. **begin**：遍历 `IStateProvider` + 脚本状态根 → 内存缓冲；同时把上一份完整状态
   落盘 `eve_snapshot.json`（回滚点）。任一 provider capture 失败 → 中止本次重载。
2. **reload**：现有资源 `tryReload`（逐项原子）+ 脚本 `dofile`，顺序不变。
3. **commit**：`mergeDefaults` 补齐新字段 → provider `restoreState` 逐个恢复。
   失败者走 `resetToDefaults`，汇总错误返回脚本层。
4. **abort/回退**：`eve.dev.abortStateReload()` 恢复会话缓冲；
   手动回退用 `eve.dev.loadSnapshot("eve_snapshot.json")`（begin 时已落盘）。
   代码本身无法回滚（Squirrel closure 不可逆），以"状态可回退 + 日志定位"为兜底。

会话全部在主线程帧间（`poll_hot_reload` 已有此节奏），不触碰帧循环；远程热更
（`poll_remote_reload`）复用同一入口。

### 4.8 Snapshot v2

```json
{
  "version": 2,
  "roots": { "gameState": { "...": "..." } },
  "native": { "dialogue": { "...": "..." }, "anim": { "machines": [ "...", "..." ] } }
}
```

- `restore` 兼容读取 version 1（仅 roots）。
- 引擎对象从"无状态"改为"可选可序列化"：未实现 `IStateProvider` 的模块照旧被跳过，
  能力机制保证裁剪与既有构建不受影响。

## 5. 分层与裁剪影响

- `common/` 新增 `StateValue.h` / `IStateProvider.h`：仅 std，无新依赖；`create_module`
  自动收集源文件，不改 `module_manifest.cmake`。
- `devtools` 扩展 `Snapshot`（`rootsFor`）并新增 `ReloadSession.h/.cpp`：
  现有依赖（event、poco、squirrel）足够。
- `filesystem/HotReload.cpp` 零改动：继续只管资源分发，会话编排在 devtools + 脚本层。
- 各状态模块自行实现 provider，无新增跨模块 `#include`；
  `scripts/module_depgraph.py --check` 应保持通过。
- 脚本 API 新增集中在 `eve.dev`（markStateRoot / unmarkStateRoot /
  clearStateRoots / stateRoots / beginStateReload / commitStateReload /
  abortStateReload，以及既有 saveSnapshot / loadSnapshot），
  `load.nut` 新增 `migrate_instance` / `remap_instances` 与可选钩子
  （eve_before_reload / eve_after_reload），旧脚本不受影响。

## 6. 测试与验证

按仓库"每模块独立测试文件、CTest 进程隔离"约定：

- `test/state_value.cpp`（新）：`StateValue` 类型往返、路径 get/set、mergeDefaults、
  非法路径行为。
- `test/reload_session.cpp`（新，devtools）：mock provider 的 begin/reload/commit/
  abort 全流程；capture 失败中止；restore 失败降级 reset；version 1 快照兼容。
- `test/dialogue.cpp` 增补：capture/restore 往返（Phase/变量/rngState_ 精确恢复）；
  台词定义变化后按 id 重解析。
- `test/animation.cpp` 增补：状态机 capture/restore；clip 重载后指针身份稳定
  （adopt），状态名与参数不丢。
- `test/animation_reload.cpp`（新）：`AnimClip::adopt` 原地换内容、注册表登记/
  注销、`.eva` 文件重写后 reloadPath 刷新已登记实例、未知/损坏源为 no-op。
- `test/rpg_reload.cpp`（新）：施法 + 冷却 capture/restore 往返、reset 打断施法。
- `test/card_reload.cpp`（新）：卡牌 phase 往返、瞬态拖拽丢弃、reset 回牌库。
- `test/debugger.cpp` 增补：状态根含函数 / 类实例时 capture 拒绝并报错。
- 脚本级（`*_proc_script.nut` 模式）：定义 `gameState` 与一个 class 实例，
  模拟三阶段重载后状态保留、新字段默认补齐、`migrateInstance` 生效。
- 手工冒烟：`make test` 全套 + `scripts/smoke_examples.sh` 保持通过。

## 7. 分阶段实施与 PR 拆分

每个阶段一个 PR，接口变更单 PR 闭环（先接口、后实现、再消费方）：

1. **PR1 公共层**：`StateValue` + `IStateProvider` + `Snapshot` v2（含 native 段，
   兼容 v1）+ `test/state_value.cpp`。纯增量，无行为变化。
2. **PR2 原生接入（已实施）**：dialogue、AnimStateMachine 实现 `IStateProvider`
   （模块级实例注册表 + 静态注册），含 capture/restore/reset 与对应测试；
   **PR2b（已实施）**：`AnimClip::adopt` + `AnimClipRegistry` + "animclip"
   `IAssetReloader`（见 §4.5），`test/animation_reload.cpp` 覆盖。
3. **PR3 脚本协议（已实施）**：`ReloadSession` + `eve.dev` API +
   `load.nut` 三阶段（capture → dofile → commit）+ `migrate_instance` /
   `remap_instances` + `test/reload_script.cpp` 脚本级测试。
   此时端到端状态热更打通（资源热更链路为既有实现）。
4. **PR4 推广与约束（已实施）**：rpg（冷却 + 施法状态机）、card（卡牌 phase）
   接入 `IStateProvider`；Snapshot 状态根严格校验——根内出现函数/类/userdata/
   线程/类实例等不可序列化值时报错并中止 capture（防止状态静默丢失）；
   `游戏模型设计.md` 的"可达状态约束"留作脚本侧 `eve_after_reload` 自定义
   校验（引擎不强加游戏语义约束）。

## 8. 风险与对策

- **Squirrel 实例迁移天然弱**（旧 class 引用、闭包捕获旧函数）：数据优先 + 显式
  `migrateInstance`；状态根校验拒绝不可序列化字段。
- **原生指针悬垂**（AnimStateMachine clip）：资源进缓存 + adopt 保身份 + 按名重解析，
  三重防护。
- **状态与定义不匹配**（字段改名/删除）：mergeDefaults 补齐 + 恢复失败降级默认值 +
  快照回退，不静默丢状态。
- **快照成本**：capture/restore 只在重载时执行（O(状态量)），不进帧循环；
  对大型世界可后续加"只快照标记根"的增量选项。
- **回滚边界**：脚本代码不可回滚是 Squirrel 语义限制，文档明示"状态可回退、代码靠
  git/版本管理"，避免误用承诺。
