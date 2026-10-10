# 去中心化冲突热点：按模块拆分频繁修改面

> 状态：设计（未实施）  
> 日期：2026-10-10  
> 前置：[#295 reduce shared manifest conflict hotspots](https://github.com/evengine/evengine/pull/295)、`docs/dev/模块编排与裁剪架构.md`  
> 动机：多 agent / 长寿命分支合 `dev` 时，双方同改文件集中在少数枢纽文件；见近期 merge 的 both-sides 统计（`Procgen.cpp`、`orchestration.cmake`、`ci.yml`、`Graphics*.h`、`architecture_contracts.json`）。

---

## 1. 指导思想

**在不影响依赖管理正确性的前提下，把“经常改”的文本所有权下放到模块（或子域）文件；集中文件只保留“顺序与闭合”信息。**

更具体的三条：

1. **单一逻辑真源，多物理碎片。**  
   依赖图、链接序、boot 列表仍然只有一份**可组合的**契约；碎片通过有序 `include` / merge 组成该契约。禁止第二份手写 `EVELIBS` / `load.nut` / 平行门禁表。
2. **改动局部化。**  
   给模块 `M` 加声明、加契约、加脚本绑定、加文档小节时，默认只碰 `M` 拥有的路径；其它 agent 合 `dev` 不应再撞同一行。
3. **顺序显式、发现可脚本化。**  
   链接序 / 声明序不能靠目录遍历的不稳定顺序；用**有序列表**（include 清单或编号前缀）固定顺序。校验脚本继续在组合后的视图上跑。

非目标：

- 不把 `graphics` 拆成多个可裁剪运行时模块（见编排文档 §七：裁剪收益 < 风险）。  
- 不做模块内散落 `#ifdef` feature flag（粒度不够就拆卫星模块）。  
- 不引入“每个模块一份独立依赖图”导致配置分叉。

---

## 2. 仍然必须集中的东西

| 资产 | 为何集中 | 允许的形态 |
|------|----------|------------|
| Third-party 链接序 `EVE_TP_ORDER` | GNU ld 左→右 | 留在 `module_manifest.cmake` 入口 |
| Link group 表 `EVE_LINK_GROUP_TABLE` | SHARED DLL 分组与无环进口 | 入口文件一小段表 |
| 模块声明的**全局顺序** | 派生 `EVELIBS` / boot list / 层号校验 | 有序 include 清单（可拆碎片） |
| 全局架构规则（`api-shape` 等） | 跨模块政策 | `_global` 碎片，数量应极少 |

集中文件的体积目标：**只含顺序与 include，不含业务声明正文。**  
#295 已把根 `CMakeLists.txt` / 旧整文件 manifest 拆过一轮；本设计继续把**仍在涨的碎片**按模块切开。

---

## 3. 目标架构（组合模型）

```
                    ┌─────────────────────────────┐
                    │  composed view (CI / CMake) │
                    │  · full module declaration  │
                    │  · full contract catalogue  │
                    │  · full docs index          │
                    └────────────▲────────────────┘
                                 │ ordered include / merge
          ┌──────────────────────┼──────────────────────┐
          │                      │                      │
   per-module cmake        per-module contracts    per-module docs
   declare fragments       JSON shards             topic pages
   (owner = module)        (owner = module)        (owner = module)
```

**冲突面从“中心文件的一行”变成“模块目录内的文件”。**  
两个 agent 分别改 `procgen` 与 `decal` 时，不再同时编辑 `orchestration.cmake` 末尾。

---

## 4. 方案分轨

### 4.1 模块声明：从层碎片 → 模块碎片（最高优先级）

**现状**

- 入口 `cmake/module_manifest.cmake` 有序 include 6 个层/域碎片。  
- `orchestration.cmake`（~292 行）仍是 L5/L6/L7 声明的汇合点：每加一个 `*_editing` / `*_editor` / bridge 都改它。  
- `scripts/check_module_manifest.py` 已支持入口的有序 `include(module_manifest/…)`（`manifest_files()`）。

**目标布局**

```
cmake/module_manifest.cmake              # TP order + link groups + include 清单
cmake/module_manifest/order.cmake        # 可选：纯 include 列表，便于审序
cmake/module_manifest/core_foundation.cmake
cmake/module_manifest/platform_resources.cmake
…
cmake/module_manifest/modules/
  procgen.cmake                          # 仅 procgen 及其卫星声明
  graphics.cmake                         # graphics + fog/raytracing/material…
  editor.cmake
  rts.cmake
  …
```

规则：

1. **一个宿主包一份声明文件**（含其 `editing` / `editor` / `*_graphics` 等卫星的 `eve_declare_module`）。  
   例如 `modules/procgen.cmake` 内依次声明 `procgen`、`procgen_editing`、`procgen_graphics_editing`、`procgen_editor`、`procgen_physics`、`heightmap_target` 等——这些改动本来就同属一个 agent 工作区。
2. **入口只维护有序 include 列表**（按 LAYER / 依赖闭合排序）。新增模块 = 新增碎片文件 + 在列表中插入一行。  
   插入点冲突仍可能发生，但冲突粒度是**一行路径**，不是整段 `eve_declare_module(…)`。
3. **禁止** `file(GLOB … module_manifest/modules/*.cmake)` 决定顺序。  
4. 校验：扩展 `manifest_files()` 跟随嵌套 include；`check_module_manifest.py` / `module_depgraph.py` 仍只见组合后的声明集。`LAYER` 与 `# L<n>` 段标仍强制一致。

**迁移步骤（行为零变化）**

1. 按宿主包把 `orchestration.cmake` / `rendering_simulation.cmake` 剪切到 `modules/<host>.cmake`（纯移动）。  
2. 入口用原相对顺序 `include` 这些文件。  
3. 跑 `python3 scripts/check_module_manifest.py`、`module_depgraph.py --check`、一次 `cmake` configure，对比声明名列表与 `#295` 时的 93/… 计数口径。  
4. 删除已空的大碎片文件。

**验收：** 给 `procgen` 加卫星或改 DEPS 时，diff 只出现在 `modules/procgen.cmake` +（偶尔）入口 include 的一行。

---

### 4.2 架构契约目录：按模块 JSON 碎片

**现状**

- 单一 `scripts/architecture_contracts.json`（~1500 行，43 条 `entries`）。  
- 新 Link / ECS / module-interface 契约几乎总是追加同一文件；`sort_architecture_contracts.py` 全文件重排放大 diff。

**目标布局**

```
scripts/architecture_contracts/
  _global.json                 # api-shape 等跨模块政策
  procgen.json
  physics.json
  graphics.json
  …
scripts/architecture_contracts.json   # 兼容层：生成物或薄包装
```

组合规则：

1. 加载器（`check_architecture_contracts.load_json` 或新 `load_catalogue()`）读取目录内所有 `*.json`，合并 `entries`，按现有 `catalogue_sort_key` 排序后校验。  
2. **每个 entry 的 `scope` 必须落在碎片所属模块路径下**（`_global` 除外）；CI 拒绝“写在 `graphics.json` 却 scope 到 `procgen/`”的错挂。  
3. 根文件 `architecture_contracts.json` 过渡期二选一（推荐 A）：  
   - **A.** 由脚本生成的 composed 快照（CI 校验“源碎片 ↔ 快照”一致，避免双源）；  
   - **B.** 删除根文件，所有调用改指目录（改动面更大）。  
4. `sort_architecture_contracts.py` 改为**按碎片内排序**或“重写 composed 快照”，不再鼓励人手编辑巨石 JSON。

**验收：** 新增一条 `procgen` link 契约只改 `architecture_contracts/procgen.json`。

---

### 4.3 巨石实现 TU：按已有子域切开（不改公共 API）

#### 4.3.1 `Procgen.cpp`（~5700 行，双方同改频次最高）

目录里已有 `road/`、`heightmap/`、`mesh/`、`PointSet.cpp`、`RuntimeGeneration.cpp` 等；`Procgen.cpp` 仍堆着 handle API、实例发布、以及两段巨大的 `expose`。

建议纯移动（无行为变化），类声明仍在 `Procgen.h`：

| 新 TU | 内容 |
|-------|------|
| `ProcgenHandles.cpp` | Params/Grid/Output/PointSet handle 生命周期 |
| `ProcgenPointSetApi.cpp` | `*PointsHandle` / filter / poisson / merge 等 |
| `ProcgenInstances.cpp` | `publish*Instances` / cell sync / remove |
| `ProcgenBindingsTable.cpp` | `expose(ssq::Table&)` |
| `ProcgenBindingsClass.cpp` | `expose(ssq::Class&)` |
| `Procgen.cpp` | 构造、ownership、薄转发或仅剩杂项 |

约束：

- 不改脚本绑定符号名与 Result 形状。  
- 匿名命名空间助手跟着调用方走，避免 `Procgen.cpp` 再变汇合点。  
- 单文件行数软上限 ~1000（与协作约定一致）；超限再拆。

#### 4.3.2 `Graphics*.h`（枢纽头，禁止拆运行时模块）

编排文档 §七：**不要**把 `graphics` 拆成多个可裁剪模块。去冲突做法是 **API 面分头文件**，总头只做聚合：

```
graphics/Graphics.h                 # 类声明骨架 + include 子头（稳定、少改）
graphics/api/MeshTypes.h            # UBO / vertex / GpuMesh…
graphics/api/PostProcessSurface.h   # AO/Bloom/DoF 等前向声明与访问器声明
graphics/api/LightingSurface.h
graphics/vulkan/GraphicsPipeline.h  # 已部分存在则继续外提
```

规则：

- 公共类型迁移必须保持 `#include "graphics/Graphics.h"` 的翻译单元可编译（子头由总头 include）。  
- 新功能默认落在 `api/<Feature>Surface.h` + 对应 `.cpp`，禁止继续拉长 `vulkan/Graphics.h`。  
- 与 §七 一致：这是**编译/冲突**拆分，不是裁剪轴拆分。

---

### 4.4 CI 工作流：平台 job 外提

**现状：** `.github/workflows/ci.yml` ~1865 行，双方同改频次高（矩阵、超时、缓存、lane）。

**目标：**

```
.github/workflows/ci.yml                 # 触发、concurrency、change-scope、needs 图
.github/workflows/ci-windows.yml         # workflow_call
.github/workflows/ci-linux.yml
.github/workflows/ci-macos.yml
…
.github/actions/eve-*/action.yml         # 重复的 setup/cache 步骤
```

规则：

- 跨平台策略（filter、失败聚合）留在入口；平台超时 / 独有步骤进平台文件。  
- 不把“源码门禁脚本列表”叉成多份——门禁仍由 `source-quality` job 调同一组 `scripts/*`。

---

### 4.5 用户文档：完成 topic 拆分

#295 已把 `graphics` rendering-effects、`procgen` pointset 拆出一页。  
`docs/usr/modules/procgen.md` 仍 ~3300 行，继续按子域拆：

- `docs/usr/modules/procgen/roads.md`
- `docs/usr/modules/procgen/terrain.md`
- `docs/usr/modules/procgen/mesh-modifiers.md`
- 主 `procgen.md` 只留索引 + 概念 + 链到子页

`docs/usr/MODULES.md` / 模块索引只改链接行，避免正文冲突。

---

### 4.6 降低“机械全仓扫荡”类冲突（流程，非文件）

历史大冲突 `2ecf0c497`（~92 文件）：一边 `EditorResult→Result`，一边批量 `@brief`。  
文件已按模块分开，但**跨模块机械 PR**仍会制造宽冲突面。

约束写进协作约定：

1. **全仓 rename / 导出宏 / Doxygen 扫荡单独成 PR**，不进功能分支。  
2. 功能分支禁止“顺便”改无关模块的 editor 头。  
3. 若必须扫荡：按宿主包分批（`editor`、`procgen/editing`、`graphics/editing`…），每批可独立合入。

---

## 5. 依赖管理如何保持不变

| 检查 | 拆分后如何保证 |
|------|----------------|
| `eve_declare_module` 仍是唯一声明源 | 碎片只能通过入口有序 include 进入；禁止旁路 CMake 再 declare |
| DEPS ↔ include 图 | `module_depgraph.py` 读组合清单，规则不变 |
| 裁剪 / profile | `GROUP` / `OPTIONAL_DEPS` 仍在同一 declare 上；profile 解析代码不改语义 |
| boot `eve.moduleList` | 仍由 manifest 派生（`cmake/modules.cmake`），不手写 |
| 架构契约“单一目录” | 组合后的 catalogue 对 CI 仍是一份；碎片是物理存储 |
| Link 组无环 | `EVE_LINK_GROUP_TABLE` 不随模块碎片复制 |

**回归探针（每轨落地时必跑）：**

```sh
python3 scripts/check_module_manifest.py
python3 scripts/module_depgraph.py --check
ARCHITECTURE_BASE=HEAD make check/architecture-contracts
# 以及一次 linux-debug configure + deps 已缓存下的增量构建
```

---

## 6. 实施分期

| 阶段 | 内容 | 风险 | 冲突收益 |
|------|------|------|----------|
| **S0** | 本文合入；在 `AGENTS.md` 协作节加“机械扫荡单独 PR / 声明按宿主碎片”指针 | 无 | 流程 |
| **S1** | `orchestration` → `module_manifest/modules/<host>.cmake` + 有序 include | 低（纯移动） | 高 |
| **S2** | `architecture_contracts/` 按模块拆 + 加载器合并 | 中（脚本与 CI 路径） | 高 |
| **S3** | `Procgen.cpp` TU 拆分 | 中（编译/绑定） | 高 |
| **S4** | CI workflow_call 外提 | 中（CI 语义） | 中 |
| **S5** | `Graphics` API 子头 + procgen 文档 topic | 中高（含路径） | 中 |

每阶段独立 PR；S1/S2 不混功能。S3 按“一次只拆一类 API”可再拆多个 PR。

---

## 7. 成功度量

在合入后约两个迭代的 merge 样本上对比：

1. **双方同改文件 Top10** 中，中心路径（`orchestration.cmake`、`architecture_contracts.json`、`Procgen.cpp`、`ci.yml`）出现次数下降。  
2. 新增模块 / 契约 / 道路文档的 PR，中心文件 diff 行数 ≈ 0（或仅 include 一行）。  
3. 门禁绿：`check_module_manifest`、`module_depgraph --check`、`check/architecture-contracts` 与现网一致。  
4. 不出现第二份模块列表或手写 `EVELIBS`。

---

## 8. 与既有规范的关系

- 遵守「One manifest, one boot list」：**逻辑**仍是一份 manifest；本设计只规定其**物理切片与组合方式**。  
- 遵守「Big files get split」：`Procgen.cpp` / 过长头文件按已有段切开。  
- 遵守「Domain satellites under host package」：声明碎片按宿主包聚合卫星，而不是按 `*_editor` 铺成顶层清单。  
- 不削弱 Result / Link / 架构契约语义；只改变契约条目的存储位置。

---

## 9. 建议的第一刀（S1 草图）

入口伪代码（顺序示意，落地时以当前声明序为准）：

```cmake
# cmake/module_manifest.cmake
include_guard(GLOBAL)
include(${CMAKE_CURRENT_LIST_DIR}/modules.cmake)
# … EVE_TP_ORDER …

include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/core_foundation.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/platform_resources.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/input_playback.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/rendering_hub.cmake)

# 原 rendering_simulation / orchestration 按宿主切开后的有序表：
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/graphics.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/physics.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/map.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/procgen.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/rts.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/module_manifest/modules/editor_satellites.cmake)
# …

# EVE_LINK_GROUP_TABLE 仍在此处
```

S1 合并标准：**声明集合与 LAYER 完全一致**（脚本 JSON dump 对比），configure 产物中的模块列表一致。

---

## 10. 开放问题

1. 声明碎片放在 `cmake/module_manifest/modules/` 还是 `src/modules/<host>/module.cmake`？  
   - **推荐前者**：CMake 契约集中、与现有 `manifest_files()` 一致、避免 `src/` 被 configure 扫进源码观感。  
   - 若选后者：入口仍要有序 include，且门禁须禁止“有目录无 declare”。
2. `architecture_contracts.json` 根文件保留为生成快照，还是直接删？推荐过渡期保留生成快照。  
3. `editor_satellites.cmake` 是按层（全部 L7 editor）还是强制每宿主一份？  
   - 宿主已有多卫星时每宿主一份；仅单行 declare 的小模块可暂留在 `modules/_misc_l5.cmake`，避免几百个十行文件，但**新模块默认新建宿主文件**。

---

## 11. 结语

去中心化不是取消依赖管理，而是：**把“顺序与闭合”留在中心，把“内容所有权”按模块切开。**  
#295 证明了拆共享清单能降 CI 冲突；本设计把同一原则推到仍在发热的 orchestration、契约目录、巨石 TU 与 CI 工作流，并明确机械扫荡必须单独成轨，避免再次出现百文件级 merge。
