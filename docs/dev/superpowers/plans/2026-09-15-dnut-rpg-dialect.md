# 实施计划：dnut 序列语言核心（L1）与 RPG 方言

> 上位设计：`docs/dev/superpowers/specs/2026-09-15-dnut-interpreter-l1-design.md`
> 状态：**已实施**（§1.1 语言核心、§1.2 RPG 词表、§1.3 测试与文档全部落地；
> `SequenceDocument` 与 dialogue 迁移按 §4 登记为后续增量）
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
    animation target=<id> clip=<uri> [loop] [await]
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
