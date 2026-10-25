# 条件树：Compare 与 PolicyCall

**C++ 入口：** `eve::condition::Condition`（实现位于 `src/engine/common/`，**不是**独立
引擎模块；无脚本槽位）。可解释、无副作用的条件 AST，供 action / emergence /
dialogue / card / rpg 等域复用。

节点种类只保留 `All` / `Any` / `Not` / `Compare` / `PolicyCall`。
tag / attribute / resource / state / authority 不是独立叶子，而是
[`ConditionKeys.h`](../../../src/engine/common/ConditionKeys.h) 里的 key 前缀与
`authority.allow` 策略约定；作者面旧叶子（`has_tag` 等）由统一 codec 展开为上述节点。

求值上下文是 `EvaluationContext`：`value(key)` 读键控值，`policy(name, args)` 调命名策略。
结果是带 `ConditionReasonCode` 的 `ConditionResult`，失败可解释且不改写世界状态。

源码：[Condition.h](../../../src/engine/common/Condition.h)，语义测试：
[condition_tests.cpp](../../../test/unit/common/common_tests.cpp)。

黑板 / FSM / utility / 影响图等决策工作区见 [决策](decision.md)（由 `npc_ai` 托管的
`eve.Decision()`）。
