# EditorHost / EditorPackage：自动装配与 MCP 投影

日期：2026-10-13  
状态：P0–P3 最小切片已落地（schema/MCP attach、Host routers、Action+Voxel 包、fluids/gpuagents incomplete、金样例 editor-host-world）；Mode B 仍延后  
存放：`docs/dev/`

相关：

- [`2026-08-29-editing-editor-layering.md`](2026-08-29-editing-editor-layering.md) — L1 `editing` / L6 `editor` 分层
- [`2026-09-04-domain-editor-workspace-ui.md`](2026-09-04-domain-editor-workspace-ui.md) — Workspace 面板契约（已冻结产品面）
- [`editor-tool-protocol.md`](editor-tool-protocol.md) — `IEditorTool` 协议
- [`AI与MCP支持.md`](AI与MCP支持.md) — `eve_editor_*` 工具面
- [`编辑器模块设计.md`](编辑器模块设计.md) / [`编辑器具体API与组件设计.md`](编辑器具体API与组件设计.md)
- [`superpowers/specs/2026-08-25-composable-editor-sdk-roadmap.md`](superpowers/specs/2026-08-25-composable-editor-sdk-roadmap.md)
- 用户文档：[`docs/usr/modules/editor.md`](../usr/modules/editor.md)

## 1. 决策

**不生成大量胶水代码；把胶水收成「发现 + 默认策略 + 双投影」。**

领域卫星只交付一份 **`EditorPackage`（描述符包）**。项目侧用 **`EditorHost`** 按
`HostProfile` 发现并 `autoMount` 已加载包，自动完成：

1. 面板安装与布局合并；
2. 命令 / Automation factory 注册；
3. selection / undo / viewport 的默认联动；
4. UI Presenter 与 MCP 工具面的同源投影。

UI、游戏内建造、MCP/AI **共用一条写入路径**（Target + Command + Transaction）。
MCP 是 Host 的投影，不是第二套领域 API。禁止为同一文档再开 `eve.mcp.tool` 写路径。

本设计**不**取代已冻结的 Workspace UI 契约：面板仍是
`id` / `title` / `region` / `order` / `capability` / `context`；
没有对应面板的操作不得先注册 automation 命令（见 2026-09-04 文档）。

## 2. 问题陈述

今天单域砖块（Target / Session / `configureWorkspace` / factory）齐备，但项目作者要
拼「世界编辑器」时仍需手写四套方言：

| 方言 | 代表例子 | 缺口 |
|------|----------|------|
| Workspace + 手写 panel | `examples/composable-editor` | 无 Host 级服务袋 |
| Domain `configureWorkspace` | `examples/combat-action-editor` | 面板冲突靠自觉；多 undo 栈 |
| Scene live session | `examples/scene-editor` | 与 Workspace 平行 |
| Script commands | `examples/editor-api-v2` | `automationAllowed=false`，进不了 MCP |

跨域 selection / undo / viewport / document 全是项目胶水。MCP 侧是固定
`eve_editor_*` 适配器：命令与 factory 手写双重注册，**无 payload JSON Schema**，
Agent 两跳发现并猜字段。`fluids` / `gpuagents` 等存在「有 factory、无 commands」的半注册。

## 3. 目标与非目标

### 3.1 目标

- 项目作者用 **&lt;30 行 Squirrel** 挂上多个领域包，得到可用壳 + AI 可执行命令面。
- 领域作者 **写一次 Package**，不再分别维护 mount 样板、MCP factory 样板、panel→window 样板。
- 人和 AI 对同一 `target` 执行同一 `command`；卸包后面板 / 命令 / tools 立刻消失。
- 与现有 `editing` / `editor` / `IGameEditorExtension` / `IEditorAutomation` 兼容，可渐进迁移。

### 3.2 非目标

- 不交付固定 Editor App 或强制 ImGui 壳。
- 不自动生成领域命中测试、预览语义、事务边界或玩法规则。
- 不用 codegen 产出一堆 `if (select) …` 桥接代码（会再次腐朽）。
- 不把 `EditorSession` / 文档存储下沉到 `editing` 或 `common`。
- 不让 `runtime-3d` 链接 `editing` / `editor` / 任何 editing 卫星。

## 4. 架构总览

