# EVEngine 架构整合整改总清单

日期：2026-08-26
状态：实施中（第二轮补缺与最终集成验证）
来源：最近三天集成审计，以及 schema、身份、玩法领域模型的后续讨论。

## 目标与边界

本轮整改的目标不是创建一个包办所有玩法的 `GameplayModule`，也不是把所有模块合并。目标是统一跨模块必须一致的协议，同时保留 RPG、Card、Dialogue、RTS、Weapon、Vehicle、Building、Procgen、Physics 各自的领域实现。

基本原则：

- 统一协议，不统一业务规则。
- 领域实体采用短根继承；正交能力使用组件；跨领域关系使用强类型 Link/Handle。
- 不建立统一 GameObject、GameplaySubject 或 GameplayActor 大根类。
- 持久身份、逻辑名称、运行时句柄必须分开。
- 领域事件保留强类型 payload，只统一 envelope。
- 核心仿真不依赖渲染；渲染、GPU、编辑器通过 adapter/capability 接入。
- 先用两个真实模块验证公共抽象，再稳定为公共 API。
- 设计出来的系统必须接入恰当的生产边界并替代重复路径；只有定义和单元测试不能算完成。
- 每项迁移都必须包含裁剪构建、契约测试和至少一条端到端组合路径。
- 所有重构必须遵守 `docs/dev/重构代码质量与系统完整性规范.md`；Result 和关键返回值另遵守 `docs/dev/Result检查与不得丢弃返回值规范.md`。

## 全阶段质量门禁

执行证据：`make check/architecture-contracts`（catalogue、增量 API 形状 lint、
fixture）与 `make check/quality`（债务净增长、profile、`[[nodiscard]]`）。当前
仍以逐项证据为准；门禁存在不等于每个业务模块已经完成迁移。

- [x] 禁止新增含混 `bool`、`nullptr/false + lastError`、无所有权说明的裸指针或可静默丢弃的关键返回值。
- [x] 公共 API 按统一语义矩阵选择返回类型：即时必有借用用引用，正常缺失用可选引用，需要失败原因用 `Result`，跨帧身份用 generation handle，所有权转移用 owning value/`unique_ptr`；裸指针仅限明确边界或局部即时 observer。
- [x] 多态边界不改变生命周期规则：即时动态派发返回接口/短根基类引用，跨帧以 handle 解析为接口引用，多态所有权用 `unique_ptr<Base>`；禁止基类按值返回造成 slicing、用裸指针表达多态所有权或建立第二套 `Base*` 对象索引。
- [x] 字符串 API 按统一语义选择：输入和静态名称用 `string_view`，独立输出用 owning `string`，缺失用 `optional`，失败用 `Result`，二进制用 `span<byte>`；`const char*` 仅留在明确的 C/第三方/NUL 边界。
- [x] 不可信数据与有顺序约束的流程使用不可伪造 typestate：unchecked/parsed/validated/resolved/prepared/committed 分型，转换消费旧状态并返回 `Result<Next>`；未完成状态不能访问后续 API，失败不得部分发布。
- [x] 每个独立 module 证明职责、权威状态、生命周期、依赖方向和真实裁剪价值；总是共同启用且一对一耦合的模块合并，只有文件组织需求时只拆 TU/子目录，宽泛或误导名称直接整改。
- [x] 将当前 transient `event` 与 deterministic `game_event` 直接重命名为 `platform_event` 与 `game_event`，更新 manifest/namespace/bindings/consumer；内部以 `GameEventLog` 表达持久序列，并严格区分尚未执行的 `GameCommand` 与已经发生的 `GameEvent`。
- [x] 每个跨域 Link 明确创建、所有权、双向销毁顺序、restore/hot reload 重建和 stale 检测。
- [x] 每份可变状态指定唯一权威 owner；其他模块只能持有有失效协议的缓存、投影或 Link。
- [x] 每个新 ECS System 声明类型范围、View、read/write set、结构变化、事件、服务和执行阶段。
- [x] 仿真时间和 RNG 由 context 注入；需要 replay/backend parity 的路径声明确定性等级与误差契约。
- [x] 公共 API 标注 thread affinity、重入、所有权和生命周期；禁止持锁调用未知 callback 或脚本。
- [x] 持久格式具有 schema/version/migration/unknown-field policy；restore 失败保持原状态。
- [x] optional capability 同时测试 present/absent；fallback 必须可观测、分类并有契约。
- [x] 多 backend/provider 运行共享 contract tests；关键失败路径使用 failure injection。
- [x] 每个新增系统有真实 producer、consumer、生产调用入口和集成测试，并已收敛原有重复实现。
- [x] soft skip、fallback、allowlist、TODO/HACK 具有 owner、issue、原因和移除条件，CI 禁止无预算增长。

## P0：立即冻结并统一的基础契约

### 1. 身份、UUID 与运行时句柄

