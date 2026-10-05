# Combat

Combat 模块提供不依赖具体角色类的伤害、韧性、战斗属性、标准 Ability 原型与移动适配。游戏仍负责角色
实体、动画和输入映射；模块不会创建通用 Actor 根类。

脚本入口 `eve.Combat()` 提供 `getName`，并可通过 `newRuntime()` 创建一个 Squirrel-owned
`CombatRuntime`。`registerFighter`、
`setMoveIntent`、`navigateTo`、`stop`、`advance`、`applyDamage`、`applyTimelineDamage` 和 `state`
全部返回统一 Result 投影；`applyTimelineDamage` 直接消费 Montage runtime event 的 payload JSON，并复用
`ActionDamageBinding` 校验，不会在示例脚本中复制伤害字段解析规则。
runtime 内部复用下述 C++ 权威实现，不在脚本层复制生命值或移动状态。runtime 随脚本对象 GC 销毁，
`ownership()` 返回 `owned`。

同一个 runtime 还内建并注册 `standardCombatAbilities()` 的 20 个定义。`grantAbility`、
`activateAbility`、`advanceAbilities`、`cancelAbility`、`abilityGrant` 和 `matchingAbilities` 直接投影
`AbilityRuntime`；冷却与 Action phase 只使用调用者注入的 tick/delta，不读取墙钟。`advanceAbilities`
推进所有活跃 execution、递减冷却并同步终态链接，脚本不应另外保存 active/cooldown 状态。
`registerTimelineAbility` 接收 `ActionTimelineEditor.snapshotJson()` 的规范 schema，将 timeline 的 actionId、
总时长和 physical split 映射为同一个 `ActionDefinition` 的 phase timing；同时显式设置 cooldown、
instancing、activation group 和可选 gameplay-event trigger。注册失败不会留下部分 definition。
`replaceTimelineAbility` 使用相同参数事务式热替换已有定义：grant、per-owner instance 与剩余冷却保持不变；
已激活 execution 继续持有提交时的旧快照，下一次激活才读取新的 timeline 与 actionId。
已有 grant 时不能热切换 instancing policy，避免悄悄改变 instance 身份语义。

## 角色移动与导航

`CombatLocomotionRuntime` 以 `SubjectRef` 注册角色，并唯一拥有水平位置、速度、朝向、移动意图和导航目标：

```cpp
DirectCombatNavigationProvider navigation;
CombatLocomotionRuntime locomotion(navigation);
locomotion.registerSubject({fighter, "fighter:player", {0.0, 0.0}, 5.0, 18.0});
locomotion.setMoveIntent(fighter, {1.0, 0.0}, 1.0);
locomotion.advance({tick, fixedDelta});
```

方向不要求预先归一化，速度比例必须位于 `[0,1]`。玩家或 AI 的直接移动会清除该角色旧导航目标；
`navigateTo` 则要求安装 `ICombatNavigationProvider`。内置 `DirectCombatNavigationProvider` 适合无障碍 arena。
启用 2D/3D profile 时，可选 `combat_navigation` 卫星提供 `PathfinderCombatNavigationProvider`：

```cpp
map::Pathfinder pathfinder(width, height);
auto navigation = combat::navigation::PathfinderCombatNavigationProvider::create(
    pathfinder, {{worldOriginX, worldOriginZ}, cellSize});
CombatLocomotionRuntime locomotion(*navigation.value());
```

它把世界 X/Z 投影到地图格，每步调用 canonical `Pathfinder` 取得下一 waypoint；不会缓存 Path，因此地图阻挡、
TileLayer revision 或代价改变会在下一步重新规划。不可达目标返回 `NotFound`，并由 locomotion 的整帧事务语义保证
所有角色都不发生部分移动。`Pathfinder` 必须比 provider 活得更久。

