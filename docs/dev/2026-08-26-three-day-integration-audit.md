# 最近三天功能与模块集成审计

日期：2026-08-26
范围：`4e53fc7c1f5035cc4565a5782f5529d6466a234c..HEAD`（453 个提交）
性质：静态架构与集成审计；未修改产品代码，运行态组合验证待后续执行。

后续修订说明（2026-08-27）：本文记录的是本轮重构前的集成基线。之后 Procgen
已完成公共入口审计：不再公开 `lastError()`、`*Owned` 或 `*Checked` 兼容命名，
改由 canonical Result/typed handle API 及 Squirrel Result 投影承载错误和生命周期。
本文对其他模块 `lastError`、裸指针或兼容 facade 的描述仍是当时的事实记录；本次
修订不改变那些模块的真实 API 状态。

## 结论摘要

最近三天的代码不是“几个新功能”，而是一次平台级扩张：模块编排、UI/presentation、编辑器、物理 3D、procgen、通用 gameplay 基础模块、EveScript、WebGPU 与 CI 同时演进。单模块实现和单元测试数量明显增加，模块依赖检查也通过；但完整系统仍处于“接口岛已建立、跨岛协议尚未收口”的阶段。

最高优先级不是继续增加功能，而是建立五个统一协议：属性与 schema、状态/快照、事件、事务、生成产物。否则目前已经出现的重复实现会快速固化为不兼容 API。

## 已确认问题

### P0：presentation 与 scriptmodel 的属性写入契约不一致

`DynamicPropertyModel` 支持 `Auto`、引用、向量、颜色、数组、Map/Struct、Action 等完整 `PropertyKind`，而 `ReflectedPropertyModel::write` 只接受 Bool/Integer/Number/String/Enum。两者都返回 `WriteResult`，但各自重复实现类型、枚举、范围和 finite 校验，并使用不同错误码前缀。

直接后果：同一个 schema 在动态 UI 中可编辑，在脚本反射 Inspector 中却可能显示为可识别类型但写入必然失败。今后任一侧新增 kind 或规则都必须人工同步。

建议：把 `validate(PropertyDescriptor, Value)` 提升为 presentation 的唯一公共校验器；adapter 只负责 `Value <-> ReflectedValue` 转换。明确数组、表、对象引用是只读还是支持深写，schema 构建时就设置一致的 flags，不应在写入时靠第二套 switch 决定。

### P0：新增 gameplay 基础模块彼此独立，但没有形成基础设施

`authority`、`definitions`、`effects`、`game_event`、`orders`、`policyregistry`、`production`、`transaction` 等模块分别实现了：字符串 ID、序列号、事件队列、JSON payload、`snapshotJson/restoreJson`、错误状态和脚本对象生命周期。依赖清单中这些模块几乎全是 L0 且互不依赖，现有业务模块也几乎没有消费它们。

这保证了编译独立，却造成语义重复：每个系统都有自己的事件、序列、快照边界、ID 分配和恢复规则。编辑器同时又有一整套 `EditorTransactions`、`EditorTransactionService`、`IEditAuthority` 和强类型 ID；新增通用 `transaction`/`authority` 并未与其合并。

直接后果：一次“procgen 生成地形并创建物理碰撞、在 UI/编辑器中可撤销”的操作无法成为一个原子事务；保存游戏需要逐模块拼接快照；跨模块因果链与回放无法统一。

建议：不要让业务模块互相 include，而应在 common/presentation 一侧定义窄协议：`EntityRef/StableId`、`EventEnvelope`、`SnapshotParticipant`、`TransactionParticipant`。现有模块可保留自己的 store，但统一 envelope、correlation/causation ID、版本和提交/回滚语义。编辑器事务作为第一个真实 consumer 接入通用事务协议，而不是立即删除成熟的编辑器实现。

### P1：procgen 仍是“生成算法 + 渲染上传 + 地图适配”的聚合模块

manifest 中 procgen 直接依赖 `graphics image map`，`Procgen.cpp` 也直接 include Graphics/Mesh/Texture/ImageData，并集中注册大量算法和脚本绑定。新的城堡、六边形地形、城市、纹理/PBR recipe 都继续进入这一聚合边界。

直接后果：服务器、工具链、离线烘焙或最小配置不能只使用纯生成算法；map 与 procgen 的归属反转困难；每加一种 output 都会扩大 Procgen 主模块和脚本 facade。`Procgen.cpp` 已承担注册、默认 palette、算法 facade、GPU 资源构建和 binding 多种职责。

建议拆成三层但保持一个用户 facade：

1. `procgen-core`：seed、Params、PointSet、Grid、recipe、deterministic build key、CPU 产物；仅依赖 data/grid/math。
2. `procgen-adapters`：map/voxel/scene 的产物适配，通过 capability 注册。
3. `procgen-render`：Mesh/Texture 上传与预览，可选依赖 graphics/image。

统一产物应是带 `type/schemaVersion/buildKey/dependencies/bounds` 的 `GeneratedArtifact`，而不是每个算法直接返回不同的裸对象。

### P1：physics 的 simulation 与 rendering/GPU 生命周期没有分离

manifest 中 physics 必需依赖 `graphics` 和 `gpgpu`，同时 scene 只是 optional。ClothGPU、DistanceField、Fluid、debug/render bridge 与 Box2D/box3d 世界都处在一个模块边界。

