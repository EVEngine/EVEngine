# EVEngine 0.6 / 0.7 发布路线图

日期：2026-10-08
状态：规划（非合同；实施以 PR 与发版 checklist 为准）
基线：最新正式 tag `v0.5.2`；树上版本号为 `0.5.x-dev`；`dev` 上已有大量 post-0.5.2 提交待收口

> 配套阅读：[发布流程](发布流程.md)、[模块设计](模块设计.md)、
> [游戏开发系统与工具差距分析](游戏开发系统与工具差距分析.md)、
> [战棋框架差距分析](2026-09-18-tbs-framework-gap-analysis.md)、
> [HDR 与反射链 AAA 升级](HDR与反射链AAA升级.md)、
> [hex 地形能力审计](hex-terrain-capability-audit.md)。

---

## 1. 版本叙事

| 版本 | 一句话 | 主题 |
|------|--------|------|
| **0.5.x** | 能力爆炸：RTS / 编辑构件 / Unity 导入 / hexmap / HD2D / 软体 / 战斗框架等已入库 | 广度 |
| **0.6** | **可交付的玩法竖切**：动作 / 战棋 / RPG 存档环跑通；地形与现代渲染链可用并写清边界 | 收口与产品面 |
| **0.7** | **作者工具 + 持久化平台 + 保真度**：让独立团队能长期做完整游戏，而不只是跑 demo | 深度与可持续 |

0.6 不追求清空所有 TODO；0.7 不重开「完整内置 Editor App」或音频中间件等 P2 长期项（见工具差距分析 §3 P2）。

---

## 2. 0.6（前置，本路线图的假设）

### 2.1 定位

把 `v0.5.2` 之后已合入的能力收成可宣布的产品面：示例稳定、文档与绑定对齐、已知边界写进用户手册。

### 2.2 Must（打 `v0.6.0` 前必过）

| # | 项 | 验收 |
|---|----|------|
| M1 | 动作战斗竖切 | `examples/combat-arena`（及 AttackVfx / settlement / weapon 脚本面）可玩；关键路径有测试 |
| M2 | 战棋确定性战斗面 | `examples/tactics` + 快照/回放/技能预检文档与行为一致；`tactics` binding gap = 0（已满足则保持） |
| M3 | RPG 产品环 | Quest/Tracker + `RPGSaveSession` + `InventorySaveSession` + `rpg-classic` 存读档路径可演示 |
| M4 | 地形 / 六边形可玩路径 | 平面 hexmap + procgen bake 示例稳定；球面缺项写入「已知边界」 |
| M5 | 渲染升级可声明部分 | Hybrid Deferred/Forward+、体积雾、Vulkan HDR present 有用户文档；天空 capture / GGX 全闭环标 limitation |
| M6 | 发版门禁 | `check/architecture-contracts`、quality metadata、五平台 SDK consumer 绿 |

### 2.3 Should（强烈建议进 0.6）

| # | 项 | 说明 |
|---|----|------|
| S1 | 跨模块存档用法文档 | 各域 `*SaveSession` 如何拼成「一局游戏存档」；至少一个端到端示例 |
| S2 | 高流量绑定文档分批 | 优先 `animation` / `graphics` / `editor` 中**示例实际调用**的 API（不必清零 859） |
| S3 | 同步 `模块设计.md` | 去掉已实现仍标 `[ ]` 的过时项（Quest 等） |
| S4 | 帧统计叠加层 | 工具差距分析 P0；控制台已有，补 FPS / 帧时间 / 实体数 |
| S5 | keyboard / joystick 可用性 | 进入构建与最小测试，避免输入面仍标 N/A |

### 2.4 明确不进 0.6

plugins 完整生命周期、`video`、独立脚本线程、SSR/RTAO/TAA、Unity 导入全保真、Cubism Core、球面 hex 全量、完整场景/地图 Editor App、NavMesh、网络大改。

---

## 3. 0.7 目标与成功标准

### 3.1 目标用户故事

独立团队用 SDK 可以：

1. **持久化一局完整游戏**（多模块事务存档 + schema 版本迁移）；
2. **在可组合编辑构件上完成日常创作**（场景 / 地图笔刷 / 资产浏览 / LSP 够用）；
3. **把外部资产与角色动画接到可发布质量**（导入保真与蒙皮/热重载主路径）；
4. **在可选图形升级上选配**（反射 / AA / 天空），且 Vulkan↔WebGPU 契约不暗回退。

### 3.2 成功标准（发 `v0.7.0` 时）

