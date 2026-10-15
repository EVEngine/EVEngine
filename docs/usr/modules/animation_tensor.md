# 神经重定向提供者：animation_tensor

**脚本入口：** `eve.AnimationTensor()`；`config.modules` 使用槽位 `"animationTensor"`。

这是独立卫星模块；配置名、默认槽位和依赖来自 module_manifest，运行时裁剪不需要编辑时可移除。
先启用 manifest 声明的宿主与依赖，创建提供者，再创建目标/会话。具体脚本构造和参数见本章源码参考。

## 任务一：建立可编辑流程

AnimationTensor 创建时注册 ISmrNeuralRetarget provider，供 Animation 的 SMR 流程查询。先准备匹配模型输入的源/目标骨架与归一化特征，再通过 Animation 的重定向入口请求结果；不是调用一个虚构的 animationTensor.predict。

## 任务二：预览、提交与清理

第二个任务是可选模型部署：检查模型资源与推理状态，provider 缺失或模型无效时向调用者报告。getName 是当前唯一脚本方法；原生 SmrFeatures/SmrOnnxRunner/SmrMeshRetNet 实现特征、模型执行与映射。退出时先停止重定向任务，再撤销 provider 并释放 Tensor。

模型资源与骨架 ID 需覆盖推理任务的寿命；推理结果是候选姿态，成功后由
Animation 的 owner 发布。不要把模型文件、临时 Tensor 和角色姿态混为一个 owner。

## 核对证据

实现：[AnimationTensor.cpp](../../../src/modules/animation/tensor/AnimationTensor.cpp)。流程与边界测试：[animation_smr_neural_retarget.cpp](../../../test/integration/gameplay-composition/animation_smr_neural_retarget.cpp)。
先看测试中的 fixture/资源条件，再移植到游戏；没有脚本方法的 C++ 契约不自动可调用。

当前脚本仅有 getName；角色姿态与重定向入口见 [Animation](animation.md)。
