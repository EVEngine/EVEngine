# Combat

Combat 模块提供不依赖具体角色类的伤害、韧性、战斗属性、标准 Ability 原型与移动适配。游戏仍负责角色
实体、动画和输入映射；模块不会创建通用 Actor 根类。

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
