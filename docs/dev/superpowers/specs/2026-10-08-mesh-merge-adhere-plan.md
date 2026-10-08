# 网格合并与曲面粘合（Merge Actors / Mesh Adhere）计划

日期：2026-10-08  
状态：计划（未实现）  
来源：对照 Blender 插件 Merge Master（非破坏曲面粘合）与 UE5 Merge Actors（多 Actor 合并优化）的能力讨论；落点为现有 `procgen` 网格栈，而非新建 Modeling Mode。

## 背景

创作侧常见两类「合并」需求，语义不同，不能混成一个工具：

1. **静态合并（UE Merge Actors 类）**  
   作者向、**提交式**：多个已变换 Static Mesh 一次算成一份 owning `MeshBuild` / 资产（默认：拼接 + 可选 weld/simplify）。**边缘/材质融合为可选能力，默认关闭**；需要缝线过渡时再显式打开。结果落盘或替换实例后，源关系结束；再调参数需重新跑合并。本引擎已有拼接/weld，**没有**接触带融合内核。

2. **动态融合（Merge Master 类粘合，引擎实时）**  
   运行时/编辑器内**持续求值**：A 相对 B 的接触带位置贴合 + 边缘/材质融合由引擎每帧或参数变更时完成；Strength / Radius / Falloff / 法线与材质混合等**可随时调整**，不必 Bake 才能看到效果。源网格拓扑保持权威；显示网格为派生缓存。可选 Bake 只是「冻结为静态资产」的出口，不是动态路径的前提。

二者共享 `MeshContactBlend` 原语；差别是生命周期：静态 = 一次提交，动态 = 实时可调会话/组件。

本计划按两阶段组织实现顺序（先静态合并，再动态融合），但**全部在同一实现 PR 内交付**，不再拆成多个功能 PR。

## 目标

1. **静态合并**入口：默认拼接 + 可选 weld/simplify；**可选**边缘/材质融合（**默认关闭**）；`Result` 诊断稳定。
2. 共享 CPU 原语 `MeshContactBlend`（接触带权重 → 边缘/法线 + 材质权重），供静态合并（显式开启时）与动态融合共用。
3. **动态融合**一等能力（节点/会话/运行时组件，建议名 `deform.meshAdhere` + live session）：
   - 引擎内实时完成求值（主线程或声明为可并行的纯 CPU 段，再回传显示网格）；
   - 参数与变换**随时可调**，调参即重算派生网格，无需先 Bake；
   - 关闭/移除 setup 恢复源外观；可选 `BakeToMesh` 冻结为静态资产。
4. 编辑器与运行时脚本均可驱动动态融合；UI 挂 `procgen_editing` / `procgen_editor`，不新建顶层模块。
5. 全部公共路径遵守 Result / `[[nodiscard]]`、确定性契约、裁剪构建与架构门禁。

## 非目标

- 不整段移植 Merge Master（GPL 插件）或 UE 源码；只参考公开交互与能力分层，算法自研。
- 不做完整 Modeling Mode / GeometryScript 命名空间 / BREP / NURBS。
- 不做各向同性 Remesh、PolyGroup Remesh、Nanite Approximate Proxy 的完整复刻（静态合并仅接现有 simplify）。
- 不做玻璃/折射材质下的粘合着色特判。
- 不把融合**权威状态**放进 `scene` loader 或 graphics 资源；GPU 网格只消费每次求值产出的派生缓冲（动态路径是持续上传/更新，不是「只能消费 Bake」）。
- v1 动态融合以 CPU `MeshContactBlend` + 现有网格顶点更新路径为准；不强制首版 GPU compute 粘合。
- 第一期不做 Cycles 式「Bake for renderer + Bevel 节点」渲染补偿。

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
| 多网格变换拼接 | `appendTransformed` / `mesh.merge` / `combinePcgStaticMeshes` | Phase A 门面第一步 |
| 按材质 section | triangle group / material id | 保留；融合带可写 blend，不强制打成单一材质 |
| Weld | `mesh.weld` | Phase A 可选硬焊 |
| **边缘融合**（接触带法线/软几何） | 无 | Phase A **可选、默认关**；内核与动态融合共用 |
| **材质融合**（接触带权重/混合） | 无 | Phase A **可选、默认关**；Adhere 复用同内核 |
| 简化 LOD | `buildPcgCombinedMeshLods` / GTS simplify | Phase A 可选；若开启融合则建议在融合之后 |
| 射线贴合 | `deform.meshFit` | 保留 |
| 接触带位置粘合 | 无 | Phase B 动态融合 |
| **实时可调动态融合** | 无（仅有离线 modifier 求值） | **Phase B 必做**：参数/位姿变更即重算 |
| 布尔去内面 | `mesh.boolean` | 静态 Bake 后可选；动态路径默认不做破坏性布尔 |
| 非破坏实时会话 | `MeshDeformationSession` 雏形 | Phase B 扩展为 live（含运行时） |
| 替换场景实例 | 无 | Phase A editing 事务（可选） |