```text
Domain *EditorModule (load)
    └─ advertise EditorPackage  (cap listener / provider)

EditorHost (project or tools shell)
    ├─ discover / filter(HostProfile)
    ├─ layout merge → EditorWorkspace
    ├─ SelectionRouter / UndoCoordinator / ViewportRouter
    ├─ CommandService + Automation factories
    └─ projectors
         ├─ UiPresenter (capability+context → panelBuilders)
         └─ McpProjector (commands/targetTypes + schema → eve_editor_*)

Game modules (optional)
    └─ IGameEditorExtension  (tools / palettes / rules — 玩法注入，仍手写)
```

依赖方向不变：

```text
editing (L1)  ←  *_editing  ←  editor (L6)  ←  EditorHost / UI / MCP
```

`EditorPackage` 的**数据形状**可放在 `editing`（UI 无关描述符）；
`EditorHost` 的**装配与投影**留在 `editor`（或薄 `editor_host` 卫星，仍属 L6/L7）。

## 5. EditorPackage（领域一次写齐）

每个可组合领域编辑器导出一个不可变包。启动时校验「最小交付物」；缺一则
`Result` 失败或标记 `incomplete`（不得静默半注册）。

### 5.1 最小交付物

| 项 | 说明 |
|----|------|
| `ownerModule` | 稳定模块名；卸包按 owner 清理 |
| `panels[]` | Workspace 面板表（遵守 2026-09-04 契约） |
| `commands[]` | Planned commands，含 `payloadSchema` 与 `panelBinding` |
| `targetTypes[]` | Automation `IEditorAutomationTargetFactory` 可创建类型 + `createSchema` |
| `selectionDomain` | 本包写入的 selection domain 名 |
| `preview` | 预览契约 id（revision/generation 丢弃策略） |
| `links[]`（可选） | 本包声明的默认联动（见 §7） |
| `undoPolicy` | `share`（默认）或 `private` |

### 5.2 描述符草图（C++）

```cpp
// 概念 API — 落点：editing 或 editor，实施时再定头文件
struct EditorPanelSpec {
    std::string id;           // "action.timeline"
    std::string title;        // 或 titleKey
    std::string region;       // left|center|right|bottom|top
    int order = 0;
    std::string capability;   // "action.timeline"
    std::string context;      // list|preview|inspector|timeline|transport
    bool primaryViewport = false;
};

struct EditorCommandSpec {
    editing::CommandId id;              // "action.clip.set_duration.v1"
    std::string displayName;
    std::string category;
    bool automationAllowed = true;
    std::vector<std::string> targetKinds; // "action-timeline"
    // PropertySchema 或等价 JSON Schema 文档；UI Inspector 与 MCP inputSchema 同源
    editing::PropertySchema payloadSchema;
    // 必须指向本包已有 panel 的 capability+context；禁止无面板先注册 automation
    std::string panelCapability;
    std::string panelContext;
    editing::EditingCommandPlanner plan;
};

struct EditorTargetTypeSpec {
    std::string type;                   // "action-timeline"
    editing::PropertySchema createSchema;
    // factory 实现仍由模块持有；Package 只广告 type + schema
};

struct EditorLinkSpec {
    std::string when;   // "selection.domain=scene" | "preview.clock=action" | ...
    std::string then;   // "activate.capability=material" | "scrub.peer=animation" | ...
};

struct EditorPackage {
    std::string ownerModule;
    std::vector<EditorPanelSpec> panels;
    std::vector<EditorCommandSpec> commands;
    std::vector<EditorTargetTypeSpec> targetTypes;
    std::string selectionDomain;
    std::string previewKind;
    std::vector<EditorLinkSpec> links;
    enum class UndoPolicy { Share, Private } undoPolicy = UndoPolicy::Share;
    bool incomplete = false; // 半注册时必须 true，且 Host 默认拒绝 mount
};
```

### 5.3 广告与发现

- 模块构造：实现 `IEditorPackageProvider`（或复用 `cap::provide` / `addListener`），返回 `EditorPackage`。
- 现有路径兼容：`configureWorkspace` / `registerEditingCommands` / `IEditorAutomationTargetFactory` 在过渡期由 **Package 适配器** 自动调用，避免双写。
- 长期：领域只维护 Package；旧 API 变为薄 facade。

### 5.4 完整性门禁

`EditorHost::mount` / `autoMount` 对每个包执行：

