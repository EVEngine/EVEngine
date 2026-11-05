# EVEngine 第一人称射击（FPS）竖切片计划书

日期：2026-11-06
状态：规划（非合同；实施以独立 PR 与验收示例为准）
基线：`dev` @ 计划起草日；配套能力盘点见对话产出与本文件 §2
存放：`docs/dev/`

> 配套阅读：
> [通用武器系统设计](通用武器系统设计.md)、
> [战斗载体与投射物行为组合框架](战斗载体与投射物行为组合框架.md)、
> [感知与目标选择系统设计](感知与目标选择系统设计.md)、
> [游戏开发系统与工具差距分析](游戏开发系统与工具差距分析.md)、
> [0.6 / 0.7 发布路线图](2026-10-08-release-roadmap-0.6-0.7.md)、
> 用户手册 [`weapon`](../usr/modules/weapon.md) /
> [`camera`](../usr/modules/camera.md) /
> [`combat`](../usr/modules/combat.md) /
> [`physics`](../usr/modules/physics.md) /
> [`sensing`](../usr/modules/sensing.md)。

---

## 1. 一句话目标

用**现有积木拼出一条可玩、可测、有文档的单人 FPS 竖切**
（第一人称看 + 走 + 开火命中扣血 + 基础手感），再按优先级补齐引擎缺口；
**不做**开箱即用的「完整 FPS 框架」或抢跑 0.8+ 的 NavMesh / 网络大改。

---

## 2. 现状结论（为何需要这份计划）

引擎已有可组合能力，但**没有 FPS 产品面**：

| 已有 | 缺口 / 边界 |
|------|-------------|
| `weapon`：弹药、装填、模式、散射、后坐力状态、ADS 标志、zoom FOV 建议、hitscan/projectile **事件** | hitscan **不**做射线与伤害结算；无枪械配件槽 |
| `CombatCarrierRuntime` + `physics.rayCast*` / `moveCapsule` | 游戏侧需自接线 |
| `combat`：`DamageRuntime`、部位、受击反应 | 无骨骼 ragdoll |
| `camera`：`firstperson`；`mouse` 相对模式 | 无 head bob；无独立 viewmodel / 双 FOV |
| `combat` 移动意图 + 胶囊 mover | 无 sprint / crouch / slide 一体控制器 |
| `sensing` / `npc_ai` / 网格寻路 | 连续 NavMesh 延期；weapon soft-lock 未落地；aim assist 明确留游戏侧 |
| `network`：UDP / RPC / NetHost | 无实体状态复制 |
| HUD toolkit / inventory | 无准星·弹药专用控件；无 FPS 竖切示例 |

参考接线：`rts` 的开火→命中→`DamageRuntime`；镜头示例 `examples/camera-controllers`；
弹道示例 `advanced-ballistics` / `world-sweep-ballistics`。  
**没有**端到端第一人称射击示例。

---

## 3. 目标与非目标

### 3.1 目标

1. **可玩竖切**：玩家相对鼠标看、胶囊走、开火命中静态/移动靶、扣血与受击反馈、弹匣与装填、基础后坐力/开镜 FOV。
2. **契约清晰**：武器仍只发事件；命中与 HP 权威在 combat / 游戏编排层（与武器设计非目标一致）。
3. **可回归**：`examples/fps-arena`（暂定名）进 smoke；关键路径有零err/用例或脚本断言。
4. **文档入口**：用户手册「这篇解决什么」+ 竖切 README；缺口写进「已知边界」。
5. **分阶段可裁剪**：每阶段独立可合入，不阻塞 0.6/0.7 现有 Must。

### 3.2 非目标（本计划明确不做）

- 完整竞技 / 大逃杀产品、经济与匹配。
- 引擎内置 aim assist / sticky aim（sensing 设计已声明；游戏侧可自做）。
- 0.8+：连续 NavMesh、网络实体复制大改、FMOD/Wwise、完整 FPS 关卡 Editor。
- 把 hitscan 射线+扣血硬塞进 `weapon` 破坏「事件 + 权威 owner」边界。
- 万能 `Player` / `GameObject` 根类型（遵守领域短根与跨域组合规范）。

