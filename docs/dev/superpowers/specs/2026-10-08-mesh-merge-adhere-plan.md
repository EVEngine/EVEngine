# 网格合并与曲面粘合（Merge Actors / Mesh Adhere）计划

日期：2026-10-08  
状态：计划（未实现）  
来源：对照 Blender 插件 Merge Master（非破坏曲面粘合）与 UE5 Merge Actors（多 Actor 合并优化）的能力讨论；落点为现有 `procgen` 网格栈，而非新建 Modeling Mode。

## 背景

创作侧常见两类「合并」需求，语义不同，不能混成一个工具：

1. **结构合并（UE Merge Actors 类）**  
   多个已变换的 Static Mesh 拼成一份 `MeshBuild` / 资产：按材质分 triangle group、可选 weld、可选简化 LOD，目标是减 draw call、统一碰撞/光照资源。本引擎已有 `MeshBuild::appendTransformed`、`mesh.merge`、`combinePcgStaticMeshes`、`buildPcgCombinedMeshLods`、`mesh.weld`。

2. **视觉粘合（Merge Master 类）**  
   活动网格 A 在接触带内贴合基体 B，带 Strength / Radius / Falloff 与法线过渡；预览非破坏，满意后再 Bake。本引擎已有射线贴合 `deform.meshFit`、`mesh.boolean`、`MeshDeformationSession`，但缺少「按距离衰减的接触带粘合 + 缝线法线混合 + 可选材质过渡」的一等节点。

本计划按两阶段组织实现顺序（先结构合并，再粘合算法与会话），但**全部在同一实现 PR 内交付**，不再拆成多个功能 PR。

## 目标

1. 提供作者/脚本可调用的 **结构合并** 入口，覆盖 UE Merge Actors 的常用路径（Merge + 可选 weld/simplify），并带稳定 `Result` 诊断。
2. 新增 **曲面粘合** 修改器节点（建议名 `deform.meshAdhere`），参数语义对齐 Merge Master 公开文档（Strength / Radius / Falloff / Normals），实现完全自研。
3. 粘合支持非破坏会话（预览 / 撤销 / Bake），Bake 产出 owning `MeshBuild`，可选再接现有 `mesh.boolean` 去掉内面。
4. 编辑器入口挂在 `procgen_editing` / `procgen_editor`（`MeshModifierEditor`），不新建顶层模块。
5. 全部公共路径遵守 Result / `[[nodiscard]]`、确定性契约、裁剪构建与架构门禁。

## 非目标

- 不整段移植 Merge Master（GPL 插件）或 UE 源码；只参考公开交互与能力分层，算法自研。
- 不做完整 Modeling Mode / GeometryScript 命名空间 / BREP / NURBS。
- 不做各向同性 Remesh、PolyGroup Remesh、Nanite Approximate Proxy 的完整复刻（结构合并阶段仅接现有 simplify）。
- 不做玻璃/折射材质下的粘合着色特判（Merge Master 亦声明不适合）。
- 不把粘合逻辑放进 `scene` loader 或 `graphics` GPU 网格；loader/GPU 仅消费 Bake 结果。
- 第一期不做 Cycles 式「Bake for renderer + Bevel 节点」渲染补偿；Eevee/本引擎前向路径以 CPU 混合法线为准。

## 模块归属

```
L5  procgen                 算法与 MeshModifierGraph / MeshGraph 节点、combine 门面
L6  procgen_editing         文档目标、gizmo、作者会话绑定
L6  asset_procgen           仅当 Bake/合并结果需写入 EVMESH 包时
L7  procgen_editor          MeshModifierEditor 工具条与面板
    graphics / asset        上传与编码消费者，不拥有合并权威状态
```

新增文件优先落在 `src/modules/procgen/mesh/`（与 `MeshBoolean`、`MeshModifierGraph` 并列）；大文件继续按现有分段拆 TU，避免继续膨胀已超阈值的 `.cpp`。

