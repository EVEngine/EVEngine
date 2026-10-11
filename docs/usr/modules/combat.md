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

## 组合边界

移动状态应由表现层投射到 Scene/Physics，由 Ability Active 阶段决定冲刺、闪避等动作何时写入移动意图。
Ability 生命周期仍由 `AbilityRuntime`/`ActionRuntime` 唯一拥有，伤害仍通过 `DamageRuntime` 或
`CombatActionDamageSink` 提交；不要在角色 Controller 中复制冷却、动作阶段或生命值。
