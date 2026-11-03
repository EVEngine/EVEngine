# Unity 开源 VFX → EVEngine 系统化移植方案

目标：把 GitHub 上可复用的 Unity VFX **配方、编排与算法思路**系统性地迁入
EVEngine，落在现有资产契约上，而不是复刻 Visual Effect Graph 运行时。

本方案对齐：

- `docs/usr/modules/particles.md`（`eve.particle-effect` v1/v2）
- `docs/dev/particle-system-professional-roadmap.md`（P0–P3）
- `docs/dev/2026-09-09-shader-effect-assets.md`（SPIR-V / MeshVfx 包）
- `docs/dev/superpowers/specs/2026-10-21-sdf-collision-fluid-foundation.md`
- `AttackVfxRecipe` 层角色（particles / meshVfx / trail / decal / …）

## 0. 结论先行

| 做 | 不做 |
|----|------|
| 重做成 `eve.particle-effect` / AttackVfx / MeshVfx | 导入 `.vfx` / Shader Graph / HLSL block |
| 抽层结构、曲线、事件链、时间线 | 整仓 fork Unity 示例工程进树 |
| 2D 精灵/ribbon 优先；3D 走 stylize | 假设 particles GPU 路径支持 3D mesh |
| 标许可证与来源归因 | 静默抄贴材质贴图（需单独清权） |

**一句话：**移植的是「特效配方与能力启发」，不是「Unity VFX Graph 兼容层」。

## 1. 业界源 vs EVEngine 能力面

### 1.1 EVEngine 已有（移植落点）

| 面 | 入口 | 形态 |
|----|------|------|
| 多发射器特效 | `eve.Particles()` | `eve.particle-effect` JSON v1/v2 |
| 子事件链 | `eventRoutes` → sub-emitter | birth / death / collision（CPU） |
| 时间线 | `timeline.cues` | start/stop/emit/setParameter |
| GPU 常驻粒子 | Vulkan resident compute | 功能受限；碰撞/SDF/力场/子发射器回退 CPU |
| 攻击编排 | `eve.StylizeAction()` | `AttackVfxRecipe` 相位 + layer role |
| 网格风格化 | stylize / `eve.shader` | MeshVfx + 最多 32 float push-constant |
| 2D SDF 碰撞 | `IParticleSdfField` | CPU only；GPU 报 `sdf` fallback |
| 拖尾 / 光束 | stylize `TrailEffect` / `BeamEffect` | 与粒子 ribbon 分路径 |

### 1.2 Unity 常见面 → EVEngine 映射字典

| Unity / VFX Graph 概念 | EVEngine 目标 | 备注 |
|------------------------|---------------|------|
| Visual Effect asset / Graph | **禁止直接导入**；人工重做成 effect JSON | ParticleGraph 是固定链，非开放节点图 |
| Particle System Prefab（Shuriken） | `eve.particle-effect` 一层或多层 emitter | 最易移植的内容源 |
| GPU Event / Output Event | `eventRoutes`（CPU）或 timeline `emit` | 无 GPU Event |
| Blackboard / Exposed Property | `parameters` + `parameterBindings` | float 为主 |
| Spawn / Update / Output contexts | emitter `Config` 字段集 | 见 particles 用户文档 |
| Ribbon / Particle Strip | `setRibbon` / `renderMode` | 已有 |
| Soft particles | `setSoftParticles` | 需 GPU resident + 真实深度 |
| SDF Collide | `setSdfField` + collision mode | 仅 2D CPU |
| Point Cache / AttributeMap | **暂无一等资产**；用贴图采样或预烘焙位置表脚本 | Phase C 能力债 |
| Skinned Mesh Sampling | `attachToBone*` / `SkinSource`（2D 投影） | 非 GPU 蒙皮采样 |
| Custom HLSL Operator/Block | GLSL → SPIR-V / MeshVfx 包 | 拒绝任意 binding |
| Shader Graph particle output | 重写成引擎 shader 合同或放弃自定义着色 | lit/distortion 现走 CPU |
| Timeline / Signal | AttackVfx 相位 + effect `timeline` | anticipate→…→aftermath |
| Decal / Camera shake / Audio | AttackVfx layer roles | 已有角色枚举 |

