# Agent 示例

`train.nut` 展示 Squirrel 中定义环境、训练策略、独立选择动作和回放。可在提供 eve.Agent 的引擎脚本中载入；专用测试直接在真实 Squirrel VM 中执行。

`Environments.h` 和 `main.cpp` 展示 C++ 网格游戏与生产 UI 布局算法。测试指标是可选目标，通用 agent 默认只优化奖励。

构建与 tensor 用法见 [Agent 手册](../../docs/usr/modules/agent.md)。