整帧更新具有原子语义：运行时先复制全部状态，再依次取得导航 steering、限制加速度并积分，全部成功后才
发布。Provider 失败、返回非有限值或非法比例时，没有任何角色移动。Provider 是借用对象，必须比运行时活得
更久，或者在销毁前调用 `clearNavigationProvider`；若清除时仍有活动目标，后续 `advance` 会明确失败，目标不会
被悄悄丢弃。到达目标会返回 owning `CombatLocomotionEventKind::Arrived` 事件。

## 动作战斗基础（3A 竖切积木）

下列运行时与 Action Timeline 窗口一起构成动作战斗竖切基础，均不引入 CombatActor 大根类：

| 运行时 / 窗口 | 职责 |
|---|---|
| `MeleeHitRuntime` | Hitbox 目录 + Hurtbox + 扫掠命中；可选部位倍率后提交 `DamageRuntime` |
| `CombatActionWindowState` | Montage hitbox/invuln 窗口；可借用 `MeleeHitRuntime` 自动 arm/disarm |
| `CombatCharacterRuntime` | 3D 跑/跳/闪避 i-frame / 攻击 Root Motion / 硬直 / 死亡；可选地面与胶囊探针 |
| `CombatCharacterPoseSource` | 把角色位置/朝向变成 melee pose（局部 +Z 为面朝方向） |
| `CombatMotionWarp` | 攻击中向 lock-on 目标做有预算的水平校正 |
| `CombatCameraFraming` | 锁定镜头数学（eye/lookAt）；camera 模块只负责 apply |
| `CombatLoopRuntime` | 一帧顺序焊接 warp → character → hurtbox → melee → guard/feel → camera |
| `HitFeelRuntime` | 命中停顿与受击硬直时长（注入仿真时间） |
| `GuardWindowState` | `combat:guard-window`（`block`/`parry`）并改写伤害请求 |
| `ActionCancelWindowState` + `ActionInputBuffer` | `input:cancel-window` 允许列表与确定性输入缓冲 |
| `ComboGraph` | 显式 Ability 转移图（与 cancel/combo 窗口联用） |
| `CombatTargetRuntime` | Soft/hard lock-on 与切换 |
| `CombatEnemyIntentSource` | 近距环敌人；可选 Telegraph / Recover 惩罚窗 |
| `BodyPartDamageRule` | 按部位倍率的 `IDamageRule` |

`CombatCharacterRuntime` 默认落在平面 `y = 0`。游戏可以借用 `ICombatGroundProvider`
（高度采样）和 `ICombatMoveProbe`（胶囊推进），缺省时行为与第一版平面控制器相同。
`HitFeelRuntime` 的 hitstop 通过 `setTimeFrozen` 暂停角色积分，时长权威仍在 feel。

`CombatLoopRuntime` 只排序借用对象，不引入 CombatActor。Motion warp 仅在 `Attacking`
且存在 lock-on 时生效；超出 per-tick 预算的 warp 被跳过而不是静默吸附。
`CombatCameraFraming` 属于 combat（L2）；`CameraController` 的 `lockon` 模式只消费
世界坐标（`setTarget` + `setSecondaryTarget`），不依赖 combat 头文件。

Timeline 新增内建窗口：`input:cancel-window`（payload `allows`，可选 `priority`）与
`combat:guard-window`（payload `mode=block|parry`）。近战武器 `MeleeLogic::fire` 会推送与远程一致的
`WeaponEventType::Fire` 事件，几何命中仍由 `MeleeHitRuntime` 拥有。

可玩组合烟测见 `examples/combat-arena`；更细的几何/取消/锁定/循环契约见 `test/combat_*.cpp`。

## 组合边界

移动状态应由表现层投射到 Scene/Physics，由 Ability Active 阶段决定冲刺、闪避等动作何时写入移动意图。
Ability 生命周期仍由 `AbilityRuntime`/`ActionRuntime` 唯一拥有，伤害仍通过 `DamageRuntime` 或
`CombatActionDamageSink` 提交；不要在角色 Controller 中复制冷却、动作阶段或生命值。
