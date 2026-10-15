# 决策：黑板、状态机和空间权重

**入口：** `eve.Decision()`，槽位 `decision`。`newContext()` 返回 Result，其 value 是
owned proxy；context 的权威状态在模块 handle pool，脚本代理不保留临时裸指针。
本模块不自动读取输入、模拟时间或玩家坐标，由游戏提供事实。

## 任务一：巡逻状态机

放在初始化或关卡加载阶段；项目需 `decision`。

```squirrel
function decision_value(result) {
    if (!result.ok) throw result.status.summary;
    return result.value;
}
local brain = decision_value(decision.newContext());
decision_value(brain.setState("guard", "idle"));
decision_value(brain.addTransition("guard", "idle", "enemy_seen", "alert"));
decision_value(brain.set("facts", "enemy_distance", "12.5")); // JSON scalar 文本
assert(brain.get("facts", "enemy_distance", "null") == "12.5");
local fired = decision_value(brain.trigger("guard", "enemy_seen"));
assert(fired && brain.state("guard") == "alert");
```

黑板值是 JSON scalar，不是任意脚本表；对象或数组输入会被拒绝。board、key 与 machine
名称属于明确领域，不在每帧热路径无限产生字符串键。触发返回 Result<bool>：`ok` 表示
操作有效，`value` 才表示有没有实际转移；NoOp 不是失败。

## 任务二：保存并恢复 AI 进度

```squirrel
local saved = brain.snapshotJson();
decision_value(brain.setState("guard", "idle"));
decision_value(brain.restoreJson(saved));
assert(brain.state("guard") == "alert");
decision_value(brain.release());
assert(brain.isStale());
```

恢复必须检查结果，坏快照不能把半份黑板暴露给运行中的 AI。release 后不再更新代理，
模块先销毁时代理也可能 stale；ownerEpoch/handle 是诊断，不是可跨进程复用的对象身份。
重新加载关卡时重建 context 与所属对象的关系。

## 选择空间目标

`newGrid(name,width,height,cellSize,originX,originY)` 创建权重网格；setCell/addCell 修改，
sample 按世界坐标读取并提供 fallback。游戏决定何时更新权重，不能假定这自动完成
导航或避障。utility/choose 接收本模块定义的 JSON 格式；复杂 AI 组织优先参考
[NpcAi](npc_ai.md)，不要把黑板、动作执行与路径服务塞进一个万能 context。

源码：[Decision.cpp](../../../src/modules/decision/Decision.cpp)，语义测试：
[decision.cpp](../../../test/unit/decision/decision.cpp)。验证合法/非法 scalar、无转移、非法网格、
坏快照和失效代理，确定性由输入事实及游戏的模拟 tick 顺序保证。

## 补充绑定参考

参数与接收者见 [源码生成参考](decision/api-reference.md)，使用前先确认本章的前置条件与生命周期。
