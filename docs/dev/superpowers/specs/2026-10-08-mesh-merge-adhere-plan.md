# 网格合并与曲面粘合（Merge Actors / Mesh Adhere）计划

日期：2026-10-08  
状态：计划（未实现）  
来源：对照 Blender 插件 Merge Master（非破坏曲面粘合）与 UE5 Merge Actors（多 Actor 合并优化）的能力讨论；落点为现有 `procgen` 网格栈，而非新建 Modeling Mode。

## 背景

创作侧常见两类「合并」需求，语义不同，不能混成一个工具：

1. **静态合并（UE Merge Actors 类 + 缝线融合）**  
   多个已变换的 Static Mesh 拼成一份 `MeshBuild` / 资产：拼接与可选 weld/simplify 之外，**还必须在源网格接触带做边缘融合（几何/法线）与材质融合**，避免「只拼拓扑、接缝硬切」的可见裂缝。本引擎已有 `appendTransformed` / `combinePcgStaticMeshes` / `mesh.weld`，但**没有**接触带融合内核。

2. **曲面粘合（Merge Master 类）**  
   活动网格 A 在接触带内贴合基体 B（Strength / Radius / Falloff + 法线/材质过渡）；预览非破坏，再 Bake。与静态合并共享同一套「接触带融合」原语，差别是粘合还要做位置贴合，且默认非破坏会话。

本计划按两阶段组织实现顺序（先静态合并含融合，再粘合会话），但**全部在同一实现 PR 内交付**，不再拆成多个功能 PR。

## 目标

1. 提供作者/脚本可调用的 **静态合并** 入口：拼接 + 可选 weld/simplify，且 **v1 即含边缘融合与材质融合**（可关，但默认开启合理半径），稳定 `Result` 诊断。
2. 抽取共享 CPU 原语（建议 `MeshContactBlend`）：按源归属做最近点/接触带权重 → 边缘（位置软焊或法线混合）+ 材质混合权重；供 `mergeStaticMeshes` 与 `deform.meshAdhere` 共用。
3. 新增 **曲面粘合** 节点 `deform.meshAdhere`（Strength / Radius / Falloff / Normals / 材质混合），实现自研；非破坏会话 + Bake；可选再接 `mesh.boolean`。
4. 编辑器入口挂在 `procgen_editing` / `procgen_editor`，不新建顶层模块。
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
| 多网格变换拼接 | `appendTransformed` / `mesh.merge` / `combinePcgStaticMeshes` | Phase A 门面第一步 |
| 按材质 section | triangle group / material id | 保留；融合带可写 blend，不强制打成单一材质 |
| Weld | `mesh.weld` | Phase A 可选硬焊 |
| **边缘融合**（接触带法线/软几何） | 无 | **Phase A 必做**（共享 `MeshContactBlend`） |
| **材质融合**（接触带权重/混合） | 无 | **Phase A 必做**（同内核；Adhere 复用） |
| 简化 LOD | `buildPcgCombinedMeshLods` / GTS simplify | Phase A 可选；建议在融合之后 |
| 射线贴合 | `deform.meshFit` | 保留 |
| 接触带位置粘合 | 无 | Phase B `deform.meshAdhere` |
| 布尔去内面 | `mesh.boolean` | Bake/合并后可选图节点 |
| 非破坏会话 | `MeshDeformationSession` | Phase B |
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
4. **边缘融合**：法线与 \(n\) 按 \(w_{\mathrm{edge}}\cdot\texttt{normalsBlend}\) 混合；可选位置软拉向 \(q\)（静态合并默认偏保守：以法线为主、位置软拉可关；粘合默认位置贴合开）。
5. \(w_{\mathrm{mat}}=\mathrm{falloff}(1-\mathrm{saturate}(d/\texttt{materialRadius}))\cdot\texttt{materialBlend}\)。
6. **材质融合**：写入 per-vertex blend 权重（顶点色 alpha 或命名 float 属性，如 `contactBlend`）及对方 material group id（或双权重）；**不改写 UV**。渲染侧用现有/后续材质图做 lerp；CPU 侧保证权重确定性。若引擎暂无运行时双材质采样，v1 至少产出可烘焙的权重属性 + 文档约定，并提供「Bake 到单一 atlas/顶点色近似」的可选路径之一（实现时选一种写死并测通）。

**输出**：owning 网格（或就地写入合并缓冲）+ 不部分发布；诊断 `procgen.mesh.blend.*`。

