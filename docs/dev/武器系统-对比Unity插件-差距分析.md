# 武器系统 vs Unity 插件 — 功能差距分析

日期：2026-08-25
分支：`codex/weapon-system-v2`
存放：`docs/dev/`
对照对象：EVEngine `weapon` v2（`src/modules/weapon/`）
参考插件：UFPS / Ultimate FPS、Arsenal、WeaponCore、Cursed Studios Weapons、
MFPS（多人）、Invector（近战/射击）、Ranged Weapon System、Better Weapon System。

> 说明：Asset Store 页面为 JS 渲染，无法直接抓取结构化特性，以下基于这些插件的
> 公开特性集合整理。目标不是"做成一模一样的 FPS 插件"，而是**按 EVEngine 的可裁剪、
> 数据驱动原则**，找出 v2 相对常见 Unity 插件缺失、且对 RPG / RTS / ARPG 有价值的功能，
> 并为每条给出落点（放在哪一层、用什么机制实现），便于排期。

图例：✅ 已有　⚠️ 部分/半成品　❌ 缺失

## 落地状态

**P0 批已实现（2026-08-25，`codex/weapon-system-v2`）**：A1 散布 bloom、A2 后坐力模型、
A3 多弹丸（霰弹）、A7 射击模式切换（safe/semi/auto）、B2 共享弹药池、F1 伤害类型/元素、
G1 开镜 ADS/zoom。全部走"数据字段 + 事件载荷"，未改架构，热武器回归基线不变；
`test/weapon.cpp` 25 个用例、210 断言全绿。P1/P2 批仍为待办。

---

## 一、我们的 v2 已经具备（基线）

| 能力 | 落点 |
|---|---|
| 多形态（melee/ranged/magic/missile） | `WeaponKind` |
| 通用触发资源（弹药/法力/充能/体力/无） | `AttackResource` + `Resource` |
| 阶段机 Idle→Windup→Active→Recover | `AttackStageSpec` / `WeaponSystem::updateStage` |
| 开火模式 single/burst/auto、冷却、连发 | `FireMode` / `cooldown` / `burst*` |
| 命中弧（近战） / AoE 半径 / 目标句柄 | `AttackRequest.arcAngle / aoeRadius / targetHandle` |
| 事件队列 fire/reload_start/reload_end/empty/windup_start/attack_end | `WeaponEvent` |
| 可插拔逻辑 begin/channel/fire/end/update | `IWeaponLogic` |
| 弹道/命中解耦（游戏侧投射物服务） | `IProjectileService` 能力 |
| 炮塔挂点（限位/转速/射界）+ 手持位 | `WeaponMountEntity` / `WeaponRigEntity` |
| JSON 数据驱动定义、Squirrel 脚本绑定 | `registerWeaponsFromJson` / `eve.Weapon` |

---

## 二、缺失功能清单（按主题 + 优先级）

### A. 载具与手感类（热武器/FPS 相关，RTS/ARPG 也受益）

| # | 功能 | 插件常见做法 | 现状 | 落点建议 | 优先级 |
|---|---|---|---|---|---|
| A1 | **散布扩散 bloom**（连续射击精度下降，停火回稳） | `spreadMin/spreadMax/spreadIncreasePerShot/spreadRecoverTime` | ✅ 已实现（spreadMin/Max/PerShot/Recover） | `WeaponDefinition` 加 `spreadBloom`；`State` 加当前散布，`canFire`/事件里用 | 高 |
| A2 | **后坐力模型（recoil pattern / 曲线）** | 射击后相机上抬 + 水平漂移 + 回正；`recoilAngle/horizontal/curve` | ✅ 已实现（recoilPitch/Yaw/Recover，事件携带） | 事件 `fire` 携带 recoil；游戏侧驱动相机；或 `WeaponDefinition.recoil` | 高 |
| A3 | **多弹丸（霰弹）** | `pelletCount`，一次发射多颗 | ✅ 已实现（projectile.pelletCount/pelletSpread） | `ProjectileSpec.pelletCount + pelletSpread`；`tryFireRanged` 循环 `logic->fire` | 高 |
| A4 | **蓄力武器（hold-to-charge）** | 按住蓄力，松开释放，伤害/速度随蓄力档位 | ⚠️ windup 只能延迟，不能"按住再松开" | 阶段机加 `Charge` 状态 + `releaseRequest` | 中 |
| A5 | **持续光束武器** | `beam`，射线每帧结算 | ❌ | `RangedForm` 加 `beam` logic，channel() 里逐帧伤害 | 中 |
| A6 | **掷弹/投掷物** | 抛物线 + 引信计时 + cook | ❌ | `MissileForm` 或新增 `grenade` kind：抛体 + 计时 | 中 |
| A7 | **射击模式切换（safe/semi/auto 运行时切换）** | 玩家切 single/burst/auto | ✅ 已实现（fireModes + setFireMode） | `WeaponEntity` 加 `selector` 运行时字段，校验合法性 | 中 |
| A8 | **干火（空膛扣扳机）** | 空膛时不同的"click"反馈 | ⚠️ 有 `empty` 事件，无专门 dry-fire 表现 | 事件扩展 `dry_fire` 或在 `empty` 上细分 | 低 |