- [x] 存档组合基础：`GameSaveSession`、RPG/Tactics participant，以及 prepare 失败不修改、provider 缺席和内容迁移测试（`test/integration/save-restore/save_restore_graphics_tests.cpp`）。
- [ ] 原定多域正式存档验收：RPG + inventory + tactics（或 combat）+ scene/map 子集的完整单事务示例；已有通用 session 不等于该组合完成。
- [ ] 创作面：`animation`/`graphics`/`editor`/`building`/`scene` binding gap 合计较 0.6 tag **下降 ≥50%**（或示例调用面 100% 有文档）
- [ ] 资产：Unity 导入「角色蒙皮 + Animator 最小集」或等价 UE 路径有可复现示例；失败项仍结构化报告
- [x] 图形实现基础：Vulkan reflection filter 预编译资产、成功后 active revision 发布与 TAA history/jitter/reprojection 已存在，见 HDR/抗锯齿设计。
- [ ] 图形发版验收：反射完整发布/失败图像证据，以及 TAA 或完整 SMAA 至少一条生产质量验收；不从源码存在推导达标。
- [x] 帧统计：`FrameStatsPanel` 提供 FPS/frameMs/可选实体数，`test/unit/devtools/devtools_tests.cpp` 覆盖有效/非法 dt。
- [x] 日志 severity 标签：`common/CrashLog::recordLogEvent(level, message)`；不等于另有可配置日志过滤合同。
- [ ] 3D 物理 debug draw 的生产接线及组合验收（`WorldDebugDraw.cpp` 当前是 Box2D b2Draw，不能替代 3D）。
- [ ] 门禁：同 0.6，外加存档 failure-injection 与 provider present/absent 用例（见架构巩固清单精神）

---

## 4. 0.7 工作流（按主题）

优先级：`P0` = 竖切阻塞 / 发版叙事；`P1` = 强烈建议；`P2` = 有余力或可滑到 0.8。

### Theme A — 持久化平台（P0）

**当前边界（2026-11-11 源码核验）**：各域已有 `snapshotJson` / `*SaveSession`，引擎级 `GameSaveSession` 也已提供事务组合基础与失败/迁移测试。剩余目标是上方指定多域组合、正式示例和发版验收；“完全缺游戏存档契约”不再准确。

| ID | 工作项 | 产出 | 依赖 |
|----|--------|------|------|
| A1 | `eve.save`（或等价）编排层：槽位、元数据、多 provider 注册、单事务 commit | 公共 API + 用户文档 | 现有 SaveSession |
| A2 | schema / 版本 / 迁移钩子；损坏与版本过新拒绝策略 | 契约测试 + failure injection | A1 |
| A3 | 接入 RPG / inventory / building / tactics（或 combat）权威状态 | 示例 `examples/*-save` 或扩展 rpg-classic | A1 |
| A4 | scene / map / hexmap **可选** provider（平面路径优先；球面存档可仍标未实现） | 文档边界清晰 | A1 |
| A5 | MCP / DevTools：列出槽位、导出/导入 JSON 存档 | 调试闭环 | A1 |

**不做（0.7）**：云存档、跨设备同步、加密 DRM。

### Theme B — 作者工具与脚本面（P0/P1）

| ID | 工作项 | 优先级 | 产出 |
|----|--------|--------|------|
| B1 | 绑定文档冲刺：`animation` / `graphics` / `editor` / `building` / `scene` | P0 | gap 显著下降；示例 API 全覆盖 |
| B2 | 帧统计叠加层（若 0.6 未完成则升为 0.7 P0） | P0 | DevTools 面板 + 脚本只读 API |
| B3 | 统一 `eve.Log`（级别、过滤、文件、控制台/MCP 消费） | P1 | 替代散落 `print`/`cerr` 的推荐路径 |
| B4 | LSP / VS Code：补全·跳转·诊断达到「日用可用」门槛 | P1 | 扩展版本与引擎 tag 对齐说明 |
| B5 | 可选资产浏览面板（缩略图 / 打开 / 热重载入口） | P1 | DevTools 或 composable editor 插件 |
| B6 | map ↔ editor 笔刷联动（模块设计未勾项） | P1 | 消费 editor 笔刷的地图编辑示例 |
| B7 | 3D 物理 `drawDebug` | P1 | 与 2D debug draw 对称 |

**不做（0.7）**：完整内置 3D 场景编辑器 App、动画/材质可视化专用编辑器（仍走数据驱动 + 热重载）。

### Theme C — 资产与动画保真（P0/P1）

| ID | 工作项 | 优先级 | 说明 |
|----|--------|--------|------|
| C1 | Unity（或 UE）角色：蒙皮网格 + 最小 Animator/状态机或 clip 集导入 | P0 | 关闭 asset-unity-import「尚未完成」中的角色主路径 |
| C2 | 动画资源热重载 + 与 scene/GPU 蒙皮同步的**最小自动路径** | P0 | 模块设计 animation/ik 后期项的可交付子集 |
| C3 | Prefab Variant：高频组件/字段覆盖（非任意数组全覆盖） | P1 | 结构化拒绝剩余项 |
| C4 | Sprite / 九宫格 UI / AudioClip·字体 canonical 的下一档 | P1 | 按用户需求排序，允许只做 Sprite+UI |
| C5 | Avatar：正式 Cubism Core 绘制路径（用户自带 SDK） | P2 | 保持插件式；不阻塞 0.7 tag |