- [x] 将 `editor::StrongEditorId` 的通用能力下沉到 `common/Identity.h`，保留 Tag 强类型。
- [x] 定义 `PersistentId`：用于跨进程、存档、网络和机器边界的稳定身份。
- [x] 默认持久身份生成策略采用 UUIDv7；提供 parse、validate、format、hash 和 nil 检查。
- [x] 定义 `LogicalId`：使用 `namespace:name` 表达人类可读的 schema、recipe、definition、policy 和 action 名称。
- [x] 定义内容确定性身份 `ContentId`，使用 Hash128 或 namespace UUIDv5，禁止与实例 UUID 混用。
- [x] ECS 实体直接复用现有 `ecs::EntityHandle`、generation、`try_get` 和 stale 检测，不另造 ECS handle。
- [x] 定义 `RuntimeHandle<Tag>{index,generation}`，仅用于 Physics、GPU、UI 等非 ECS registry 的进程内高频对象。
- [x] 不把 Physics/ECS/GPU 的整数/generation handle 全量替换为 UUID。
- [x] 明确持久对象可同时拥有 `PersistentId` 与当前 world 的运行时句柄：ECS 对象用 `ecs::EntityHandle`，非 ECS registry 用对应 `RuntimeHandle<Tag>`。
- [x] 将 `AssetGuid`、`DocumentId`、持久 Scene ObjectId、ArtifactId、EventId、TransactionId 逐步迁移到 UUID-backed strong ID。
- [x] SDL Joystick GUID 包装为 `GamepadGuid`，保持 SDL 协议值，不重新生成 UUID。
- [x] 统一字符串 ID 的 namespace 规则，停止各模块继续生成无作用域的 `task-0001`、`effect-0001` 等跨边界 ID。

验收标准：编译期禁止 AssetId/ObjectId/TransactionId 互传；持久 ID 可稳定 round-trip；运行时 handle 能检测 stale generation。

### 2. Version、Generation、Revision、Sequence、Tick

- [x] 定义强类型 `SchemaVersion`、`Generation`、`Revision`、`EventSequence`、`SimulationTick`、`FrameIndex`。
- [x] 明确 `SchemaVersion` 表示数据格式，`Generation` 表示替换后句柄失效，`Revision` 表示内容变化。
- [x] 明确 `EventSequence` 只在单一 stream 内排序，不承担全局身份。
- [x] 明确 `SimulationTick` 是确定性逻辑时间，不得直接使用 wall clock 替代。
- [x] Definition、Policy、Editor、Procgen、Physics 中现有裸 `uint64_t` 逐步标注并迁移。
- [x] 为 overflow、restore 后单调性和 stale handle 添加公共测试。

### 3. 通用 owning Value 与 Payload

- [x] 在 common/data 层确定唯一 owning dynamic `Value`：Null、Bool、Int64、Double、String、Array、Object。
- [x] Object 使用确定性 key 顺序；明确 NaN/Infinity 的拒绝或编码策略。
- [x] 提供 JSON、Squirrel、StateValue、EditorValue、presentation value 的双向转换。
- [x] `presentation::Value` 迁移到公共 Value 或成为零成本别名。
- [x] EffectsPayload 与 OrderPayload 改用 `Value::Object`，移除重复的 canonical JSON fragment map。
- [x] Procgen `Params` 对外提供 typed setter/getter，内部使用 owning
      `Value::Object` 保留 bool/int/double/string 类型；确定性 build key 包含类型标签，
      不再把 bool/number 编码为字符串。
- [x] Event payload、snapshot payload、definition metadata 使用同一 Value 基础。
- [x] 不在公共 Value 中加入 Graphics、Physics、glm 或编辑器专属类型。

验收标准：Value 在 JSON/Squirrel/Editor/State 四条路径 round-trip 后类型不丢失；确定性序列化 hash 稳定。

### 4. Result、Error 与 Diagnostic

- [x] 以 `docs/dev/Result检查与不得丢弃返回值规范.md` 作为 Result 和返回值检查的正式规范。
- [x] 定义公共 `Result<T>`、`Status`、`Diagnostic`、`DiagnosticCode`、`Severity`。
- [x] Result 类型标记 `[[nodiscard]]`，直接丢弃在编译期产生诊断。
- [x] Debug 下 Result 持有 `observed/mustObserve` 检查责任；未检查即析构时触发 `EV_ASSERT`。
- [x] `ok/status/value/error/diagnostics/match` 等访问算作检查；提供显式 `ignore(reason)` 和 `expect(message)`。
- [x] Result 默认 move-only；移动转移检查责任并解除 moved-from，移动赋值不得覆盖未检查目标。
- [x] Release 编译掉 observation 字段与分支，不引入运行期开销。
- [x] 统一 Applied/NoOp/Pending/Rejected/Conflict/NotFound/Unsupported/Cancelled/Failed 等状态。
- [x] Diagnostic 至少包含稳定 code、message、path 和结构化 details。
- [x] EditorResult 迁移为公共 Result 的 adapter 或别名，保留编辑器诊断扩展。
- [x] Property WriteResult、Schema ValidationError、StatePatch PatchResult 接入公共诊断结构。
- [x] Procgen、Registry、Orders、Effects、Transaction 新 API 不再增加 `bool/nullptr + lastError`。
- [x] Procgen 公共 API 已移除 `lastError()`、`*Owned`、`*Checked` 兼容入口：C++
      使用 `Result<...HandleRef>`，Squirrel 使用基础方法名并返回统一 Result 投影。
      其他模块仍真实存在的 `lastError` 由各自模块单独记录，本条不宣称其已删除。