### B. 弹药与生命周期

| # | 功能 | 插件常见做法 | 现状 | 落点建议 | 优先级 |
|---|---|---|---|---|---|
| B1 | **部分装填 / 手动装填** | 弹匣不满时逐发/逐匣补，可打断 | ⚠️ 有 `cancelReload`，但装填总是补满 | `State.reloadProgress` 保留已装弹数；`startReload` 支持 `fillPartial` | 中 |
| B2 | **跨武器共享弹药池** | 手枪/冲锋枪共用手枪弹储备 | ✅ 已实现（AmmoPoolEntity + bindAmmoPool） | 弹药池实体 `AmmoPoolEntity` 或 `ammoPool` 引用 | 中 |
| B3 | **弹药拾取/补给** | 世界拾取补充 reserve | ❌ | 事件 + `addAmmo` API | 低 |
| B4 | **拉栓/泵动动作** | 逐发上膛（栓动/泵动霰弹） | ❌ | 阶段机把 active/recover 拆成"上膛"子阶段 | 低 |

### C. 装备 / 组合 / 切换

| # | 功能 | 插件常见做法 | 现状 | 落点建议 | 优先级 |
|---|---|---|---|---|---|
| C1 | **武器槽位 / 切换 / 收纳动作** | 多武器栏、切枪动画、holster/draw | ❌ | `Loadout` 槽位列表（引用 WeaponEntity）+ 切换事件 | 高 |
| C2 | **配件 / 改装（瞄具/枪口/弹匣/枪托）** | 配件改变 spread/recoil/zoom/弹道 | ❌ | `WeaponAttachment` 模板 + 统计修正器（复用 rpg 数值思路） | 高 |
| C3 | **皮肤 / 外观** | 材质/模型替换 | ❌ | `WeaponDefinition.skin` 引用；渲染由游戏侧 | 低 |
| C4 | **武器拾取/掉落交互** | 世界物体 ↔ 手持替换 | ❌ | 事件 + `Rig` 换装 API | 低 |

### D. 近战深度（冷兵器，ARPG/RPG 关键）

| # | 功能 | 插件常见做法 | 现状 | 落点建议 | 优先级 |
|---|---|---|---|---|---|
| D1 | **连招（combo 链）** | 多次攻击按序衔接，逐段伤害/动画 | ❌ 单次挥击 | `MeleeForm` 加 `combo` 定义（N 段）+ `comboStep` 状态 | 高 |
| D2 | **格挡 / 招架 / 弹反** | 受击时按时机抵消并反打 | ❌ | 事件 `block`/`parry`；与受击侧联动 | 中 |
| D3 | **命中确认 / 打击感** | 命中停顿、镜头震动、受击反馈 | ❌ | `fire` 事件已携带 arc，需游戏侧；可加 `hitConfirm` 标记 | 中 |
| D4 | **局部伤害部位（hitbox）** | 头/躯干/四肢倍率 | ⚠️ vehicle 有 armorZones，weapon 无 | 命中判定由游戏侧做，事件带命中部位 | 中 |

### E. 法杖 / 法术（Magic，RPG 关键）

| # | 功能 | 插件常见做法 | 现状 | 落点建议 | 优先级 |
|---|---|---|---|---|---|
| E1 | **持续伤害 / 增益 / 减益 / 控制** | DOT、buff、debuff、眩晕 | ❌ | `MagicForm` 加 `statusEffect` 载荷，事件驱动 | 高 |
| E2 | **元素 / 法术学派** | 火/冰/雷属性互克 | ❌ | `WeaponDefinition.element` + 事件字段 | 中 |
| E3 | **引导可打断 / 蓝耗恢复延迟** | 施法被攻击打断、法力回复有延迟 | ⚠️ 有 channel，无打断/回复延迟 | channel 被打断钩子 + 回复延迟字段 | 中 |
| E4 | **召唤 / 复合法术** | 生成随从/组合效果 | ❌ | 走 `IProjectileService` / 事件 + 游戏侧 | 低 |

### F. 伤害结算（跨形态）

