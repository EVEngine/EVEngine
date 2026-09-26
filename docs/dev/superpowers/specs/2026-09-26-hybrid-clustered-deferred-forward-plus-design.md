# 混合渲染：Clustered Deferred（不透明）+ Forward+（半透明）

日期：2026-09-26  
状态：设计（待实施）  
关联：[`3D渲染管线.md`](../../3D渲染管线.md)、[`模块设计.md`](../../模块设计.md)、
[`ClusteredLight.h`](../../../../src/modules/graphics/ClusteredLight.h)、
[`2026-08-19-gpu-driven-rendering-phase-0-1-design.md`](./2026-08-19-gpu-driven-rendering-phase-0-1-design.md)

## 背景

当前主管线是 **前向 / Clustered Forward + 可采样 GBuffer**：

- `RenderControl` 默认：`shadow → gbuffer → [decal] → forward → … → hair`
- 光照在 `forward` Pass 用 `mesh3d` / `mesh3d_clustered` 写入 scene color
- GBuffer（normal / 线性深度 / albedo / hwDepth）主要供 AO、雾、SSR/GI、贴花、HZB
- `ClusteredLight` 已提供 CPU 分簇（16×9×24）+ SSBO light list；半透明路径**已排除**
  clustered（透明仍走普通前向）
- **没有** classic / tiled / clustered deferred lighting Pass；`"deferred"` 特性不存在

目标：在桌面平台上用 **Clustered Deferred** 承载复杂多灯光与统一材质光照，同时保留
**Forward+** 处理半透明；并做成可配置模式（可退回全 Forward+）。

## 目标

1. **混合默认（桌面建议）**：不透明 Opaque/Masked → Clustered Deferred；半透明
   Transparent（含 hair）→ Forward+ / 现有透明前向。
2. **可配置 lighting mode**，至少支持：
   - `forwardPlus` — 全量不透明也走现有 clustered forward（今日行为）
   - `hybrid` — 不透明 deferred + 半透明 forward+
   - （可选后续）`deferred` — 强制尝试全 deferred（半透明仍必须 forward；该模式主要
     用于对比/调试，语义上等于 hybrid）
3. **复用**现有 `ClusteredLight` 网格、上传、Vulkan/WebGPU SSBO 布局，避免第二套光列表。
4. **两边 backend 对等**（Vulkan 权威，WebGPU 跟进），与现有 parity 规范一致。
5. **可回退**：mode=`forwardPlus` 或能力/编译失败时行为与今日一致，不静默半接通。

## 非目标（本设计首期）

- 不做移动端默认 deferred（移动建议默认 `forwardPlus`；见平台预设）。
- 不把 hair / 自定义 mesh shader / X-ray / offscreen canvas 迁入 deferred lighting。
- 不在本阶段做 GPU light cull（可继续 CPU `buildClusteredLighting`；GPU cull 另开）。
- 不与 `visResolve`（visibility-buffer 材质解析）合并；两者正交，可后接。
- 不扩展为完整 area/spot/IES 灯类型（沿用当前 point + primary directional）。

## 为什么混合比「纯 Deferred」或「纯 Forward+」更合适

| 路径 | 优势 | 劣势 |
|------|------|------|
| Clustered Deferred（不透明） | 光照与几何解耦；多灯/复杂 BRDF 成本近似与 overlap 成正比；易加屏空间光效 | GBuffer 带宽；半透明难写进同一 lighting |
| Forward+（半透明） | 正确混合、排序、折射近似；无额外 MRT | 多灯时每像素重复 BRDF；与 opaque 材质路径分叉 |
| 纯 Forward+（今日） | 实现简单、移动友好 | 桌面复杂光效下不透明侧劣势明显 |

半透明继续 Forward+ 是行业惯例（UE / Frostbite 类混合管线同理）。

## Lighting mode API

### 推荐形态（强类型，避免布尔组合爆炸）