---

## 4. 架构原则（实施约束）

1. **组合优于新巨石模块**：优先脚本编排 + 现有 runtime；确需引擎能力时按宿主包扩展（如 `combat` 步态、`graphics` viewmodel），不新建顶层 `fps` 大杂烩目录，除非评审认定横切过厚。
2. **单一权威**：弹药权威在 weapon；HP 在 combat；移动位置在 physics mover / 游戏 pose owner；相机看向在 camera。
3. **Result / 事件**：公开 API 遵守 Result 与 `[[nodiscard]]`；跨模块用事件或明确 Link，不丢弃关键返回值。
4. **热路径声明**：每帧开火/射线/mover 文件标 `eve-hot-path`；禁止每帧字符串键查找。
5. **可选依赖可测**：World3D sweep、sensing LOS 等 optional 路径要有 present/absent 覆盖意识。
6. **示例即合同**：竖切示例是验收面；VFX-only 示例不算 FPS 竖切完成。

---

## 5. 分期计划

优先级：`P0` = 竖切阻塞；`P1` = 手感与可演示质量；`P2` = 产品增强；`P3` = 与 0.8+ 对齐、本计划只记账。

### Phase A — 胶水竖切（P0，以示例为主）

**意图**：零或极少引擎改动，证明「镜头 + mover + weapon 事件 + rayCast + DamageRuntime」可跑通。

| # | 工作项 | Owner 建议 | 验收 |
|---|--------|------------|------|
| A1 | 新建 `examples/fps-arena`：相对鼠标 + `firstperson` + WASD + `moveCapsule` | examples | smoke ≥2s；README 步骤可复现 |
| A2 | 消费 `weapon` Fire 事件：准星中心 `rayCast`（或短段 sweep）；命中写入 `DamageRuntime` | examples + 参考 rts | 靶子 HP 下降；空弹/装填可观察 |
| A3 | 应用 recoil 事件到 `CameraController.addInput`；`setAiming` 时套用 `getZoomFov` | examples | 开火抬头/侧偏可感；开镜 FOV 变化 |
| A4 | 最小 HUD：准星十字 + 弹匣/储备数字 + HP 条（可用 `ui` 现件或 2D 绘制） | examples | 帧内数值与 weapon/combat 一致 |
| A5 | 用户手册短文：`docs/usr/` 入口或 `weapon`/`camera` 交叉链到本示例 | docs | 「这篇解决什么」列出 FPS 组装路径 |

**退出标准**：新人按 README 能在本机跑通「看到→走到→开枪打死靶」。

**刻意不做**：sprint/crouch、viewmodel、配件、敌人 AI。

---

### Phase B — FPS 玩家栈（P1，引擎最小增量）

**意图**：把示例里散落的步态与镜头手感收成可复用 runtime / 文档化 API。

| # | 工作项 | Owner 建议 | 验收 |
|---|--------|------------|------|
| B1 | 步态状态机：Walk / Sprint / Crouch（可选 Slide）；驱动胶囊半高与速度 | `combat` 或轻量 `locomotion` 宿主片段 | 单元测试状态迁移；示例切换步态 |
| B2 | 落地/空中与 `moveCapsule` / `ICombatMoveProbe` 对齐；蹲下不可卡地形的策略写清 | physics + combat | 坡地/台阶用例或示例场景 |
| B3 | Head bob / 落地沉降（参数化，可关） | camera 或 combat camera | 示例开关；默认不过度 |
| B4 | 鼠标灵敏度与俯仰钳制约定（脚本默认 + 文档） | docs + example | 文档给出推荐缩放 |

**退出标准**：`fps-arena` 改用引擎步态 API，手感参数可调，无「只能复制示例胶水」的阻塞。