- [x] 约定业务可恢复失败返回 Result；参数违约、stale handle 和内部 invariant 使用 Exception/assert。
- [x] ScriptDiagnostic 保留 source URI、line、column，通过组合公共 Diagnostic 接入。
- [x] C++→Squirrel 绑定层完成结构化投影时消费 C++ Result；脚本侧保留显式 `ignore()`。
- [x] 审计非 Result 返回值，为 Entity/Resource handle、UUID、Subscription、scope/token 等添加函数级 `[[nodiscard]]`。

验收标准：脚本和 C++ 调用方无需读取共享 `lastError` 就能定位失败；错误码可用于 UI、本地化和 CI 断言。

### 5. Schema 底座

- [x] 保留 `schema::SchemaDefinition` 作为底层 DataSchema，不强行用 PropertySchema 替代。
- [x] SchemaRegistry key 从 `id` 改为 `(schemaId, schemaVersion)`，允许多个版本共存。
- [x] 增加 schema migration chain 和兼容性查询。
- [x] 增强嵌套 object、array item schema、引用、union/discriminator 能力；控制范围，不必一次实现完整 JSON Schema 标准。
- [x] 动态 Definition 与 Policy metadata 在真实写入边界按已注册 `(schemaId, version)` 校验；强类型 Snapshot/Event/Artifact codec 保持唯一准入真源，不重复维护 Schema 字段表。
- [x] 为 schema 生成文档和 binding contract，减少手写 allowlist。
- [x] binding gap allowlist 增加 owner、issue、原因和 expiry；禁止净增长。

## P0：消除已经确认的重复实现

### 6. PropertyAccess 与 Editor PropertySchema

- [x] 把 PropertyDescriptor 类型、enum、范围和 finite 校验集中到属性模型的唯一公共校验器。
- [x] `ReflectedPropertyModel::write` 不再维护第二套 kind switch。
- [x] 明确 Array、Map、Struct、ObjectRef 的读写能力，在 schema flags 中表达，不在 adapter 中隐式拒绝。
- [x] Editor PropertySchema 变成公共 property schema + 编辑器扩展，而不是复制 PropertyType、PropertyFlag 和 NumericMetadata。
- [x] 保留 Editor 特有能力：Transform、Replicated、asset filter、validator RuleId、多选 Mixed、Relative/Reset。
- [x] 同一 schema 在 runtime UI、editor inspector、automation host 和 script reflection 上执行 parity tests。
- [x] 将职责不明确的 `presentation` module 一次性重命名为 `property_access`，namespace 改为 `eve::property_access`，`IPropertyModel` 改为 `IPropertyAccess`，错误码改为 `property_access.*`，同步更新 manifest、所有 provider、consumer、测试与文档。
- [x] 删除仅做别名的 `presentation/Value.h`；`property_access` 直接使用 `common/Value.h`，且不把属性 schema、flags、校验或访问端口状态下沉到 common。
- [x] 删除只有测试 consumer 的公开 `DynamicPropertyModel`，或先证明至少一个生产 authority 真正使用；契约测试使用局部 fixture，禁止以 mock 证明系统落地。
- [x] 用契约测试证明 `IPropertyAccess` 只允许 schema 声明字段，不能充当任意对象的临时 `PropertyBag` 或第二权威状态。
- [x] 保留直接 reflection API；仅 UI/automation 等需要同时消费 reflection 与 editor transaction authority 的运行时多态边界使用 `IPropertyAccess`。

### 7. RPG AttributeSystem 与 attributes

- [x] 以独立 `attributes` 模块作为属性核心，吸收 RPG 已成熟的 modifier 计算规则。
- [x] 统一 AttributeModifier 的 id、attribute、source、priority、sequence 和 operation 表达。
- [x] 明确定义 base add、additive percent、multiplicative percent、override、clamp 的顺序与语义。
- [x] 内建 operation 使用 enum；自定义 operation 通过 PolicyId/注册接口扩展。
- [x] 将 RPGActor 属性迁移到 AttributeSet，保留兼容 facade。
- [x] 后续选择性接入 Card stats、Vehicle health/armor、Weapon mana/stamina、RTS unit stats。
- [x] 不把位置、物理速度、动画帧等所有数字都改造成 attribute。

### 8. RPG Status/Effect 与 effects

- [x] 统一为 `EffectDefinition`、`EffectInstance`、`EffectContainer`、`EffectExecutor` 四层。
- [x] 通用 effects 负责实例、subject/source、时限、stack 和生命周期事件。
- [x] RPG EffectDefinition 的 modifier、period、tags、metadata 下沉为通用 definition schema。
- [x] 将 stack count、duration refresh、magnitude、overflow 拆成独立策略维度。
- [x] RPG StatusSystem 适配通用 EffectContainer；保留 RPG tick/settlement executor。
- [x] Card、RTS、Vehicle、Weapon 后续复用相同生命周期，但保留各自 effect executor。

### 9. VehicleOrder 与 orders

- [x] Vehicle 不再维护独立队列生命周期，改用通用 OrderQueue。
- [x] Vehicle 保留 Move/AttackMove/Attack/Stop/Hold 强类型 adapter 和 executor。
- [x] 通用 orders 只负责 queue、priority、replace、interrupt、timeout、cancel 和状态转换。
- [x] Build/Gather/PlayCard 等领域 order 由各自模块注册 codec/executor，禁止塞入 orders core。
- [x] 统一 order 事件 envelope、失败原因和 transaction correlation。

