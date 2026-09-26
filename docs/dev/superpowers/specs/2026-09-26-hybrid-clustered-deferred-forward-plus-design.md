# 混合渲染：Clustered Deferred（不透明）+ Forward+（半透明）

日期：2026-09-26  
状态：设计（待实施）  
关联：[`3D渲染管线.md`](../../3D渲染管线.md)、[`模块设计.md`](../../模块设计.md)、
[`ClusteredLight.h`](../../../../src/modules/graphics/ClusteredLight.h)、
[`PbrSurface.h`](../../../../src/modules/graphics/PbrSurface.h)、
[`Material.h`](../../../../src/modules/graphics/Material.h)、
[`canonical-material-v14.md`](../../canonical-material-v14.md)、
[`2026-08-19-gpu-driven-rendering-phase-0-1-design.md`](./2026-08-19-gpu-driven-rendering-phase-0-1-design.md)

## 背景

当前主管线是 **前向 / Clustered Forward + 可采样 GBuffer**：

- `RenderControl` 默认：`shadow → gbuffer → [decal] → forward → … → hair`
- 光照在 `forward` Pass 用 `mesh3d` / `mesh3d_clustered` / `pbr_surface` 写入 scene color
- GBuffer（normal / 线性深度 / albedo / hwDepth）主要供 AO、雾、SSR/GI、贴花、HZB；
  今日仅把 metallic/roughness **粗量化进 normal.a**（各 3 bit），**不够做完整 PBR deferred**
- `Material` 已有两档不透明 PBR：
  - 轻量：`metallic` / `roughness` 标量 + albedo/normal（`mesh3d*`）
  - 完整：`hasPbrSurface()` → `PbrSurface`（MR 贴图、emissive、occlusion、specular、
    clearcoat、anisotropy、植被扩展等）走独立 `pbr_surface` 前向；且被 gpu-driven 排除
- `ClusteredLight` 已提供 CPU 分簇（16×9×24）+ SSBO light list；半透明路径**已排除**
  clustered（透明仍走普通前向）
- **没有** classic / tiled / clustered deferred lighting Pass；`"deferred"` 特性不存在

目标：在桌面平台上用 **Clustered Deferred** 承载 **完整 metallic-roughness PBR** 多灯光，
同时保留 **Forward+** 处理半透明；并做成可配置模式（可退回全 Forward+）。

## 目标

1. **混合默认（桌面建议）**：不透明 Opaque/Masked → Clustered Deferred；半透明
   Transparent（含 hair）→ Forward+ / 现有透明前向。
2. **可配置 lighting mode**，至少支持：
   - `forwardPlus` — 全量不透明也走现有 clustered / PBR forward（今日行为）
   - `hybrid` — 不透明 deferred PBR + 半透明 forward+
   - （可选后续）`deferred` — 强制尝试全 deferred（半透明仍必须 forward；该模式主要
     用于对比/调试，语义上等于 hybrid）
3. **首期必须支持核心 PBR**（见下一节），Hybrid 下与 Forward+ 同场景外观对齐（审计容差内）。
4. **复用**现有 `ClusteredLight` 网格、上传、Vulkan/WebGPU SSBO 布局，避免第二套光列表。
5. **两边 backend 对等**（Vulkan 权威，WebGPU 跟进），与现有 parity 规范一致。
6. **可回退**：mode=`forwardPlus` 或能力/编译失败时行为与今日一致，不静默半接通。

## PBR 支持范围（硬性）

Hybrid / deferred 不是「只画 albedo 的假延迟」；不透明路径必须能表达引擎已有的
**核心 metallic-roughness PBR**，并与前向 BRDF 一致。

### 核心 PBR（阶段 B/C 必达 — Deferred 一等公民）

