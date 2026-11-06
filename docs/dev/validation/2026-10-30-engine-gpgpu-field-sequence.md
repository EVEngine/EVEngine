# 引擎 GPGPU 三阶段场组合验证

本记录把 GPU 场管线第一次放入 EVEngine 测试进程，而不是独立 Vulkan 探针。
新增的三个计算 shader 位于 `src/modules/physics/field/gpu/`，使用既有
`gpgpu::ComputeShader`、`GpuBuffer` 和 `Sequence`，不改 Graphics/Gpgpu
公共接口或默认模块行为。

Windows MSVC Debug/SHARED 严格警告构建中，
`make test/win32-debug FILTER=physics.field.gpu` 通过一项真实窗口与
Vulkan 设备的集成用例。用例运行四帧，每帧按以下顺序在一个命令序列中：

1. 设备端移动 1024 个独立源并归约到八簇质量及一阶矩；
2. 设备端合并分块矩量；
3. 设备端按八个摘要更新 1024 个目标，再只回读准入字。

源与目标均使用输入/候选缓冲。测试对最终源位置、目标位置和 CPU binary64
逐源四帧积分参考做比较。随后把一个目标移到近场，确认该帧拒绝、
候选源已写入，而已发布源和目标缓冲保持不变。

这证明现有引擎 GPGPU 能承载该受限三阶段命令序列，包含缓冲重绑定、
跨 dispatch 可见性和结果回读；还没有正式 GPU 模拟 owner、stamp/revision、
一般自适应层次、独立源动力学或设备丢失恢复。八簇半径固定为 0.04，
超过误差界的输入必须拒绝。用例没有量测百万对象整帧。

下一步应把这个序列封装在可选的、单一状态 owner 中，严格规定 source/target
版本、provider 生存期、失败回滚与 GPU/CPU 同核误差，然后扩展为自适应层次。
不能把测试夹具中手动交换缓冲的代码作为生产所有权实现。

最终 `make check` 通过（533 项脚本测试、5 项跳过），日志为
`build/engine-gpgpu-field-sequence-check.log`；`git diff --check` 通过。