在 `RenderControl` 增加显式 mode，而不是只堆 `"deferred"` bool：

```cpp
/** @brief 3D lighting strategy for opaque geometry. Transparent always Forward+. */
enum class LightingMode : uint8_t {
    ForwardPlus = 0,  ///< Today's clustered-forward for opaque (default until deferred ships)
    Hybrid = 1,       ///< Opaque: clustered deferred; Transparent: forward+
};
```

公共 API（示意）：

```cpp
void setLightingMode(LightingMode mode);
LightingMode getLightingMode() const;
```

脚本 / 字符串桥（可选，与现有 feature 字符串风格兼容）：

- `rc.setLightingMode("forwardPlus"|"hybrid")`
- 或 feature 别名：`enable("clusteredDeferred")` ⇒ `Hybrid`；`disable` ⇒ `ForwardPlus`
  （实现时二选一作为真源，避免双真源；**推荐 enum 为真源**，feature 只是投影）

默认值策略：

- **首期落地默认保持 `ForwardPlus`**，避免未完成 lighting Pass 时桌面场景回归。
- Hybrid 可用后：桌面 preset 可切 `Hybrid`；移动 / software Vulkan（Lavapipe CI）保持
  `ForwardPlus`，除非显式测试。

### 与现有 feature 的关系

| Feature | Hybrid 下行为 |
|---------|----------------|
| `"gbuffer"` / `"gbufferAlbedo"` | **强制开**（deferred 依赖）；关闭 gbuffer 时 mode 回退 ForwardPlus 或拒绝 compile |
| `"clustered"` | 仍控制 light list 构建；Hybrid 下 opaque lighting 与 transparent forward 都消费同一 upload |
| `"forward"` | 仍存在：半透明 +（ForwardPlus 模式下的）不透明 lit draws |
| `"shadow"` | 不变；deferred lighting 采样同一 CSM |
| `"decal"` | 仍在 gbuffer 与 lighting 之间 |
| `"ao"` / `"gi"` / `"ssr"` | 消费 GBuffer / lit color，顺序见下 |

`supports("deferred")`：首期可开始返回 true（表示 Hybrid 可用），或引入
`supports("clusteredDeferred")`；**不要**让 `supports("deferred")` 在未接线时变 true。
现有测试 `CHECK(!rc.supports("deferred"))` 需在接线时同步更新。

## Pass 图

### `LightingMode::ForwardPlus`（今日）

```
shadow → gbuffer → [decal] → forward(opaque+transparent lit) → … → hair
```

### `LightingMode::Hybrid`

```
shadow
  → gbuffer          // 仅 Opaque/Masked；写扩展材质 GBuffer
  → [decal]          // 改 GBuffer（现有语义）
  → deferredLighting // fullscreen/cluster 读 GBuffer + light lists → scene color
  → forwardPlus      // 仅 Transparent（+ 不走 deferred 的特例材质）
  → … post / hair
```

`compile()` 产出的 pass 名建议：

| Pass 名 | Hybrid | ForwardPlus |
|---------|--------|-------------|
| `shadow` | ✓ | ✓ |
| `gbuffer` | ✓（强制） | 按现有 feature |
| `decal` | 可选 | 可选 |
| `deferredLighting` | ✓ | ✗ |
| `forward` | ✓（半透明为主） | ✓（不透明+半透明） |
| `hair` | ✓ | ✓ |

命名注意：仓库里已有 `GraphicsDeferredGraph` = **CPU 延后录制** FrameGraph，不是本 lighting
mode。文档与 API 一律用 `LightingMode` / `deferredLighting` / `clusteredDeferred`，避免
再叫笼统的 “deferred graph”。

## 材质分流（与现有代码对齐）

`RenderSystem3D` 已有分流：

```text
surfaceMode == Transparent → transparentItems（排序后前向）
else                       → opaque（含 Masked）
```

Hybrid 规则：

