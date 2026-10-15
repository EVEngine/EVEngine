# RTS：世界、阵营、命令与固定步进

原生游戏的卡牌消耗、生产队列与经济联动见
[玩法支付组合](../GAMEPLAY_PAYMENT_COMPOSITION.md)。这些组合入口使用共享事务协议。

**入口：** `eve.RTS()`，槽位 `rts`。RTS facade 组合领域服务并拥有 RTS ECS roots；
经济、群体运动、动作、战斗与生产仍各自拥有事实。渲染与 HUD 消费 inspectState，
不能另写一套位置或资源来绕过 RTS 命令。

## 任务一：建立最小阵营世界

以下可作为完整 main.nut，配置包含 `modules=["gfx","rts"]`。
它验证创建、步进和迷雾查询；完整可玩的单位/建造界面见下一节。

```squirrel
function rts_value(result) {
    if (!result.ok) throw result.status.summary;
    return result.value;
}
local simulation = null;
local faction = null;
local accumulated = 0.0;
eve_init = function() {
    simulation = eve.RTS();
    rts_value(simulation.configureScriptWorld(4, 4, 1.0, 0.0, 0.0));
    faction = rts_value(simulation.newFaction("00000000-0000-7000-8000-00000000fc01"));
    rts_value(simulation.scriptCellExplored(faction, 0, 0));
    rts_value(simulation.scriptCellVisible(faction, 0, 0));
};
eve_update = function(dt) {
    accumulated += dt;
    local steps = 0;
    while (accumulated >= 1.0 / 30.0 && steps < 8) {
        rts_value(simulation.stepScript(1.0 / 30.0));
        accumulated -= 1.0 / 30.0;
        ++steps;
    }
};
eve_render = function() { gfx.clear(); };
```

此处补步上限为每帧 8 次，保留积压时间；持续过载时要明确游戏自己的暂停/追赶策略。
不能为了追赶无限推进一帧，也不能声称不同 dt 下可变步进天然一致。
RTS 的持久主体 ID 是 UUID；例子的值用于独立演示，项目要给每个主体不同 ID。

## 任务二：生产单位并命令移动

运行 `eve run examples/rts-sandbox`。其 [config.nut](../../../examples/rts-sandbox/config.nut)
列出组合模块；[main.nut](../../../examples/rts-sandbox/main.nut) 的 resetGame 依次：

1. loadScriptContent 读取 [content.json](../../../examples/rts-sandbox/data/content.json)，检查 Result。
2. configureScriptWorld 创建网格，newFaction 建立阵营，addScriptResource 发放初始资源。
3. 配置导航障碍/成本，newBuilding/newResourceNode/newUnit 使用唯一主体 UUID 与
   `building:<id>`、`unit:<id>` 的内容逻辑 ID。
4. 玩家操作选择单位并调用普通命令；stepScript 推进真实生产和单位运动。
5. inspectState 生成 HUD/渲染投影，坐标通过游戏自己的 world→screen 映射。

不要直接在 inspectState 返回表上改单位位置来发命令，它是观察结果。生产队列扣款、
完成、取消与失败按 RTS/Production 域接口处理；不能又手动从 Economy 扣同一笔资源。
测试 [rts_script.cpp](../../../test/integration/gameplay-composition/gameplay_composition_graphics_rts_script_tests.cpp) 覆盖支付后产出和帧回调身份，
[rts_crowd_production.cpp](../../../test/integration/rts-unit-lifecycle/rts_unit_lifecycle_tactics_rts_tests.cpp) 验证组合。

## 重载、保存与失败

RTS facade 拥有当前 generation 的实体 roots。rts-sandbox 重载时显式重建 simulation，
不把 `persist sim` 当作所有原生实体可跨 generation 存活的保证；选择集合、UI 等投影
也需要按新主体重新连接。需要保留比赛时走该域快照/恢复契约，并验证 UUID 链接；
通用引擎调试 Snapshot 不等于玩家存档。

newFaction/newUnit 等返回 Result，既要检查 ok，也要只在成功后保存 value。内容缺失、
重复 UUID、失效阵营、无路可达、资源不足应显示领域拒绝原因。检查失败操作前后权威
状态不变，按同样命令序列测试冷启动、重载、恢复和完成生产。
源码：[RTS.cpp](../../../src/modules/rts/RTS.cpp)，完整运行示例与断言是上面的 sandbox
和 [rts_script_2.cpp](../../../test/rts_script_2.cpp)。

## 补充绑定参考

参数与接收者见 [源码生成参考](rts/api-reference.md)，使用前先确认本章的前置条件与生命周期。

## 脚本事务组合

`sim.stageAbility(batch, casterId, ability, targetId, x, y)`、`sim.stageProduction(batch, producerId, newUnitId, definitionId)` 和 `sim.stagePayment(batch, factionId, costJson)` 提交本域操作，不依赖 Card。

生命周期、费用合并与完整示例见 [玩法事务组合](../GAMEPLAY_PAYMENT_COMPOSITION.md#script-composition)。
