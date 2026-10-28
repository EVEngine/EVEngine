# EVEngine Agent-Native 引擎架构

日期：2026-08-29
状态：精简架构提案，尚未代表代码已实现

## 1. 一句话目标

> 让 Editor、Agent、脚本和测试通过同一套游戏语义读取和修改项目，并能自动运行游戏确认结果。

不是让 Agent 模拟点击 UI，也不是先建设一套庞大的分布式 Agent 平台。

最小闭环只有五步：

```text
读取项目 -> 提交修改 -> 在独立工作区运行 -> 自动检查结果 -> 合并通过的修改
```

对应五个核心机制：

| 机制 | 解决的问题 |
| --- | --- |
| 结构化文档 | Agent 怎样可靠理解 Scene、材质、PCG 等内容 |
| 领域命令 | UI 和 Agent 怎样用同一种方式修改内容 |
| 独立工作区 | 一个或多个 Agent 怎样试错而不污染正式项目 |
| 场景验证 | 怎样证明修改在游戏里真的有效 |
| 候选合并 | 多个方向的改进怎样安全进入主线 |

先做好这五件事，其他机制遇到真实问题再增加。

## 2. 最简单的整体结构

```text
用户 UI / Agent / 脚本
          |
      领域命令
          |
  Scene / Material / PCG 等权威文档
          |
      构建运行时世界
          |
   场景测试 + 截图 + 指标
```

这里最重要的规则只有两条：

1. 一份数据只有一个正式来源。例如树的位置以 Scene Document 为准，Inspector 文本框和渲染矩阵只是它的显示或运行结果。
2. UI、Agent 和脚本不各写一套修改逻辑。它们都提交相同的领域命令。

例如移动玩家：

```json
{
  "command": "scene.transform.set",
  "target": "scene-object://player",
  "position": [10, 0, 0]
}
```

这个命令可以来自 Inspector、Gizmo、Agent 或测试。Scene 模块负责检查对象是否存在、数值是否合法、如何记录 undo，以及怎样通知运行时更新。

反例是：UI 直接改 C++ 对象，Agent 修改 JSON，脚本调用第三套 `setPosition()`。三条路径很快就会产生不同的校验、事件和结果。

## 3. 结构化文档：让 Agent 看懂游戏

### 3.1 文档是什么

Scene、Material Graph、PCG Graph、UI 和玩法配置应有明确的数据结构。它们可以保存为 JSON，但运行时不需要使用 JSON。

以材质为例：

```text
可读 JSON                    强类型内存模型                 运行产物
节点、端口、连接   ->   MaterialGraphDocument   ->   Shader / GPU Pipeline
```

- JSON 方便 Git、Agent 和外部工具读取；
- 强类型模型负责材质语义和校验；
- GPU Pipeline 只负责高效运行，可以随时重新生成。

这就是本文所说的 IR：稳定表达某个领域含义的数据模型。IR 不等于 JSON；JSON 只是它的一种可读保存形式。

### 3.2 Schema 只做最实用的事

Schema 先回答：

- 有哪些字段；
- 字段是什么类型；
- 是否必填；
- 数值范围；
- 引用的是哪类对象；
- 当前格式版本。

例如：

```json
{
  "name": "roughness",
  "type": "number",
  "minimum": 0,
  "maximum": 1
}
```

复杂游戏规则不要硬塞进 Schema。例如“驾驶载具时不能切换跳跃配置”由 Gameplay Validator 检查。

### 3.3 文档的最小要求

只有跨进程、需要保存或者需要工具编辑的内容，才要求稳定文档格式。每种文档至少具有：

- 类型名称；
- 版本；
- 稳定对象 ID；
- 明确的未知字段处理方式；
- 加载失败不破坏现有内容；
- 从旧版本升级的入口。

大块 Mesh、纹理和 PCG 点数据不塞进 JSON。JSON 保存引用和参数，二进制数据保存为独立 artifact。

## 4. 领域命令：让所有工具用同一条修改路径

### 4.1 什么是领域命令

领域命令表达一次用户能理解的操作：

- `scene.object.create`
- `scene.transform.set`
- `material.node.connect`
- `pcg.biome.paint`
- `ui.widget.reparent`

它不是万能的 `execute(string)`，也不是把内部 setter 全部暴露出去。

一个命令最少包含：

```text
命令类型
目标对象
参数
执行结果
诊断信息
```

需要防止并发覆盖时再增加 `expectedRevision`。不要让每个最简单的本地操作从第一天就携带完整的分布式事务元数据。