---

## Phase A — 静态合并（拼接 + 边缘/材质融合）

### A1. 领域 API

```text
MeshMergePlan
  - appendSource(const MeshBuild&, transform, defaultMaterialId)
  - options:
      weldTolerance (optional)          // 硬距离焊，与软边缘融合可并存
      simplifyProfile (optional)        // 建议 fuse 之后
      mergeVertexColors
      pivotMode (firstSource | worldOrigin)
      // 接触带融合（v1 一等公民；radius=0 可关闭）
      edgeRadius, materialRadius
      strength, falloff
      normalsBlend, materialBlend
      softSnapPositions (bool, default false for static merge)

Result<MeshBuild> mergeStaticMeshes(const MeshMergePlan&)
```

流水线（成功才替换输出）：

1. 变换并拼接各源（保留 per-vertex `sourceId` / material group）。
2. 可选 `weld`（硬拓扑合并）。
3. **`MeshContactBlend`**：跨 source 边缘融合 + 材质融合。
4. 可选 simplify LOD。
5. 按 pivotMode 重定位。

约束：

- 输入仅借用；失败不发布部分网格。
- 诊断：`procgen.mesh.merge.*` / `procgen.mesh.blend.*`。
- `combinePcgStaticMeshes` 可继续作「纯拼接、无融合」底层；canonical 作者入口是 `mergeStaticMeshes`（含融合）。文档标明二者差异，避免双真相。
- Pivot：`firstSource` / `worldOrigin` 对齐 UE 常见选项。
- 物理碰撞合并仍非本 PR 承诺。

### A2. 图 / 脚本

- `eve.mergeStaticMeshes(plan)`；融合参数与 C++ 同构。
- 现有 `mesh.merge`（二输入 append）保留为无融合快路径；文档引导需要缝线融合时走新门面。

### A3. 编辑器

- 选中多网格 → Merge → 选项含 **Edge Radius / Material Blend / Normals Blend**（及 weld/simplify）。
- 「替换源」可选事务；失败回滚。
- 主线程亲和；不在持锁时调脚本。

### A4. 验收

- 拼接：两/三网格变换、材质 group、weld 开/关、空计划失败、确定性。
- **融合**：两相交或贴合的盒子/平面——`edgeRadius>0` 时接缝法线连续（夹具度量）；`materialBlend>0` 时边界顶点权重落入 (0,1)；`edgeRadius=0` 且 `materialBlend=0` 时与纯拼接（容差内）一致。
- 组合：合并 → `CanonicalMesh` 往返（含 blend 属性）。
- usr 文档：写清融合属性名、与「仅 combine」的差异。

### A5. 实现顺序（同 PR 内）

1. `MeshContactBlend` + 单测（法线/材质权重夹具）。
2. `MeshMergePlan` + `mergeStaticMeshes`（拼接 → blend → 可选 weld/simplify）+ 单测。
3. 脚本绑定 + usr 文档。
4. Editor 合并工具（含融合滑条）。

---

## Phase B — 曲面粘合（Merge Master 类）

### B1. 算法节点 `deform.meshAdhere`

**输入**：源网格 A、表面网格 B（与 `deform.meshFit` 相同的双输入约定）。  
**输出**：owning 变形后的 A（拓扑与索引不变；位置/法线 + 材质融合权重，与静态合并同一属性约定）。

**参数（v1）**：与 `MeshContactBlend` 对齐，并增加粘合专用项：

| 参数 | 含义 |
|------|------|
| `strength` / `edgeRadius` / `materialRadius` | 同共享原语；Global 可联动 |
| `falloff` | `smooth` / `linear` / `sharp` / `sphere`（自定义曲线可延期） |
| `normalsBlend` / `materialBlend` | 边缘法线与材质融合（**v1 必做**，非延期项） |
| `softSnapPositions` | 默认 `true`：位置贴向 B |
| `surfaceOffset` | 沿命中法线间隙 |
| `maxQueryDistance` | 最近点搜索上限 |

