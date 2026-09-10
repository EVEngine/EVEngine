# Agent Tensor

**脚本入口：** `eve.AgentTensor()`；默认槽位 `eve.agentTensor`。

这是 `agent/tensor` 下的可裁剪 provider，依赖 agent、tensor 和 gpgpu。创建后同时注册 eager CPU 与 GPU 服务。GPU 在设备 owner 线程延迟编译 tensor 图，支持推理和显式 SGD 反向传播。模块销毁前撤销注册并释放缓存图，不保留环境引用；必须先于 Graphics 设备销毁或重建。

`backend="tensor"` 标签为 tensor-cpu / cpu-sgd；`backend="gpu"` 标签为 tensor-gpu / tensor-gpu-sgd。没有隐式回退；设备不可用或 shader 不支持时返回错误。详见 [Agent](agent.md)。

## API 快查

- `getName()`：返回 AgentTensor。
