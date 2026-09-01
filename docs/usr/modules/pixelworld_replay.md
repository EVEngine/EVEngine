# PixelWorld 确定性重放

`pixelworld_replay` 是可裁剪的 L1 重放适配模块。它依赖 `pixelworld`，保存按
simulation tick 排序的 canonical edit command，并周期性记录世界与各 Chunk 的
位精确摘要；它不复制或拥有可变地形状态。

`PixelReplayLog::append()` 要求 edit sequence 严格连续、tick 不倒退。
`captureCheckpoint()` 从同步借用的世界生成 checkpoint，不保留世界引用。
`replay()` 只接受空目标世界，并在每个目标 tick 推进前提交对应命令。返回的
`PixelReplayReceipt` 区分完整匹配与数据分歧；发生分歧时包含首个错误 tick、revision
以及首个不一致 Chunk 的摘要，便于 replay、网络校正和存档诊断。

所有 API 均为 simulation-thread affine，不执行回调。日志是输入和诊断投影，不是
第二份 world authority。当前摘要用于确定性一致性验证而非密码学认证。

`snapshot(instanceId, hashProvider)` 生成公共 `SnapshotEnvelope`，type 为
`pixelworld.replay-log`，schema 为 `pixelworld:replay-log`，当前 version 2。内容
hash 必须由宿主注入，模块不默认选择弱 hash。`restoreSnapshot()` 在解析前
验证 envelope，使用有界 candidate 解码后一次性替换；坏 hash、截断、未知字段、
非 canonical Chunk 顺序和未知新版本都不会部分修改旧日志。version 1 可迁移到
version 2，其 checkpoint 保留 world digest，但没有当时尚未编码的每 Chunk 诊断。

源码：[`src/modules/pixelworld_replay/`](../../../src/modules/pixelworld_replay/)。
测试：[`test/pixelworld_replay.cpp`](../../../test/pixelworld_replay.cpp)。