1. `panels` 非空（纯 schema stub / inspector 包可豁免，但不得 `automationAllowed`）；
2. 每个 `automationAllowed` 命令的 `panelBinding` 能解析到本包 panel；
3. 每个 `targetTypes[].type` 有对应 factory 实现；
4. 每个 command 有非空 `payloadSchema`（允许 `emptyObject` 显式声明「无字段」）；
5. `incomplete==true` → 默认跳过并诊断，除非 `host.mount(..., { allowIncomplete: true })`。

## 6. EditorHost（组合运行时）

### 6.1 职责

| 服务 | 职责 |
|------|------|
| `discover` / `autoMount` | 收集已加载 Package，按 profile 与 allowlist 过滤后安装 |
| `layout` | 合并面板到 `EditorWorkspace`；处理 region 冲突 |
| `SelectionRouter` | 通道化 selection；按 link / 默认规则激活 inspector |
| `UndoCoordinator` | 跟踪焦点文档栈；`share`/`private` 策略；统一 `canUndo` 展示 |
| `ViewportRouter` | 单一 primary viewport 的指针/工具分发 |
| `DocumentTabs`（可选一期） | 多文档焦点；与 DocumentService 对接 |
| `UiPresenter` | `capability+context` → 项目或默认 `panelBuilders` |
| `McpProjector` | 导出 commands / targetTypes / schema 到 `IEditorAutomation` |

Host **不**拥有领域文档内容；文档仍在各 Target / DocumentService。

### 6.2 项目侧 API（Squirrel 目标形态）

```squirrel
local host = eve.EditorHost({
    id = "world",
    profile = "developer",   // developer | runtimeBuilder | automation
    channel = "world"
})

// A. 全自动：挂上当前 profile 允许的全部完整包
host.autoMount()

// B. 显式子集
host.mount(["scene", "voxel", "action"], {
    voxel = { undo = "private" }
})

// 覆盖默认联动
host.link({ when = "scene.selection", then = "material.inspect" })

// UI：项目 panelBuilders；缺省用引擎默认 Presenter（若有）
host.present(myBuilders)

// MCP：同一命令面（不是第二写入路径）
eve.mcp.attachEditor(host)
// 或 host.exportMcp({ mode = "commands" })  // 见 §8
```

验收口径：上述「B + present + attachEditor」可在 &lt;30 行内完成多域宿主。

### 6.3 布局合并策略

1. 按 `region` 分组，组内按 `order` 稳定排序。
2. `primaryViewport==true` 的 panel 全局至多一个；冲突 → `Conflict` 诊断，后挂载者失败。
3. 同 `id` → 失败（不静默覆盖）。
4. 同 `capability` 多 `context`（list/preview/inspector/…）允许并存（2026-09-04 表）。
5. 项目可提供 `layoutProfile`（如 `focus-timeline`）调整 region 默认尺寸；不改 panel id。

过渡期：`configureWorkspace(ws)` 仍可用；Host.mount 内部调用等价逻辑，**禁止**领域再保留 Workspace 裸指针。

### 6.4 C++ 草图

```cpp
class EditorHost {
public:
    struct Options {
        std::string id;
        HostProfile profile = HostProfile::developer();
        std::string selectionChannel = "default";
    };

    explicit EditorHost(Options opt);

    // 发现 cap 上全部 IEditorPackageProvider，过滤后 mount
    Result<MountReport> autoMount();

    // owners: 模块名或包 id 列表
    Result<MountReport> mount(std::span<const std::string> owners,
                              const MountOverrides& overrides = {});

    Result<void> unmount(std::string_view ownerModule);

    Result<void> link(const EditorLinkSpec& link);
    Result<void> present(IPanelPresenter& presenter);

    EditorWorkspace& workspace();
    UndoCoordinator& undo();
    SelectionRouter& selection();

    // 供 MCP / 测试
    Result<Json> exportCommandCatalog() const;
    Result<Json> exportTargetCatalog() const;
};
```

## 7. 默认联动（消灭 if 胶水）

### 7.1 内置规则（Host 默认启用）

