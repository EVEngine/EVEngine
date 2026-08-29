# Editing 契约与领域编辑扩展分层设计

日期：2026-08-29  
状态：已确认设计，待分批实施

## 1. 决策

EVEngine 不把现有 `editor` 模块整体降低层级，也不把完整编辑契约放进
`engine/common`。新增可裁剪的 L1 `editing` 模块，承载开发编辑器、游戏内建造、
自动化和热更新共同使用的 UI 无关编辑协议。

具体领域编辑实现从 `editor` 迁移到高于对应运行时领域的 satellite 模块，例如
`physics_editing`、`audio_editing`、`map_editing`。现有高层 `editor` 保留会话、
工作区、扩展装配和脚本 facade；具体窗口、输入及渲染宿主继续属于 `ui`/EditorHost。

目标依赖方向如下：

```text
engine/common
    ^
property_access   schema   transaction       (L0)
          \         |         /
                 editing                    (L1)
                     ^
       physics_editing / map_editing / ...
                     ^
                   editor                     (L6 composition)
                     ^
              editor host / UI shell
```

箭头表示“上层依赖下层”。运行时领域模块不得依赖 `editing`、领域 editing
satellite 或 `editor`。

## 2. 为什么不放进 common

`common` 只保留所有 profile 都需要、只依赖标准库且长期稳定的机制：

- `Result`、`Diagnostic` 和明确的失败分类；
- `StableId`、`Revision`、generation handle 等通用身份原语；
- owning `Value`、订阅生命周期和 capability registry；
- 与任何具体编辑工作流无关的线程、所有权和生命周期词汇类型。

以下概念只有 editing consumer 需要，必须可被纯运行时裁掉，因此放入独立模块：

- editable target 与 capability discovery；
- selection snapshot；
- domain operation、operation plan 和 revision precondition；
- command descriptor 与 transaction participant adapter；
- property-edit adapter；
- editing extension descriptor。

不得因某个类型被多个编辑器 consumer 使用，就把会话、文档、Inspector、undo history
或领域资产实现继续下沉到 `common`。公共抽象至少需要两个真实 consumer，并且必须减少
重复契约或依赖，而不是只移动代码。

## 3. 模块职责

### 3.1 `editing`（L1）

`editing` 依赖 L0 的 `property_access`、`schema` 和 `transaction`，自身不依赖
graphics、physics、scene、UI、Squirrel runtime 或文件系统。

它负责：

- `EditingValue` 仅作为公共 `Value` 的语义别名或薄 adapter，不维护第二份动态值树；
- `EditingResult<T>` 复用公共 `Result<T>`，仅增加稳定的 editing diagnostic code；
- `IEditableTarget`、target descriptor、capability id 和 revision 查询；
- owning `DomainOperation`、prepare/validate/commit 所需的不可变输入；
- selection snapshot 与 property path，不保存运行时对象裸指针；
- command/transaction 接口及结构化 receipt；
- extension 的静态描述、依赖和 provider-present/provider-absent 查询契约。

它不负责：

- `EditorSession`、workspace、dock、toolbar、history 的具体实现；
- 文件、AssetDB、项目设置或文档存储；
- Inspector、ImGui、viewport、gizmo 绘制或 offscreen preview；
- Squirrel 类注册或固定 `eve.editor` 产品 API；
- 任一具体领域的字段、资产格式、runtime builder 或 publishing sink。

公共 header 必须写明 owning/borrowed 语义、线程亲和性、回调重入、revision/stale
行为以及失败是否保持原状态。可能失败、建立身份或提交状态的返回值必须
`[[nodiscard]]`。

### 3.2 领域 editing satellite

领域编辑实现由对应领域维护，但不编入运行时领域模块。命名统一使用
`<domain>_editing`，依赖 `editing` 和该领域的公开运行时契约。

它负责：

- 领域 target、document、schema adapter 和 validator；
- versioned snapshot、migration 和 unknown-field policy；
- candidate-first runtime publication adapter；
- 领域 graph/timeline compiler、import plan 和 preview data；
- 领域专属 overlay geometry，但不负责最终 UI/renderer presentation；
- 向通用 editing extension registry 注册 provider。

每份可变状态只能有一个权威 owner。editing document 若是权威状态，runtime publication
必须从已验证 candidate 原子更新；runtime 对象若是权威状态，target 只能通过领域操作修改它，
不得维护无法判定真源的镜像。每个 link 必须定义两种销毁顺序、stale 检测和 restore/hot
reload 重建策略。