## P1：统一跨模块生命周期和基础设施

### 10. EventEnvelope

- [x] 以现有 GameEvent envelope 为基础，增加全局 EventId、schemaId/schemaVersion。
- [x] 统一 source、subject、correlation、causation、tick、flags、stream sequence。
- [x] Authority、Definitions、Effects、Orders、Production、StatePatch、Transaction 事件接入统一 envelope。
- [x] 领域事件继续使用强类型 payload，不改成无类型 JSON 大杂烩。
- [x] 定义 `EventId = UUIDv7`，`EventSequence = stream-local uint64`。
- [x] CorrelationId 表示一条业务链，CausationId 指向直接导致当前事件的 Event/Command。
- [x] 加入事件顺序、恢复、截断和跨模块因果链测试。

### 11. Transaction 协议与 Editor Undo/Redo

- [x] 定义 `ITransactionParticipant::prepare/commit/rollback` 和 TransactionContext。
- [x] 通用事务负责原子提交、participant 生命周期、correlation/causation。
- [x] Editor transaction 在其上保留 command history、undo/redo、compensation、dry-run 和 authority preflight。
- [x] 不在第一阶段直接删除或替换成熟的 EditorTransactions。
- [x] 让 editor 成为通用 transaction 协议的第一个真实 consumer。
- [x] 建立 procgen artifact + physics collider + scene node 的跨模块原子提交案例。
- [x] 明确 rollback 与 compensation 的区别：未提交用 rollback，已产生外部效果用 compensation。

### 12. SnapshotEnvelope 与 migration

- [x] 定义统一外层：type、schema、schemaVersion、instanceId、revision、tick、contentHash、payload。
- [x] createdAt 等 wall-clock metadata 不参与确定性 hash。
- [x] Authority、Definitions、GameEvent、Transaction、StatePatch、Production 等 snapshot 迁移到公共 envelope。
- [x] 各模块只负责 payload 和 migration，不再重复解析 version/header。
- [x] snapshot restore 默认事务化；失败不得部分修改现有状态。
- [x] 建立旧版本迁移、未知新版本拒绝、hash 不匹配和跨 backend 恢复测试。

### 13. VersionedRegistry

- [x] 从 Definitions 和 PolicyRegistry 提取通用 `VersionedRegistry<Key,Value>`。
- [x] 统一 insert、replace、remove、resolve、generation-qualified handle 和 tombstone 语义。
- [x] 统一注册冲突、替换、删除和 stale handle 的 Result/事件。
- [x] Definitions 与 PolicyRegistry 先迁移，RecipeRegistry 作为第二批评估对象。
- [x] UI ObjectRegistry、GPU registry 等高频 slot map 不迁移到持久化 registry。

### 14. ResourceRef、AssetRef 与 URI

- [x] 区分文件路径、虚拟 URI、Asset UUID、Definition LogicalId、Scene Object UUID 和临时资源名。
- [x] 定义 `AssetRef`、`ResourceUri`、`DefinitionRef`、`ObjectRef`。
- [x] 统一 URI scheme：`asset://`、`project://`、`builtin://`、`generated://`、`memory://`。
- [x] 连接 Editor AssetDatabase、Filesystem、Procgen Artifact 和 Graphics loader。
- [x] 资产重命名保持 sidecar UUID，不以当前路径作为唯一身份。

### 15. Time、Tick 与 Duration

本切片已接入 Particles、Effects、Animation 的 `SimulationStep` checked 消费者；Physics 仍由独立代理负责，replay 当前没有可安全覆盖的独立 runtime consumer。

- [x] 统一 SimulationTick、FrameIndex、Duration、monotonic timestamp、wall-clock timestamp。
- [x] Physics、Particles、Effects、Production、Animation 和 replay 使用明确 time source。
- [x] 暂停、慢动作、回放和 fixed-step 由 scheduler 注入时间，不让模块私自读取墙钟。
- [x] 存档中的确定性状态使用 tick，不使用 wall clock。
- [x] 明确 CPU/GPU backend 对相同 tick/dt 的可观察误差范围。

### 16. Subscription 与 Observer 生命周期

本切片新增 Event poll listener 与 AnimClipRegistry reload listener 两个真实 consumer，并统一异常隔离和回调内增删语义；PropertyModel、Definitions、PolicyRegistry 的既有接入继续有效。

- [x] 将 presentation 的 move-only RAII Subscription 下沉为公共实现。
- [x] Registry、PropertyModel、asset changes、definition reload、event listeners 统一使用 Subscription。
- [x] 回调派发允许订阅者在 callback 中安全增删订阅。
- [x] Physics 高频 contact event 可以保留 batch/poll，但订阅生命周期使用相同 token。

### 17. 对象所有权与脚本句柄

- [x] 为脚本 API 规定 Value、Owned、Borrowed 三种对象语义；`Owned` 表示所有权
      语义，不表示带 `Owned` 后缀的第二套兼容方法。