### Theme D — 图形与表现升级（P1，可选包）

在 0.6「可声明 + limitation」之上，把下列项做成**可开关、可测、双后端契约明确**的升级：

| ID | 工作项 | 优先级 | 备注 |
|----|--------|--------|------|
| D1 | 反射探针：Vulkan staging + GGX/irradiance filter **可发布**路径 | P1 | 见 HDR 文档未勾项；资产缺失须 Unsupported 而非假发布 |
| D2 | 天空 / IBL capture（方向性）最小子集 | P1 | 六面常量 backup 之上的下一步 |
| D3 | TAA **或** 完整多 Pass SMAA（二选一作为生产默认候选） | P1 | MSAA/VXAO/RTAO 仍可选 |
| D4 | SSR 或 Planar Reflection（择一竖切） | P2 | 与 D1 抢人力时让路 |
| D5 | stylize：GBuffer 描边 / 可分离渗色多 Pass | P2 | 风格化增强，非发版阻塞 |
| D6 | WebGPU 与 Vulkan 在 D1–D3 上的 parity 矩阵 | P1 | ClassicScenes 或专用对比夹具 |

### Theme E — 战棋 / 玩法深化（P1）

0.6 已宣布 tactics 可用；0.7 吃掉差距分析 Phase 2 的高价值项：

| ID | 工作项 | 对照 |
|----|--------|------|
| E1 | 部署阶段 + 部署区 | TBS gap Phase 2.1 |
| E2 | 多格单位 / OccupancyPolicy | 2.2 |
| E3 | 战棋 AI 评估层 + `npc_ai` 真实调用路径 | 2.3 |
| E4 | tactics + hexmap + RPG 血量/冷却的组合存档（接 Theme A） | 2.4 |
| E5 | 技能目录与 combat abilities 正式对接 | 2.6 |
| E6 | 迷雾三态（未探索 / 记忆 / 当前可见）平面路径 | Phase 3.2，可视人力降为 P2 |

网络适配（原 2.5）放到 **0.8**，与 Theme F 网络一并做。

### Theme F — 平台与基础设施（P1/P2）

| ID | 工作项 | 优先级 | 说明 |
|----|--------|--------|------|
| F1 | filesystem：归档 mount（zip 等）生产可用 + 测试 | P1 | 模块设计未勾；SDK 分发常用 |
| F2 | filesystem：路径 `.`/`..` FIXME 与增删改查完整面 | P1 | 含现有 5 条债务中的 filesystem 项 |
| F3 | network：异步 IO + 超时/错误模型与 `event` 整合 | P2 | 大改，可开 0.7.x 后半或 0.8 |
| F4 | plugins：string 依赖声明 + 加载/卸载生命周期（热替换可选） | P2 | 绿野；仅当有外部插件用户故事再拉高 |
| F5 | `video` 模块 | P2 | 非叙事主线 |
| F6 | 独立脚本线程 | — | **不做**（Squirrel VM 非线程安全；保持文档「暂不支持」） |
| F7 | 球面 hex：河流/道路/迷雾/单位网格 + 存档格式 | P2 | 平面优先；与 E6/A4 协调 |

### Theme G — 质量与文档卫生（贯穿）

| ID | 工作项 |
|----|--------|
| G1 | `模块设计.md` / 用户手册 / binding gap 与代码持续对齐（0.6 S3 的延续） |
| G2 | 债务信封：`todo`/`soft_skip`/`fallback`/`binding_gap` 净增长保持 0；0.7 目标再砍一档 legacy 计数 |
| G3 | 架构契约：存档、可选依赖 present/absent、跨模块组合测试进 CI |
| G4 | 发布说明模板：Must 故事 / Known limitations / 迁移指南（若有存档 schema） |

---

## 5. 建议排期（按波次，不估日历）

波次表达依赖与可合并性，不承诺人周。

```text
Wave 0  (0.6 ship gate)
  M1–M6, S1–S5  →  tag v0.6.0

Wave 1  (0.7 骨架)
  A1–A3, B1–B2, G1–G2
  →  “能存一局 RPG+背包，创作绑定不再瞎”

Wave 2  (0.7 主内容)
  C1–C2, D1+D6, E1–E4, B3–B5, F1–F2
  →  “角色进得来、反射可发布、战棋能部署+组合存档”

Wave 3  (0.7 抛光 / 可滑 0.7.x)
  A4–A5, C3–C4, D2–D3, E5–E6, B6–B7, G3–G4
  →  tag v0.7.0

Parking lot → 0.8+
  D4–D5, F3–F5, F7, C5, NavMesh 再评估, 完整 Editor App（仍默认不做）
```