---

## 共享原语 — `MeshContactBlend`（A/B 共用，同 PR 先落地）

静态合并与曲面粘合都依赖同一接触带逻辑，**先实现共享内核，再挂两套入口**。

**输入约定**：

- 若干「源片」：每片有 world 空间 `MeshBuild`（或已变换副本）与稳定 `sourceId`（静态合并 = 各 Actor；粘合 = A vs B）。
- 融合参数：`edgeRadius`、`materialRadius`（可与 edge 共用或独立）、`strength`、`falloff`、`normalsBlend`、`materialBlend`。

**对每个顶点（归属 source S）**：

1. 在**其他源片**表面上求最近点 \(q\)、法线 \(n\)、对方 `sourceId` / material group（BVH；构建顺序确定性）。
2. \(d=\|p-q\|\)；超出查询上限则跳过。
3. \(w_{\mathrm{edge}}=\mathrm{falloff}(1-\mathrm{saturate}(d/\texttt{edgeRadius}))\cdot\texttt{strength}\)。
4. **边缘融合**：法线与 \(n\) 按 \(w_{\mathrm{edge}}\cdot\texttt{normalsBlend}\) 混合；可选位置软拉向 \(q\)（静态合并若开启融合：默认 `softSnapPositions=false`；动态融合默认位置贴合开）。
5. \(w_{\mathrm{mat}}=\mathrm{falloff}(1-\mathrm{saturate}(d/\texttt{materialRadius}))\cdot\texttt{materialBlend}\)。
6. **材质融合**：写入 per-vertex blend 权重（顶点色 alpha 或命名 float 属性，如 `contactBlend`）及对方 material group id（或双权重）；**不改写 UV**。渲染侧用现有/后续材质图做 lerp；CPU 侧保证权重确定性。若引擎暂无运行时双材质采样，v1 至少产出可烘焙的权重属性 + 文档约定，并提供「Bake 到单一 atlas/顶点色近似」的可选路径之一（实现时选一种写死并测通）。

**输出**：owning 网格（或就地写入合并缓冲）+ 不部分发布；诊断 `procgen.mesh.blend.*`。

---

## Phase A — 静态合并（默认拼接；融合可选且默认关）

### A1. 领域 API

```text
MeshMergePlan
  - appendSource(const MeshBuild&, transform, defaultMaterialId)
  - options:
      weldTolerance (optional)
      simplifyProfile (optional)
      mergeVertexColors
      pivotMode (firstSource | worldOrigin)
      // 接触带融合：可选；默认全部关闭
      enableContactBlend (bool, default false)
      edgeRadius, materialRadius          // 仅 enableContactBlend 时生效
      strength, falloff
      normalsBlend, materialBlend
      softSnapPositions (bool, default false)

Result<MeshBuild> mergeStaticMeshes(const MeshMergePlan&)
```

流水线（成功才替换输出）：

1. 变换并拼接各源（保留 per-vertex `sourceId` / material group，便于后续可选融合）。
2. 可选 `weld`（硬拓扑合并）。
3. **仅当 `enableContactBlend==true`**：跑 `MeshContactBlend`（边缘 + 材质融合）。默认跳过本步，行为对齐纯拼接。
4. 可选 simplify LOD。
5. 按 pivotMode 重定位。

默认值契约：

- 新建 `MeshMergePlan` / 编辑器「Merge」对话框：`enableContactBlend=false`，融合半径与 blend 强度为 0 或不生效。
- 用户勾选「接触带融合」后才应用 `edgeRadius` / `materialBlend` 等；文档与 UI 不得暗示默认已融合。

约束：

- 输入仅借用；失败不发布部分网格。
- 诊断：`procgen.mesh.merge.*` / `procgen.mesh.blend.*`。
- `combinePcgStaticMeshes` 可作底层拼接；`mergeStaticMeshes` 为 canonical 作者入口（默认无融合，可选开融合）。避免「默认会融合」的双真相。
- Pivot：`firstSource` / `worldOrigin` 对齐 UE 常见选项。
- 物理碰撞合并仍非本 PR 承诺。