### 1.3 能力缺口（移植时必须降级或推迟）

1. 开放式 GPU 粒子图与自定义 HLSL block  
2. GPU Event、Point Cache、AttributeMap  
3. particles 模块内 3D mesh renderer（roadmap P2 remaining）  
4. GPU SDF / lit-resident / 活动 motion-vector 写入  
5. Unity ShaderLab / Shader Graph 自动翻译  

缺口不阻塞 **配方移植**；只阻塞「原样复现百万级 GPU Graph 效果」。

## 2. 移植分类（四种交付物）

每条候选源必须归入且只归入一类，避免范围膨胀：

| 代号 | 交付物 | 典型输出路径 | 引擎改动 |
|------|--------|--------------|----------|
| **C** Content | 特效配方包 | `examples/particle-effects/` 或 `share/eve/...` | 无（或仅测试夹具） |
| **R** Recipe | 玩法攻击/技能编排 | `examples/attack-vfx/` + AttackVfx JSON/脚本 | 无或薄适配 |
| **S** Shader/MeshVfx | 风格化网格着色 | `examples/shader_effect_package/` 风格资产 | 偶发 shader 合同扩展 |
| **K** Kernel | 运行时能力 | `particles` / `gpgpu` / `stylize` | **独立 PR**；不混进内容包 |

规则：C/R/S 可并行；K 必须单独 PR，并引用本方案章节与 roadmap 条目。

## 3. 源目录（准入清单）

### 3.1 Tier A — 优先抽配方（C/R）