---

## 6. 明确不做 / 推迟到 0.8+

| 项 | 原因 |
|----|------|
| 完整内置 3D/2D 编辑器 App | 与「可组合 editor 构件」策略冲突 |
| FMOD/Wwise、NavMesh（除非 3D 竖切强需求） | 工具差距分析 P2 |
| 独立脚本线程 | VM 线程模型限制 |
| RTAO / VXAO / 硬件 MSAA 全家桶 | 可选图形长尾 |
| Unity 任意组件 Variant 全覆盖 | 保真长尾；0.7 只做高频子集 |
| 战棋联网同步完整方案 | 依赖网络模型 F3 |
| 清空全部 859 binding gap | 用「示例面 + 主题模块下降 ≥50%」代替 |

---

## 7. 风险与决策点

1. **存档编排层 API 形状**：新建 `eve.save` vs 扩展现有 gameplay/MCP 协议——Wave 1 第一个 ADR 定案，禁止双栈。
2. **Animator vs clip-only**：若 Animator 全量过大，0.7 允许「clip 集 + 引擎侧状态机」等价路径，但必须在用户文档写清与 Unity Animator 的差异。
3. **反射 filter 与 CI 工具链**：D1 依赖嵌入 SPIR-V / glslc；CI 无编译器时不得标完成——与现有 `__has_include` 守卫一致。
4. **人力互斥**：Theme D 与 Theme C 抢图形/资产同学时，**C1/C2 > D2/D4**（角色进游戏优先于 SSR）。
5. **文档债**：0.6 若未修 `模块设计.md`，0.7 Wave 1 必须先做，否则路线图与仓库真相继续分叉。

---

## 8. 追踪方式

- 本文件为路线图；打勾在发版 checklist / PR 中更新，避免第二套真相。
- 新 TODO/HACK/FALLBACK/ALLOWLIST 必须带 owner / issue / reason / expiry（见 AGENTS.md）。
- 每个 Theme 的第一个 PR 应附：范围、非目标、测试计划、对用户文档的影响。
- 建议在 `v0.6.0` promote 完成后，开 issue 里程碑 `v0.7.0`，按上表 ID 建跟踪项。

---

## 9. 修订记录

| 日期 | 变更 |
|------|------|
| 2026-10-08 | 初稿：基于未完成项盘点、0.6 收口建议与 post-0.5.2 主题 |
| 2026-11-11 | 对照源码更新完成状态，拆分功能/验收/待决策项，补充标记对账与按需优先级；烘焙播放复用 release profile smoke |

---

## 10. 剩余 126 条标记的源码核验快照（2026-11-11）

本节是一次盘点结果，代码基线为 `4132abf95`，入口是本轮开始时 `docs/**/*.md` 中的 126 条未勾选列表。功能合同仍由各模块设计、公共头和测试定义，本节不增加架构 catalogue、不代替发版 checklist，也不把历史执行日志当成本轮测试通过。优先级是本次按数据完整性、主路径与可选增强作出的建议，不是已批准的排期或工期估算。

### 10.1 口径与数量

| 入口分类 | 原标记数 | 处置 |
|---|---:|---|
| 通用规范/评审问题 | 43 | 17 条模块机制、12 条系统完整性、10 条领域建模、4 条去重评审；从功能欠账剔除，规范继续有效 |
| 烘焙 M1–M8 整体验收里程碑 | 8 | 不计作 8 个“尚未实现的系统”；只提取其实际剩余功能和证据 |
| 重复投影 | 8 | 合并到对应主题；不重复计算模块总览、专项设计和路线图 |
| 已有实现的过时标记 | 2 | 脚本 ECS 热重载/VM 隔离钩子；BuildingFx 鬼影绘制；已修正原标记 |
| 范围或机制待决策 | 6 | 不计作已定义功能工单，亦不视为完成或取消；见下文 |
| 有效功能来源标记 | 36 | 归并为 30 个功能主题 |
| 有效验收来源标记 | 23 | 归并为 14 个验收主题 |
| **合计** | **126** | **43 + 8 + 8 + 2 + 6 + 36 + 23** |

再从烘焙里程碑提取 **1 个剩余功能主题（F31）与 3 个验收主题（V14–V16）**，当前为 **31 个功能缺口主题 + 17 个验收主题**。累计外力仅归入 V12 边界测试，不预设实现缺陷或要求限幅；烘焙独立播放已通过现有 release profile matrix 接入 `physics-bake-only`，不另建三平台双配置 codec job。主题不等于原子工单，其中大量为可选增强。

没有发现 126 条中独立的“纯历史日志复选项”。物理计划正文的旧失败、早期“待实现”和会话记录不在这 126 条之外另加库存；M1–M8 是当前总体验收门槛，不能误归档为已完成历史。