### 4.2 返回结果

命令不返回含糊的 `bool`。它至少需要区分：

```text
Applied       已应用
Unchanged     请求合法，但没有产生变化
Rejected      输入或业务规则不允许
Conflict      内容已被别人修改，需要重新读取
Unsupported   当前构建没有这项能力
Failed        执行时发生故障
```

失败同时返回稳定错误码、对象和字段路径。例如：

```text
code: material.port.type_mismatch
path: noise.color -> clamp.value
```

Agent 可以据此修正；只有“材质失败”四个字没有可操作性。

### 4.3 事务只围绕一次用户意图

事务的直观含义是：一次操作要么整体成功，要么正式内容不变。

例如创建道路同时需要：

- 写入 Road Document；
- 创建 Scene 对象；
- 创建 Physics Collider。

正式提交前先检查并准备三部分。任何一部分准备失败，都不发布道路。

不必事务化的内容：

- hover；
- 面板展开状态；
- 搜索框文字；
- 运行时物理每帧更新；
- 拖动过程中的每个鼠标采样点。

### 4.4 Preview 的简单做法

拖动滑块或地形笔刷时使用一个临时副本或 overlay：

```text
开始预览 -> 多次更新临时结果 -> 松手后提交一次
                         `-> Esc 时直接丢弃
```

正式文档只在最后一次提交时改变。不要先改正式数据，再依赖“取消时调用 setter 改回去”。

### 4.5 自定义 Editor 怎么接入

```text
自定义 UI
   |
构造领域命令
   |
领域模块校验并执行
   |
