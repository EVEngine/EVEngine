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
`navigateTo` 则要求安装 `ICombatNavigationProvider`。内置 `DirectCombatNavigationProvider` 适合无障碍 arena，
有地图阻挡的项目应实现同一接口并返回当前步 steering。

整帧更新具有原子语义：运行时先复制全部状态，再依次取得导航 steering、限制加速度并积分，全部成功后才
发布。Provider 失败、返回非有限值或非法比例时，没有任何角色移动。Provider 是借用对象，必须比运行时活得
更久，或者在销毁前调用 `clearNavigationProvider`；若清除时仍有活动目标，后续 `advance` 会明确失败，目标不会
被悄悄丢弃。到达目标会返回 owning `CombatLocomotionEventKind::Arrived` 事件。

## 组合边界

移动状态应由表现层投射到 Scene/Physics，由 Ability Active 阶段决定冲刺、闪避等动作何时写入移动意图。
Ability 生命周期仍由 `AbilityRuntime`/`ActionRuntime` 唯一拥有，伤害仍通过 `DamageRuntime` 或
`CombatActionDamageSink` 提交；不要在角色 Controller 中复制冷却、动作阶段或生命值。