重复项的具体合并：HDR 计时描述并入反射链验收；模块总览 AA 并入 SMAA/TAA 专项；AnimLink 专项并入动画自动挂接；Avatar 的 Cubism/口型两项并入模块总览；路线图图形验收并入反射/TAA；Hybrid 的两条跨阶段场景/FrameGraph 项分别并入前一阶段同项。共 8 条。

六个待决策项都来自《模块设计》：独立脚本线程（明确延期且不共享同一 VM 并发执行）、GPGPU 与 thread 的具体调度协议（Sequence 已有异步 submit/poll/wait）、“更多 locomotion”未定义资产/覆盖目标、完整地图编辑器产品范围、数据库热重载与持久库之间的一致性策略、可选独立 entity 模块包装。后两者尤其不能用“再造一个模块/自动覆盖数据库”充当默认实现方案。完整内置 Editor App 仍不属于当前产品策略；地图 target/Brush 的项目级验收可单独定义。

### 10.2 优先级规则

- **P0 核验**：先取得数据完整性/失败隔离与平台发布证据。这里没有把任何未经运行复现的问题宣布为 P0 故障。
- **P1**：近期产品主路径与本路线图的成功标准。功能主题 4 个。
- **P2**：可选表现、性能、内容格式或后续工具增强。功能主题 27 个。

### 10.3 真正剩余的功能主题

以下“缺”均限定于所列生产入口/合同。存在基础机制时只列剩余连接或算法，不把整个模块算成未实现。没有明确合同的泛化需求已进入 10.1 的待决策项。

