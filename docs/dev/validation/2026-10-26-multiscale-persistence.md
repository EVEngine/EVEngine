# 递归模型保存与恢复验证

日期：2026-10-26。基线 HEAD：`5f2cd482ad1f88a7e11116000823a2b139c54a61`，工作区未提交。

本切片复用 common SnapshotEnvelope/Value/JSON，不更改旧存档、旧模块默认值或已有调用。
新增 `RecursivePersistence.h/.cpp` 及 `RecursiveInstance::restoreState`。
它提供 C++ 内存快照与规范 JSON 往返；尚未接入引擎文件系统、编辑器保存或场景恢复。

## 格式与发布契约

- schema：`eve:recursive-model`，type：`recursive-model`，version：1。
- payload 只存 definition 和 edits；不存派生摘要、运行时 owner 或候选凭据。
- unsigned 64 位字段使用十进制字符串，避免 JSON 数值中转损失种子和版本精度。
- 未知字段、未知版本、缺失字段、非有限数、超限资源均拒绝。首版无前驱迁移。
- 持久身份和模拟 tick 由宿主提供；摘要 provider 缺席、失败、哈希不符均返回结构化错误。
- 恢复先验证，再重建独立 owner；宿主仅在成功后移动发布。失败不修改现有实例。
- 保存先复制权威状态，再调用无锁同步摘要 provider。摘要不是身份验证或签名。

公开 API 使用 Result、nodiscard 和 Doxygen 生命周期/线程/成本契约；复用唯一存档机制。
没有引入 ECS System、跨域 Link、后台任务或架构例外。

## 聚焦验证

Windows MSVC Debug DLL 探针：37 cases、21881 assertions，全部通过。
同源最新测试的 Windows Debug 静态链接、Linux GCC Debug 静态链接 CTest 均通过。
对全部新增模块源码直接运行 architecture-contracts lint 无新增违规；`git diff --check` 通过。
本轮已运行 `make check`，日志位于 `build/multiscale-persistence-source-check.log`。
终态：529 项脚本测试，4 failures、1 error、5 skipped，源质量未通过。
这些测试此前也因 Windows/Bash 路径转换失败；本切片没有修改或跳过它们。
覆盖极大种子、尺度指数 900、修改后重存 JSON 一致、身份/tick/版本保留、旧候选失效，
以及未知版本/字段、篡改、provider 缺席/失败、编辑和摘要预算耗尽。
额外验证内容哈希正确但容量非法的快照仍被拒绝，完整性检查不能替代语义验证。

命令使用 `benchmarks/physics_field` 的同源契约探针：

```text
cmake/with-msvc.cmd cmake --build build/field-contract --parallel 4
build/field-contract/field_contract_tests.exe
```

这不是全引擎 SHARED/OBJECT 或完整兼容验收。百万独立对象、1080p、60 FPS 整帧
16.67 ms 目标保持不变，当前 CPU 场性能未达标，渲染接入仍未完成。