## 能力对照

| 能力 | 现状 | 本计划 |
|------|------|--------|
| 多网格变换拼接 | `appendTransformed` / `mesh.merge` / `combinePcgStaticMeshes` | Phase A 统一门面 + 诊断码 |
| 按材质 section | triangle group / material id | 保持；门面文档化 |
| Weld | `mesh.weld` | Phase A 可选步骤 |
| 简化 LOD | `buildPcgCombinedMeshLods` / GTS simplify | Phase A 可选 profile |
| 射线贴合 | `deform.meshFit` | 保留；粘合不替换它 |
| 接触带粘合 | 无 | Phase B `deform.meshAdhere` |
| 法线过渡 | 贴合后整网 `recalculateNormals` | Phase B 带状混合 |
| 材质过渡 | 无 | Phase B 可选（强度/半径）；可延期 B2 |
| 布尔去内面 | `mesh.boolean` | Bake 后可选图节点，不内嵌死绑 |
| 非破坏会话 | `MeshDeformationSession` | Phase B 复用或薄封装 |
| 替换场景实例 | 无一等「Replace Source Actors」 | Phase A 可选 editing 事务；默认可只产出网格 |

---

## Phase A — 结构合并（Merge Actors 类）

### A1. 领域 API

在 `procgen` 增加 owning 门面（名称可微调，语义固定）：

```text
MeshMergePlan
  - appendSource(const MeshBuild&, transform, defaultMaterialId)
  - options: weldTolerance (optional), simplifyProfile (optional),
             mergeVertexColors, pivotMode (firstSource | worldOrigin)

Result<MeshBuild> mergeStaticMeshes(const MeshMergePlan&)
```

约束：

- 输入仅借用；成功才返回新 `MeshBuild`；失败不发布部分网格。
- 诊断前缀稳定：`procgen.mesh.merge.*`（空计划、非法变换、零缩放、不兼容属性等）。
- 与现有 `combinePcgStaticMeshes` 的关系：门面可内部委托；旧 API 保留为兼容或薄包装，文档标明 canonical 入口。
- Pivot：`firstSource` 对齐 UE「首选项枢轴」；`worldOrigin` 对齐「Pivot Point at Zero」。
- 不做物理碰撞合并的第一期承诺；若后续接 collider cook，另开子任务并用 Result 标明未实现。

### A2. 图 / 脚本

- `MeshGraph` / 脚本：`eve.mergeStaticMeshes(plan)` 或等价；参数与 C++ 同构。
- 现有 `mesh.merge`（二输入 append）保留；文档说明多源 + 选项走新门面。

### A3. 编辑器

- `procgen_editor`：选中多个网格目标 → Merge → 弹出选项 → 写出新网格资源/实例。
- 「替换源」为可选事务：成功写入后再移除/隐藏源；失败回滚，不留半替换场景。
- 主线程亲和；不在持锁时调脚本。

### A4. 验收

- 单测：两/三网格变换合并、材质 group 合并、weld 开/关、空计划失败、自引用失败、确定性（同输入同字节布局）。
- 组合测：合并 → 上传 graphics mesh（若现有测试夹具允许）或 `CanonicalMesh` 编码往返。
- 文档：`docs/usr/modules/procgen.md` 增加 Merge 小节，对照 UE Merge / Simplify 的覆盖范围与非目标。

### A5. 实现顺序（同 PR 内）

1. `MeshMergePlan` + `mergeStaticMeshes` + 单测。
2. 脚本绑定 + usr 文档。
3. Editor 工具 + 替换源事务（与算法同 PR；若排期紧张可先做只产出网格、不替换场景的最小编辑器入口，但仍落在同一 PR）。

---

## Phase B — 曲面粘合（Merge Master 类）

### B1. 算法节点 `deform.meshAdhere`