| ID | 优先级 | 剩余范围与源码依据 |
|---|---|---|
| F01 | P2 | 渲染按独立 scene/debug world 选择实体。`graphics/RenderSystem.cpp` 仍按 ECS View 收集；`RenderSystem3D` 已有 visible、层距离和视锥剔除，但这些不等于独立世界过滤入口。 |
| F02 | P2 | Point Cube 的 GPU atlas/绘制消费路径。`graphics/ShadowScheme.h` 的 `enablePointCube` 明确保留关闭；face VP helper 不代表阴影已渲染。 |
| F03 | P2 | 体积雾后处理采样真实 CSM。`graphics/shaders/volumetric_raymarch.frag` 当前为朝光源屏幕 UV 的深度遮挡近似；不能称作 CSM 纹理采样。 |
| F04 | P2 | 完整 SMAA edge/weight + area/search LUT。`graphics/AntiAliasing` 当前 `smaa` 是单 Pass 启发式。近期发版允许 TAA 等价路线，不要求两者都补齐。 |
| F05 | P2 | 独立物体空间 AO / VXAO / RTAO 路径。已有 SSAO/HBAO/GTAO、材质 AO 或路径追踪不能自动算作这几个专用入口。 |
| F06 | P2 | GI buffer 的专用双边空间模糊。`graphics/GlobalIllumination.cpp` 已有 SSGI 和 temporal resolve；尚未找到原设计要求的独立 bilateral GI pass，不能因已有时间累积就勾选。 |
| F07 | P2 | `video` 资源模块、解码/播放与脚本面。当前模块清单及资源面未发现该模块交付；不把普通纹理/音频资源代替视频。 |
| F08 | P2 | 插件依赖解析和真正卸载两项。`plugins/Plugins.cpp` 已有注册事务与热替换，但 `unload()` 仍直接返回 false，已初始化镜像保持驻留。 |
| F09 | P1 | HTTPS/TLS。`network/HttpRequest.cpp` 遇 https 显式报告 `tls` 错误；HTTP/TCP 超时和事件已存在。 |
| F10 | P2 | 完整 Spine mesh/deform/IK/path。`animation/SpineSkeletonData.cpp` 明确拒绝相关 constraint/timeline，仅 region/basic timeline；官方 runtime 插件是可选交付路线。 |
| F11 | P1 | Scene 与动画 owner 生命周期的自动挂接/姿态推进/解绑恢复。`AnimSkin::applyToMesh` 显式 palette 上传已存在；`scene` 中未发现对应 AnimLink 生产桥。“更多脚本 Desc 值类型”未列出字段合同，不另算缺失功能。 |
| F12 | P2 | 独立 `ik::Skeleton3D` 与动画骨架/Scene 的通用适配。`animation/FootIKSolver` 已对 AnimPose 提供专用腿部 IK，缺的不是所有 IK 算法，而是原计划的独立 ik 模块接线。 |
| F13 | P2 | procgen 脚本/插件算法注册闭环及 `generateAsync` 完成事件。`GeneratorRegistry::registerAlgorithm` 的 C++ 注册已实现；现有绑定主要是算法枚举/调用，不能替代这些扩展入口。 |
| F14 | P2 | Stylize 的 GBuffer 描边、开放风格注册和专用可分离渗色。`stylize/StyleShaders.cpp` 为固定 definition 表；`StyleChain` 多 Pass ping-pong 已存在，但不是这三项算法/注册交付。 |
| F15 | P2 | spatial 的 map 对象适配与 `queryFrustum`。`spatial` 现有 AABB/sphere 等查询；`scene::collectFrustumIdsAt` 是已有 Scene 消费，不能重复宣传为 spatial 的新入口。 |
| F16 | P2 | voxel 的 chunk 内更细增量、LOD 与压缩存档。`VoxelWorld::remeshDirty` 已提供 dirty chunk 及预算；不能再次计入完整 chunk 重建。 |
| F17 | P2 | 雪场 GPU compute 写入、局部纹理提交与 GBuffer POM。`weather/SnowField` 当前 CPU 栅格修改，`Snow::updateTexture` 上传整幅；基础脚印/高度图已存在。 |
| F18 | P1 | 脚本 ECS 跨会话身份重建与版本迁移生产组合。`common/ScriptComponentSnapshot.h` 明确快照身份为 VM 内 entityId；已有 schema/候选恢复不等于跨进程稳定 ID 已闭环。 |
| F19 | P2 | RPG tick/cast/Tracker 与 `platform_event` 的明确适配。当前独立 poll 缓存是已有机制；是否接入瞬态平台事件需保持与 deterministic `game_event` 的边界。 |
| F20 | P2 | 背包格子的领域拖拽组件/交互适配。`ui/UISystem` 已有通用 drag/drop，inventory 有移动/转移规则；没有找到自动组成库存格子语义的模块适配，不能再算“UI 无拖拽”。 |
| F21 | P2 | Building 装配非矩形网格持久化与跨玩法 save participant。`BuildingAssemblyPersistence` 有 v1 世界/蓝图，`checkGrid` 只收正方形矩形网格；等距/六角运行时已实现。 |
| F22 | P2 | 正式 Cubism SDK 插件，以及真实音频到角色口型的生产桥。`Live2DNullBackend`/demo 插件不渲染 Cubism；`DialogueVoice` 已能读取提供的 envelope、audio editing 已有 RMS，但 `Dialogue::updateLipSync` 仍用打字机 sin 包络，缺的是实际连接而非全部包络算法。 |
| F23 | P2 | GPU Agents 的 GPU solver/CPU 对照与 GPU 深度纹理 surface capture。现有 CPU solver、双 vector 和 `SurfaceCapture::captureFromTriangles` 不等于 GPU 模拟/读深度。 |
| F24 | P2 | Groom Alembic 导入及 hair 编辑卫星。`GroomAsset`、groups/LOD、guides/仿真已有；容器及通用属性编辑不能证明 Alembic 属性映射与独立作者面已交付。 |
| F25 | P2 | 完整事件/对话图的项目级作者交互。`DialogueGraph` 编译器、运行桥和紧凑字段示例已有；只计节点/路由编辑到发布的剩余工作，不扩成内置通用 Editor App。 |
| F26 | P1 | GBuffer 完整 MR/normal/occlusion/emissive 贴图求值。`RenderSystem3D` 的 GBuffer 绘制传入 albedo、标量 roughness/metallic，尚未像前向 Material 一样消费完整 PbrSurface 贴图。 |
| F27 | P2 | deferredLighting 的完整 FrameGraph 资源声明和 JobSystem 并行录制。当前 `RenderSystem3D` 在 begin3DFrame 后直接 drawDeferredLighting；串行灯光已存在，缺的是调度整合。 |
| F29 | P2 | 轮胎完成态存档按需扩展：仅在要求恢复瞬时轮速/接触状态时安排；普通存档允许重建运行态时不构成缺口。Jolt coupling/射线轮胎同属可选后端扩展。`VehicleTireRuntime` 完成后发布到 SuspensionState，但未找到 wheel runtime save 路径；`Coupling3D` 与 `VehicleTires3D` 对非 Box3D 显式 Unsupported。 |
| F30 | P2 | GeometryCollection 与 building 表现 owner 的可选桥。内部 `stageConstraints` 的 Weld 投影已存在；不能用它替代占格/实例销毁与碎块表现之间的连接，英雄物体场景精度归 V12。 |
| F31 | P2 | 非 Linux 烘焙文件落盘耐久屏障：仅在要求保存成功后断电仍保留资产时安排；可重新生成的烘焙缓存不以此阻塞交付。`physics/backend/bake/BakedAssetFile.cpp` 的 fsync/匿名 staging 路径只在 Linux；portable hard-link 路径提供原子可见，不能当成平台耐久实现。 |
| F32 | P2 | 3D 物理 debug draw 按调试需求接线，作为开发工具增强，不阻塞运行/烘焙主路径。`WorldDebugDraw.cpp` 是 Box2D `b2Draw`，未找到等价 World3D 入口；FPS 面板及 `CrashLog::recordLogEvent(level,...)` 的 severity 标签已存在，不重复计作缺失。可配置日志过滤若另有要求须先定义合同。 |

