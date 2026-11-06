# 统一编辑器 UI 开发框架设计

日期：2026-09-22。状态：分批实施中；Audio/Animation 首批实现见 [迁移说明](2026-10-23-editor-ui-migration.md)。本文其余类型名仍为职责草案。

## 1. 目标与现状证据

目标是让新增领域编辑器主要编写领域操作、数据适配和专属可视化；布局装配、属性编辑、快捷键、撤销、输入手势、诊断和预览生命周期由框架承担。保留项目自组装编辑器和游戏内编辑器的能力。

当前已经有可以复用的底座，不需要重建整套编辑系统：

| 代码位置（相对仓库根） | 已有能力 | 本次设计补齐什么 |
| --- | --- | --- |
| `src/modules/editor/EditorWorkspace.h` | 面板 descriptor、区域布局、selection/focus、revision | descriptor 到实际面板的统一装配 |
| `src/modules/editor/EditorPropertyPresenter.{h,cpp}` | schema 属性行、验证、PropertyEditIntent | 控件选择、双向绑定、输入草稿和诊断 |
| `src/modules/editor/EditorSession.h` | 工具输入、pointer capture、命令计划、事务及文档服务接入 | 各面板共享的交互控制与命令展示 |
| `src/modules/ui/EditorShell.h` | DevTools 三类面板与菜单壳 | 开放面板组合；不把固定三类面板推广成领域框架 |
| `examples/audio-source-editor/main.nut`、`examples/animation-clip-editor/main.nut` | 可运行领域 UI | 去掉重复的 mountPanels、eventParts、applyHistory、快捷键边沿检测、revision/按钮状态同步 |
| `src/modules/audio/editor/AudioSourceEditor.h`、`src/modules/animation/editor/AnimationClipEditor.h` | 各自拥有 LocalTransactionBackend | UI 绑定唯一 history owner，避免再加一套撤销栈 |

本提案是对 [Workspace UI 契约](2026-09-04-domain-editor-workspace-ui.md) 的实现框架补充，遵守 [editing/editor 分层](2026-08-29-editing-editor-layering.md)。现有文档中的领域产品要求继续有效。

## 2. 分层与代码归属

```text
项目编辑器配方（选择面板、主题、权限和领域适配器）
    ↓
editor/presentation：宿主无关控制器与不可变呈现快照
    ↓                         ↘
现有 editor 服务              领域 editor 适配器
    ↓                         ↓
editing 公共协议 ←──────── 领域 editing 文档、验证与操作

ui 的编辑器呈现卫星 → editor/presentation + ui 控件/输入/绘制
领域专属呈现适配器 → 领域公开预览数据 + 宿主绘制接口
```

箭头表示依赖。`editor/presentation` 起初只是 editor 内的文件组织，不新增空壳模块。具体 UI 接线放在 UI 包内的高层卫星，实施前核对现有 `ui/editor` 的 manifest 层级与依赖；可以复用时直接复用，不能让基础 ui 反向依赖 editor。新构建节点只有具备独立生命周期、依赖和裁剪价值才加入唯一 manifest。

领域 editing 不依赖控件或宿主；editor 核心不 include Audio、Animation 等领域头。宿主在安装阶段按 descriptor 的 capability/context 选择工厂，缺失时显示带原因的不可用面板。不增加 `switch(domain)`。

## 3. 统一的开发模型

一个编辑器由四类贡献组合：

1. **Workspace 配方**：复用已有面板 id、region、order、capability、context，指定布局偏好。
2. **领域适配器**：把已有 target/schema/selection/command/history/preview 暴露给公共面板。
3. **标准面板**：结构树或列表、Inspector、工具栏、状态与诊断、任务进度、预览容器。
4. **专属编辑面**：时间轴、曲线、图、波形、体素或 3D 视口，组合公共输入与事务控制器。

一个音频编辑器配方只需表达“Sources 使用列表；Inspector 绑定 source schema；Waveform 使用音频 renderer；Transport 绑定试听动作”。Volume/Pitch 不再各写一组创建 slider、读取值、写字段和刷新状态代码。新增加 schema 字段时，默认属性 UI 自动出现；领域扩展只提供真正特殊的控件或验证。

初版提供 C++ canonical 实现与薄 Squirrel 配置投影。脚本不复制手势、事务或 stale 检查。暂不设计新的通用 UI DSL，也不重写现有 Widget 系统。

## 4. 公共组件与边界