**输入**：源网格 A、表面网格 B（与 `deform.meshFit` 相同的双输入约定）。  
**输出**：owning 变形后的 A（拓扑与索引不变；仅位置/法线，可选顶点色作材质混合权重）。

**参数（v1）**：

| 参数 | 含义 |
|------|------|
| `strength` | 朝 B 表面拉动的强度 \[0,1\]（或与 Global 联动的标量） |
| `radius` | 接触带世界空间半径 |
| `falloff` | `smooth` / `linear` / `sharp` / `sphere`（曲线预设；自定义曲线可 B2） |
| `normalsBlend` | 过渡带法线混合强度 |
| `surfaceOffset` | 沿命中法线的间隙（复用 meshFit 语义） |
| `maxQueryDistance` | 最近点搜索上限（性能与稳定性） |

**数学（v1，可测）**：

1. 对 A 每个顶点 \(p\)，求 B 上最近点 \(q\) 与法线 \(n\)（三角网格：BVH + 点到三角；无 BVH 时允许暴力实现，但必须用相同 Result 路径，并在文档标明复杂度）。
2. 距离 \(d = \|p-q\|\)；若 \(d > \texttt{maxQueryDistance}\) 则跳过。
3. 权重 \(w = \mathrm{falloff}(1 - \mathrm{saturate}(d / \texttt{radius})) \cdot \texttt{strength}\)。
4. \(p' = \mathrm{lerp}(p, q + n\cdot\texttt{surfaceOffset}, w)\)。
5. 法线：在带内将顶点法线与 \(n\) 按 \(w \cdot \texttt{normalsBlend}\) 混合后归一化；带外保持或整网重算策略在实现前写死一种并测黄金夹具。

**确定性**：同输入网格与参数 → 同顶点浮点结果（CPU 容差内）；禁止依赖哈希表遍历顺序；BVH 构建顺序固定。

**与 `deform.meshFit` 的分工**：

- `meshFit`：沿指定方向的有界射线贴合（已有）。
- `meshAdhere`：各向最近点 + 半径衰减粘合（新建）。二者并存，不互相废弃。

### B2. 可选增强（不阻塞 v1）

- 材质混合：在半径内写 blend 权重属性或顶点色；不改写 UV。
- 自定义 falloff 曲线。
- Bake 后自动 `mesh.boolean(difference/union)` 去内面（打印/封闭实体工作流）。
- 多 A 共享同一 B 的独立参数（参数存在图节点或 editing 文档上，权威在图/会话，不在 GPU mesh）。
- 链式粘合：B 可为上一节点输出（图组合自然支持）。

### B3. 非破坏会话与 Bake

- 复用或扩展 `MeshDeformationSession`：Activate 保存 A 快照；参数修改只重算预览；`RemoveSetup` 恢复快照；`BakeToMesh` 提交 owning 网格并结束会话。
- Activate 时若需 apply 已有 modifier，遵循「先求值图再粘合」；不在粘合节点内隐式破坏调用方持有的源指针。
- 失败：返回 `Result`，预览缓冲不部分发布。

### B4. 编辑器

- `MeshModifierEditor`：选 A（active）+ B → Adhere 工具 → 面板暴露 Global（联动 strength/radius/normals）与分项滑条。
- 快捷键与 Blender 插件无需一致；文档写清本引擎绑定。
- 可视化：可选接触带 overlay（editing 层），运行时裁剪配置可关。

### B5. 验收

- 单测：奇异平面上立方体贴合、radius=0 为恒等、strength=0 为恒等、远离表面不变、法线混合夹具、失败注入（空 B、非法 radius）。
- 与 boolean 组合测：Adhere → Bake → `mesh.boolean` 一条成功路径。
- 架构：`ARCHITECTURE_BASE=... make check/architecture-contracts`；无新向上依赖；`procgen` 不依赖 editor。
- 法律：实现与测试夹具均为自研；文档「参考」一节只列公开行为对照表，不附第三方源码。

### B6. 实现顺序（同 PR 内，接在 Phase A 之后）

1. CPU 节点 `deform.meshAdhere` + BVH/最近点 + 单测。
2. 会话 Activate / Bake / Remove + 图集成。
3. Editor 面板与 usr 文档。
4. B2 增强（材质混合、自定义 falloff、Bake 后自动 boolean 等）**不进本 PR**；需要时另开后续工作，不在此计划拆 PR。

---

## 跨阶段质量与架构门禁

适用规范（交接时逐条汇报）：

- `docs/dev/重构代码质量与系统完整性规范.md`
- `docs/dev/Result检查与不得丢弃返回值规范.md`
- `docs/dev/领域短根继承与跨域组合架构.md`
- 顶层门禁：`ARCHITECTURE_BASE=<base> make check/architecture-contracts`

硬性要求：

- 公共 API 用 `Result` + `[[nodiscard]]`；禁止新增含混 `bool` / lastError。
- 可变权威：合并/粘合输出的 `MeshBuild` 由调用方或 editing 文档拥有；graphics 资源为派生缓存。
- 仿真/离线图执行注入时间与种子（本功能默认无 RNG；若 B2 引入抖动需命名流）。
- 新 TODO/HACK/软跳过必须带 owner / issue / 原因 / 移除条件。
- 格式：仅 `git clang-format` 改动行；测试按模块落在 `test/procgen_mesh_*.cpp`。

## 风险与缓解

| 风险 | 缓解 |
|------|------|
| 最近点查询在大网格上过慢 | v1 加 BVH；测大网格上限；允许后续 worker 线程（纯函数、无回调） |
| 粘合产生自交/破洞 | 单测夹具 + 参数钳制；文档警告过大 strength/radius；可选后接 boolean |
| 与 meshFit 用户混淆 | usr 文档对照表；编辑器工具提示 |
| 把 GPL/UE 代码带入仓库 | 仅读公开文档；代码审查禁止第三方摘录 |
| combine 旧 API 双真相 | Phase A 标明 canonical；旧路径委托新实现 |

## 交付方式（单 PR）

- **计划文档**可先合入（本文件所在变更）。
- **实现**将 Phase A（结构合并 API + 脚本 + 编辑器）与 Phase B v1（`deform.meshAdhere` + 会话 + 编辑器 + 测试 + usr 文档）放在**同一个实现 PR** 中一次提交完整能力面；本地可按 A→B 顺序开发与提交多个 commit，但不拆多个 PR。
- Phase B2（材质混合等可选增强）明确排除在该实现 PR 之外，避免范围膨胀；不为此预拆 PR 号。
- 合并前一次跑通：相关 `procgen_mesh*` 单测、组合路径、`check/architecture-contracts`、格式检查。

## 参考（公开行为，非实现来源）

- Merge Master 文档：非破坏粘合、A=active / B=target、Global Merge、Bake / Remove Setup、可选 Boolean。
- UE5 Merge Actors：Merge / Simplify / Batch / Approximate；Replace Source Actors；枢轴与材质 section。
- 本仓库：`docs/usr/modules/procgen.md`（`deform.meshFit`、`combinePcgStaticMeshes`、`mesh.boolean`）。

## 交接检查清单（唯一实现 PR）

- [ ] Phase A 与 Phase B v1 同 PR 完整交付（含编辑器入口）
- [ ] Canonical API 与诊断码已文档化
- [ ] 新旧 combine/merge 无双真相或已标明兼容层
- [ ] Adhere 与 meshFit 分工写清
- [ ] 确定性与线程/重入注释齐全
- [ ] 单测 + 至少一条组合路径（含 Adhere → Bake → 可选 boolean）
- [ ] `check/architecture-contracts` 与相关 `make test FILTER=procgen_mesh*` 通过
- [ ] 无第三方源码摘录；B2 未偷加进本 PR