**实现**：双源调用 `MeshContactBlend`（A←B；粘合阶段通常只变形 A）；位置项 \(p'=\mathrm{lerp}(p,q+n\cdot\texttt{surfaceOffset},w_{\mathrm{edge}})\)。

**与 `deform.meshFit` 的分工**：

- `meshFit`：沿指定方向的有界射线贴合（已有）。
- `meshAdhere`：各向最近点 + 半径衰减粘合 + 与静态合并相同的边缘/材质融合。二者并存。

### B2. 可选增强（不进本实现 PR）

- 自定义 falloff 曲线编辑器。
- Bake 后自动 `mesh.boolean` 去内面。
- 多 A 共享同一 B 的独立参数存储 UX。
- 运行时双材质采样着色器（若 v1 仅 CPU 权重 + 顶点色近似，完整 shading 可后续）。
- 链式粘合 UX 包装（图组合本身已支持）。

### B3. 非破坏会话与 Bake

- 复用或扩展 `MeshDeformationSession`：Activate 保存 A 快照；参数修改只重算预览；`RemoveSetup` 恢复快照；`BakeToMesh` 提交 owning 网格并结束会话。
- Activate 时若需 apply 已有 modifier，遵循「先求值图再粘合」；不在粘合节点内隐式破坏调用方持有的源指针。
- 失败：返回 `Result`，预览缓冲不部分发布。

### B4. 编辑器

- `MeshModifierEditor`：选 A（active）+ B → Adhere 工具 → 面板暴露 Global（联动 strength/radius/normals）与分项滑条。
- 快捷键与 Blender 插件无需一致；文档写清本引擎绑定。
- 可视化：可选接触带 overlay（editing 层），运行时裁剪配置可关。

### B5. 验收

- 单测：平面上立方体贴合；radius/strength=0 恒等；远离不变；**法线与材质权重**与静态合并夹具共用断言助手；失败注入（空 B、非法 radius）。
- 组合：Adhere → Bake → 可选 `mesh.boolean`；与 `mergeStaticMeshes` 共用 blend 属性名回归。
- 架构 / 法律：同总清单。

### B6. 实现顺序（同 PR 内，接在共享内核与 Phase A 之后）

1. `deform.meshAdhere` 接 `MeshContactBlend`（`softSnapPositions=true`）+ 单测。
2. 会话 Activate / Bake / Remove + 图集成。
3. Editor 面板与 usr 文档。
4. B2 项不进本 PR。

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
| 与 meshFit / 纯 combine 混淆 | usr 对照表；编辑器默认打开融合滑条 |
| 把 GPL/UE 代码带入仓库 | 仅读公开文档；审查禁止第三方摘录 |
| combine 旧 API 双真相 | canonical = 含融合的 `mergeStaticMeshes`；纯拼接保留并文档化 |

## 交付方式（单 PR）

- **计划文档**可先合入（本文件所在变更）。
- **实现**同一 PR 交付：共享 `MeshContactBlend` + Phase A（含边缘/材质融合的静态合并 + 脚本 + 编辑器）+ Phase B v1（`deform.meshAdhere` + 会话 + 编辑器 + 测试 + usr 文档）。本地顺序建议：共享内核 → A → B；不拆功能 PR。
- B2（自定义曲线、自动 boolean、完整双材质 shading 等）不进该实现 PR。
- 合并前跑通：`procgen_mesh*`、融合夹具、`check/architecture-contracts`、格式检查。

## 参考（公开行为，非实现来源）

- Merge Master 文档：非破坏粘合、A=active / B=target、Global Merge、Bake / Remove Setup、可选 Boolean。
- UE5 Merge Actors：Merge / Simplify / Batch / Approximate；Replace Source Actors；枢轴与材质 section。
- 本仓库：`docs/usr/modules/procgen.md`（`deform.meshFit`、`combinePcgStaticMeshes`、`mesh.boolean`）。

## 交接检查清单（唯一实现 PR）

- [ ] `MeshContactBlend` 落地；静态合并与 Adhere 共用
- [ ] Phase A：拼接 + **边缘融合 + 材质融合** + 脚本 + 编辑器
- [ ] Phase B v1：`deform.meshAdhere` + 会话 + 编辑器（含材质/法线融合）
- [ ] Canonical API 与诊断码已文档化；纯 combine vs 融合 merge 无双真相
- [ ] Adhere 与 meshFit 分工写清；blend 属性名两端一致
- [ ] 确定性与线程/重入注释齐全
- [ ] 单测含边缘/材质融合夹具 + 至少一条组合路径
- [ ] `check/architecture-contracts` 与相关 `make test FILTER=procgen_mesh*` 通过
- [ ] 无第三方源码摘录；B2 未偷加进本 PR