| SurfaceMode | GBuffer | Deferred lighting | Forward+ |
|-------------|---------|-------------------|----------|
| Opaque | ✓ | ✓ | ✗ |
| Masked（cutoff/dither/coverage） | ✓（alpha test 在 GBuffer） | ✓ | ✗ |
| Transparent | ✗ | ✗ | ✓ |
| Hair / transparent hair | ✗ | ✗ | ✓（hair pass） |
| 自定义 mesh `Shader*` | ✗（或仅 depth pre） | ✗ | ✓（保持今日例外） |
| Double-sided / 特例 | 与今日 clustered 排除集一致，优先 forward | | |

深度：GBuffer / deferred 写 HW depth；透明前向做 depth test、通常不写或按材质
`depthWrite`（保持现有 `setMesh3DSurface` 语义）。

## GBuffer 布局扩展

今日布局对 **post** 够用，对 **PBR deferred lighting** 不够（缺独立 metal/rough/emissive）。

### 首期建议（尽量少增带宽）

保持 3×RGBA8 + D32，收紧 packing：

| Attachment | RGB | A |
|------------|-----|---|
| **normal** | world normal octahedral 或现有 `*0.5+0.5` | roughness（或 packed） |
| **depthColor** | R=linear depth；G/B=velocity（保留） | metallic 或 motion 保留位重新分配 |
| **albedo** | albedo×tint | emissive intensity 或 linear depth（SSGI 现依赖 A=depth —— **迁移时必须改采样点**） |

更干净的中期方案（桌面可接受多一个 RT）：

| Slot | Format | 内容 |
|------|--------|------|
| 0 | RGBA8 | albedo + unused/AO |
| 1 | RG16F / RGBA16F | encoded normal |
| 2 | RGBA8 | metallic, roughness, packed flags, unused |
| 3 | R11G11B10F 或 RGB8 | emissive |
| Depth | D32 | hw depth |

实施原则：

- `GBuffer.h` 文档与 `readGBufferToImageData` 附件名同步更新。
- 任何 packing 变更必须跑 `RenderImageAudit` / `graphics.imageAudit.gbufferViews` /
  AO/GI 相关用例。
- SSGI / fog 若读 albedo.a 作深度，改为 `hwDepth` 或 `depthColor.r`（契约写进
  `3D渲染管线.md`）。

## Deferred lighting Pass

### 输入

- GBuffer attachments（采样）
- `ClusteredLightingUpload`（与 Forward+ **同一份**：lights / clusterTable / indices /
  primaryDir / ambient / CSM）
- Scene color target（可 clear 为天空/ambient，或先画 atmosphere 再 additive 灯 —— 首期建议：
  deferred Pass **写入** scene color，天空/IBL 在 lighting shader 内或此前单独 clear+sky）

### 调度

**Clustered deferred（推荐，复用现有 grid）**：

1. Fullscreen triangle（或按 tile dispatch）；每像素：
   - 重建 view 位置（hwDepth + 逆投影）
   - 读 cluster → 迭代 point lights（与 `mesh3d_clustered.frag` 同一索引约定）
   - 加 primary directional + CSM
   - 写 lit RGB；A 可继续写 linear depth 供后处理

避免 classic 多 draw light volume（与现有 cluster 表重复）。

### Shader 共用

抽出与 `mesh3d_clustered.frag` 共享的 BRDF / light loop 函数（GLSL/WGSL 各一份或公共
`.inc` 生成），保证 Hybrid 下 opaque deferred 与 transparent forward **光照外观一致**
（允许 ≤1 ULP / 审计阈值容差）。

## 平台与质量预设

| 环境 | 建议默认 mode |
|------|----------------|
| 桌面 Vulkan/D3D 类（未来） | `Hybrid`（功能就绪后） |
| 移动 / 带宽敏感 | `ForwardPlus` |
| CI Lavapipe / headless | `ForwardPlus`（除非专测 `FILTER=…deferred…`） |
| 用户显式 `setLightingMode` | 覆盖预设 |