| 输入 | GBuffer / lighting 要求 |
|------|-------------------------|
| Base color（albedo × tint，含 `albedoTextureStrength`） | 全精度 RGB（≥8-bit/通道）写入 GBuffer |
| Metallic / Roughness（标量 **或** `MetallicRoughness` 贴图采样结果） | **独立全精度通道**（不得再用 3-bit pack） |
| Tangent-space normal（`Normal` 槽 + `normalScale` / `normalMode` 解码后的 **世界法线**） | 写入 GBuffer；lighting 只读世界法线 |
| Occlusion（贴图 × `occlusionStrength`，缺省 1） | 写入 GBuffer；乘在 ambient/IBL（及约定的直接光项） |
| Emissive（RGB × `emissiveStrength`） | 写入 GBuffer（允许 HDR 目标或强度分离）；lighting 末尾相加 |
| Dielectric F0（`specularFactor` + `ior` → 与前向相同的 F0 推导） | packing 进 params 或在 lighting 用固定 0.04×specularFactor；**须与 `pbr_surface` / `mesh3d` 一致** |
| Direct lights | Clustered point list + primary directional + CSM（同 Forward+） |
| IBL / env | 与前向同一 irradiance / prefiltered specular（或显式文档化的等价近似） |
| Unlit（`shadingModel=="unlit"` / `PbrSurface.unlit`） | GBuffer 仍可写；lighting 输出 albedo+emissive，跳过灯循环 |
| Masked alpha | GBuffer 几何 Pass 做 cutoff/dither/coverage（与今日 GBuffer alpha 变体对齐） |

几何 Pass（GBuffer fill）负责：**采样全部相关贴图、做植被/detail 之前的标准表面求值，
写出 shading 所需的最终 per-pixel 参数**。Lighting Pass **不再采样材质贴图**（decals 若在
gbuffer 与 lighting 之间改 GBuffer，则 lighting 读修改后的缓冲）。

轻量 `Material`（无 `hasPbrSurface`）与完整 `PbrSurface` 的 **核心子集** 必须都能走
同一套 deferred GBuffer/lighting；禁止「只有标量 metallic 进 deferred、有 MR 贴图的仍
强制前向」这种半接通。

### 扩展 PBR（阶段 E — 可暂留 Forward，或后续扩 GBuffer）

下列今日 `PbrSurface` 能力**不阻塞** Hybrid 首期合入，但必须有明确策略（不得静默丢效果）：

| 能力 | 首期策略 |
|------|----------|
| Clearcoat（factor / roughness / normal） | **留在 Forward+**（opaque 特例仍前向），或阶段 E 增加 coat 层 RT/packing |
| Anisotropy | 同上 |
| SpecularColor / 彩色 F0 | 首期可用灰度 `specularFactor`；彩色进阶段 E |
| Vegetation color/detail/extras/motion/translucency 等 TVE 扩展 | **Opaque 植被继续前向**（今日已是重前向变体），直到专档 deferred 植被设计 |
| Color mask / motion highlight | 优先在 GBuffer fill 阶段 bake 进 albedo，再进 deferred |

规则：**凡声称走 deferred 的 draw，其可见 shading 必须完整**；做不到的扩展特征
→ 该 draw 整条留在 forward（可观察：debug 计数 / 文档），禁止只 bake 一部分。

### BRDF 单一真源

- 抽出与 `mesh3d_clustered.frag` / `pbr_surface.frag` **同一套** `shadeLight` /
  IBL 项（GLSL + WGSL 共享 `.inc` 或代码生成）。
- Deferred lighting 与 Forward+ 透明 / 特例 opaque **禁止复制粘贴第二套 BRDF**。
- 验收：ClassicScenes PBR chart + DamagedHelmet 在 `forwardPlus` vs `hybrid` 下图像审计
  容差内一致（金属、粗糙、IBL 边缘高光）。

## 非目标（本设计首期）

- 不做移动端默认 deferred（移动建议默认 `forwardPlus`；见平台预设）。
- 不把 hair / 自定义 mesh shader / X-ray / offscreen canvas 迁入 deferred lighting。
- 首期不把 clearcoat / anisotropy / 全套 TVE 植被扩展塞进 deferred（见上表策略）。
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