| # | 功能 | 插件常见做法 | 现状 | 落点建议 | 优先级 |
|---|---|---|---|---|---|
| F1 | **伤害类型 / 元素 / 状态** | `damageType/element` | ✅ 伤害类型/元素已实现；状态效果（DOT/buff）待做 | 定义加字段，事件携带 | 高 |
| F2 | **距离衰减（falloff）** | 近满伤、远减伤 | ❌ | `range` + falloff 曲线，或游戏侧 | 中 |
| F3 | **暴击 / 命中倍率** | crit chance / headshot | ❌ | 事件携带命中部位/倍率，游戏侧结算 | 中 |
| F4 | **击退 / 硬直 / 击飞** | knockback/stagger | ❌ | 事件载荷，游戏侧（physics） | 中 |
| F5 | **穿透层数 / 墙体** | 穿透 n 个物体 | ⚠️ 有 `penetration` 标量 | 游戏侧用事件做穿透 | 中 |

### G. 瞄准与镜头（表现层）

| # | 功能 | 插件常见做法 | 现状 | 落点建议 | 优先级 |
|---|---|---|---|---|---|
| G1 | **ADS / 开镜缩放 / FOV 冲击** | 机瞄放大 + 视野收缩 | ✅ 已实现（zoomFov + setAiming，aim_in/aim_out 事件） | 事件 + 游戏侧相机；`WeaponDefinition.zoom` | 中 |
| G2 | **相机抖动 / 后坐镜头** | shoot kick + recover | ❌ | 事件驱动游戏侧相机 | 中 |
| G3 | **枪口火光 / 抛壳 / 曳光** | 发射特效 | ⚠️ 有 `effects.muzzle/sound`，其余游戏侧 | 事件已含坐标，补 tracer 参数 | 低 |
| G4 | **枪械摆动 / 呼吸（sway）** | 闲置晃动 | ❌ | 渲染层（游戏侧） | 低 |

### H. 瞄准 / 制导 / 锁定

| # | 功能 | 插件常见做法 | 现状 | 落点建议 | 优先级 |
|---|---|---|---|---|---|
| H1 | **锁定 / 制导导弹（已设计）** | lock-on + homing | ⚠️ `MissileForm` 已出数据模型，未实现 | 按 Phase 2 实现 | 高 |
| H2 | **预判提前量（predictive lead）** | 朝目标运动方向前导 | ❌ | `AttackRequest.targetHandle` + 游戏侧 | 中 |
| H3 | **自动瞄准 / 辅助** | aim assist | ❌ | 游戏侧 | 低 |

### I. 系统性

| # | 功能 | 插件常见做法 | 现状 | 落点建议 | 优先级 |
|---|---|---|---|---|---|
| I1 | **网络同步 / 多玩家** | 射击/装填/命中复制 | ❌ | 事件 + 组件状态可序列化，留给游戏侧 | 中 |
| I2 | **存档 / 恢复武器状态** | 弹药/配件/皮肤持久化 | ⚠️ 组件可序列化，无现成存档 | 复用 rpg/inventory 存档 | 中 |
| I3 | **对象池（弹丸/特效）** | 减少 GC/卡顿 | ❌ 游戏侧负责 | `IProjectileService` 实现方做池化 | 低 |
| I4 | **武器编辑器 / 工具** | Inspector 可视化调参 | ⚠️ 只有 JSON | 可后续做 `editor` 面板，数据驱动已具备 | 低 |

---

## 三、推荐落地顺序（贴合 RPG / RTS / ARPG 三种形态）

1. **P0 — 数据模型补全（低成本、覆盖广）**：A3 多弹丸、A7 射击模式切换、
   B2 共享弹药池、F1 伤害类型/元素 —— 都只是往 `WeaponDefinition` / 事件加字段，
   不改架构，立即让 JSON 表达能力对齐插件。
2. **P0 — 手感三件套**：A1 散布 bloom、A2 后坐力模型、G1 ADS/zoom ——
   事件驱动游戏侧表现，weapon 只负责产出数据。
3. **P1 — 装备层**：C1 武器槽位/切换、C2 配件统计修正器（复用 rpg 数值）、
   B1 部分装填。这是"从单体武器走向角色装备系统"的分水岭。
4. **P1 — 形态深化**：D1 近战连招、E1 法杖状态效果、H1 导弹制导（完成 Phase 2）。
5. **P2 — 反馈与网**：D2/D3 格挡命中确认、F3/F4 暴击击退、I1 网络、I2 存档。

---

## 四、设计原则约束

- **不重蹈"模块内 feature flag"覆辙**：新功能优先以 **数据字段 + 事件载荷 + 可选
  `IWeaponLogic` 提供者**表达，而不是 `#ifdef`。
- **weapon 不依赖渲染/相机/physics**：后坐、瞄准、特效全部走事件，游戏侧消费。
- **可裁剪**：新增形态（grenade、beam）继续用 `WeaponKind` + 注册的 logic 提供者，
  不新增物理子模块。
- **热武器回归基线不变**：改的都是新增字段/可选路径，ranged 成熟管线保持不变。