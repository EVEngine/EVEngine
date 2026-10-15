# 属性：基础值与来源修饰

**入口：** `eve.Attributes()`，槽位 `attributes`。`newSet(subject)` 创建脚本持有的
AttributeSet。subject 是本项目主体键；对接其他域时使用其正式 ID，不将运行时数组下标
当持久身份。属性集拥有基础值和修饰，HUD 只读取最终值。

## 任务一：装备加成与卸下

初始化阶段执行，`config.modules` 含 `attributes`；所有脚本参数显式传入。

```squirrel
local stats = attributes.newSet("tutorial:hero");
stats.setBase("attack", 100.0);
assert(stats.addModifier("sword.flat", "attack", "equipment:sword", "add", 10.0, 0));
assert(stats.addModifier("skill.buff", "attack", "ability:rage", "multiply", 1.5, 10));
assert(stats.getFinal("attack", 0.0) == 165.0);
assert(stats.removeModifier("skill.buff"));
assert(stats.getFinal("attack", 0.0) == 110.0);
assert(stats.removeBySource("equipment:sword", "attack") == 1);
assert(stats.getFinal("attack", 0.0) == 100.0);
```

操作按 priority 然后 sequence 排序，因此 `(100+10)*1.5` 与 `100*1.5+10` 不同。
修饰 ID 表示具体贡献，source 表示装置/效果来源；卸下装备按来源移除，不能用反向减法
猜测当前最终值。`getBase/getFinal` 的 fallback 必须显式传入；空属性不等于数值 0。

## 任务二：上限、下限与脚本定义错误

```squirrel
stats.setBase("attack", 100.0);
assert(stats.addModifier("attack.cap", "attack", "difficulty", "clamp_max", 90.0, 20));
assert(stats.getFinal("attack", 0.0) == 90.0);
assert(!stats.addModifier("invalid", "attack", "test", "divide", 2.0, 0));
assert(stats.getFinal("attack", 0.0) == 90.0);
```

内置 operation 包括 add、additive_percent、multiplicative_percent、multiply、override、
clamp_min（保底）和 clamp_max（封顶）；历史别名 min 对应 clamp_min，max 对应 clamp_max，
不要把别名理解成数学 min/max 函数。
自定义策略必须先由提供者注册，不能随意拼一个 operation 字符串。修改基础值用于权威
数值变化，添加修饰用于可撤销贡献；技能结束应移除修饰，不把加成永久烘入基础值。

## 生命周期、恢复与组合

AttributeSet 创建后保存到主体拥有的状态，不每帧创建。当前脚本 add/remove 返回 bool，
removeBySource 返回数量；C++ remove API 的 Result 不能套到这个脚本面。
getModifierId/Attribute/Source/Operation/Priority/Sequence 可供 Inspector 解释贡献。
读取时检查下标范围；先生成新的投影再替换 HUD，不在遍历修饰时删改同一集合。

RPG/Combat 已拥有属性时，用它们的接口取得或同步属性，不再另外创建一套 HP/attack。
存档保存权威基础数据；装备等可重建贡献在恢复后按域契约重建，避免双重叠加。
验证排序、缺失属性、非法 operation、重复 ID 和按来源移除；浮点计算使用游戏指定容差。

证据：[Attributes.cpp](../../../src/modules/attributes/Attributes.cpp)、
[attributes.cpp](../../../test/unit/attributes/attributes.cpp)、
[RPG 属性统一测试](../../../test/integration/gameplay-composition/attributes_rpg_unification.cpp)。

## 补充绑定参考

参数与接收者见 [源码生成参考](attributes/api-reference.md)，使用前先确认本章的前置条件与生命周期。