- [x] 创建 API 明确 owner 和释放时机；Procgen 的 Squirrel `newParams/newGrid/`
      `newOutput/newPointSet` 等基础方法返回 Result，其 `value` 是由 generation
      handle 支持的 owned proxy，C++ 对应 `...Handle` API。
- [x] world-owned ECS 对象使用 `ecs::EntityHandle`；非 ECS registry 对象使用对应 generation handle；不以长期裸指针表达身份。
- [x] restore、hot reload、module unload 后旧句柄必须能检测失效。
- [x] C++ 中优先 unique_ptr；仅真实共享生命周期使用 shared_ptr。

本次切片已落地 common `SquirrelOwnership`、UI `ObjectRegistry` 的 ownership
helper，以及 `RuntimeHandle + generation + ownerEpoch` 对 release、registry
clear 和 module instance reload 的 stale 检测。Procgen 的创建/生成脚本入口已经
收敛为基础方法名 + Result 投影；其中 `Owned` 只描述返回 proxy 的所有权语义。
其他模块的创建入口与历史兼容状态仍按各自模块的真实实现单独审计，不能据此
宣称全仓库已完成。

## P1：Procgen 与 Physics 分层

### 18. GeneratedArtifact

- [x] 定义 `GeneratedArtifact`：ArtifactId、type、schemaVersion、buildKey、bounds、dependencies、metadata、payload。
- [x] Artifact payload 使用强类型 variant：Grid、PointSet、MeshData、ImageData、Collider、Composite。
- [x] JSON 只承担扩展 metadata，不承载所有高频几何数据。
- [x] 支持一次生成产生 mesh、collider、topology、anchors 等 CompositeArtifact。
- [x] 区分 ArtifactId 与 buildKey：前者是实例身份，后者表示确定性输入/内容等价性。
- [x] 先迁移 hex terrain 与 castle 两条新路径，验证后再扩展城市/PBR/纹理 recipe。

### 19. Procgen 内部职责边界

- [x] 保持公开 `eve.procgen` 模块入口和常用脚本方法名稳定；方法返回契约统一为
      Result 投影，同时在内部建立 core、adapter、render 三个职责边界。
- [x] core：seed、Params、PointSet、Grid、recipe、build key、CPU artifact；算法实现不 include graphics/map/scene。
- [x] adapters：map、voxel、scene 的 artifact 转换，通过 capability 注册。
- [x] render bridge：Mesh/Texture 上传与预览，可选依赖 graphics/image。
- [x] `Procgen` 便利层直接使用 canonical Result/handle API，明确执行“生成 owning
      CPU data → adapter/upload”的两阶段流程；不保留 `lastError`、`*Owned`、
      `*Checked` 的第二套公共入口。
- [x] `Procgen.cpp` 拆分注册、默认 palette、算法 facade、上传和 binding 职责。
- [x] PointSet/Grid/MeshData 等中间数据保持紧凑普通数据，不逐点 ECS 化。
- [x] Artifact 发布后才按需创建 GeneratedTerrain/Building/ResourceNode 等领域 ECS 实体，并保存 ArtifactRef/BuildKey。
- [x] 增加 procgen-core-only 配置、构建和确定性测试。

### 20. Physics 内部职责边界

- [x] 保持公开 `eve.physics` 模块不变，先在内部建立 domain、accelerator、presentation 三个职责边界。
- [x] domain core 的显式源列表包含 World/Body/Shape/Joint/query/fixed-step；该边界不依赖 graphics/gpgpu。
- [x] 现有生产 GPU cloth 与 GPGPU surface fluid 接入 `ISimulationBackend`；`MockAccelerator` 仅限 contract test。
- [x] presentation bridge 保留 debug draw、cloth mesh、fluid surface reconstruction，并与 solver 分离。
- [x] CPU/GPU backend 共享 `ISimulationBackend` 的 tick/settings/observation/failure contract。
- [x] 在 `docs/dev/物理系统分层与后端契约.md` 中记录 fallback、确定性、误差基线、迁移和 snapshot 行为。
- [x] Surface fluid 的 simulation、surface constraint 和 screen-space reconstruction 分离。
- [x] Physics solver/broadphase/manifold 继续使用专用数据结构，不以 ECS 取代。
- [x] Body/Shape/Joint 的非 ECS 身份使用各自 typed runtime handle registry，通过 solver handle 连接后端。
- [x] 玩法实体通过 PhysicsLink/PhysicsShapeLink/PhysicsJointLink 连接 Physics 对象，不继承 Physics 领域类型。
- [x] 增加 physics-core-only/headless/server profile 的独立源码边界检查。
- [ ] 在 GCC/Vulkan 独立构建中执行 CPU/生产 GPU parity、provider present/absent、snapshot round-trip/failure-injection 和 runtime smoke。

当前代码整改已落地，但最后一项仍需正式工具链和可运行依赖验证；未以静态检查代替运行时验收。

## P1：玩法领域公共机制

### 21. GameplayAction Pipeline

- [x] 定义通用 ActionDefinition、ActionRequest、ActionExecution 和 ActionPhase。
- [x] 标准阶段：Requested、Validating、Windup、Active、Recover、Completed/Cancelled/Failed。
- [x] Action 组合 Condition、Cost、Targeting、Effect，不直接硬编码 RPG 或 RTS。
- [x] RPG Skill 与 Weapon Attack 作为首批两个 adapter。
- [x] Card Play 后续复用条件、成本和效果；卡牌容器移动仍由 Card/Container 处理。
- [x] Dialogue Command 可作为即时或异步 executor；Line/Choice/Branch 不迁入 Action。
- [x] Weapon 保留弹匣、散布、后坐、炮口和弹丸等领域字段。

