# API 重复与相似面收敛计划

> 日期：2026-10-10  
> 状态：**计划（未实施）**  
> 来源：对 `common` / graphics-asset / gameplay 公共 API 的重复面审计  
> 关联：`2026-08-26-architecture-consolidation-checklist.md`、`Result检查与不得丢弃返回值规范.md`、`领域短根继承与跨域组合架构.md`、`2026-09-15-模块边界审查台账.md`

## 1. 目标与非目标

### 1.1 目标

1. 新公共 API 只走已冻结的协议：`Result` / `Status` / `Diagnostic`、`eve::Value`、身份句柄矩阵、`cap::query`。
2. 消除或降级仍在生产路径上的 **旁路结果类型** 与 **第二动态值树**，使调用方不必在两套错误/数据模型间翻译。
3. 收缩标注为 compatibility / legacy 的门面：保留脚本兼容期，但 C++ 新代码与文档指向 canonical API。
4. 用文档与契约测试固定「有意分层」边界，避免后续代理再造第三条管线。

### 1.2 非目标

- 不合并领域短根（`RPGActor`、RTS Unit、Card*、Weapon*、Vehicle*、Scene* 等）。
- 不合并 Image(CPU) 与 Graphics(GPU)、Assimp/`Model3D` 与 Evpack CanonicalMesh、`AnimSkeleton` 与 `ik::Skeleton3D`、`npc_ai` 与 `agent`、`game_event` 与 `platform_event`。
- 不新建统一 `GameObject` / `GameplayActor`。
- 不在本计划内做玩法规则重写或 Settlement/Effects 能力扩张（见既有结算计划）。

### 1.3 硬约束

1. **先复用，再新增。** 新类型必须删除至少两处真实重复，否则留在调用方。
2. **一种事实一种表示。** 兼容投影只能是单向、可重建、不可写回权威状态。
3. **接口变更一个 PR 打穿。** 改公共形状时同步 backend、consumer、测试与文档；禁止中间态破坏 CI。
4. **债务条目带元数据。** 新 TODO/compat allowlist 必须有 owner、issue、原因、expiry/移除条件。
5. **阶段门禁。** 每阶段结束跑 `ARCHITECTURE_BASE=HEAD make check/architecture-contracts` 与相关模块测试；涉及公共 API 时补契约测试。

---

## 2. 现状基线（审计结论）

### 2.1 已收敛、应直接复用（勿再平行发明）

| 协议 | 权威位置 | 备注 |
|---|---|---|
| 可恢复失败 | `common/Result.h` + `Status` + `Diagnostic` | 脚本投影唯一入口：`SquirrelBinding.h` |
| Owning 动态值 | `common/Value.h` | JSON facade `common/Json.h` 为借用解析，不替代 Value |
| 身份 | `Identity.h` / `RuntimeHandle` / `ecs::EntityHandle` / `SubjectRef` | 持久 / 运行时 / ECS 分轨 |
| 属性 | `attributes::AttributeSet` | RPG/RTS/Card/Weapon/Vehicle 用 adapter |
| 容器转移 | `eve::container::IContainer` | inventory/card/building/vehicle adapters |
| 结算 | `settlement::Settlement` | 领域 policy + adapter |
| 跨模块事件 | `game_event::GameEventLog` | 本地队列可保留，重要结果应上报 |
| 决策原语 | `decision::Condition` / `DecisionContext` | |
| 物理查询 | `physics::World` / `World3D` | LoS 用既有 adapter |
| CPU 资源缓存 | `ResourceManager` | GPU path cache 仍在 Graphics，分层保留 |
| 向上依赖 | `eve::cap::provide/query` | 禁止新的向上 `#include` |

### 2.2 仍待收敛的重复 / 相似面

| ID | 问题 | 证据位置 | 严重度 |
|---|---|---|---|
| D1 | `StateValue` 与 `eve::Value` 双动态值树 | `common/StateValue.h`、`IStateProvider`、`rpg/RpgState.h`；台账已记 | **P0** |
| D2 | `WriteResult` / `PatchResult` 仍是旁路结果形状 | `property_access/PropertyAccess.h`、`statepatch/StatePatch.h` | **P0** |
| D3 | Capability / 热重载仍大量 `bool + err*` | `IRenderCapture`、`ISceneQuery`、`IProcgenQuery`、`IStateProvider` | **P0** |
| D4 | Graphics / Particles 多重兼容入口 | `Graphics::newTexture*`、Particles `from*`/`updateTimeline` vs `try*`/`advance*` | **P1** |
| D5 | 放置 API 三套相近签名，缺所有权说明 | `Renderable3D` / `SceneNodeRef` / `hd2d::Sprite3D` | **P1**（文档+契约，不合并类型） |
| D6 | 字符串 `SubjectId` vs `SubjectRef` | `StateAccess.h`、`tags/TagStore.h`、`sensing::Subject` | **P1** |
| D7 | 玩法 legacy 投影仍可写混用 | Weapon/Vehicle Resource·Health、RPG 字符串属性门面、Order `syncCompatibility` | **P2** |
| D8 | `editing::Value` 与 canonical Value 长期双树 | 大量 `using EditorValue = editing::Value` | **P2**（评估别名化 vs 保留隔离） |