可用现有 `OS`/GPU 查询做 preset，但不在 RenderControl 内偷偷改用户已设 mode。

## 实施阶段

### 阶段 A — 契约与模式（无视觉变化）

- 加入 `LightingMode` API；默认 `ForwardPlus`
- `compile()` 在 Hybrid 时插入 `deferredLighting` pass 名（可先空执行 / 未实现则
  `ensureCompiled` 失败或自动回退并打可观察日志 —— **禁止静默空 pass**）
- 更新 `MaterialRenderControl` 测试；文档勾选“设计中”

### 阶段 B — 扩展 GBuffer + opaque 只填材质

- 扩展 packing / 可选第 4 RT
- Opaque/Masked 只写 GBuffer；ForwardPlus 模式下仍走今日 forward lit
- 图像审计：gbuffer 附件

### 阶段 C — Clustered deferred lighting（Vulkan 先）

- Fullscreen lighting shader + CSM/IBL 对齐
- Hybrid：opaque 不再跑 lit forward；transparent 仍 Forward+
- ClassicScenes / imageAudit 对比 `forwardPlus` vs `hybrid`（同场景容差）

### 阶段 D — WebGPU parity + 默认预设

- WebGPU 同等 Pass
- 桌面预设切 Hybrid；CI 保持 ForwardPlus
- 用户文档：`docs/usr/modules/graphics/rendering-effects.md`

每阶段单独可回退；**接口变更与 Vulkan/WebGPU/消费者同 PR**（协作规范）。

## 测试计划

| 用例 | 断言 |
|------|------|
| `RenderControl` mode | set/get；非法字符串 → Result/`Unsupported`；compile pass 序 |
| Feature 投影 | Hybrid ⇒ hasPass(`deferredLighting`)；ForwardPlus ⇒ 无 |
| 透明排除 | Hybrid 下半透明像素与 ForwardPlus 一致（同灯） |
| 不透明多灯 | Hybrid 与 ForwardPlus 审计图容差内一致（证明 BRDF 共用） |
| GBuffer 读回 | normal/metal-rough/albedo 非空且布局契约 |
| 回退 | 关 gbuffer 时无法停在 Hybrid；或 Result 失败 |
| Backend | Vulkan + WebGPU 各跑最小 hybrid 场景 |

## 风险

1. **GBuffer packing 破坏 SSGI/雾** — 阶段 B 必须先改消费者再改默认。
2. **与 FrameGraph “deferred record” 命名混淆** — API/文档用 `LightingMode`。
3. **MSA A + deferred** — 首期 Hybrid 可禁用 MSAA 或只 resolve 后 lighting（与 TAA 链类似）；
   写进 compile 约束。
4. **自定义 shader 材质** — 继续 forward，避免半接通。
5. **双路径维护** — 强制共享 light loop；禁止复制粘贴两套 BRDF。

## 架构规范核对

- **Result / 不得丢弃**：`setLightingMode` 对非法值返回 `Result` 或强枚举不提供非法态；
  不新增含混 bool。
- **单一真源**：mode enum 为真源；feature 字符串若保留则为投影。
- **可选依赖**：无 deferred lighting 实现时不得宣称 supports；fallback 显式可观察。
- **跨 backend**：契约测试两端共享。
- **不引入** 万能 GameObject；改动限制在 graphics `RenderControl` / `RenderSystem3D` /
  backend Graphics / shaders。

## 决议摘要（拟采纳）

1. 混合渲染：不透明 Clustered Deferred，半透明 Forward+。
2. 可配置：`LightingMode::{ForwardPlus, Hybrid}`，首期默认 ForwardPlus。
3. 复用 `ClusteredLight` 表；新增 `deferredLighting` pass，不复用
   `GraphicsDeferredGraph` 语义名。
4. 分阶段落地；桌面复杂光效用 Hybrid，移动/CI 用 ForwardPlus。
