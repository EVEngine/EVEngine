# Agent Tensor

**脚本入口：** `eve.AgentTensor()`；默认槽位 `agentTensor`。

这是 `agent/tensor` 下的可裁剪 provider，依赖 agent、tensor 和 gpgpu。创建后同时注册 eager CPU 与 GPU 服务。GPU 在设备 owner 线程延迟编译 tensor 图，支持推理和显式 SGD 反向传播。模块销毁前撤销注册并释放缓存图，不保留环境引用；必须先于 Graphics 设备销毁或重建。

`backend="tensor"` 标签为 tensor-cpu / cpu-sgd；`backend="gpu"` 标签为 tensor-gpu / tensor-gpu-sgd。没有隐式回退；设备不可用或 shader 不支持时返回错误。详见 [Agent](agent.md)。

## API 快查

- `getName()`：返回 AgentTensor。

## 把训练与游戏循环分开

任务一是 CPU 推理：用 Agent 的环境/观察契约创建模型与 backend，按模拟 tick 提供
观察并消费动作；环境 owner 仍推进游戏，provider 只计算。固定命名 RNG 和训练种子。
任务二是 GPU 训练：在有设备的 owner 线程准备图和资源，显式选择 gpu backend，
检查每次推理/SGD 状态；不可用就报告，不把 tensor-cpu 输出冒充 GPU 验证。

模型资源与图缓存持有时间须覆盖推理任务；退出先取消/等待任务，再撤销 provider，
最后重建或销毁 Graphics。getName 是诊断入口，训练操作走 Agent 绑定，
不是在 agentTensor 上调用未绑定的 predict/train。