| 源 | 许可证需核实 | 建议抽取 | 归类 |
|----|--------------|----------|------|
| [Kodrin/VFX-Essentials](https://github.com/Kodrin/VFX-Essentials) | MIT（图例外，README 声明） | waves、burning、leaves、pickup 层结构 | C |
| [Unity-Technologies/VisualEffectGraph-Samples](https://github.com/Unity-Technologies/VisualEffectGraph-Samples) | Unity Companion / 样本条款 | Bonfire、Portal、Ribbon Pack 的层意图 | C |
| [keijiro/VfxPyro](https://github.com/keijiro/VfxPyro) | 查 LICENSE | 烟花二级爆炸链 | C |
| [dilmerv/UnityVFXEssentials](https://github.com/dilmerv/UnityVFXEssentials) | 查 LICENSE | 参数对照表（非资产拷贝） | C 文档附注 |

### 3.2 Tier B — 能力启发（K，可选后置）

| 源 | 启发 | 对齐 |
|----|------|------|
| [keijiro/SdfVfxSamples](https://github.com/keijiro/SdfVfxSamples) | SDF 驱动外观/碰撞用法 | `IParticleSdfField` 示例 + SDF 规格 P2 |
| [aman-tiwari/MeshToSDF](https://github.com/aman-tiwari/MeshToSDF) | 网格→SDF 烘焙流程 | SDF 规格烘焙工具 |
| [PaulDemeulenaere/vfx-neighborhood-grid-3d](https://github.com/pauldemeulenaere/vfx-neighborhood-grid-3d) | GPU 邻域网格 | gpgpu 实验；非默认粒子路径 |
| [Fire-Aalt/com.firealt.vfx-forge](https://github.com/Fire-Aalt/com.firealt.vfx-forge) | 实例池 + GraphicsBuffer | `ParticleEmitterPool` / GPU 参数面 |
| [OrsonFavrel/VFXGraph-ClockedAndLoaded](https://github.com/OrsonFavrel/VFXGraph-ClockedAndLoaded) | 粒子草 / 地形对齐 | 环境演示；2D 或 stylize |

### 3.3 Tier C — 默认拒绝

- RealSense / Kinect / Depthkit / Record3D 硬件桥  
- Gaussian Splatting / 体积视频专用栈  
- 「百万粒子」纯展示图（无配方可映射）  
- 无许可证或贴图版权不清的 Asset Store 导出仓  

新增源进入清单前必须填 §5 工作单的「源准入」栏。

## 4. 标准移植流水线（每个效果一条）

```
① 源准入 → ② 意图拆解 → ③ 路径选型 → ④ 重作者 → ⑤ 验收 → ⑥ 入库
```

### 4.1 源准入

- [ ] 仓库 URL、commit/tag、许可证全文可读  
- [ ] 区分「图/代码/音频」权利；贴图不明则只移植参数意图  
- [ ] 标注 Unity 管线（Built-in / URP / HDRP）与是否依赖 VFX Graph  
- [ ] 归入 C / R / S / K  

### 4.2 意图拆解（禁止抄节点图）

用一页表描述，不贴 Unity 资产文件：

| 字段 | 内容 |
|------|------|
| 视觉意图 | 一句话（例：篝火 = 芯焰 + 余烬 + 烟） |
| 层列表 | 每层：发射形状、寿命、颜色曲线、混合、面向 |
| 事件 | 死亡再生烟？碰撞溅射？ |
| 时间线 | 充能 / 爆发 / 残留 |
| 规模 | 峰值粒子数、是否可接受 CPU |
| 降级 | 砍掉的 Unity 专属能力 |

### 4.3 路径选型决策树

```
是否攻击/技能相位编排？
  ├─ 是 → AttackVfxRecipe（R）+ 粒子 URI 指向 effect 资产
  └─ 否 → 纯环境/道具特效？
        ├─ 主要是网格着色/全息 → MeshVfx / eve.shader（S）
        └─ 精灵/ribbon 粒子 → eve.particle-effect（C）
需要新运行时原语？
  └─ 是 → 停 C/R/S；开 K 规格 PR，引用 roadmap
```

**GPU 路径选择：**若层依赖 collision / sdf / force field / sub-emitter /
lit / distortion / custom shader / 多段曲线 → 标注 `sim=cpu`。仅当层可裁到
resident 支持集时才标 `sim=gpu-candidate`，并在验收中查询
requested vs active GPU。

### 4.4 重作者合同（C 类最低标准）

目标文件：`*.effect.json`，`type: eve.particle-effect`，`version: 2`。

必填：

1. `parameters` 至少一个可调 float（如 `intensity`）  
2. 命名 `emitters[]`；层名稳定（供 eventRoutes / 脚本 `getEmitterByName`）  
3. 需要链式生成时写 `eventRoutes`，不靠脚本每帧 `emit` 硬编码（演示脚本除外）  
4. 有限时长效果写 `timeline`（duration / cues）；循环环境效果可 `looping: true`  
5. `buffer` 与峰值发射匹配；禁止「开很大 buffer 碰运气」  
6. 头部注释或同目录 `SOURCES.md` 记录源仓、许可证、降级项  

R 类：AttackVfx 相位至少覆盖意图中的 anticipate / release / impact /
aftermath 子集；particles 层 URI 指向已验收的 C 资产。

S 类：遵守 `eve.shader/1` 准入（无任意 storage/额外 binding）；参数 ≤ 32 float。

### 4.5 验收门禁

| 门禁 | 要求 |
|------|------|
| 解析 | `newEffectFromFile` / AttackVfx 加载成功；失败路径有结构化错误 |
| 烟雾 | `MIN_RUN_SECONDS=2 bash scripts/smoke_examples.sh <example>` |
| 确定性 | 固定 seed 下可 `advance` 到关键帧；记录 seed |
| GPU 声明 | 若标 gpu-candidate：断言未静默回退，或文档写明回退原因 |
| 视觉 | 引擎 MCP / 离屏呈现截帧入 `docs/images/examples/` 或测试夹具（禁止用 Xvfb 帧当正确性证据） |
| 质量元数据 | 新增 TODO/ALLOWLIST 必须带 owner/issue/reason/expiry |

### 4.6 入库布局

```
examples/particle-effects/           # C：参考配方包（已有 fire/smoke/impact/trail/weather）
  SOURCES.md                         # 本包源与许可证总表
  bonfire.effect.json                # 新增示例命名：意图清晰的英文短名
  portal.effect.json
  fireworks.effect.json
examples/attack-vfx/                 # R：编排演示（可引用上列 URI）
examples/<shader-demo>/              # S：仅当确需新 shader 包
docs/dev/superpowers/specs/…        # K：能力规格，单独 PR
```

不把 Unity 工程、`.vfx`、`.shadergraph`、HDRP 资产树提交进本仓库。

## 5. 效果工作单模板

每个效果复制一节（可放在 PR 描述或 `SOURCES.md`）：

```markdown
### <effect-id>

- Class: C | R | S | K
- Source: <url>@<commit>  License: <SPDX or text>
- Intent: <one line>
- Layers: <name: role…>
- Events / Timeline: <…>
- Sim path: cpu | gpu-candidate
- Downgrades: <Unity features dropped>
- Outputs: <paths>
- Verification: smoke=<pass/fail> seed=<n> frame=<path or test>
- Owner: <agent/human>
```

## 6. 分阶段交付

阶段按**仓库价值**排序，不绑定日历。每阶段结束必须有可合并产物。

### Phase 0 — 基线冻结（文档）

- [x] 映射字典、分类、流水线、准入/拒绝清单  
- [x] `examples/particle-effects/SOURCES.md` 台账  
- 退出：方案合入 `dev`；后续内容 PR 引用本文件

### Phase 1 — 配方包扩展（纯 C，零引擎改动）**【本批执行】**

目标：在现有 `particle-effects` 上增加 **3** 个可演示配方。

| ID | 意图 | 主要参考 | 映射要点 | 状态 |
|----|------|----------|----------|------|
| `bonfire` | 芯焰 + 余烬 + 烟分层循环 | VFX Graph Samples Bonfire / Kodrin | 多 emitter + intensity；烟 alpha，焰 additive | 已落地 |
| `portal` | 环带 + 向内吸粒子 + 核心闪 | Samples Portal | `ring` + `radialAcceleration`（非世界力场） | 已落地 |
| `fireworks` | 主爆 → 二级星屑 | keijiro VfxPyro | `eventRoutes` death→stars/smoke；timeline emit | 已落地 |

退出：

- [x] 三份 `*.effect.json` + `SOURCES.md`  
- [x] 可运行 `examples/unity-vfx-port-lab`（smoke PASS，`UNITY_VFX_PORT_READY`）  
- [x] 单元夹具覆盖 fireworks `eventRoutes`；render-presentation 参考包纳入 8 个配方并断言层名  
- [x] `particles.reference.effectPackLoadsAndRenders` 出帧 `particle_p2_reference_pack.png` 

### Phase 2 — 攻击编排对齐（R）

目标：把 Phase 1 配方挂进 AttackVfx 相位，补齐「充能→释放→残留」。

| 工作项 | 说明 |
|--------|------|
| 元素皮肤复用 | `attack-vfx` 的 fire/water/earth/air 改为引用共享 effect URI（减少重复 JSON） |
| aftermath 层 | 爆炸后烟/火星用 eventRoutes 或 Aftermath 相位 |
| 文档 | `docs/usr` 增加「从 Unity 配方迁移」短节，链到本方案 |

退出：`examples/attack-vfx` smoke + `ATTACK_VFX_PASS`；无新 K 改动。

### Phase 3 — 视觉保真度（C/S 增强，仍避免 K）

| 工作项 | 说明 |
|--------|------|
| flipbook 烟/火 | 参考 `examples/smoke-flipbook`；统一进 effect 包 |
| ribbon 消散 | Samples Ribbon Pack → 粒子 ribbon 参数预设 |
| 轻量 MeshVfx | 仅当 2D 粒子无法表达（全息、能量盾）时新增 S 资产 |

退出：保真度对比表（意图 / 降级 / 截帧）；仍无 VFX Graph 运行时。

### Phase 4 — 内核能力（K，可选，独立 PR）

仅当 Phase 1–3 反复撞墙时开做，并回链 roadmap：

| 能力 | 触发条件 | 对齐 |
|------|----------|------|
| particles 内 mesh renderer | 多个 S workaround 仍不够 | roadmap P2 remaining |
| GPU lit-resident / soft 深度更稳 | 大数量加光照粒子 | roadmap P2 |
| Point-cache 类资产或脚本烘焙点集 | Kodrin burning / mesh emit 刚需 | 新规格 |
| 2D/3D SDF 示例与工具 | 移植 SDF 碰撞类效果 | SDF 规格 P2 |
| ParticleGraph 开放度 | 仅编辑器需求驱动 | roadmap P3；**不做** Unity 图互换 |

K 类 PR 禁止夹带大宗 C 资产，避免冲突热点。

## 7. 参数对照速查（Shuriken / Graph → Config）

| Unity 侧常见项 | EVEngine 字段 / API |
|----------------|---------------------|
| Start Lifetime | `particleLifetime` `[min,max]` |
| Start Speed | `speed` |
| Start Size | `particleSize` + `sizes` 曲线 |
| Start Color / Color over Lifetime | `colorStart`/`colorEnd` 或 `colorOverLifetime` |
| Emission rate / burst | `emissionRate` / timeline `emit` / `emit` API |
| Shape cone / sphere | `direction` + `spread`（无完整 ShapeModule：降级） |
| Gravity Modifier | `gravity` |
| Drag | `damping` |
| Simulation Space | local/world（见 emitter API） |
| Renderer stretch billboard | `renderMode` `billboard`/`velocity` + `stretch` |
| Material additive | `blendMode` `additive`/`alpha`/`opaque` |
| Sub Emitter on Death | `eventRoutes` `on: death` |
| Texture sheet | flipbook API（见 particles 文档） |
| External Forces | `ForceField`（CPU） |

完整字段以 `docs/usr/modules/particles.md` 与 `ParticleEmitter::Config` 为准；
本表只服务移植时的快速降级决策。

## 8. 架构与协作约束

- **模块边界：**内容 PR 不新增 particles→graphics 向上 `#include`；跨模块走
  `eve::cap` / `common` 接口（见协作公约）。  
- **Result：**加载/重载失败不得丢弃；示例脚本检查 `ok` 或打印
  `getLastEffectError()`。  
- **确定性：**展示回放与测试固定 seed；GPU 路径单独声明非确定性政策。  
- **机械大扫除：**导出宏、Doxygen、全库改名不得混进配方 PR。  
- **格式：**只 format 改动行；`make check` 在含代码的 PR 上运行。  
- **纯文档 PR：**本方案文件可不跑引擎测试；一旦带上 JSON/示例，必须 smoke。

## 9. 风险与缓解

| 风险 | 缓解 |
|------|------|
| 许可证/贴图版权 | 只移植数值意图；贴图用自有或 CC0；SOURCES 必填 |
| 「看起来像」期望过高 | 每个效果写明 Downgrades；截帧对比意图而非 Unity 截图像素 |
| GPU 回退导致掉帧 | 默认按 CPU 预算设计；gpu-candidate 单独验收 |
| 配方 PR 变内核坑 | 严格 C/R/S vs K 分离 |
| 与粒子 roadmap 抢主 | Phase 4 必须引用 roadmap 条目；否则不做 |

## 10. 首批执行清单（可直接开 PR）

1. **Doc（本文件）** — Phase 0  
2. **Content PR：**`bonfire` / `portal` / `fireworks` + `SOURCES.md` — Phase 1  
3. **Recipe PR：**attack-vfx 引用共享 effect — Phase 2  
4. **（可选）Kernel PR：**仅当评审确认撞墙 — Phase 4  

## 11. 应用的强制架构规则（交接用）

- 模块接口契约：移植落点声明在六面中的 **资产 / 脚本绑定 / 事件**；不新增平行 JSON 契约目录。  
- Result 与不可丢弃返回值：加载 API 已有；示例必须检查。  
- 领域短根：不引入万能 `VfxGraphAsset` 根类型。  
- 成本：昂贵烘焙（SDF/point cache）若引入须 `@cost` 与两步 API。  
- 例外：不做 Unity 资产互换——显式产品决策，非沉默豁免。

## 12. 参考路径

- `examples/particle-effects/`  
- `examples/attack-vfx/`  
- `examples/flame-jet/`、`examples/smoke-flipbook/`、`examples/shader_effect_package/`  
- `docs/usr/modules/particles.md`  
- `docs/dev/particle-system-professional-roadmap.md`  
- `docs/dev/2026-09-09-shader-effect-assets.md`  