## Pass 并行与流水线重叠

Hybrid 的 **GPU 执行顺序**仍受资源读写约束；并行主要来自三类：
**(1) 同层无依赖 pass 真并行**、**(2) CPU 录制并行**、**(3) 跨帧 / 跨阶段流水线重叠**。
不要假设整条 `shadow→…→hair` 能在同一帧 GPU 上全部并行。

### 资源依赖（同一帧 GPU，硬边）

```text
          ┌── shadow0 ──┐
          ├── shadow1 ──┼──► (CSM array ready)
          └── shadow2 ──┘         │
                                  ▼
gbuffer ──────────────────► [decal?] ──► deferredLighting ──► forward(透明) ──► post/hair
   │                              │              ▲
   └──────── PBR GBuffer ─────────┴──────────────┘
                    cluster light lists (CPU→SSBO) ──┘
```

| 边 | 原因 |
|----|------|
| `shadow*` ∥ `gbuffer` | **无读写冲突**（不同 image）；已在今日 FrameGraph 同层 |
| `gbuffer` → `decal` → `deferredLighting` | decal/lighting **读写同一套 GBuffer**；必须串行 |
| `shadow` → `deferredLighting` | lighting 采样 CSM |
| `deferredLighting` → `forward(透明)` | 透明要 **depth test** 对上已 lit 的 opaque 深度；且写入同一 scene color |
| `forward` → `hair` / post | scene color / depth 消费顺序 |

结论：**真正同帧 GPU 可并行的，主要是「3 个 shadow cascade ∥ gbuffer」**；
其后 `decal` / `deferredLighting` / 透明 `forward` / `hair` 在资源上是链式的。

### 已有能力（保持并扩展）

今日 Vulkan 已把 `shadow0..2 + gbuffer` 放在同一 FrameGraph 无依赖层，用 JobSystem
**并行录制**四条 CB 再同 queue submit（`GraphicsDeferredGraph.cpp`、
`2026-08-20-framegraph-mt-render.md`）。

Hybrid 落地时应：

1. **继续** CSM ∥ GBuffer 并行录制（GBuffer 变重后更值钱）。
2. 把 `decal`、`deferredLighting` 收进同一 FrameGraph，用 attachment 依赖自动插 barrier；
   与 shadow/gbuffer **不同层**（GPU 串行），CPU 仍可流水线准备。
3. **不要**让透明 forward 与 deferredLighting 并行（深度与混合语义）。

### 流水线式并行（推荐挖的潜力）

| 重叠 | 做法 |
|------|------|
| **CPU cluster build ∥ GPU shadow/gbuffer** | `buildClusteredLighting` 只依赖相机与灯列表；可在 JobSystem 上与 gbuffer 录制/执行重叠，须在 lighting record 前完成 SSBO 上传 |
| **CPU 透明排序 ∥ deferredLighting GPU** | 透明 draw list 与 opaque lighting 无数据依赖；lighting 结束后立刻录 forward |
| **帧 N+1 准备 ∥ 帧 N GPU** | 已有 `kAsyncResourceCopies=2`；继续 FrameGraph-MT「快照 / 渲染线程」解耦 |
| **Post 只读准备** | AO/SSR 可提前准备；composite 到 swapchain 仍有顺序 |

可选进阶（非首期）：`deferredLighting` 改为 per-tile compute，便于 async compute 与
graphics 重叠（需实测）；额外 depth-pre 与 shadow 同层并行收益也需实测。

### 不该并行

- GBuffer fill ↔ deferredLighting（读写冲突）
- DeferredLighting ↔ 透明 forward（depth + scene color 混合顺序）
- 多 camera 共享同一 GBuffer/scene color 时相机之间仍串行

### 对实施的含义

- 阶段 C：`deferredLighting` 声明 sample(GBuffer+CSM)、write(sceneColor)；与
  shadow/gbuffer 分层。
