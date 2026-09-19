# 实施计划：dnut 序列语言核心（L1）与 RPG 方言

> 上位设计：`docs/dev/superpowers/specs/2026-09-15-dnut-interpreter-l1-design.md`
> 状态：**已实施**（§1.1 语言核心、§1.2 RPG 词表、§1.3 测试与文档全部落地；
> §5 角色移动与 §6 动画播放按增量补齐；`SequenceDocument` 与 dialogue 迁移按 §4 登记为后续增量）
> 目标（用户原话）：给 dnut 语言中的 rpg 方言添加人物的移动、动画播放、对象的选取、
> 技能的获取、属性的修改、物品、装备获取等等功能——**完整实现**。

## 0. 与设计文档的偏差声明

设计的「阶段 1」把 dialogue 的 conversation 方言整体迁入 L1。本计划**先交付可运行的
RPG 方言与其 L1 语言核心**，把 dialogue 迁移留作后续增量，理由：

1. 用户目标是**能力**（RPG 方言可表达并可执行），不是模块搬迁本身；
2. dialogue 迁移会让一个 PR 触及 15 个文件 + 8 个测试文件，把「新能力」与
   「纯搬迁」混在一次交付里，失败模式不可分离；
3. 语言核心（词法/资产模型/步骤注册表/运行时）一经建立，dialogue 迁移是**纯消费者改造**，
   不再需要新机制。

因此顺序为：**语言核心（L1）→ RPG 方言语汇 → RPG 接入与绑定 → dialogue 迁移（后续）**。
`docs/dev/superpowers/specs/2026-09-15-dnut-interpreter-l1-design.md` §8 的阶段划分
按此顺序重读：本计划覆盖其阶段 1 的「新模块 + 语言」与阶段 2、3 的 RPG 部分。

## 1. 交付物清单

### 1.1 新模块 `src/modules/dnut_interpreter/`（L1）

| 文件 | 职责 |
| --- | --- |
| `DnutDiagnostic.h` | 与领域无关的诊断：`Severity`/`path`/`line`/`column`/`message` |
| `DnutLexer.h/.cpp` | 统一词法器（今天 `DnutParser.cpp:101-217` 的升格），带行/列 |
| `DnutBlockScanner.h/.cpp` | 顶层块切分（`story` / `pool` / `conversation`），供多方言共用 |
| `SequenceAsset.h/.cpp` | 资产模型 `SequenceAsset`/`SequenceNode`/`SequenceRoute` + 图校验 |
| `StepKindRegistry.h/.cpp` | 步骤类型注册表：descriptor + schema + validate + handler + dispatch |
| `DnutCompiler.h/.cpp` | 注册表驱动的 `.dnut` → `SequenceAsset` 编译器（控制流核心 + 通用步骤） |
| `SequenceRuntime.h/.cpp` | 跨帧游标运行时：分支/调用/等待/命令派发/挂起恢复/状态捕获/预算护栏 |
| `SequenceDocument.h/.cpp` | 作者文档反射（字段表由 `payloadSchema` 派生） |

### 1.2 `src/modules/rpg/` 增补（L1 消费者）

| 文件 | 职责 |
| --- | --- |
| `RpgDialect.h/.cpp` | 注册 RPG 步骤词表 + 领域 handler + `.dnut` story 装载门面 |
| `RPG.cpp` | 脚本绑定：装载、执行、观测 |
| `StoryEvent.h/.cpp` | 保持 v1 线格式与游标语义；新增 `.dnut` 内容源（不破坏既有 JSON 路径） |

### 1.3 测试与文档

- `test/dnut_interpreter_language.cpp`：词法、块切分、编译、诊断、注册表校验。
- `test/dnut_interpreter_runtime.cpp`：控制流、挂起/恢复、捕获重建、预算护栏。
- `test/rpg_dialect.cpp`：七类 RPG 步骤的语义与失败路径。
- `docs/dev/dnut序列语言与解释器.md`：模块 owner 文档（六面边界 + 裁剪声明）。
- `docs/usr/modules/rpg.md`：RPG 方言语法与用法。

## 2. RPG 方言（语言表面）

```
story <id> [repeatable] {
    move      actor=<id> x=<n> y=<n> [duration=<n>]
    animation target=<id> clip=<uri> [loop] [hold]
    select    object=<id> [prompt=<text>]
    skill     actor=<id> learn=<id>          # forget=<id> 亦为同一步骤的模式
    attribute actor=<id> name=<id> op=set|add value=<n>
    item      add=<id> [count=<n>] | remove=<id> [count=<n>]
    equipment actor=<id> equip=<id> | unequip=<slot>
    message   text=<text>
    dialogue  id=<conversationId>
    wait      duration=<n>
    camera    x=<n> y=<n> [duration=<n>]
    if <condition> { ... } [else { ... }]
    choice { option "label" [when <condition>] -> <target> }
    call <storyId>
    end
}
```

- 步骤类型**不是**编译器里的 `switch`：`move`/`animation`/... 全部由 `rpg` 注册，
  编译器只按注册表校验字段。
- `if`/`choice`/`call`/`end`/`wait` 是语言核心的控制流，编译为 `branch`/`choice`/`call`/
  `end`/`wait` 节点。
- 条件沿用 `decision::Condition` 的 `eve::Value` 形态（`var`/`op`/`value`、`all`/`any`/`not`）。

## 3. 验收

```sh
python3 scripts/check_module_manifest.py
python3 scripts/module_depgraph.py --check --check-layers
make build/win32-debug
build/win32-debug/test/unit_test.exe "dnut_interpreter.*"     # 或用 CTest 过滤
build/win32-debug/test/unit_test.exe "rpg_dialect.*"
```

