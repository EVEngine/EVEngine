# Agent 示例

`train.nut` 展示 Squirrel 中定义环境、训练策略、独立选择动作和回放。可在提供 eve.Agent 的引擎脚本中载入；专用测试直接在真实 Squirrel VM 中执行。

`Environments.h` 和 `main.cpp` 展示 C++ 网格游戏与生产 UI 布局算法。测试指标是可选目标，通用 agent 默认只优化奖励。

构建与 tensor 用法见 [Agent 手册](../../docs/usr/modules/agent.md)。

在已完成仓库依赖初始化的源码树根目录，可以独立构建并运行 C++ 演示：

```powershell
cmake/with-msvc.cmd cmake.exe -S test/agent -B build/agent-demo -G Ninja -DCMAKE_BUILD_TYPE=Debug -DEVE_AGENT_SCRIPT=OFF -DEVE_AGENT_TENSOR=OFF
cmake/with-msvc.cmd cmake.exe --build build/agent-demo --target agent_demo
build/agent-demo/agent_demo.exe
```

Linux / macOS 使用相同 CMake 参数并省略 `cmake/with-msvc.cmd`，执行
`build/agent-demo/agent_demo`。成功时输出四条 `replay=PASS`，分别覆盖网格游戏、
UI 布局与两种策略。关闭 script / tensor 选项只缩小独立演示的构建范围；
`train.nut` 的真实 VM 测试仍由默认启用这些选项的 agent 测试目标覆盖。
