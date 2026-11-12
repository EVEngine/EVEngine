# 首次验证记录：2026-11-11

本次验证评测器的正确性和 CLI 连接情况，**没有形成有效的模型生产力排名**。

| 验证 | 结果 | 可得出的结论 |
| --- | --- | --- |
| 合同任务 + noop 控制 | fail | 未修复样例不会通过 |
| 合同任务 + reference 控制 | pass | 正确修复可通过独立验收 |
| Codex CLI 0.159.2 | blocked | 可写临时运行目录可用；provider 连接遭代理 HTTP CONNECT 403 拒绝 |
| DeepSeek CLI 模板 | adapter_error | 当前环境未安装 deepseek 可执行文件，尚未确定具体 CLI 实现 |
| 四项真实引擎任务 | 未实测 | 当前环境未发现可运行的 eve 二进制和图形运行依赖；不能以模拟结果代替 |
| 评测器回归测试 | 26 项通过 | 控制校准、越界修改、错误识别、成本缺失、超时清理、配对比较等检查通过 |
| Ruff | 通过 | benchmark Python 源码检查通过 |
| make check | 通过 | source-quality gate passed，包含新增的 check/agent-benchmark |

初次检查缺少 Ruff、clang-format、CMake/CTest/Ninja 等工具。它们安装在 `/tmp` 的用户目录，
配置 PATH/PYTHONPATH 后完成仓库 source-quality 检查；没有更改仓库的工具链版本或系统设置。
完整引擎启动和四项玩法验收仍需要在具备引擎二进制的环境完成正反控制校准。

可复现命令见 [README](README.md)。本次原始运行目录为 `/tmp/eve-agent-final-{noop,reference,codex,deepseek}`，
仓库 source-quality 日志为 `/tmp/eve-agent-make-check-final.log`。临时目录会随执行环境回收；此记录仅保存
验证结论，不把环境阻塞写成模型失败率，也不把参考修复控制当作模型成绩。

## 多 Agent 协作基准验证（同日）

新增的 [TEAM.md](TEAM.md) 基准已完成无需模型的流程校准：

- 三个场景的 reference 控制均通过，noop 控制均失败。
- shared-catalog 触发真实 Git 冲突；正确集成可通过，不解决冲突会失败。
- dependency-chain 可以 Git 干净合并，但旧端口导致独立语义验收失败。
- 硬链接工作文件、跨工作区污染、缺失归属和合并时丢失其他 Worker 变更均被拒绝。
- 串行/并行控制分别观察到单调用和重叠调用；未把微型脚本控制的耗时当作模型加速比。
- `make check/agent-benchmark` 共 42 项通过，Ruff 和仓库 `make check` 通过。

多 Agent 控制使用真实子进程与本地 Git 仓库，不调用付费模型。当前仍没有有效的 Codex/DeepSeek
多 Agent 生产力成绩；该轨道验证 JSON 项目内容集成，没有提供真实引擎游玩证据。
本次全量检查日志：`/tmp/eve-team-make-check.log`；42 项测试日志：`/tmp/eve-team-tests-verified.log`。