> 说明：总清单 §4 勾选了「WriteResult/PatchResult 接入公共诊断」，但当前公共返回类型仍是独立 struct，**未完成 Result 形状统一**。本计划以代码现状为准，不把勾选当作完成。

### 2.3 有意分层（本计划只固化，不合并）

- Image 解码 → Graphics 上传；Font 同理。
- `Model3D`（源格式）vs Evpack CanonicalMesh（烹饪包）。
- `Camera3D` vs `CameraController`。
- `action::Ability*`（时间轴）vs `rpg::Skill*`（施法/CD）。
- `animation` 蒙皮 + FootIK vs `ik` 独立 FABRIK。

---

## 3. 分阶段计划

### P0 — 协议旁路消除（优先，可独立合入）

#### P0.1 `IStateProvider` / 热重载：`StateValue` → `eve::Value`

**做法**

1. 为 `IStateProvider::captureState` / `restoreState` 增加 Result 形状重载（或新接口 `IStateSnapshot`），payload 使用 `eve::Value`。
2. 保留旧 `StateValue` 路径一个兼容窗口：内部用现有转换器桥接，标记 owner/expiry。
3. 迁移生产 provider（至少 RPG、以及 cap 注册的其它 state providers）；测试覆盖 present/absent 与 restore 失败不部分提交。
4. 到期删除公共 `StateValue` API（或降为 `detail` + 单一转换 TU）。

**验收**

- 新 provider 不再依赖 `StateValue`。
- `ARCHITECTURE_BASE=HEAD make check/architecture-contracts` 通过。
- 热重载相关测试：成功 round-trip、失败保持原状态。

**PR 粒度**：接口 + 全部 provider + 测试，一个 PR。

#### P0.2 `WriteResult` → `eve::Result`（或薄别名）

**做法**

1. `validatePropertyValue` / `IPropertyAccess::write` 改为返回 `eve::Result<void>`（或 `Result<Applied>`），diagnostic code 复用现有 `property_access.*`。
2. 脚本绑定走统一 Result 投影；删除或私有化 `WriteResult`。
3. 更新 editor / automation consumers。

**验收**：无生产调用方再构造 `WriteResult`；契约测试断言失败码稳定。

#### P0.3 `PatchResult` → `eve::Result` + 结构化细节

**做法**

1. batch commit 返回 `Result<PatchCommitInfo>`（changedCount、revisionBefore/After 进成功值；错误进 diagnostics）。
2. 迁移 StatePatch 调用方；保留短暂兼容 facade 仅当脚本依赖且带 expiry。

**验收**：公共头文件不再导出 `bool success` 的 `PatchResult`（或明确 `@compatibility` + 移除日期）。

#### P0.4 Capability 查询面：bool/err → Result

**范围（按接口拆 PR，避免巨型变更）**

1. `IRenderCapture`（`savePng` / mask / gbuffer）
2. `ISceneQuery` 变更类方法（get/set 中需要失败原因者）
3. `IProcgenQuery` err outs
4. 与 P0.1 对齐的 `IStateProvider`

**规则**：同一接口上已有 Result 方法的，新方法必须 Result；迁移时旧 bool 方法标 `@compatibility` 并给 expiry，禁止新增 bool/err。

**验收**：每个接口至少一条契约测试覆盖失败诊断；trimmed profile 下 absent provider 行为不变。

---

### P1 — 资源与场景调用面澄清

#### P1.1 Graphics / Particles 兼容入口收缩

**做法**

1. 文档与头注释指定唯一推荐入口：
   - 纹理：`newTexture(ImageData*, TextureCreateInfo)` / `IImageResourceFactory::uploadRgba8`；文件路径：`newTextureFromFile`。
   - Particles：`tryFrom*` / `advanceTimeline`。
2. 将 `newTextureFromImageData`、`newTextureWithSampler`（若仅为薄包装）、Particles void/bool facade 标为 compatibility，脚本绑定可保留，C++ 示例与测试改走 canonical。
3. 不删除脚本入口，除非绑定测试与 examples 已全部迁移。

**验收**：`docs/usr` 与模块头文件推荐路径一致；新增测试不调用兼容入口。

#### P1.2 放置 API 所有权说明（不合并类型）

**做法**

1. 在 `Renderable3D`、`SceneNodeRef`、`Sprite3D` 头文件交叉引用「何时用谁」。
2. 增加一条小型组合测试或文档示例：层级节点改 transform 不绕过 Scene；游离 drawable 用 Renderable。

**验收**：审计清单 D5 关闭为「documented intentional」；无类型合并。

#### P1.3 网格双管线文档固化

**做法**：在 `model3d` 与 `asset`（Evpack mesh）用户文档标明 Source vs Cooked；禁止第三公共 builder。

#### P1.4 `SubjectId`（string）→ 边界使用 `SubjectRef`

**做法**