| 组件（拟议职责名） | 框架统一负责 | 领域继续负责 |
| --- | --- | --- |
| WorkspacePresenter | 安装、销毁、布局、焦点、面板状态、扩展缺失诊断 | 面板组合及 context |
| ActionBinding | 菜单/按钮/快捷键共用可用性、禁用原因与执行入口 | 命令 payload、权限与业务前置条件 |
| InspectorPresenter | schema 控件映射、分组、搜索、只读、多选 mixed、默认值、字段诊断 | schema、领域 validator、特殊属性插件 |
| CollectionPresenter | 稳定 item id、虚拟列表、筛选、选择、展开、拖放反馈 | 树关系、可移动规则、增删/重排命令 |
| GestureController | capture、begin/update/commit/cancel、一次手势一次历史 | 候选值计算、领域命中与编辑约束 |
| PreviewController | 请求取消、过期结果过滤、busy/error/旧画面标识 | 波形、姿态、合成等生成算法与资源发布 |
| DiagnosticsPresenter | 字段错误、全局错误、定位动作、失败恢复入口 | 稳定错误码与可操作说明 |

时间轴/曲线/图编辑分阶段抽取。先在 Action 与 Animation 两个真实消费者中确认缩放、平移、吸附、框选和拖动的不变式；轨道含义、曲线插值与 graph 执行语义仍归领域所有。只因视觉相似不建立公共领域模型。

主题统一 spacing、字体、行高、选中/禁用/错误/过期状态，支持 DPI 和窗口缩放。键盘导航、焦点可见性、标签与稳定 automation id 是标准控件的共同契约，不依赖颜色单独表达错误。

## 5. 唯一状态所有者

| 状态 | 权威 owner | UI 可以持有 |
| --- | --- | --- |
| 文档字段、revision | 原领域 target/document | 不可变投影、短期输入草稿 |
| history | 每个文档已绑定的唯一 transaction backend | canUndo/canRedo 与 receipt 投影 |
| selection/focus | 绑定时指定的现有 channel 服务 | 带 sequence 的快照 |
| 布局、展开、筛选 | workspace 呈现状态 | 仅本工作区 UI 状态 |
| 预览产物 | 领域 preview/runtime provider | 带版本的产物句柄/lease |

不能同时由 Workspace 和领域编辑器各维护一套可写 selection。迁移时为每个适配器明确使用哪一个已有 owner，另一入口转发到该 owner。同样，不能把 LocalTransactionBackend 的一次修改再放入 Session 的另一条历史。

初版每个文档绑定现有 history；跨文档原子事务仅在现有 transaction 协议能证明 prepare/rollback 完整时开放，不做 UI 层伪组合。

## 6. 输入、事务与刷新

规范路径：UI 事件 → owning intent → 原有 command plan/领域 operation → 验证与 prepare → 原子提交 → receipt → 呈现失效通知。

属性呈现复用 Developer/RuntimePropertyPresenter。其通用 `editor.property.set` intent 由绑定适配器映射到领域既有 canonical 命令；路径映射只在适配器一处声明，控件不持有 setter。领域验证仍是权威，UI 的格式检查不能代替提交验证。

手势契约：

- Down/开始输入：捕获 target handle、revision、selection sequence 和起始值，创建 opaque edit token。
- Move/输入：更新候选草稿及可选预览；不生成逐帧历史。非法候选保留输入并给字段诊断，文档不变。
- Up/Enter：按起始 revision 提交一个完整操作，成功仅生成一个 receipt；净变化为零不记历史。
- Esc、失焦、窗口关闭、目标销毁或 provider 卸载：取消草稿并释放 capture。异步失败不能把已取消手势复活。
- 外部命令改变 revision：旧 token 返回 Conflict，显示重新加载/重试入口，不自动覆盖新值。

需要 live audition 等即时效果时，领域提供可撤销的候选预览通道；缺少此能力则只展示草稿，提交后更新预览。不能靠逐帧提交再合并历史假装事务原子性。

快捷键路由顺序：模态交互 → 文本控件 → active panel/tool → workspace。文本输入时 Ctrl/Cmd+Z 优先属于文本编辑，文档 undo 由 host 平台键位映射；pointer 离开面板后仍由捕获者接收 Up。

刷新由 document revision、selection sequence、schema version、任务/transport sequence 驱动。UI 值回写使用不发用户 change 事件的更新路径，避免反馈循环。每帧只更新可见动态内容；不得逐帧遍历全部 schema、重建全部控件或做字符串 capability 查找。

## 7. 预览、生命周期与错误

预览请求携带 document identity/revision、provider generation、request sequence，以及影响结果的时间、seed 和视口参数。接收结果时逐项检查；同 revision 的旧 seek 结果也必须因 sequence 过期被丢弃。失败时保留上次成功画面并明确标为过期，展示当前错误。

框架只统一请求与发布生命周期，不统一领域策略。例如文档替换后的音频 stop/unbind 与 volume 的 live publication 行为，应由音频适配器依据真实变化分类处理；图形预览则按资源 lease 保持旧成功帧。