直接后果：纯物理服务器和确定性仿真仍被迫带入渲染栈；WebGPU profile 中“physics 可用”并不等于所有 GPU 路径语义一致；CPU/GPU cloth、surface fluid、collision world 的 timestep、ownership 和 fallback 很难由统一 scheduler 管理。

建议拆分 `physics-core`（world/body/shape/joint/query/fixed-step）、`physics-gpu`（cloth/SDF compute）和 `physics-render`（debug draw/surface reconstruction）。通过 capability 提供 GPU accelerator 和 debug renderer；core 必须能在两者缺失时完整工作。统一 fixed-step、单位、坐标系、对象 ID 和 scene-link 生命周期。

### P1：CI 强化了单模块与后端覆盖，但缺少组合契约矩阵

本次 CI 增加了 WebGPU 原生 parity、浏览器截图、fuzz、缓存以及大量测试注册，方向正确；`module_depgraph.py --check` 当前通过，无新增 back-edge。

缺口是测试仍主要按源文件/单 case 注册。检索不到 UI↔physics、UI↔procgen、procgen↔physics 的组合测试；现有跨系统测试主要是 map+procgen 或 fluids+physics。完整 unit_test 还链接 `${EVE_MODULE_LIBS}`，因此“全量构建成功”无法证明裁剪 profile 的公开 header、模块启动和 runtime lookup 真能独立工作。

此外，binding gap 文件把 `procgen:getBool/setBool` 留在 allowlist，说明 API 文档门禁可以通过维护例外绕过；这类例外应有 owner、原因和失效日期。

建议新增四类门禁：

- profile configure/build/smoke：minimal、2d、3d、web，以及 procgen-core-only、physics-core-only。
- contract tests：PropertyModel adapter parity、Snapshot round-trip/version rejection、Event ordering、Transaction rollback。
- integration journeys：procgen→scene/map→physics→UI inspector；编辑器修改→撤销→热重载→保存恢复。
- allowlist budget：新增 gap 禁止增长，旧条目绑定 issue/owner/expiry。

### P1：模块层级表达与实际职责不完全一致

`editor` 声明出现在 L0 区段但标记为 LAYER 6，且行首有异常缩进；`scene` 位于“L5/L6 aggregates”区段却标记为 LAYER 1。检查器只验证 back-edge，无法发现这种文档分区和声明语义漂移。

建议让 manifest 按真实 layer 排列，并增加 lint：声明所在 section 必须匹配 `LAYER`；每个 optional dependency 必须有 capability/空缺行为测试。

### P2：错误模型与脚本 API 形态仍不统一

新 API 混用 bool + `lastError`、throw `Exception`、nullable raw pointer、`WriteResult` 和字符串 JSON。大量 `newX()` 返回由模块内部容器持有或由脚本 VM 管理的裸指针，但所有权从函数签名不可见。

建议定义脚本边界规范：命令返回结构化 Result；查询返回 optional/空对象；参数错误统一抛出或统一 Result（二选一）；对象句柄包含 owner/generation，热重载和 restore 后旧句柄必须可检测失效。

### P2：提交质量门禁有小范围退化

三日 diff 的 `git diff --check` 报告 patch、文档、example 和 devtools README 中存在 trailing whitespace/行尾格式问题。它们不是系统逻辑错误，但说明集中合并时 changed-lines 格式门禁没有完全覆盖所有文件类型。

## 尚不能宣称的问题

- 依赖图检查通过，因此目前没有证据说明新增了 C++ 反向 include 边。
- 单元测试注册大幅补齐，不能简单判断“没有测试”；问题是缺少跨模块 journey 和裁剪矩阵。
- 本轮没有现成 build 目录，尚未运行完整 Linux/Vulkan/WebGPU suite；GPU timestep、资源销毁顺序、热重载后悬空句柄等属于高风险待验证项，不应写成已发生 bug。

## 推荐整合路线

### 第一阶段：冻结共享 API，先写契约（1 个整合 PR）

冻结新增 L0 gameplay API；确定 StableId、Result/Error、EventEnvelope、SnapshotHeader、TransactionParticipant、GeneratedArtifact 六个最小协议。先写 adapter 和 contract tests，不大规模搬代码。

### 第二阶段：消除已确认分叉（2–3 个 PR）

合并 PropertyModel 校验；让 editor transaction/authority 适配通用协议；给各 snapshot 加 schema/version/module/build identity；将事件接到统一 envelope，保留模块内 event kind。

### 第三阶段：切开重模块（分别独立 PR）

先拆 procgen-core/render/adapters，再拆 physics-core/gpu/render。每次拆分以 profile smoke 作为完成条件，不以“文件移动后全量 build 通过”为完成条件。

### 第四阶段：建立黄金组合路径

选择一个小场景作为系统验收：固定 seed 生成六边形地形，产出 mesh/collider，创建 physics world，UI/presentation inspector 修改参数，编辑器事务可撤销，保存后恢复得到相同 build key 和 physics query 结果。Vulkan 与 WebGPU 至少验证同一 backend-neutral 可观察结果。

## 讨论时建议先决定的四件事

1. procgen 的核心产物究竟是通用 artifact，还是 map/mesh 等具体对象？
2. 通用 transaction 是游戏运行时账本，还是也要成为 editor undo/redo 的底座？
3. physics 的 GPU cloth/fluid 是 physics 加速器，还是独立 simulation product？
4. 【已决议】脚本 API 以 Result 为主；Procgen 不再保留公共 `lastError`，其他模块按
   各自的真实迁移状态单独处理。

这四个决定会影响模块边界，应该先定，再分派独立开发任务。