- JobSystem：`clusterBuild ∥ record(shadow∥gbuffer)`，join 后再 `record(lighting)`。
- 调试 HUD 区分「录制并行」与「GPU pass 并行」，避免误解。

## 材质分流（与现有代码对齐）

`RenderSystem3D` 已有分流：

```text
surfaceMode == Transparent → transparentItems（排序后前向）
else                       → opaque（含 Masked）
```

Hybrid 规则：

| SurfaceMode / 材质 | GBuffer | Deferred lighting | Forward+ |
|--------------------|---------|-------------------|----------|
| Opaque + 核心 PBR（标量或 `PbrSurface` 核心子集） | ✓ 求值后写入 | ✓ | ✗ |
| Masked（cutoff/dither/coverage）+ 核心 PBR | ✓（alpha test 在 GBuffer） | ✓ | ✗ |
| Opaque + clearcoat / anisotropy / 全套 TVE（首期） | ✗ 或仅 depth | ✗ | ✓（整 draw） |
| Transparent | ✗ | ✗ | ✓ |
| Hair / transparent hair | ✗ | ✗ | ✓（hair pass） |
| 自定义 mesh `Shader*` | ✗（或仅 depth pre） | ✗ | ✓（保持今日例外） |
| Unlit opaque | ✓ | ✓（跳过灯，albedo+emissive） | ✗ |
| Double-sided / 特例 | 与今日 clustered / PBR 排除集一致；做不到完整 shading 则整 draw 前向 | | |

深度：GBuffer / deferred 写 HW depth；透明前向做 depth test、通常不写或按材质
`depthWrite`（保持现有 `setMesh3DSurface` 语义）。

## GBuffer 布局（面向完整核心 PBR）

今日布局对 **post** 够用，对 **PBR deferred lighting 不够**（metallic/roughness 仅 3-bit；
无 occlusion/emissive 独立通道）。Hybrid **采用中期四色附方案为权威布局**（桌面带宽可接受）；
三 RT 紧打包仅作移动/低端可选 profile，不得作为桌面 Hybrid 默认（避免再次欠采样 PBR）。

### 权威布局（Hybrid 默认 — 桌面）

| Slot | 建议格式 | 内容 |
|------|----------|------|
| 0 `albedo` | RGBA8 或 R11G11B10F | RGB = base color；A = packed flags（unlit、specularMode、…）或 unused |
| 1 `normal` | RG16F（oct）或 RGBA16F | 编码世界法线（已含 normal map / scale） |
| 2 `pbrParams` | RGBA8 | R = metallic；G = roughness；B = occlusion；A = specularFactor（或 F0 灰度） |
| 3 `emissive` | R11G11B10F 或 RGB8+scale | emissive × strength（HDR 优先） |
| Depth `hwDepth` | D32 | NDC z；lighting 重建位置；AO/GI 继续用 |
| （并行）`velocity` | 可留在独立目标或 depthColor 兼容层 | TAA/SSR；**不要**再和 metallic 抢同一通道 |

可选兼容附件：保留今日 `depthColor`（linear depth + velocity）给 volumetric/Canvas，直到
消费者迁到 `hwDepth` / velocity。

`GBuffer` API 扩展：

- 现有：`getDepthTexture` / `getHwDepthTexture` / `getNormalTexture` / `getAlbedoTexture`
- 新增：`getPbrParamsTexture()`、`getEmissiveTexture()`（或 `getBuffer("pbrParams"|"emissive")`）
- `hasBuffer` / `readGBufferToImageData` 附件名同步；Doxygen 写明格式与通道语义

### 低端可选（非默认）

保持 3×RGBA8 + D32 的紧打包 **仅**在显式 `GBufferProfile::Compact` 下启用，且必须仍提供
**8-bit metallic + 8-bit roughness**（禁止回到 3-bit）。Emissive 可降为 luma×色度或
lighting 内忽略（文档化质量降级）。桌面 Hybrid 默认 **禁止** Compact。

### 实施原则