1. 跨模块 gameplay / settlement / game_event 边界新 API 只接受 `SubjectRef`。
2. `tags`、`sensing`、state store 的 string key 若保留，需在模块内转换，并文档化「存储键 ≠ 跨域身份」。
3. 逐步给高频边界加 overload，旧 string API 标 compatibility。

---

### P2 — 玩法投影与编辑值树

#### P2.1 Legacy 投影只读化

**范围**：Weapon/Vehicle Resource·Health 投影、RPG 字符串属性门面、Vehicle/RTS `syncCompatibility`。

**做法**

1. 权威写入只走 AttributeSet / Orders / Settlement。
2. 投影 API 改为 const 刷新或内部-only；脚本若仍写 legacy 字段，经 adapter 转写 canonical 并在文档声明废弃窗口。
3. 每域至少一条测试：写 canonical → 读 legacy 一致；禁止写 legacy 绕过校验（或写后立即冲突失败）。

#### P2.2 `editing::Value` 去重评估

**选项 A（优选，若无隔离硬需求）**：`editing::Value` 变为 `eve::Value` 的别名或零成本 wrapping，删除重复实现。  
**选项 B**：保留编辑隔离树，但强制边界只经现有 converter，禁止第三套编辑值类型。

**决策门禁**：列出编辑器特有不变式；若无，则选 A。单独 PR，不与 P0 捆绑。

#### P2.3 事件上报惯例

**做法**：盘点 Inventory/Building/Economy/Combat 等本地 `clear*Events` 队列，对「跨模块可观测结果」补 `GameEventLog` 发布（已有 settlement/container 先例）。不删除本地热路径队列。

---

## 4. PR 与验证矩阵

| 阶段 | 建议 PR 标题前缀 | 必跑验证 |
|---|---|---|
| P0.1 | `refactor(state): Value-backed state provider` | architecture-contracts；state/rpg 相关测试 |
| P0.2 | `refactor(property_access): Result write path` | property_access + editor 契约 |
| P0.3 | `refactor(statepatch): Result commit` | statepatch 测试 |
| P0.4.x | `refactor(cap): Result on I*` | 对应 query 测试 + absent provider |
| P1.1 | `docs+chore(graphics): canonical texture/particle APIs` | 相关单元测试；examples 抽查 |
| P1.2–P1.3 | `docs: intentional layering for scene/mesh` | 文档 + 可选组合测试 |
| P1.4 | `refactor: SubjectRef at gameplay boundaries` | 触及模块测试 |
| P2.* | 按域拆分 | 域测试 + 投影单测 |

公共命令（Linux Cloud）：

```sh
ARCHITECTURE_BASE=HEAD make check/architecture-contracts
# 模块过滤示例：
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
export ALSOFT_DRIVERS=null
export XDG_RUNTIME_DIR=/tmp/xdg-runtime
xvfb-run -a make test/linux-debug FILTER=<prefix>
```

---

## 5. 代理实施指引（防回归）

新代码默认选择：

1. 失败 → `eve::Result`，禁止新的 `WriteResult`/`PatchResult`/`lastError` 同类物。
2. 动态 payload → `eve::Value`；编辑器宿主内部可用 `editing::Value`，出边界转换。
3. 属性 → `AttributeSet` + 域 adapter。
4. 物品/座位/驻军转移 → `IContainer`。
5. 伤害/治疗 → `settlement`。
6. 纹理：CPU 用 `image`，GPU 用 Graphics / Result factory。
7. LoS/探针 → `World3D`（或既有 sensing adapter）。
8. 跨域身份 → `SubjectRef`；运行时槽位 → `RuntimeHandle`；ECS → `EntityHandle`。

合并前自检：

- [ ] 是否新增了与上表平行的公共类型/方法？
- [ ] 兼容门面是否带 expiry/owner？
- [ ] 是否更新了「有意分层」文档而非静默第二实现？
- [ ] `make check/architecture-contracts` 是否通过？

---

## 6. 成功标准（计划完成时）

1. 生产热重载与 property/statepatch 公共写路径不再暴露独立 bool 结果 struct。
2. `StateValue` 不再是新代码依赖；公共双树债务从台账关闭或降为有期限 compat。
3. Graphics/Particles 头文件与用户文档指向单一推荐入口。
4. 放置 / 网格 / IK / AI 等平行 API 均有「为何不合并」的稳定说明。
5. 无新的 universal gameplay root；短根 + Link/Handle 约定保持。

---

## 7. 建议执行顺序

```text
P0.2 WriteResult ──┐
P0.3 PatchResult ──┼─► P0.4 Capability Result 面（可并行按接口）
P0.1 StateValue ───┘         │
                             ▼
                    P1.1 Graphics/Particles
                    P1.2–P1.3 文档固化（可与 P1.1 并行）
                             ▼
                    P1.4 SubjectRef 边界
                             ▼
                    P2.1 Legacy 投影只读化（按域）
                    P2.2 editing::Value 决策
                    P2.3 GameEvent 上报补齐
```

P0.2 / P0.3 风险面较小，适合先做以建立 Result 迁移样板；P0.1 触及热重载，需单独排期与完整 provider 清单。