### 22. Condition / Rule / Expression

- [x] 定义无副作用 Condition AST：All、Any、Not、Compare、HasTag、HasAttribute、HasResource、StateEquals、AuthorityCheck、PolicyCall。
- [x] 统一 ConditionResult：passed、稳定 reason code、evidence/details。
- [x] RPG Skill、Card Play、Dialogue Choice、RTS Command、Building Placement 共享条件结果协议。
- [x] UI 使用同一结果展示“为什么不可用”。
- [x] Script condition 声明依赖和确定性等级；禁止条件求值偷偷修改状态。
- [x] Authority、Policy 和业务条件保持不同领域语义，通过 Condition node 调用，不直接合并实现。

### 23. TargetRef、TargetSet 与 TargetingSpec

- [x] 定义 SubjectRef、WorldPoint、WorldArea、ZoneRef 等目标值类型。
- [x] TargetSet 支持 primary、多个 subjects、point 和 area。
- [x] TargetingSpec 描述 domain、数量、range、tag query、line-of-sight 等约束。
- [x] Sensing 提供候选；Physics/Scene 提供空间查询 capability；Authority 提供许可。
- [x] 具体技能、武器、卡牌和 RTS 命令保留目标选择算法。
- [x] 禁止 targeting core 直接依赖 physics、scene、UI。

### 24. ResourceAccount 与 Cost

- [x] 定义 ResourceId、Amount、CostSpec、Reservation/Receipt 和 `IResourceAccount`。
- [x] 统一 canAfford、reserve、debit、credit、commit、rollback 语义。
- [x] Reservation credential 携带 common 分配的 opaque AccountNonce；同 id/cost 的同类型及跨 adapter 账户不可互相提交/回滚。
- [x] 首批 AttributeSet 与 EconomyLedger adapter 及 ResourceDebitParticipant 已接入；后续玩法 adapter 仍按消费者迁移。
- [x] Mana/Stamina adapter 接 AttributeSet；Ammo 接 Weapon/Inventory；Card mana 接玩家账户；RTS 资源接 EconomyLedger。
- [x] Item cost 接 Inventory；Dialogue 金钱/声望通过相应 account adapter。
- [x] 不强迫弹匣、经济资源和属性存入同一个 map。
- [x] 所有成本支付纳入 Transaction，避免部分扣费。

### 25. Container、Zone 与 Transfer

- [x] 定义 ContainerId、capacity、ordering、slot、filter、membership 和事务化 transfer 协议。
- [x] Card Deck/Hand/Discard、Inventory Bag/Equipment、Vehicle Seat、Building Garrison 作为 adapter。
- [x] Deck draw/shuffle 保留在 Card；Hand 扇形布局、hover、drag 保留在 presentation/UI。
- [x] 定义逻辑 Zone：shape、coordinate space、accepted condition、capacity。
- [x] 支持 Screen、World2D、World3D、Grid 坐标空间，禁止混用。
- [x] 统一 enter/exit/accepted/rejected 事件 envelope。

### 26. Settlement：伤害、治疗与状态结算

- [x] 定义 SettlementRequest：source、target、kind、magnitude、tags、context。
- [x] 定义 SettlementResult：requested、applied、absorbed、resisted、critical、stage results。
- [x] 定义可组合 pipeline：validate、source modifiers、target mitigation、armor/shield、clamp、apply、event、trigger。
- [x] RPG 提供暴击/抗性/元素 policy；Vehicle 提供装甲区/穿深/入射角；Card 提供护甲/屏障/死亡触发。
- [x] Weapon 只产生 SettlementRequest，不直接修改目标 health。
- [x] Effects 周期伤害/治疗走同一 pipeline。
- [x] RTS 可选择 RPG、Vehicle 或自定义 settlement policy。

### 27. 领域短根继承与跨域组合

- [x] 以 `docs/dev/领域短根继承与跨域组合架构.md` 作为实体建模和继承评审规范。
- [x] 不创建统一 GameObject、GameplaySubject 或 GameplayActor 大基类。
- [x] RPGActor、CardData/Deck/Hand/Zone、VehicleEntity、WeaponEntity、SceneObject、Renderable、UIHost 等保留独立领域短根。
- [x] 领域根通常保持 2–3 层稳定继承；Base 必须对应真实 `View<Base,...>` consumer。
- [x] Derived 必须满足 Base 的查询闭包；Base 全部组件对所有 Derived 必须始终有意义。
- [x] 类型表达“是什么”，组件表达 Attributes/Effects/Orders 等正交能力，Link 表达 Scene/Render/Physics/UI 对应物。
- [x] 跨领域组件不得保存长期裸指针；使用 `ecs::EntityHandle` 或目标领域 generation handle。
- [x] Link 契约必须定义 owner、失效检测、销毁联动、重建策略和 snapshot 行为。
- [x] 公共 SubjectRef 可引用不同领域对象，但不要求统一实体父类。
- [x] System 设计先写目标 `View<Base, Components...>`，再反推 Base 与组件边界。

