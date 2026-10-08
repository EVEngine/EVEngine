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

- [ ] 正式存档：跨至少 RPG + inventory + tactics（或 combat）+ scene/map 子集的单事务存读档示例与契约测试
- [ ] 创作面：`animation`/`graphics`/`editor`/`building`/`scene` binding gap 合计较 0.6 tag **下降 ≥50%**（或示例调用面 100% 有文档）
- [ ] 资产：Unity 导入「角色蒙皮 + Animator 最小集」或等价 UE 路径有可复现示例；失败项仍结构化报告
- [ ] 图形：反射探针 Vulkan filter 发布路径闭环 **或** 文档明确 Unsupported 且无假发布；TAA 或完整 SMAA 至少一条生产可用
- [ ] 输入与工具：帧统计 + 级别化日志 + 3D 物理 debug draw 可用
- [ ] 门禁：同 0.6，外加存档 failure-injection 与 provider present/absent 用例（见架构巩固清单精神）

---

## 4. 0.7 工作流（按主题）

优先级：`P0` = 竖切阻塞 / 发版叙事；`P1` = 强烈建议；`P2` = 有余力或可滑到 0.8。

### Theme A — 持久化平台（P0）

**问题**：各域已有 `snapshotJson` / `*SaveSession`，缺引擎级「游戏存档」契约（schema id、版本、未知字段、迁移、部分失败不污染）。

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