UI/controller 操作在 owner thread；worker 接收 owning 请求，完成后回 owner thread 验证再发布。回调不持锁；回调中的关闭/卸载排入安全点执行。注入 dt、时钟和具名 RNG，不从控件读取全局时间或随机数作为领域输入。

Provider 使用现有 generation/lease 注册机制，禁止新造另一套注册生命周期。宿主先销毁：取消请求、释放订阅/捕获、销毁视图再释放适配器。Provider 先卸载：停止接收、取消/排空工作、失效句柄、显示缺失诊断，再销毁 provider；在途 lease 释放前不得释放其代码或资源。热重载根据稳定身份重新解析，旧 generation 不复用。

所有可失败公共操作使用 `[[nodiscard]] Result<T>`，新 API 写明 Doxygen 所有权、失效、线程、重入和错误契约。昂贵预览采用 prepare/request + token 的显式两步操作，并用 `@cost` 标注相对成本和可复用维度。控件错误显示稳定 code、说明、关联字段和可恢复动作。

## 8. 持久化、模块六面与自动化

工作区状态单独使用带 schema id/version 的 envelope，只保存布局与展示偏好；不保存进程句柄或文档副本。首版明确支持当前版，未来引入新版时提供 N/N-1 逐版迁移。未知可选扩展字段保留但不解释，未知必需字段或未来版本拒绝。先验证完整候选再原子替换；损坏文件保留原文并提示用户显式重置。

实施时在 `scripts/architecture_contracts.json` 的 module-interface 条目登记：

| 边界面 | 契约 |
| --- | --- |
| capability in | command/property/collection/preview 等窄 provider，缺席有明确诊断 |
| capability out | host 的 presenter 工厂与挂载服务；生命周期复用现有机制 |
| ECS + Link | 初版不新增 ECS System；target/provider/UI 实例使用 typed generation handle |
| events | commit/selection/task 等已有事件投影；一次提交只广播一次权威结果 |
| script | 同一 controller 的 Result/handle 投影，配置和扩展回调可用 |
| tool protocol | 稳定 widget/action id，操作与 UI 共用 command，不绕过权限和 revision |

机制选型遵循 R-MECH：安装期一对多开放扩展用 provider discovery；已绑定的高频一对一交互用 typed handle/lease；已知目标且需原子结果用 command/transaction；一对多非原子通知用 Subscription。字符串仅用于安装/协议边界，热路径使用已解析身份。

## 9. 实施切片与完成标准

1. **Audio + Animation 纵向试点**：共用面板装配、ActionBinding、Inspector、history 状态与诊断。保留各自波形/姿态 renderer。两个示例删除相应手工事件路由、字段同步和快捷键重复代码，才算完成公共抽象验证。
2. **统一手势与预览**：在两者验证拖动提交/取消、外部 revision 冲突、关闭与乱序完成。若原领域缺少候选编辑能力，在对应 editing 层补齐，并同批更新所有消费者。
3. **Avatar + Biome**：以两个实际列表消费者稳定 CollectionPresenter，迁移参数编辑、选择与列表操作；领域合成与确定性铺点保留。
4. **Action + Animation 编辑面**：抽取被两者证明通用的时间轴交互，再评估 graph/curve/viewport 共享；不提前制造大而全接口。
5. **收口**：示例只剩配方、领域数据和 renderer；旧内部路径随迁移删除。公共旧 API 如需兼容，文档注明 compatibility-only 且转发 canonical 实现，不保留双 owner。

每批交付包含以下验证：

- 公共控制器契约：多选 mixed、只读/权限拒绝、字段验证、一次拖动一次 undo、Esc/失焦取消、重入关闭。
- 两个真实 adapter 共用测试：apply/reject/undo/redo、stale target、provider present/absent、提交失败保持文档及 runtime、乱序预览不得覆盖新结果。
- 真实 UI journey：打开、选择、改值、预览、撤销、关闭；通过引擎自身 screenshot 校验视觉结果，音频同时检查 transport 状态。
- 工作区保存恢复、未知版本、迁移失败与损坏输入，不留下部分可见状态。
- 性能证据：大列表只生成可见行；未变化属性不重新读取；preview 请求有合并/取消；记录每帧控件更新数、属性读取数和请求数。
- `make check`、相关模块测试、示例 smoke；运行时 profile 与无 preview provider profile 分别 configure/build/link/run，记录 L1/L2/L3/L4 证据，不用 include 图代替链接证明。

设计原始交付为文档；当前实现和验证范围按 [首批迁移说明](2026-10-23-editor-ui-migration.md) 单独记录，不据此宣称全部框架完成。设计适用唯一 owner、Result、generation 生命周期、六面契约、R-BOUND/R-MECH/R-COST 等规范；不引入 ECS 大根类，也没有申请或使用规范例外。