### A2. 图 / 脚本

- `eve.mergeStaticMeshes(plan)`；融合参数与 C++ 同构；脚本侧默认亦为关闭融合。
- 现有 `mesh.merge`（二输入 append）保留；需要缝线融合时设 `enableContactBlend=true`。

### A3. 编辑器

- 选中多网格 → Merge → **默认不勾选融合**；选项区含可选 Edge / Material / Normals Blend（及 weld/simplify）。
- 「替换源」可选事务；失败回滚。
- 主线程亲和；不在持锁时调脚本。

### A4. 验收

- 默认路径：两/三网格变换合并与纯拼接（容差内）一致；不写 blend 属性（或权重全 0）。
- 显式开启融合：`enableContactBlend=true` 且半径/强度 >0 时接缝法线连续、材质权重落入 (0,1)。
- weld 开/关、空计划失败、确定性。
- 组合：默认合并与开启融合各一条 → `CanonicalMesh` 往返。
- usr 文档：写清默认关闭、如何开启、融合属性名。

### A5. 实现顺序（同 PR 内）

1. `MeshContactBlend` + 单测（法线/材质权重夹具）。
2. `MeshMergePlan` + `mergeStaticMeshes`（默认跳过 blend；可选开启）+ 单测。
3. 脚本绑定 + usr 文档。
4. Editor 合并工具（融合开关默认关）。

---

## Phase B — 动态融合（引擎实时、随时可调）

### B1. 算法节点 `deform.meshAdhere`

**输入**：源网格 A、表面网格 B（与 `deform.meshFit` 相同的双输入约定）。  
**输出**：每次求值产出 owning/派生变形网格（拓扑与索引相对源 A 不变；位置/法线 + 材质融合权重，属性名与静态合并一致）。

**参数（v1）**：与 `MeshContactBlend` 对齐，并增加粘合专用项：

| 参数 | 含义 |
|------|------|
| `strength` / `edgeRadius` / `materialRadius` | 同共享原语；Global 可联动 |
| `falloff` | `smooth` / `linear` / `sharp` / `sphere`（自定义曲线可延期） |
| `normalsBlend` / `materialBlend` | 边缘法线与材质融合（**v1 必做**） |
| `softSnapPositions` | 默认 `true`：位置贴向 B |
| `surfaceOffset` | 沿命中法线间隙 |
| `maxQueryDistance` | 最近点搜索上限 |