| when | then | 说明 |
|------|------|------|
| `selection.changed` | 激活同 channel 下匹配 `selectionDomain` / capability 的 `inspector` 面板 | 通用 Inspector 路由 |
| `document.focus` | UndoCoordinator 切到该文档策略栈 | 状态条 `canUndo` 跟焦点 |
| `preview.clock=<pkg>` | 同 channel 声明 `scrub.peer` 的包同步时间 | action ↔ animation |
| `scene.component=<cap>` | 打开提供该 cap 的 inspector（复用 SceneComponentPayload 方向） | scene↔material/audio/… |
| `tool.activate` | ViewportRouter 把指针交给活跃工具；非 primary viewport 忽略 | 工具互斥 |

### 7.2 包级 / 项目级覆盖

- Package.`links[]`：领域作者声明的**默认**联动（可被 Host 关闭）。
- `host.link`：项目覆盖或追加。
- 规则引擎只做「路由与激活」，不执行领域 mutation；mutation 仍走 Command。

### 7.3 Undo 策略

- `Share`：包内编辑进入 Host 共享事务消费者（适合属性面板、轻量命令）。
- `Private`：包保留独立栈（适合时间轴拖拽、笔刷 stroke）；Coordinator 只切换「当前焦点栈」的 undo/redo 入口。
- **禁止**静默跨栈合并（避免 action 拖条冲掉 scene TRS）。需要跨文档原子操作时走显式复合 Command。

## 8. MCP 投影（Auto-MCP）

### 8.1 原则

1. 一条写入路径：`eve_editor_plan` / `execute` / `commit` / `undo`。
2. 描述符带 schema；Agent **不猜字段**。
3. 仅导出 `automationAllowed && HostProfile::automation()` 允许的命令。
4. 脚本命令默认仍关闭 automation；`registerScriptCommand(..., { automation = true })` 显式 opt-in，且必须带 `payloadSchema` + `panelBinding`（或声明为 host-only 无面板命令并禁止改文档——本设计不鼓励）。
5. `eve.mcp.tool` 仅用于非文档 observe/actuate；文档修改不得并行注册。

### 8.2 导出模式

**模式 A（默认，稳妥）**

- 保留现有泛化工具：`eve_editor_commands`、`eve_editor_execute`、`eve_editor_target_*` 等。
- `eve_editor_commands` 每条增加 `inputSchema`（由 `payloadSchema` 转换）。
- `eve_editor_target_list` 每个 `supportedTypes[]` 项增加 `createSchema`。
- 改动面小，兼容现有 Agent 流程。

**模式 B（发现友好，可选）**

- `tools/list` 动态附加 `editor.<commandId>`，`inputSchema` 含 `target` + payload。
- 内部转发同一 `execute` 路径；卸包即从 `tools/list` 移除。
- 需工具数量门禁（例如每 Host 上限）与 profile 过滤。

`eve.mcp.attachEditor(host)` / `host.exportMcp({ mode })` 选择 A 或 B；默认 A。

### 8.3 Schema 桥

```text
editing::PropertySchema  →  JSON Schema (draft 子集)
  - 标量 / 枚举 / ObjectRef / 数组
  - 禁止把原生指针编进 schema
  - 稳定 $id = command id 或 target type
```

`EditorAutomationProvider::commandsJson()` / `target-list` 改为读 Host 目录或
CommandService 上挂载的 schema，不再只吐 id/displayName。

### 8.4 与现有工厂的关系

- `IEditorAutomationTargetFactory` 保留；Package.`targetTypes` 驱动发现与 createSchema。
- 模块不得再「只挂 factory 不挂 commands」通过完整性门禁。
- `supportedTypes` 继续运行期聚合，禁止 MCP schema 硬编码类型子集（已有教训）。

## 9. UI 投影

- Presenter 输入：Workspace panel 快照 + selection/focus + schema。
- 默认库（可分阶段）：对标准 `context`（`list` / `preview` / `inspector` / `timeline` / `transport`）提供引擎侧参考 Presenter；项目可按 `capability` 覆盖。
- 项目 `panelBuilders` 继续可用（`composable-editor` 模式）；Host.present 只保证「有 descriptor 必能问到 builder」。
- **不**在 C++ 里创建 OS 窗口或提交 GPU（保持现契约）。

## 10. 迁移路径