不要为了文件归属把实现放回 `physics`、`audio` 等运行时 target 并增加
`EVENGINE_HAS_EDITOR`。模块内部 feature flag 会使源码边界、裁剪和 provider 缺失行为不可验证。

### 3.3 `editor`（高层 composition facade）

`editor` 依赖 `editing`，按 profile 可选依赖领域 editing satellite。它负责：

- `EditorSession`、selection/focus channel 和 command discovery；
- workspace、document/asset service 的产品级装配；
- undo/redo history 对公共 transaction receipt 的持有与投影；
- 权限、任务、automation 和扩展生命周期；
- 兼容的 `eve.editor` facade；
- 将已链接的领域 provider 暴露给宿主，但不枚举封闭的领域类型列表。

`EditorSession` 需要进一步拆分：纯 target/command/transaction 协调部分可以形成中层
service；disk store、presentation 和 workspace policy 留在高层。不得把当前
`EditorSession` 整体搬入 `editing`。

### 3.4 Editor host 与 UI

EditorHost、ImGui/UI shell、输入路由、窗口布局和最终 preview renderer 保持高层。宿主通过
extension descriptor 和 capability 查询组装领域工具，不通过 `switch(domain)` 或固定 enum
发现功能。

游戏内建造 UI、开发者 editor 和 MCP/自动化 host 使用相同 target、operation、command 和
transaction 路径；差异仅限权限、持久化策略、shell 和 presentation。

## 4. 初始迁移映射

第一批迁入 `editing` 的候选：

- `EditorIds`；
- `EditorTarget` / `EditorTargetV2` 的统一后继接口；
- `EditorCommandTypes`；
- `EditorProtocol` 中不依赖 host policy 的 operation/descriptor；
- `EditorSelection` 的 owning snapshot；
- `EditorResult`、`EditorValue` 与公共 `Result`/`Value` 的 adapter；
- `EditorTransactionConsumer` 中纯 transaction participant bridge。

继续留在高层 `editor`：

- `EditorSession`、`EditorWorkspace`、`EditorDock`、`EditorToolbar`；
- `EditorDocumentService`、disk store、AssetDB 和 project settings；
- `EditorTaskService`、plugin permissions、network telemetry、profiler presentation；
- `EditorPropertyPresenter` 和具体 UI/preview orchestration；
- 兼容脚本绑定。

领域迁移示例：

- `EditorPhysics*` -> `physics_editing`；
- `EditorAudio*` -> `audio_editing`；
- `EditorMap*`、地图专属 road/object import -> `map_editing`；
- `EditorAnimation*` -> `animation_editing`；
- `EditorParticle*` -> `particles_editing`；
- `EditorMaterial*`、lighting/decal/environment 的具体 target 按真实 owner 分别迁入对应
  editing satellite，不建立新的 graphics 大杂烩；
- 通用 graph、curve、brush 和 gizmo contract 只有在两个迁移后的真实 satellite 仍共享时才
  保留在 `editing` 或独立低层模块，具体 graph domain 和 presenter 留在 satellite/host。

## 5. 扩展发现与生命周期

领域 satellite 通过窄 capability 向 registry 提供 extension factory。registry 不拥有模块，
只返回带 generation/lease 的 provider handle，不能把 provider 裸指针保存到下一帧或模块卸载后。

每个 extension 声明：

- extension id、schema/version 和其提供的 target/document/tool capability；
- 必需运行时模块和可选 preview provider；
- create、activate、deactivate、unregister 顺序；
- main/render/worker thread affinity；
- callback 是否可重入，以及卸载期间如何拒绝新任务；
- provider 缺失时返回 `Unsupported` 的稳定诊断，不静默 soft-skip。

Registry 派发未知 callback 前不得持锁。卸载先停止接收工作、取消或排空任务、注销 provider，
最后销毁 extension；旧 handle 必须解析为 `StaleHandle`。

## 6. 持久化与事务

跨进程 editing document 必须使用公共 snapshot envelope，并定义 schema id、version、未知字段
策略及逐版本 migration。新版本未知字段默认保留还是拒绝，由领域 contract 明确声明，不由
通用 editor 猜测。

变更路径统一为：

```text
untrusted input
  -> parse owning Value
  -> schema/domain validation
  -> resolve generation-qualified references
  -> prepare owning candidate
  -> transaction commit
  -> atomic runtime publication
  -> receipt/history/event
```