### 10.4 剩余验收主题

“待验收”表示已有相关实现或局部测试，尚缺对应范围的证据；不意味着本次已复现失败。当前没有查询托管 CI 的 revision 终态，不能把文档缺证据写成“平台一定没通过”。

| ID | 优先级 | 必须补齐的证据与已有基础 |
|---|---|---|
| V01 | P1 | Vulkan/WebGPU capture → copy → filter → active lighting 的图像/失败/计时闭环。已有 `ReflectionProbeCapture::filterAndPublish` 成功后交换；WebGPU copy 单独计时受 pass 边界限制，应明确测量范围而非虚构 timestamp。 |
| V02 | P2 | 真 HDR 显示器上的 Windows/Linux 输出、元数据和 SDR 回退。色彩空间选择已有；本次未找到引擎 HDR metadata 发布调用，先核定平台实现要求，再做实机验收，不能只凭 Lavapipe/API 支持宣称完成。 |
| V03 | P0 核验 | 一局游戏的跨 RPG + inventory + tactics/combat + scene/map 事务保存/恢复、跨进程身份、未知版本与后段失败不污染。`GameSaveSession`、RPG/Tactics participant、failure/absent/migration 单测已有；缺原定完整生产组合。与 F18/F21 有依赖。 |
| V04 | P1 | binding gap 按明确 tag/示例调用面计算的达标报告。绑定提取通过不等于“比 0.6 降低 ≥50%”；不能沿用旧路线图中的 859 作为当前数字。 |
| V05 | P1 | 外部角色导入到运行蒙皮/动画的正式竖切。`UnityFbx` 已能经 glTF 转换皮肤/骨架/clip，`UnityCollection` 仍报告 renderer/Animator binding 不转换；允许路线图规定的 clip + 引擎状态机等价路线，需真实资产及差异说明。 |
| V06 | P1 | 当前目标 revision 的发版门禁、五平台 SDK consumer 与 native host 结果。上一轮 make check 已通过，但不能外推到全部宿主/SDK。本轮文档 gate 结果另记录于交付，不复制历史日志当新证据。 |
| V07 | P1 | GCC/Vulkan 的生产 GPU cloth/fluid parity、provider present/absent、snapshot/failure 与 runtime smoke 的统一报告。接口/shared contract 存在，不用 mock 或 include 图替代真实 GPU/trimmed executable。 |
| V08 | P1 | height/biome/mask → PointSet → 真实资源解析 → 渲染的组合验收。地形对象/detail points、mask 规则及 grass adapter 已存在，需对同一输入和身份贯通验证。 |
| V09 | P1 | Result 消费方短路组合的范围审计及 `everyResultValid` 独立失败覆盖。helper 已存在，未找到直接调用它的专项回归；通用 Result 编译器/移动/投影测试不重复计为缺项。 |
| V10 | P1 | TAA 动态遮挡、反遮挡、镜头 cut、尺寸/曝光变化与跨后端图像；history/jitter/reprojection 已有，不能据此宣布生产质量达标。 |
| V11 | P1 | PBR chart、DamagedHelmet 的 ForwardPlus/Hybrid 固定容差图像对照；两阶段重复标记只保留一组。与 F26 的贴图覆盖有关。 |
| V12 | P1 | 物理完整数值与失败合同：同一步内各批均有限但累加可能溢出的极值、复杂齿轮/电机/接触闭环、场边界连续性/能量、接触摩擦不重复施加、轮胎步长收敛及英雄碎块精度。已有中心力/浮力/偏心/坡道/失败测试；需要矩阵和可复现报告；正常力应叠加，不预设任意限幅/新预算机制，也不把尚未运行的极值测试宣布为实现缺陷。 |
| V13 | P1 | 机械/force-lab 交付组合：六面文档、完整新增 VM 入口、physics-core-only provider 双配置、真实宿主构建/领域测试、正式示例 ≥2 秒 smoke 与引擎自有截图。历史多批成功/5 项基线失败需按当前 revision 复核，不把某一旧会话的失败数当当前结论。 |
| V14 | P1 | 烘焙裁剪 profile 的实际构建/播放 smoke、普通平台回归中的 Windows DLL 导出/主宿主组合与存储故障合同。独立 profile 及本地历史 Linux 证据已有；复用 `release-ci.yml` 的 physics-bake-only configure/build/smoke 与普通平台回归，收取目标 revision 结果；不要求另建 codec 六组合 job。F31 的耐久范围需单独证明。 |
| V15 | P1 | 烘焙粒子源别名、Graphics owner、shader/camera/canvas/材质/纹理退役重建的剩余资源组合。已有多个替换/销毁测试；禁止源别名删除的当前合同不是“安全删除已验收”。 |
| V16 | P1 | 房屋约 8 秒处源姿态突变的根因与误差基线。`resampling_error`/`collapse_collision_reference` 等已有；需区分源仿真、采样与插值，不凭当前现象直接归咎于播放器。 |
| V17 | P2 | 多选变换的项目交互/Undo/失败恢复验收。`scene/editing/SceneTarget` 已有先验证后发布的 multi-transform，`SceneEditorSession::commitPhysicsPlacement` 把 selected objects 提交为一个 batch；`scene.physicsPlacement.rotates_a_multi_selection_as_one_rigid_layout` 测试已有。缺的是原定完整交互验收，不能再列“多选事务未实现”；指定另造 ImGui 后端不是功能门槛。 |