### 28. Definition 与 Runtime Instance

- [x] 统一 InstanceIdentity：instance UUID、DefinitionRef、definition generation。
- [x] CardDefinition/CardData 与 EffectDefinition/Instance 已接入相同引用模式；Skill、Weapon、Vehicle 等消费者列为后续跨写集。
- [x] 定义并实际执行 KeepInstanceValues、ReapplyDefaults、RebuildInstance、RejectWhileActive reload policy。
- [x] 使用 Definitions 的 generation-qualified handle 检测热重载后的 stale definition。
- [x] 各玩法保留强类型 definition，不统一成一个巨大的字段表。

本切片的实现、snapshot/hot-reload 语义和边界见
`docs/dev/2026-08-26-definition-runtime-instance.md`；physics、procgen、editor、
particles、animation 未纳入本次写集。

## P1：Dialogue 与 RTS 的整合边界

### 29. Dialogue 世界状态接入

- [x] Conversation local variable 仅属于当前 runner/frame。
- [x] 世界状态通过 IStateQuery/IStateMutation 访问，不在 Dialogue 内维护第二份真相。
- [x] flags/tags 接 TagStore；声望和数值接 AttributeSet/SocialGraph；持久修改走 StatePatch + Transaction。
- [x] Dialogue Condition 使用统一 EvaluationContext。
- [x] Dialogue Command 产生 Operation/GameplayAction，不直接跨模块修改内部状态。
- [x] 保留 Dialogue 特有内容：Line/Choice/Branch/Call、typing、voice/lip sync、本地化、history、call stack、node migration。
- [x] `Dialogue::DataValue`、`VarValue` 逐步接公共 Value，避免第三套动态值模型。

### 30. RTS 作为组合 profile

- [x] RTS Unit/Building 建立自己的领域短根，组合 Identity、Attributes、Tags、Effects、Orders、Sensing、Steering/Crowd、Weapon/Action、Settlement 组件或 Link。
- [x] RTS Building 通过组件组合 Definition、Placement、Production、Economy、Orders、Effects、Settlement，不继承这些能力类型。
- [x] RTS Player/Faction 组合 Authority、Economy、Social/Faction、Selection、GameEvent。
- [x] Move/Attack/Build/Gather 通过通用 Orders + Action executor 实现。
- [x] RTS 模块只保留框选/编队、command fan-out、fog of war、formation、战略 AI、科技树和大规模调度等特有能力。

### 30.1 Orders、Production 与任务调度边界

- [x] 禁止把 `orders`/`production` 向上并入 `rts`；Vehicle 等非 RTS consumer 不得为使用通用队列反向依赖 RTS。
- [x] 以真实 consumer 和 profile 裁剪证据审核是否横向合并为 `task_scheduling`；结论是不合并：Vehicle 只需要短时抢占式 `CommandQueue`，Building/生产链可只需要持续进度式 `WorkQueue`，合并会制造无关依赖；删除仅承载 handle tag、没有权威状态和真实 consumer 的空泛 `task_scheduling` 壳。
- [x] 统一两类队列重复的 Task/Command identity、priority、terminal reason、lifecycle event envelope、序列和 versioned transactional snapshot 基础，不强行统一 timeout/preemption 与 duration/pause/slot 的特有策略。
- [x] 将 `OrderPayload` 的“预编码 JSON fragment map”迁移到 canonical `Value::Object` 或明确 validated payload 类型，禁止在 L0 用字符串绕过解析与类型校验。
- [x] 把 `current/find/taskAt/eventAt` 等借用返回值迁移到 `OptionalRef`，跨 tick 保存使用 generation handle；移除 legacy raw-pointer queue factory 和 Checked/unchecked 双 API。
- [x] `rts` 只保留 command fan-out、formation、fog、strategic AI 和 RTS adapter；Vehicle、Building、Crafting 等直接依赖所需 scheduling capability。

## P2：CI、模块清单与质量门禁

### 31. 裁剪与组合测试矩阵

- [x] CI 增加 minimal、2d、3d、web profile configure/build/smoke。
- [x] 增加 procgen-core-only、physics-core-only、headless/server profile。
- [x] 验证公开 header 在依赖缺失时可独立使用。
- [x] 每个 optional dependency 必须有“能力存在”和“能力缺失”测试。
- [x] 全量 unit_test 链接成功不能作为模块独立性的唯一证据。

### 32. 契约测试

- [x] PropertyModel adapter parity。
- [x] Value canonical serialization 和跨 adapter round-trip。
- [x] PersistentId、现有 `ecs::EntityHandle`、非 ECS RuntimeHandle 和 stale generation。
- [x] Result/Diagnostic script projection。
- [x] Result `[[nodiscard]]` 编译诊断、Debug 未观察析构、移动责任和显式 ignore/expect。
- [x] Snapshot round-trip、migration、version rejection。
- [x] Event ordering、correlation/causation。
- [x] Transaction prepare/commit/rollback/compensation。
- [x] Definition/Policy generation-qualified handle。
- [x] Condition 无副作用与解释结果。
- [x] Resource cost 原子扣费。

### 33. 黄金端到端组合路径

