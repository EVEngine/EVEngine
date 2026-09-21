# RPG 示例 —— 地牢生存

用 `eve.RPG()` 模块搭的一个最小可玩 demo：玩家 vs 一波波强度递增的敌人，
用来演示属性 / 效果 / 状态 / 技能 / 结算五套系统怎么协同工作。完整设计说明见
[RPG 系统设计](../../docs/dev/RPG系统设计.md)。

## 运行

```sh
make run/macosx-debug GAME=examples/rpg
# 或对应平台： run/linux-debug / run/win32-debug
```

（等价于 `cd examples/rpg && ../../build/<platform>-debug/src/engine/eve run .`）

## 玩法

- `1` 普通攻击（无消耗，短冷却）
- `2` 火球术（消耗 15 MP，读条 0.6s，命中后附加「灼烧」DOT，可叠加到 3 层）
- `3` 力量姿态（消耗 10 耐力，自身增益，提升攻击力）
- `4` 治疗药水（瞬间回血）
- `R` 死亡后重开

敌人会自动用「利爪」普通攻击和「怒吼」（削弱玩家防御）反击，一波波变强，
撑得越久分数越高。

## 数据文件

- `data/effects.json`：效果模板（治疗 / 力量姿态 / 衰弱 / 灼烧 / 被动回复）。
- `data/skills.json`：技能定义（消耗、冷却、读条、附加效果）。

改数值不用碰脚本：保存 JSON 后热重载即生效。

## 演示了什么

| 系统 | 用到的点 |
|------|----------|
| 属性 | `setBaseAttribute` / `getFinalAttribute` / `addAttributeModifier` 用永久 `clampMin`/`clampMax` modifier 把血条钳在 `[0, max]` |
| 效果 | JSON 注册 4 种策略：instant（治疗药水）、duration+refresh（力量姿态/衰弱）、duration+period+stack（灼烧最多叠 3 层）|
| 状态 | `getStatusCount/EffectId/Stacks/Remaining` 逐帧读出来做 UI；`pollTicks` 驱动灼烧伤害与被动回复 |
| 技能 | 学习/冷却/消耗/读条时间；`canCastSkill`/`canCastSkillReason` 给出施法失败原因；`getCastProgress` 画读条进度条 |
| 结算 | 示例为教学目的把伤害公式写在脚本 `computeDamage`；正式 Battle 使用通用 Settlement，旧 named pipeline 只作兼容 |

## 已知限制 / 扩展点

- **`runSettlement` 是兼容入口**：旧 `SettlementContext` 数值袋仍可由原生插件注册回调并由脚本触发，
  其调度已委托通用 Settlement。新项目应配置 `Battle` 的 `settlement.rules`，让伤害、治疗、Status tick、
  Buff 与触发链走同一正式路径；不要用兼容数值袋直接提交权威状态。
- **自定义施法条件同理**：`SkillSystem::registerCastCondition` 也是 C++-only。

## 踩过的坑（写给以后改这个模块/写新脚本绑定的人）

`RPGActor` 的属性接口在 C++ 侧用的是 `double`（`AttributeSystem` 内部全用
`double` 计算）。**直接把 `double` 参数/返回值的方法用 `addFunc(&Class::method)`
绑定到 Squirrel 会在运行时炸掉**——报错形如：

```
Type error bad cast expected: INSTANCE got: FLOAT
```

原因：`simplesquirrel` 只有在 squirrel 编译时定义了 `SQUSEDOUBLE`（`SQFloat`
为 64 位）才认识 C++ `double`；本项目的 squirrel 没开这个宏（`SQFloat` 是
32 位 `float`），于是 `double` 特化版本的 `popValue`/`pushValue` 根本不存在，
落到通用模板——那个模板是给 `INSTANCE`/`USERDATA` 类型准备的，于是把一个
FLOAT 值硬解释成"应该是个对象"，直接抛异常。同样的坑 `mouse/Mouse.cpp`、
`timer/Timer.h` 里也写了注释提醒过。

`src/modules/rpg/RPG.cpp` 的修法：在 `expose(ssq::Table&)` 里不直接
`addFunc("setBaseAttribute", &RPGActor::setBaseAttribute)`，而是包一层 lambda，
脚本侧一律用 `float`，进 C++ 之前再转 `double`：

```cpp
actor.addFunc("setBaseAttribute", [](RPGActor *a, const std::string &attribute, float value) {
    if (a) a->setBaseAttribute(attribute, double(value));
});
```

**结论：任何要暴露给 Squirrel 的新方法，参数/返回值只要涉及浮点数，一律用
`float`（或者像这里一样包一层转换 lambda），不要指望 `double` 能直接绑定。**