烘焙 GPU resident、ribbon、相机/年龄排序仍在 `particles/ParticleRecording.cpp` 显式拒绝。它们作为未支持的可选扩展保留在边界说明，不新增为已批准 sprite 录制交付的必过项。四通道核心、recipe 身份、时钟与已覆盖生命周期不重新列为“待实现”。

### 10.5 建议推进顺序

1. 先围绕实际游戏主路径验证 V03 跨域存档，以及 V05 角色导入/动画，已有测试和 CI 承担常规回归。
2. 近期实现聚焦 F18/F11/F26：跨会话身份、动画自动接线与完整 PBR 贴图；项目使用 HTTPS 时安排 F09。
3. 健壮性和调试增强按实际需求安排：累计外力只在相关物理修改时补 V12 边界测试；轮胎瞬时态存档 F29、非 Linux 断电耐久 F31、3D debug draw F32 按需安排；烘焙 profile 接线已完成，由现有 release CI 收取 V14 运行结果。
4. F01–F08 等 P2 图形长尾、完整格式和作者工具按实际项目需求选配。完整 SMAA 与已在验收的 TAA 是可替代路线；不以“清空复选框”为发版目标。

### 10.6 原始来源对账

表中为本轮入口的冻结计数，后续修正标记会改变实时 raw 数量。N=规范，A=总里程碑，D=重复，O=过时，S=待决策，F=功能来源，V=验收来源。部分一条标记含多个子合同，拆分细节以上表为准。

| 文档 | 入口数 | 分类 |
|---|---:|---|
| [2026-08-26-architecture-consolidation-checklist.md](2026-08-26-architecture-consolidation-checklist.md) | 1 | V 1 |
| [2026-09-29-chaos-destruction-geometry-collection设计.md](2026-09-29-chaos-destruction-geometry-collection设计.md) | 1 | F 1 |
| [2026-10-05-gpu-agents-simulation-framework.md](2026-10-05-gpu-agents-simulation-framework.md) | 2 | F 2 |
| [2026-10-08-release-roadmap-0.6-0.7.md](2026-10-08-release-roadmap-0.6-0.7.md) | 6 | D 1、F 1、V 4 |
| [2026-10-10-api-dedup-consolidation-plan.md](2026-10-10-api-dedup-consolidation-plan.md) | 4 | N 4 |
| [2026-10-21-physics-bake-replay.md](2026-10-21-physics-bake-replay.md) | 8 | A 8 |
| [GUI框架设计.md](GUI框架设计.md) | 1 | F 1 |
| [HDR与反射链AAA升级.md](HDR与反射链AAA升级.md) | 3 | D 1、V 2 |
| [Result检查与不得丢弃返回值规范.md](Result检查与不得丢弃返回值规范.md) | 1 | V 1 |
| [2026-09-22-physics-mechanics-and-forces.md](plans/2026-09-22-physics-mechanics-and-forces.md) | 12 | F 1、V 11 |
| [2026-09-26-hybrid-clustered-deferred-forward-plus-design.md](superpowers/specs/2026-09-26-hybrid-clustered-deferred-forward-plus-design.md) | 5 | D 2、F 2、V 1 |
| [全局光照模块设计.md](全局光照模块设计.md) | 1 | F 1 |
| [场景节点ECS打通设计.md](场景节点ECS打通设计.md) | 1 | D 1 |
| [对话与Avatar模块设计.md](对话与Avatar模块设计.md) | 2 | D 2 |
| [抗锯齿模块设计.md](抗锯齿模块设计.md) | 2 | F 1、V 1 |
| [模块接口契约与机制选型规范.md](模块接口契约与机制选型规范.md) | 17 | N 17 |
| [模块设计.md](模块设计.md) | 33 | D 1、O 2、S 6、F 23、V 1 |
| [毛发Groom子系统设计.md](毛发Groom子系统设计.md) | 2 | F 2 |
| [程序化生成模块设计.md](程序化生成模块设计.md) | 2 | F 1、V 1 |
| [重构代码质量与系统完整性规范.md](重构代码质量与系统完整性规范.md) | 12 | N 12 |
| [领域短根继承与跨域组合架构.md](领域短根继承与跨域组合架构.md) | 10 | N 10 |