- [x] 固定 seed 生成六边形地形。
- [x] Procgen 输出 CompositeArtifact：mesh、collider、topology/metadata。
- [x] adapter 创建 scene/map、graphics 和 physics 对象。
- [x] UI/presentation inspector 使用统一 schema 修改参数。
- [x] Editor transaction 支持提交、撤销、重做。
- [x] 保存恢复后 buildKey、artifact identity 规则和 physics query 结果符合契约。
- [x] Vulkan 与 WebGPU 验证同一 backend-neutral 可观察结果。
- [x] 任一 participant 失败时不得留下半更新 scene/collider/resource。

### 34. Manifest 与格式门禁

- [x] manifest 声明按真实 layer 排列，修复 editor/scene 所在 section 与 LAYER 标记不一致。
- [x] 增加 section 与 LAYER 一致性 lint。
- [x] 保持 `module_depgraph.py --check`，但不把“无 back-edge”当成完整架构验收。
- [x] `git diff --check` 覆盖 patch、docs、examples 和 README。
- [x] 新增 binding gap、soft skip、fallback 必须有 owner、issue 和 expiry。

## 明确禁止的过度整合

- [x] 不把所有模块合并成 `GameplayModule`。
- [x] 不建立 RPGActor、VehicleEntity、CardData 的庞大共同基类。
- [x] 不建立统一 GameObject 或 GameplaySubject 根来同时承载 Scene、Render、Physics 和玩法职责。
- [x] 不用继承表达 Selected、Dead、Poisoned、PlayerControlled 等可变状态。
- [x] 不为仅 definition 数据不同的卡牌、武器、怪物创建 C++ 实体子类。
- [x] 不把所有整数运行时 ID 换成 UUID。
- [x] 不把 CardState、OrderState、Dialogue phase 等不同生命周期状态强行合并。
- [x] 不把所有 graph 执行语义合并；只评估复用通用 graph 容器。
- [x] 不把 Weapon ammo、Economy resource、Attribute value 强迫存入同一数据结构。
- [x] 不把所有领域事件 payload 降级成无类型 JSON。
- [x] 不让 common 层 include Graphics、Physics、UI、Editor 或具体玩法模块。
- [x] 不在没有两个真实 consumer 的情况下稳定新的公共抽象。

## 推荐实施批次

### Batch A：公共值类型与已确认去重

- [x] Identity/UUID、现有 `ecs::EntityHandle` 与非 ECS RuntimeHandle。
- [x] Version/Revision/Generation strong types。
- [x] Value/Payload。
- [x] Result/Diagnostic。
- [x] Property validation 单一实现。
- [x] RPG AttributeSystem → attributes。
- [x] RPG Status lifecycle → effects。
- [x] VehicleOrder → orders。

### Batch B：生命周期基础设施

- [x] EventEnvelope。
- [x] SnapshotEnvelope。
- [x] TransactionParticipant + Editor adapter。
- [x] VersionedRegistry。
- [x] ResourceRef/URI。
- [x] Time source 与 Subscription。

### Batch C：重模块分层

- [x] GeneratedArtifact。
- [x] Procgen 公开 facade 下的 core/adapters/render 内部边界。
- [x] Physics 公开 facade 下的 domain/accelerator/presentation 内部边界（见 `docs/dev/物理系统分层与后端契约.md`）。
- [x] core-only CI profile 的源码边界与独立配置入口。

### Batch D：玩法组合机制

- [x] ConditionResult。
- [x] TargetSet/TargetingSpec。
- [x] ResourceAccount/Cost。
- [x] Settlement pipeline。
- [x] GameplayAction（先 Skill + Weapon）。
- [x] Container/Transfer（Card + Inventory 首批 adapter 已完成；扩展 adapter 正在收尾）。
- [x] Dialogue state/action adapter。
- [x] RTS composition profile。

## 开工前需要确认的架构决策

- [x] UUID 实现和编码格式：标准文本、二进制布局、UUIDv7 库或自研最小实现。
- [x] 公共 Value 最终放在 common 还是 data；是否允许 common 依赖现有 JSON parser。
- [x] Result 在 C++ API 是否允许 exceptions 与 Result 混用的精确边界。
- [x] SchemaRegistry 是否追求 JSON Schema 标准兼容，还是明确维护 Eve Schema 子集。
- [x] GameplayAction 模块命名和依赖层级。
- [x] Settlement 是独立模块，还是先作为 RPG pipeline 的公共接口孵化。
- [x] Container 是否在 Card+Inventory 两个 adapter 成功后再升级为正式模块。
- [x] 存档 migration 的兼容窗口和旧版本支持政策：发布版本支持 `N/N-1`，逐版本显式迁移，拒绝未知新版本与窗口外旧版本，不支持 downgrade。

## 完成定义

一项整改只有同时满足以下条件才算完成：

- 公共协议有 Doxygen 文档和稳定错误码。
- 至少两个真实模块使用，而非只有抽象接口和 mock。
- 原重复 API 已迁移或有明确 deprecation 路线。
- 单模块、裁剪 profile 和端到端 journey 均有测试。
- snapshot、hot reload、stale handle 和失败回滚行为有定义。
- 脚本 binding 与 C++ API 的语义一致。
- 没有新增反向依赖，common 层没有吸收业务实现。