**边界**：不引入完整公园our；攀爬继续用现有 `climbing` 可选组合。

---

### Phase C — 开火反馈与命中表现（P1）

| # | 工作项 | Owner 建议 | 验收 |
|---|--------|------------|------|
| C1 | 命中去耦：表面标签 → 弹孔 decal + 可选音效 one-shot | examples + decal/audio | 墙面/靶子不同反馈 |
| C2 | 受击：已有 Flinch 等接到靶子网格或简模 | combat + example | 命中有可见反应 |
| C3 | 可选：载体弹（非 hitscan）一条枪定义走 `CombatCarrierRuntime` | weapon example | 与 A2 并存、可切换 |
| C4 | 文档：命中权威时序（fire 事件 → cast → damage → FX） | docs | 时序图或步骤列表 |

**退出标准**：竖切「手感完整度」达到可对外录屏，而不只是数值扣血。

---

### Phase D — Viewmodel（P1/P2，图形向）

**意图**：第一人称武器/手臂独立 FOV 与深度，避免世界 FOV 拉伸。

| # | 工作项 | Owner 建议 | 验收 |
|---|--------|------------|------|
| D1 | 设计稿：第二相机或 view-layer、深度/阴影策略、与主相机同步 | graphics 设计 PR | 评审通过后再写代码 |
| D2 | 最小实现：静态枪模跟随眼点；开火播简单动画或弹簧后坐 | graphics + example | 左右手位置稳定；ADS 可选隐藏/偏移 |
| D3 | 与 weapon Rig 的关系写清（world rig vs viewmodel） | docs | 避免两套持握互相打架 |

**退出标准**：竖切默认启用 viewmodel；关闭时回退到「仅世界枪/无枪」有文档。

---

### Phase E — 配件与负载（P2，按需）

| # | 工作项 | Owner 建议 | 验收 |
|---|--------|------------|------|
| E1 | 配件槽数据模型（optic / muzzle / grip…）改定义字段或独立表 | weapon | JSON 可注册；未知槽失败 Result |
| E2 | 修饰器：散射、后坐力、ADS FOV、伤害落差等可叠加且可测 | weapon | 单元测试修饰顺序 |
| E3 | 与 `inventory` / `EquipmentSet` 换装演示 | example | 换镜改 FOV/准星 |

**前置**：A–C 稳定。若无改装枪玩法需求，本阶段可整段跳过。

---

### Phase F — 敌人与关卡（P2，仍避开 NavMesh 强依赖）

| # | 工作项 | Owner 建议 | 验收 |
|---|--------|------------|------|
| F1 | 简易射手 AI：sensing 扇形 + LOS + 停步射击（网格或航点） | npc_ai + sensing + example | 敌人能发现并开火 |
| F2 | SmartObject 掩体点：到点隐藏/探出（不做完整 peek 动画集） | npc_ai | 至少一个掩体点行为 |
| F3 | 关卡：`scene_editor` 或手工场景 + 刷怪点表 | example | 两间房 + 走廊可打通 |

**明确推迟**：连续 NavMesh bake（记入 §7，对齐 roadmap 0.8+ 再评估）。

---

### Phase G — 记账项（P3 / 0.8+，本计划不排期实现）

| 项 | 说明 |
|----|------|
| 实体复制 / NetworkVariable | 见 network 改进设计 Phase 3；局域网死亡竞赛另开计划 |
| 连续 NavMesh | 差距分析 P2；3D FPS 大规模再评估 |
| 骨骼 ragdoll | 现有 hit lean / softbody 不替代；需独立设计 |
| Weapon soft-lock 获取 | sensing Phase 3 消费者；aim assist 仍游戏侧 |
| Magic/Missile weapon form | 武器设计 Phase 2，非 FPS 阻塞 |
| FMOD/Wwise、完整 FPS 地图工具链 | 工具差距分析 P2 |

---

## 6. 与发布路线图的关系