验证、prepare 或任一 participant 失败不得留下部分 document、runtime、scene、physics 或资源
状态。Undo/redo 走同一 canonical transaction/publication 路径，不直接回写私有字段。

## 7. 模块与构建规则

`cmake/module_manifest.cmake` 仍是模块、依赖、profile 和 boot list 的唯一真源。实施时：

- 新增 `editing` 为 L1 模块；
- `editor` 的直接依赖改为 `editing`，逐步删除已由它承接的低层重复依赖；
- 每个 satellite 声明 `editing` 与真实领域依赖；
- `editor` 只可选依赖已完成迁移的 satellite；
- 不手改 `EVELIBS`、ThirdParty 或 `load.nut`；
- 不以 broad allowlist、baseline 或新的 back-edge 静默通过门禁。

不是每个小文件都必须产生一个模块。只有当一组实现具有独立职责、依赖、生命周期和真实裁剪
价值时才建立 satellite；始终共同启用且一对一耦合的实现应合并。纯文件组织可以使用模块内
子目录，但不得因此跨越依赖方向。

## 8. 实施阶段

### Phase A：契约冻结

- 为现有 editor public types 建立引用和 consumer 清单；
- 识别与公共 `Value`、`Result`、property、transaction 的重复；
- 写出 `editing` public API 和 compatibility adapter；
- 增加 API shape、header independence、ownership 和 stale-handle contract tests。

此阶段不移动领域实现，不改变 `eve.editor` 行为。

### Phase B：两个真实 satellite 试点

先迁移 `physics_editing` 和 `audio_editing`。二者分别覆盖多对象 backend publication 与
资源/transport 工作流，足以验证公共抽象不是为单一领域定制。

每个试点必须覆盖：

- provider present/absent；
- target apply/reject、stale revision、undo/redo；
- snapshot round-trip、unknown version、migration failure；
- candidate publication failure 不改变 editing/runtime 权威状态；
- 只启用运行时领域、不启用 editing satellite 的裁剪构建；
- 一个开发 editor host 和一个 runtime/in-game host 的组合路径。

### Phase C：大领域迁移

按 scene/map、animation/particles、graphics 内容、其余 specialist domain 分批迁移。每批只移动一组
权威状态和所有 consumer，禁止留下 editor 与 satellite 两套 registry、codec 或 snapshot 真源。

### Phase D：高层收敛

- 将 `Editor.cpp` 收敛为通用 facade 和 extension discovery；
- 移除固定领域 factory/binding 和相应 `EVENGINE_HAS_*` 分支；
- 评估 Session 中层 service 与高层 host policy 的最终边界；
- 更新用户模块文档、示例和 SDK 安装清单。

## 9. 验证和完成定义

每个实施 PR 至少执行：

```sh
python scripts/module_depgraph.py --check
python scripts/check_bindings.py --strict
ARCHITECTURE_BASE=HEAD make check/architecture-contracts
git diff --check
```

并按改动运行 focused CTest、provider present/absent、相关裁剪 profile 和至少一条端到端 host
组合测试。源文件移动或静态扫描通过不能替代运行时 publication/rollback 验证。

整体迁移只有在以下条件都满足时完成：

- 纯运行时 profile 不链接 `editing` 或任何领域 satellite；
- `editing` public headers 不 include 具体领域、renderer、UI、filesystem 或 Squirrel；
- 至少 physics/audio 两个真实 satellite 共享并通过相同 contract suite；
- provider 缺失、卸载、stale handle、失败回滚和 snapshot migration 均有测试；
- `editor` 不再直接实现具体领域 target/runtime builder；
- developer editor、游戏内建造和 automation 至少各有一条共享 transaction 路径证据；
- 无新增 back-edge、allowlist、无元数据的 TODO/FALLBACK 或第二来源真相。

## 10. 明确不做

- 不把整个 `editor` 改成 L0/L1；
- 不把 editing API 永久塞进 `common`；
- 不让运行时领域模块依赖 editor/editing；
- 不建立包含所有领域类型的 `EditorObject`、variant 或 enum；
- 不用无类型 JSON/`Value` 取代稳定的领域对象和 handle；
- 不在一次 PR 中机械搬迁全部三万余行 editor 代码；
- 不在没有两个真实 consumer 和裁剪证据前稳定新的公共抽象。