- `GBuffer.h` 与 `3D渲染管线.md` 同步更新通道契约。
- Packing 变更必须跑 `RenderImageAudit` / `gbufferViews` / AO/GI/SSR 用例。
- SSGI / fog 若读 albedo.a 作深度，改为 `hwDepth` 或专用 linear-depth；**albedo.a 不再表示深度**。
- Decal Pass 若写入 metallic/rough/emissive，必须改权威 `pbrParams` / `emissive` 附件
  （与今日 decal 改 GBuffer 语义一致）。

## Deferred lighting Pass

### 输入

- 权威 GBuffer：albedo、normal、pbrParams、emissive、hwDepth（+ 可选 velocity）
- `ClusteredLightingUpload`（与 Forward+ **同一份**）
- IBL / env maps（与前向 PBR 同一绑定约定）
- CSM shadow map
- Scene color target

### 每像素求值（核心 PBR）

1. 重建世界/视空间位置（hwDepth + 逆投影）
2. 解码世界法线、albedo、metallic、roughness、occlusion、specularFactor、emissive、flags
3. 若 unlit → `out = albedo + emissive`，结束
4. Ambient/IBL：与前向相同的 diffuse irradiance + specular LOD；乘 occlusion
5. Primary directional + CSM → 共享 `shadeLight`
6. Cluster light list → 同一 `shadeLight` 循环
7. `out.rgb += emissive`；`out.a` = linear depth（供后处理，若需要）

### Shader 共用

见上文「BRDF 单一真源」。GBuffer fill 侧复用 `PbrSurface` 贴图采样/解码逻辑
（可从 `pbr_surface.frag` 抽 `evaluatePbrSurface(...)` → 写 MRT；lighting 只消费 MRT）。

## 平台与质量预设

| 环境 | 建议默认 mode | GBuffer profile |
|------|----------------|-----------------|
| 桌面 Vulkan（功能就绪后） | `Hybrid` | 权威四色附 PBR |
| 移动 / 带宽敏感 | `ForwardPlus` | （不用 deferred）或显式 Compact |
| CI Lavapipe / headless | `ForwardPlus` | — |
| 用户显式 `setLightingMode` | 覆盖预设 | 可另设 profile |

可用现有 `OS`/GPU 查询做 preset，但不在 RenderControl 内偷偷改用户已设 mode。

## 实施阶段

### 阶段 A — 契约与模式（无视觉变化）

- 加入 `LightingMode` API；默认 `ForwardPlus`
- `compile()` 在 Hybrid 时插入 `deferredLighting` pass 名（未实现则失败或显式回退 ——
  **禁止静默空 pass**）
- 更新 `MaterialRenderControl` 测试；文档勾选「设计中」

### 阶段 B — PBR GBuffer（权威布局）+ opaque 材质写出

- 落地四色附 + `pbrParams` / `emissive` API
- GBuffer fill：轻量 Material **与** `PbrSurface` 核心子集（MR/normal/occlusion/emissive
  贴图求值后写入）；迁移 SSGI/fog 深度采样
- ForwardPlus 模式下 opaque 仍走今日 lit forward（GBuffer 仅供 post）
- 图像审计：各 PBR 附件读回

### 阶段 C — Clustered deferred PBR lighting（Vulkan 先）

- Fullscreen lighting：核心 PBR + CSM + IBL
- Hybrid：核心 PBR opaque 不再 lit forward；transparent / 扩展特征 opaque 仍 Forward+
- ClassicScenes：PBR chart、DamagedHelmet；`forwardPlus` vs `hybrid` 容差对比
- FrameGraph：`deferredLighting` sample(GBuffer+CSM)/write(sceneColor)；
  JobSystem：`clusterBuild ∥ record(shadow∥gbuffer)` 后再 record lighting

### 阶段 D — WebGPU parity + 预设

- WebGPU 同等 GBuffer/lighting
- 桌面预设可切 Hybrid；CI 保持 ForwardPlus
- 用户文档更新