| 路线图版本 | 本计划关系 |
|------------|------------|
| **0.6** | **不**纳入 0.6 Must。Phase A 可作为 `dev` 示例增强单独合入，不挡发版叙事。 |
| **0.7** | Phase B/C 若成熟，可作为「玩法竖切」Should 候选；需另改路线图条目。 |
| **0.8+** | Phase G（NavMesh、网络复制、ragdoll）与现有 Parking lot 对齐。 |

本文件**不修改** `2026-10-08-release-roadmap-0.6-0.7.md` 正文；若产品决定把 FPS 竖切升为发版 Must，另提路线图修订 PR。

---

## 7. 建议模块归属（避免冲突热点）

| 能力 | 归属 | 备注 |
|------|------|------|
| 开火时序 / 弹药 / 散射 / 后坐力数值 | `weapon` | 保持事件边界 |
| 命中结算 / HP / 受击 | `combat` | 已有 |
| 胶囊移动 / 射线 | `physics` | 已有；步态高度策略可协作 |
| 步态状态机 | `combat`（优先）或 manifest 内新卫星目录 | 勿新建无宿主的顶层 `player` |
| 第一人称镜头手感 | `camera` | bob/沉降 |
| Viewmodel 渲染 | `graphics`（+ 设计稿） | 机械跨模块清扫另 PR |
| 感知 / 简易 AI | `sensing` / `npc_ai` | |
| 竖切编排 | `examples/fps-arena` | 游戏侧真相来源之一 |

跨模块机械改动（导出宏、全库 Doxygen）**禁止**夹带在 FPS 功能 PR 内。

---

## 8. 验收总表（竖切「完成」定义）

同时满足方可称 **FPS 竖切 v1 完成**（对应 Phase A–C，D 可为 v1.1）：

- [ ] `examples/fps-arena` 可 run / smoke 绿
- [ ] 相对鼠标第一人称看 + 胶囊移动 + 跳跃
- [ ] 至少一把 hitscan 枪：弹匣、装填、开火、命中扣血
- [ ] 后坐力影响视角；ADS 改变 FOV
- [ ] 准星 + 弹药 + HP 可读
- [ ] 命中有至少一种世界反馈（decal 或闪白/受击）
- [ ] 用户文档写清组装路径与「武器不结算 HP」边界
- [ ] `make check` 相关门禁不因该示例/模块回归

v1.1：viewmodel（Phase D）。  
v1.2：步态完整化（Phase B 若未进 v1）+ 简易敌人（Phase F）。

---

## 9. 风险与缓解

| 风险 | 缓解 |
|------|------|
| 示例胶水过重、无法复用 | Phase A 后立刻抽 B1；拒绝长期只靠复制 `main.nut` |
| Viewmodel 牵扯渲染管线延期 | D1 必须先设计 PR；v1 可无 viewmodel 发 |
| 步态与 climbing/combat 移动重复 | 共用 probe/mover；状态机单 owner |
| 误把复制/NavMesh 拉进范围 | §3.2 / Phase G 显式推迟；评审打回 |
| 破坏 weapon 事件边界 | 代码审禁止在 `HitscanLogic::fire` 内直接扣血 |
| 与动作/战棋竖切抢人 | FPS 全程旁路 0.6 Must；小步 PR |

---

## 10. 建议落地顺序（执行清单）

1. 评审本计划：确认非目标与 Phase A 范围。  
2. 开 `examples/fps-arena` PR（Phase A）——最快可见进度。  
3. 并行短文：手册交叉链接。  
4. 评审是否抽步态进引擎（Phase B）或先参数化示例。  
5. Phase C 表现；再决定 D（viewmodel）设计稿。  
6. E/F 按玩法需要排队；G 只开议题不排期。

---

## 11. 修订记录

| 日期 | 变更 |
|------|------|
| 2026-11-06 | 初稿：基于引擎能力盘点与 0.6/0.7 路线图边界 |