统一 Result + undo + change event
```

自定义 Editor 可以自由设计 UI，也可以注册新的领域命令，但不直接修改其他模块的私有状态。

## 5. 独立工作区：Agent 可以放心试错

单 Agent 也应该在独立工作区工作，多 Agent 更是如此。

默认只隔离三样东西：

| 工作内容 | 简单隔离方式 |
| --- | --- |
| C++、shader、构建文件 | 独立 Git worktree/branch |
| Scene、Material、PCG 等创作内容 | 工作区副本或 copy-on-write overlay |
| 运行和截图 | 独立临时目录、端口和运行实例 |

每个任务只需要一份简短任务说明：

```yaml
goal: 改善玩家跳跃反馈
area: visual
mayChange:
  - assets/effects/player/**
  - material://player-trail
mustPass:
  - scenario://player-jump
mustNotRegress:
  - gpu_frame_time
```

不必一开始就建立复杂的 TaskContract 服务、租约中心和分布式锁。任务说明可以先是仓库内的数据文件或内存对象。

只有确实发生两个任务修改同一权威对象时，才使用 revision 冲突检查；只有确实存在不能并行使用的设备或发布目标时，才增加临时 lease。

## 6. 场景验证：Agent 必须证明结果

### 6.1 最小 Scenario

Scenario 是可重复的“准备、操作、检查”：

```yaml
scene: project://scenes/test_arena
seed: 42
steps:
  - input: {action: player.jump, state: pressed}
  - advance: {ticks: 10}
checks:
  - player.motion.airborne == true
  - error_diagnostics == 0
  - gpu_frame_time_p95 < 14ms
  - capture: jump.png
```

第一阶段不需要发明通用测试 DSL。可以先用现有测试框架和少量强类型 Scenario API，只要它们能够：

- 固定场景、seed、输入和推进时间；
- 读取结构化状态；
- 获得结构化诊断；
- 由引擎自己捕获画面；
- 输出成功或失败证据。

### 6.2 为什么不能只看编译

修改跳跃高度后，编译通过只证明代码能构建。真正需要检查的是：

- 是否真的腾空；
- 最高点是否符合设计；
- 是否能正常落地；
- 画面是否正确；
- 帧时间是否退化；
- 是否出现错误诊断。

### 6.3 确定性只覆盖需要复现的路径

测试相关玩法、PCG 和 Physics 使用固定 tick、dt、seed 和输入。能做到完全一致时比较 hash；GPU 等不能完全一致时使用事先定义的容差。

不要求所有游戏效果都完全确定，只要求自动验证的结果能够稳定判断通过或失败。

## 7. 多 Agent：隔离开发，最后组合验证

### 7.1 最直观的协作模型

```text
同一个主线版本
   |-- Agent A -> 开发森林关卡 -> 独立候选 A
   |-- Agent B -> 开发矿洞关卡 -> 独立候选 B
   `-- Agent C -> 开发村庄关卡 -> 独立候选 C

A + B + C -> 合并 -> 检查关卡和连接 -> 通过后进入主线
```

Agent 之间不共享可变工作区，也不直接覆盖主线。每个 Agent 最终提交：

- 代码或文档修改；
- 它基于哪个主线版本；
- 改了哪些领域对象；
- 运行了哪些检查；
- 截图、指标和诊断结果。

这组内容叫“候选变更”。它可以先直接使用 Git commit + 一份验证报告，不必立即设计新的 CandidateChangeSet 数据库。

关卡是第一版最适合的并行单位，因为每个关卡通常已经有自己的 Scene Document、局部对象、导航、灯光、PCG 参数和测试入口。只要不同 Agent 不同时修改共享资产或世界总表，它们的写入天然分离。

每个关卡建议是一个自包含 package：

```text
levels/forest/
  level.json          关卡对象与引用
  lighting.json       局部灯光
  encounters.json     敌人和事件
  navigation.json     导航配置或构建输入
  tests/              入口、出口、玩法和性能检查
```

这只是逻辑边界，不强制使用这些文件名。关键是能独立加载、运行和验证。

### 7.2 如何判断能不能合并

按三层检查：

1. 文件层：Git 能否合并；
2. 内容层：是否修改同一个 Scene 对象、材质节点或配置字段；
3. 行为层：组合后游戏是否仍通过 Scenario。

前两层没有冲突，也不能跳过第三层。

例如 Visual Agent 新增跳跃拖尾，Performance Agent 优化旧粒子。Git 可能无冲突，但组合后拖尾让 GPU 帧时间超标。只有组合运行才能发现这个问题。

关卡合并主要检查四件事：

1. 每个关卡单独仍能加载和完成；
2. 世界总表正确引用所有关卡；
3. 关卡入口、出口、传送门和任务状态能够衔接；
4. streaming、内存和共享资源总预算没有超标。

### 7.3 Integration Agent 不需要特殊架构地位

Integration Agent 只是执行普通集成流程的 Agent：

```text
读取候选 -> 合并 -> 构建 -> 运行组合测试 -> 生成报告
```

它没有权力：

- 擅自降低测试阈值；
- 无条件重录正确截图；
- 扩大其他任务的修改范围；
- 绕过发布权限；
- 把失败候选标记为成功。

早期甚至不需要固定的 Integration Agent；CI 或负责当前任务的 Agent 都可以执行这套流程。

### 7.4 持续改进方向

可以并行维护这些方向：

- Level：森林、矿洞、村庄、地下城等不同关卡；
- Gameplay：手感、AI、平衡和关卡逻辑；
- Visual：场景、材质、动画、VFX 和 UI；
- Performance：CPU、GPU、内存和加载；
- Reliability：崩溃、恢复、平台和资产兼容；
- UX/Accessibility：输入、可读性、本地化和辅助功能；
- QA：发现问题、增加 Scenario 和回归证据。

每个方向除了“要改善什么”，还要写“不能退化什么”。画质改进保护帧时间，性能改进保护视觉和玩法结果。

## 8. 一个完整例子：三个 Agent 并行开发关卡

主线已经定义世界结构：

```text
村庄 -> 森林 -> 矿洞
```

World Manifest 只保存关卡引用和连接关系，不保存每个关卡的内部对象。每个关卡获得自己的 LevelId 和独立 Scene Document。

### 8.1 先定义简单的关卡契约

每个关卡任务只需要这些共同约束：

```yaml
level: forest
entry: from_village
exit: to_mine
playerLevel: 3-5
requiredAbilities: [jump, interact]
memoryBudgetMiB: 700
frameTimeP95Ms: 16
mustPass:
  - level-loads
  - entry-to-exit
  - no-missing-assets
  - no-error-diagnostics
```

这里的契约不是复杂平台，只是一份关卡验收数据。它让 Agent 知道从哪里进入、应该通向哪里，以及不能突破哪些预算。

### 8.2 各 Agent 独立开发

Village Agent：

- 负责村庄布局、NPC、商店和通往森林的出口；
- 只修改 `level://village` 和自己的局部资源；
- 验证玩家可以出生、交互并抵达出口。

Forest Agent：

- 负责林地、敌人遭遇、湖泊和道路；
- 使用已有共享树木资产，不直接修改它；
- 验证从村庄入口可以走到矿洞出口。

Mine Agent：

- 负责洞穴、机关、战斗和返回出口；
- 验证没有导航断点，内存和灯光数量在预算内。

三者使用独立 worktree、运行实例和截图目录，开发期间不会覆盖彼此。

### 8.3 共享内容如何处理

如果 Forest Agent 认为共享树木材质需要修改，它不直接改全局材质，因为这可能改变 Village 和其他关卡。简单处理方式有两个：

1. 创建森林局部材质实例，只影响森林；
2. 单独创建“修改共享树木材质”的任务，让所有使用者运行回归测试。

World Manifest 也由单独的小集成改动维护。关卡 Agent 输出“需要连接的入口和出口”，集成阶段再更新总表，避免三个 Agent 同时编辑同一个世界文件。

### 8.4 合并与组合验证

三个关卡候选分别通过后，进行集成：

```text
合并 Village + Forest + Mine
-> 更新 World Manifest
-> 分别加载三个关卡
-> 运行 Village 到 Forest 的连接测试
-> 运行 Forest 到 Mine 的连接测试
-> 运行完整 Village -> Forest -> Mine 旅程
-> 检查 streaming、内存、帧时间和缺失资源
```

假设每个关卡单独都通过，但完整旅程中发现 Forest 出口名为 `mine_gate`，Mine 入口名为 `from_forest`。这是连接契约冲突，不需要复杂 merge 算法；修正 World Manifest 或其中一个稳定端口名称后重新运行连接测试即可。

另一个可能问题是三个关卡单独运行都在内存预算内，但 streaming 过渡时 Village 尚未卸载、Forest 已经加载，峰值内存超标。这个问题只有组合旅程才能发现。

### 8.5 同一关卡内部也可以继续并行

关卡足够大时，还可以按局部边界拆分：

- layout/terrain；
- encounter/gameplay；
- lighting/visual；
- optimization/QA。

但这比不同关卡并行更容易修改同一 Scene Document。第一版优先“一名 Agent 一个关卡”；只有大型关卡确实成为瓶颈时，再引入 sub-scene、layer 或明确的局部 ownership。

## 9. 什么时候才增加高级机制

默认方案解决不了真实问题时再升级：

| 遇到的问题 | 再增加的机制 |
| --- | --- |
| 两人频繁覆盖同一个对象 | revision / optimistic concurrency |
| 多个字段可以安全自动合并 | 领域 merge/rebase |
| GPU、发布目标不能并发使用 | 有期限 lease |
| 跨进程任务中断后必须恢复 | durable task/transaction record |
| 操作包含上传、删除等外部效果 | preflight + compensation |
| 同一请求可能因网络超时重复提交 | operation id + 幂等去重 |
| 项目规模大到查询会拖垮引擎 | query 分页和资源预算 |
| 多个 provider 可热替换 | generation handle / capability lease |

这些机制不是错误，但不应成为第一版的前置条件。

## 10. EVEngine 的最小落地方案

优先复用现有实现：

- `editor::DomainOperation`
- `editing::IEditableTarget`
- `IEditAuthority`
- `EditorTransactionService`
- `common/IEditorAutomation`
- `transaction::ITransactionParticipant`
- `property_access`
- `IRenderCapture`
- DevTools Snapshot/ScenarioRecorder

不要新建宽泛的 `agent` module。第一轮只做两个真实领域：

1. Scene Transform；
2. Material Parameter。

具体步骤：

### 第一步：统一修改入口

- UI 和 headless automation 调用同一个 DomainOperation；
- 每个操作返回统一 Result/Diagnostic；
- 旧的直接写路径收敛到该入口。

验收：Agent 无需点击 UI，可以移动 Scene 对象和修改材质参数；UI 的 undo/redo 同样有效。

### 第二步：运行验证

- 增加一个固定测试 Scene；
- 可以注入输入、推进固定 ticks、读取状态；
- 使用 engine-owned capture；
- 把结果保存为普通测试 artifact。

验收：Agent 修改后能自动证明对象位置、材质结果和画面符合预期。

### 第三步：并行候选

- 每个任务使用独立 worktree、内容 overlay 和临时目录；
- 候选先使用 Git commit + 验证报告；
- 准备三个小型测试关卡，每个关卡可独立加载和完成；
- World Manifest 单独维护关卡引用和稳定入口/出口；
- 合并后运行单关卡、相邻连接和完整旅程 Scenario。

验收：三个 Agent 可以分别完成 Village、Forest、Mine；关卡内部修改互不覆盖；入口/出口不匹配、缺失共享资产和 streaming 峰值超预算能在集成时被阻止。

### 第四步：再决定是否抽象

只有当 Scene、Material 之外至少还有两个真实领域复用相同模式，才稳定通用 discovery/schema/command 描述；只有出现独立 CLI/CI consumer，才评审是否拆出 automation orchestration 模块。

## 11. 第一版明确不做什么

- 不建立通用分布式事务；
- 不建立永久运行的多 Agent 调度中心；
- 不设计万能查询语言；
- 不自动推断任意领域对象的合并语义；
- 不把所有资产转换为 JSON；
- 不强制所有运行时状态进入 Editor 事务；
- 不为未来可能存在的 provider 预建大量接口；
- 不在没有真实 consumer 时新增公共抽象；
- 不把编译成功当成功验证；
- 不允许 Agent 通过更新 baseline 或放宽阈值隐藏退化。

## 12. 第一版完成标准

第一版只需要证明：

- Scene 与 Material 各有真实 UI 和 Agent consumer；
- UI 与 Agent 共享领域命令和校验；
- 修改失败不会留下半更新文档；
- Agent 在独立工作区试错，不污染主线；
- 每个候选带测试、诊断以及必要截图/指标；
- 多个关卡候选合并后会运行单关卡、连接和完整旅程 Scenario；
- 相关裁剪构建和 provider 缺失有明确结果；
- 没有新增 `bool + lastError`、长期裸指针或第二权威状态；
- `make check/architecture-contracts` 通过。

达到这些条件后，EVEngine 已经具备实用的 Agent-native 基础。高级机制根据真实冲突、规模和可靠性需求逐项加入，而不是一次建完。

## 13. 术语速查

| 术语 | 直白含义 |
| --- | --- |
| 权威文档 | 某项创作内容最终以它为准 |
| IR | 稳定表达 Scene、Material、PCG 等领域含义的数据模型 |
| Schema | 数据字段和基本格式规则 |
| 领域命令 | UI、Agent、脚本共用的一次业务修改 |
| Transaction | 一组修改整体成功或正式内容不变 |
| Preview | 尚未写入正式文档的临时效果 |
| Revision | 文档每次提交后的版本号 |
| Artifact | 从文档编译或生成出的运行产物 |
| Scenario | 可重复的游戏输入和检查 |
| 候选变更 | 一个隔离任务的修改加验证证据 |
| Rebase | 基于别人已经修改后的新版本重新计算候选 |
| Lease | 对确实不能并发使用的资源进行临时占用 |

## 14. 架构规则适用情况

- Result：失败操作返回结构化 Result/Diagnostic，不新增含混 `bool`。
- 唯一 owner：Document 是创作真源，runtime object/artifact 是投影或产物。
- 领域短根：不建立统一 AgentObject/GameObject 根。
- Link/Handle：跨帧身份使用 typed handle，不保存临时裸指针。
- ECS：运行时 System 更新不强制进入 Editor Transaction。
- Schema：持久格式有版本和 migration，加载失败不部分发布。
- Optional dependency：缺失返回 `Unsupported`，不静默降级。
- 模块边界：复用 consumer-owned capability，不让 common 吸收高层业务。
- 多 Agent：默认依赖独立工作区和组合 Scenario，不引入全局共享可变状态。
- 例外：无。

## 15. Review 结论

本文保留了前两轮 Review 的核心结论，但把它们从默认实现中分离：

第一轮检查架构安全性，保留了唯一权威状态、结构化 Result、失败不部分发布、版本化持久格式、provider 缺失可观察和组合验证等底线。

第二轮检查可实施性，确定直接复用 EVEngine 现有 Editor/Transaction/Automation/RenderCapture 基础，只从 Scene Transform 与 Material Parameter 两个真实领域开始，不创建宽泛 `agent` module。

本次精简检查进一步做出以下取舍：

- 默认主线从十余类协议收敛为五个机制；
- Git commit + 验证报告先替代专用 CandidateChangeSet 服务；
- 简短任务说明先替代完整 TaskContract 平台；
- CI/普通 Agent 先承担集成角色，不建立特殊 Integration Agent 系统；
- 普通测试 API 先替代通用 Scenario DSL；
- revision、lease、幂等、持久恢复和 compensation 全部改为问题出现后按需启用。
- 多 Agent 第一案例改为 Village、Forest、Mine 三个关卡并行；同一功能的玩法/视觉/性能协作只作为后续进阶场景。

结论：第一版的复杂度应由 Scene、Material 和一个真实多 Agent 组合案例来证明；不能为了“以后可能需要”提前建设平台。