### 阶段 E — 扩展 PBR（可选）

- Clearcoat / anisotropy / colored specular 进 deferred 或确认永久前向特例
- 植被 TVE deferred 专档（另文）

每阶段单独可回退；**接口变更与 Vulkan/WebGPU/消费者同 PR**（协作规范）。

## 测试计划

| 用例 | 断言 |
|------|------|
| `RenderControl` mode | set/get；非法字符串 → Result/`Unsupported`；compile pass 序 |
| Feature 投影 | Hybrid ⇒ hasPass(`deferredLighting`)；ForwardPlus ⇒ 无 |
| PBR GBuffer | metallic/roughness/occlusion/emissive 附件存在且通道语义正确（非 3-bit） |
| MR 贴图材质 | `hasPbrSurface` opaque 在 Hybrid 下走 deferred，不回退半接通 |
| 透明排除 | Hybrid 下半透明与 ForwardPlus 一致 |
| 不透明 PBR 多灯 | Hybrid vs ForwardPlus 审计容差（chart / helmet / IBL） |
| Unlit | deferred 输出无直接光，仅 albedo+emissive |
| 扩展特征 | clearcoat/植被等要么完整 deferred，要么整 draw 前向（可计数） |
| 回退 | 关 gbuffer 时无法停在 Hybrid |
| Backend | Vulkan + WebGPU 最小 hybrid PBR 场景 |

## 风险

1. **GBuffer packing 破坏 SSGI/雾** — 阶段 B 必须先改消费者再改默认。
2. **与 FrameGraph “deferred record” 命名混淆** — API/文档用 `LightingMode`。
3. **MSAA + deferred** — 首期 Hybrid 可禁用 MSAA 或 resolve 后 lighting；写进 compile 约束。
4. **自定义 / 扩展 PBR** — 整 draw 留 forward，避免半接通。
5. **双路径 BRDF** — 强制共享；PBR chart 回归锁外观。
6. **带宽** — 四 RT 桌面可接受；移动默认 ForwardPlus，不把 Compact 欠采样当默认。
7. **误并行** — 不得让 lighting 与 gbuffer、透明与 lighting 同层写同一目标。

## 架构规范核对

- **Result / 不得丢弃**：`setLightingMode` 对非法值返回 `Result` 或强枚举不提供非法态；
  不新增含混 bool。
- **单一真源**：mode enum 为真源；feature 字符串若保留则为投影；BRDF 单一实现。
- **可选依赖**：无 deferred lighting 实现时不得宣称 supports；fallback 显式可观察。
- **跨 backend**：契约测试两端共享。
- **不引入** 万能 GameObject；改动限制在 graphics `RenderControl` / `RenderSystem3D` /
  `GBuffer` / `Material`·`PbrSurface` 消费路径 / backend Graphics / shaders。

## 决议摘要（拟采纳）

1. 混合渲染：不透明 Clustered Deferred，半透明 Forward+。
2. **核心 metallic-roughness PBR 为 Hybrid 硬性要求**（albedo、MR、世界法线、occlusion、
   emissive、F0/IBL/CSM）；禁止 3-bit 伪 PBR。
3. 权威 GBuffer：四色附 + hwDepth；低端 Compact 非桌面默认。
4. 扩展特征（clearcoat / anisotropy / 全套 TVE）首期整 draw 留 Forward+，或列入阶段 E。
5. 可配置：`LightingMode::{ForwardPlus, Hybrid}`，首期默认 ForwardPlus。
6. 复用 `ClusteredLight`；新增 `deferredLighting`；不复用 `GraphicsDeferredGraph` 语义名。
7. **并行**：同帧 GPU 仅 `shadow∥gbuffer`；链式 pass 靠 CPU 录制并行 + cluster build /
   透明准备与 GPU 流水线重叠；跨帧继续双缓冲解耦。
8. 分阶段落地；桌面复杂光效用 Hybrid，移动/CI 用 ForwardPlus。