## 4. 未纳入本计划的既有债务（显式登记）

- dialogue 的 conversation/pool 方言尚未迁到 L1 语言核心（设计阶段 1 剩余部分）。
  迁移前，`.dnut` 仍有两套前端；本计划**不新增第三套**——RPG 方言直接消费 L1 词法器与
  块切分器，dialogue 的两套方言保持原状，待迁移时统一。

## 5. 增量：角色移动（对设计 §8 阶段 3 的有意偏离）

设计文档 `2026-09-15-dnut-interpreter-l1-design.md` §8 阶段 3 把移动描述为
「**基于既有 `map::Pathfinder` + 一个按格移动控制器**」。本次交付**不那样做**，
改为在 `rpg` 侧提供一个宿主注入的移动端口：

```cpp
struct RpgStoryBinding {
    …
    StoryMoveHandler moveActor;   // Result<StoryMoveStatus>(const StoryMoveRequest&)
};
```

偏离理由——设计里那条路会把方言**焊死在一种玩法上**：

1. `map::Pathfinder` 是**格子地图**的寻路器。把 `move` 的实现接到它上面，等于规定
   `actor` 必须是地图上的实体、`x`/`y` 必须是格坐标、移动必须存在一条格子路径。
   战棋、连续 3D、横版与轨道序列都会被迫先造一张假地图。
2. `rpg` 是 **L1**，`map` 更高层；直接依赖会新增一条上行依赖，违反
   `scripts/module_depgraph.py --check-layers` 的判据。
3. `move` 的真值只有「谁、去哪里、多久」——**怎么走**是玩法的自由度，不是语言事实。

端口形态保留了两个方向的能力：

- **便捷**：装一个 handler，`move` 步骤自动派发，宿主不再需要在循环里写
  `if (kind == "move")`；返回 `Arrived` 时（瞬移、已在目标点）剧情不挂起。
- **灵活**：不装 handler 时行为**逐字不变**——仍是宿主呈现步骤，脚本读 payload 后
  `advance()`。非队伍主体（地图物件、相机、编队）也能移动，`actor` 投影为 null
  而步骤照常执行。

取舍明说：**本次不提供按格移动控制器**，因此「内容作者想要一条自动绕障的路径」这件事
仍然没有开箱实现——那是某个具体玩法适配器的职责，应该建在 `map` 之上而不是
`dnut_interpreter` 或 `rpg` 里。`x`/`y` 的单位与语义由宿主定义并在其文档中写明。

证据：

- `src/modules/rpg/RpgDialect.h`：`StoryMoveStatus` / `StoryMoveRequest` / `StoryMoveHandler`。
- `src/modules/rpg/RpgDialect.cpp`：`performMove` + `bind("move", performMove)`；无控制器时
  返回 `Blocked`，与运行时的宿主呈现挂起**观测等价**。
- `test/rpg_dialect.cpp`：`FakeMoveWorld` 参考端口 + 六条语义/失败/模式无关用例。
- `test/rpg_dnut_script.cpp`：Squirrel 端到端驱动 `move` 并核对效果。

## 6. 增量：动画播放（与 §5 同构）

§2 的词表早就声明了 `animation`，但只到「编译能过、运行时把它当宿主呈现步骤挂起」为止：
没有 handler、没有端口、没有测试、没有样例。本次按 §5 已定的同一形状补齐它，理由是
这两步的**真值形状完全一样**——「谁、播什么、播完没有」，而「怎么播」同样是玩法的自由度。

端口形态：

```cpp
enum class StoryAnimationStatus { Finished, Playing, Unavailable };
struct StoryAnimationRequest { targetId, actor, clip, loop, hold };

struct RpgStoryBinding {
    …
    StoryAnimationHandler playAnimation;   // Result<StoryAnimationStatus>(const StoryAnimationRequest&)
};
```

与 §5 一致的取舍：

- **便捷**：装一个 handler，`animation` 步骤自动派发，宿主不再需要在循环里写
  `if (kind == "animation")`；返回 `Finished` 时（瞬发、零长 clip）剧情不挂起。
- **灵活**：不装 handler 时行为**逐字不变**——仍是宿主呈现步骤，脚本读 payload 后
  `advance()`。特效挂点、UI 元素等非队伍主体也能作为 `target`，`actor` 投影为 null
  而步骤照常执行。
- **不绑死**：`clip` 对引擎是不透明字符串，不解析、不加载。接上某个动画栈（骨骼、
  Sprite、Spine、montage）是宿主适配器的职责，不是 `dnut_interpreter` 或 `rpg` 的。

字段命名修正：§2 原先写 `[await]`，实现与 `docs/usr/modules/rpg.md` 都是 `[hold]`。
**以 `hold` 为准**——`await` 与步骤形状 `StepShape::Await`（该步骤本来就挂起）语义重复，
而 `hold`（播完停在最后一帧）才是宿主真正需要的、引擎无法自行推断的信息。本次同步把
计划书的 `[await]` 改成 `[hold]`。`loop` / `hold` 均按作者原样透传，引擎不解释。

证据：

- `src/modules/rpg/RpgDialect.h`：`StoryAnimationStatus` / `StoryAnimationRequest` / `StoryAnimationHandler`。
- `src/modules/rpg/RpgDialect.cpp`：`performAnimation` + `bind("animation", performAnimation)`；无控制器时
  返回 `Blocked`，与运行时的宿主呈现挂起**观测等价**。
- `test/rpg_dialect.cpp`：`FakeAnimStage` 参考端口 + 七条语义/失败/模式无关用例。
- `test/rpg_dnut_script.cpp`：Squirrel 端到端驱动 `animation` 并核对效果。