**实现**：双源调用 `MeshContactBlend`（A←B；通常只变形 A）；\(p'=\mathrm{lerp}(p,q+n\cdot\texttt{surfaceOffset},w_{\mathrm{edge}})\)。

**与 `deform.meshFit` 的分工**：`meshFit` = 定向射线贴合；`meshAdhere` = 各向最近点 + 实时边缘/材质融合。并存。

### B2. 实时生命周期（相对静态合并的关键差异）

| | 静态合并 | 动态融合 |
|--|----------|----------|
| 时机 | 作者提交一次 | 引擎持续/按需求值 |
| 融合 | **可选，默认关** | 路径核心，实时求值 |
| 参数 | 写入 plan 后算完即固定 | **随时改** strength/radius/… 与 A/B 位姿 |
| 源网格 | 可替换为合并结果 | 源保持权威；显示为派生 |
| Bake | 合并本身即提交 | **可选**冻结；不 Bake 也可用于运行时 |

权威状态建议：

- `MeshAdhereLive`（或扩展 `MeshDeformationSession`）：持有对 A/B 源的借用或 generation-safe handle、融合参数、revision。
- `setParam*` / 源变换脏标记 → 递增 revision → `evaluateResult()` 重算派生 `MeshBuild`（失败不发布半帧）。
- 编辑器与运行时脚本共用同一 evaluate 路径；tick 策略：`evaluateOnDirty`（默认）与可选 `evaluateEveryFrame`（B 或 A 持续运动时）。
- 将派生顶点更新到显示 mesh 走现有 graphics 更新 API；**禁止**把 live 参数藏进 GPU mesh 元数据当第二真相。
- `RemoveSetup`：丢掉派生、恢复源显示；`BakeToMesh`：写出 owning 静态网格并可选择结束 live。

线程：求值函数纯输入→输出；默认主线程调用；若丢到 worker，完成回调回主线程再发布，且不持锁调脚本。

### B3. 可选增强（不进本实现 PR）

- 自定义 falloff 曲线编辑器。
- Bake 后自动 `mesh.boolean` 去内面。
- GPU compute 加速最近点/融合。
- 完整运行时双材质采样着色器（v1 可用权重属性 + 顶点色近似）。
- 多 A 共享 B 的批量 live 管理器 UX。

### B4. 编辑器

- 选 A（active）+ B → Dynamic Adhere：面板滑条**拖动即重算**（脏标记 evaluate），不必点 Apply/Bake。
- Bake / Remove 为显式按钮；文档写清「动态默认实时，Bake 仅冻结」。
- 可选接触带 overlay；运行时裁剪配置可关 overlay。

### B5. 验收

- 算法夹具：同 Phase A 共享的法线/材质断言；radius/strength=0 恒等。
- **实时可调**：同一会话内连续改 `strength`/`edgeRadius` 至少两档，派生网格随 revision 变化且源网格缓冲未改；`RemoveSetup` 后显示回到源。
- **位姿可调**：只移动 A 或 B 后 evaluate，接触带跟随（确定性夹具）。
- 可选 Bake 路径一条；与静态合并 blend 属性名一致。
- 架构 / 法律：同总清单。

### B6. 实现顺序（同 PR 内，接在共享内核与 Phase A 之后）

1. `deform.meshAdhere` + `MeshContactBlend` 单测。
2. Live 会话：脏参数/位姿 → evaluate → 更新显示；Remove / 可选 Bake。
3. 脚本 tick/调参 API + Editor 实时滑条 + usr 文档（静态 vs 动态对照表）。
4. B3 项不进本 PR。

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
| 融合/粘合产生自交或材质闪烁 | 夹具 + 参数钳制；静态合并默认 `softSnapPositions=false`；文档警告过大半径 |
| 材质融合无运行时双采样 | v1 固定一种可测路径（权重属性 ± 顶点色近似）；完整双材质 shading 放 B2 |
| 与 meshFit / 纯 combine 混淆 | usr 对照表；静态合并 UI **默认关融合**，避免误开 |
| 把 GPL/UE 代码带入仓库 | 仅读公开文档；审查禁止第三方摘录 |
| combine 旧 API 双真相 | canonical = `mergeStaticMeshes`（默认无融合）；融合为显式选项 |

## 交付方式（单 PR）

- **计划文档**可先合入（本文件所在变更）。
- **实现**同一 PR 交付：共享 `MeshContactBlend` + Phase A（静态合并默认拼接，**融合可选默认关**）+ Phase B（**引擎实时动态融合**：随时调参/位姿重算 + 可选 Bake + 编辑器/脚本）。本地顺序：共享内核 → A → B；不拆功能 PR。
- B3 增强（GPU compute、完整双材质 shading 等）不进该实现 PR。
- 合并前跑通：`procgen_mesh*`、融合夹具、动态调参 revision 测、`check/architecture-contracts`、格式检查。

## 参考（公开行为，非实现来源）

- Merge Master 文档：非破坏粘合、A=active / B=target、Global Merge、Bake / Remove Setup、可选 Boolean。
- UE5 Merge Actors：Merge / Simplify / Batch / Approximate；Replace Source Actors；枢轴与材质 section。
- 本仓库：`docs/usr/modules/procgen.md`（`deform.meshFit`、`combinePcgStaticMeshes`、`mesh.boolean`）。

## 交接检查清单（唯一实现 PR）

- [ ] `MeshContactBlend` 落地；静态合并与 Adhere 共用
- [ ] Phase A：默认无融合拼接 + **可选**边缘/材质融合（默认关）+ 脚本 + 编辑器
- [ ] Phase B：动态融合 live（调参/位姿随时重算、源权威、可选 Bake）+ 编辑器实时滑条
- [ ] Canonical API 与诊断码已文档化；纯 combine vs 静态融合 merge vs 动态 live 无双真相
- [ ] Adhere 与 meshFit 分工写清；blend 属性名两端一致
- [ ] 确定性与线程/重入注释齐全
- [ ] 单测含边缘/材质融合夹具 + 动态 revision 调参/RemoveSetup + 至少一条组合路径
- [ ] `check/architecture-contracts` 与相关 `make test FILTER=procgen_mesh*` 通过
- [ ] 无第三方源码摘录；B3 未偷加进本 PR