| 阶段 | 动作 | 成功标准 |
|------|------|----------|
| P0 | 扩展 `EditingCommandDescriptor`：`payloadSchema`、`panelBinding`、`targetKinds`；`commandsJson` / target-list 导出 schema；脚本命令 opt-in automation | 现有 MCP 测试通过；Agent 可按 schema 填 payload |
| P1 | 引入 `EditorPackage` + `IEditorPackageProvider`；Action（模板域）与 Voxel 先迁；`EditorHost::mount` / `autoMount`；布局合并 | `examples/editor-host-world`：&lt;30 行挂 scene+voxel+action |
| P2 | `McpProjector` + `eve.mcp.attachEditor`；模式 A 默认、B 可选；完整性门禁拒绝半注册 | 卸包后 commands/tools 消失；fluids/gpuagents 补齐或标 incomplete |
| P3 | SelectionRouter / UndoCoordinator / ViewportRouter 默认规则；Level History、PixelWorld 迁入标准路径；`configureWorkspace` 缩为 Package 适配器 | combat 类多域壳可删掉手写联动的大部分 if |

兼容策略：

- 旧 `configureWorkspace` / 手写 `registerEditingCommands` 保留至 P3；双写期用适配器从 Package 生成，避免领域两处维护。
- `IGameEditorExtension` 继续服务**游戏玩法**注入，不替代领域 Package。

## 11. 例子与测试

### 11.1 金样例（P1 交付）

`examples/editor-host-world`（名可调）：

- `autoMount` 或显式 `mount(["scene","voxel","action"])`；
- 默认 UI Presenter 或最小 panelBuilders；
- MCP：`target_list` → `target_create` → `commands`(含 schema) → `execute` → `undo`；
- 证明 selection 通道与 undo 焦点切换。

### 11.2 契约测试

- Package 完整性门禁（缺 panelBinding / 缺 schema / 半注册）。
- 布局冲突（双 primaryViewport、重复 id）。
- 卸包清理（命令、factory、面板、MCP 目录）。
- Schema 往返：`PropertySchema` → JSON Schema → 校验合法/非法 payload。
- Profile 过滤：`runtimeBuilder` 看不到 developer-only 命令。
- 架构：`ARCHITECTURE_BASE=HEAD make check/architecture-contracts`；`module_depgraph` 无新的向上 include。

## 12. 风险与明确拒绝

| 风险 | 应对 |
|------|------|
| Package 变成「大一统 God object」 | 只放描述与注册；逻辑仍在 Target/Planner/Tool |
| 自动联动误触发 | 默认规则少而稳；项目可 disable；mutation 不进规则引擎 |
| MCP 工具爆炸（模式 B） | 默认模式 A；B 有上限与 profile |
| 与 2026-09-04「先面板后 automation」冲突 | 门禁强制 panelBinding |
| 生成胶水代码分支 | **拒绝**；只允许 schema/绑定的有限 codegen |

## 13. 实施落点（文件级预期）

| 组件 | 预期位置 |
|------|----------|
| Package / CommandSpec 扩展 | `src/modules/editing/`（描述符）+ 领域 `*/editor` 广告 |
| EditorHost / Routers / Projector | `src/modules/editor/`（如 `EditorHost.*`、`EditorMcpProjector.*`） |
| Script API | `Editor.cpp` 或独立 `EditorHostScriptBindings.cpp` |
| MCP 导出 | `EditorAutomationProvider.cpp` 读 catalog；`McpServer.cpp` 仅模式 B 需动态 tools |
| 门禁测试 | `test/editor_host_*.cpp`、扩展 `test/mcp_editor_*.cpp` |
| 金样例 | `examples/editor-host-world/` |

具体类名在实装 PR 中可微调，但 **Package → Host → 双投影** 形状不得绕开。

## 14. 架构规范对照

本设计适用并遵守：

- 模块编排：跨域经 capability / 描述符，不新增向上 `#include`。
- Result / `[[nodiscard]]`：mount、link、export 均返回结构化 `Result`。
- 可选依赖：无某领域包时 Host 可运行；有包则完整性门禁生效（双配置测试）。
- 持久化：不改文档 schema；MCP schema 是命令入参投影，非资产格式。
- 新 TODO/半注册：必须带 owner / reason / removal（incomplete 包显式标记）。

实装交接时运行：

```sh
ARCHITECTURE_BASE=HEAD make check/architecture-contracts
```

## 15. 一句话验收

> 领域只交 `EditorPackage`；项目只调 `EditorHost.autoMount` + `present` + `attachEditor`；
> 人和 AI 共享带 schema 的同一命令面；卸包即干净。
