# Agent

**脚本入口：** `eve.Agent()`，默认槽位 `eve.agent`。C++ 命名空间 `eve::agent`，库 `EVAgent`。

Agent 面向游戏自动操作、决策策略学习、仿真优化与自动测试。默认优化领域返回的累计奖励；覆盖率和失败奖励默认都是零，测试场景自行开启。

算法为进化 rollout 搜索加策略蒸馏：种群采样、精英选择、动作变异，以及两层 tanh MLP 的 masked-softmax 交叉熵训练。这不是严格的多温度 MCMC。保留均匀随机策略用于基线比较。

## 从脚本创建 agent

实现 `reset(seed)` 与 `step(action, dt)` 即可接入领域。回调返回观察表；执行错误可抛出脚本异常，绑定转换为 CallbackFailure Result。

```squirrel
local brain = eve.Agent();
local environment = {
    x = 0,
    observe = function(reward) {
        return {features=[x / 3.0], legalActions=[0,1], reward=reward,
                outcome=x == 3 ? "success" : "running"};
    },
    reset = function(seed) { x = 0; return observe(0.0); },
    step = function(action, dt) {
        local before = x;
        x += action == 0 ? 1 : -1;
        if (x < 0) x = 0;
        if (x > 3) x = 3;
        return observe((x - before).tofloat());
    }
};
local trained = brain.run({featureCount=1, actionCount=2,
    population=8, elites=2, generations=4, horizon=8}, environment);
if (!trained.ok) throw trained.code;
local chosen = brain.act(trained.value.policy, environment.reset("42"));
if (!chosen.ok) throw chosen.code;
local next = environment.step(chosen.value, 1.0 / 60.0);
```

完整脚本见 [train.nut](../../../examples/agent/train.nut)。`act` 只选择合法动作，不直接改变游戏；领域是状态和操作的唯一权威。

## 观察与配置

观察表字段：

- `features`：有限数值数组，长度等于 featureCount，建议归一化到 `[-1,1]`。
- `legalActions`：无重复整数动作 ID，范围 `[0,actionCount)`，Running 时必须非空。
- `reward`：本步增量奖励，默认零；初始观察奖励必须为零。
- `outcome`：`running`、`success`、`failure`，默认 running。
- `coverage`：可选语义覆盖标签数组。
- `finding`：稳定失败编号；failure 时必须非空。

配置均可省略以使用 Config 默认值，未知字段拒绝：

- 网络：`featureCount`、`actionCount`、`hiddenWidth`。
- 搜索：`population`、`generations`、`horizon`、`elites`、`mutationProbability`、`randomProbability`。
- 训练：`trainingEpochs`、`learningRate`。
- 时间与随机源：`dt`、`environmentSeed`、`searchSeed`、`learningSeed`。
- 策略：`strategy="evolution"` 或 `"random"`；`backend="cpu"`、`"tensor"` 或 `"gpu"`。
- 测试目标：`coverageWeight`、`failureWeight`、`maxFindings`。

seed 可用非负整数或十进制字符串；reset 回调收到**十进制字符串**，避免完整 uint64 seed 失真。每个候选回合使用同一 environment seed；搜索和权重初始化使用独立 seed。多个场景需显式运行多个 environment seed。

## 使用 tensor

```squirrel
local tensorProvider = eve.AgentTensor(); // 构建需包含 agent_tensor
local trained = brain.run({featureCount=1, actionCount=2, backend="tensor",
    population=8, elites=2, generations=4, horizon=8}, environment);
if (!trained.ok) throw trained.code;
local decision = brain.actTensor(trained.value.policy, environment.reset("42"));
if (!decision.ok) throw decision.code;
```

`agent_tensor` 位于 `agent/tensor`，依赖 agent 和 tensor；核心 agent 不反向依赖 tensor。provider 随模块构造注册，析构前撤销。缺少 provider 时，显式 tensor 请求返回 Unsupported，不隐式切换后端。

`backend="tensor"` 使用生产 Tensor 的 eager CPU 算子，报告 `backend="tensor-cpu"`、`trainingBackend="cpu-sgd"`。`backend="cpu"` 使用核心 double 网络。

### GPU 推理与训练

在 Graphics 设备完成初始化后（例如 `eve_init`），创建 `eve.AgentTensor()`，将上例配置改为 `backend="gpu"`，并使用 `brain.actGpu(policy, observation)` 或 `brain.inferGpu(policy, observation)`。

GPU 模式把两层 tanh、masked softmax、交叉熵反向传播、逐参数梯度裁剪及 SGD 更新编译成 tensor GPU 图，通过 Gpgpu 提交。报告为 `backend="tensor-gpu"`、`trainingBackend="tensor-gpu-sgd"`。搜索、环境回调、精英选择和随机采样仍在 CPU；没有引入通用自动微分 API。

网络形状不变时复用推理/训练图。Policy 的 owning 权重仍为唯一权威，每次调用上传输入并下载结果；失败不发布部分更新。未注册 provider、设备未就绪或计算图不支持时返回错误，**不会静默退回 CPU**。`getBackends` 报告设备就绪状态，具体网络的编译仍可能失败。

GPU 使用 FP32，CPU 使用 double，按数值容差比较；小差异可能改变后续随机动作，因此不承诺跨设备逐位相同的训练轨迹。已在 Vulkan 上验证生产生成 shader 的推理和 SGD；尚未验证 WebGPU。单个小网络可能受提交/读回开销影响，不承诺比 CPU 更快，当前未提供端到端加速比。

AgentTensor 拥有缓存图和设备资源，必须在 Graphics 设备销毁或重建前销毁；设备重建后重新创建 provider。不要在环境回调内销毁设备。所有 GPU 操作在设备 owner 线程同步完成，无异步资源借用。

## 测试与回放

测试时显式设置 `coverageWeight=1.0`、`failureWeight=100.0` 等领域目标。评分为累计奖励加本回合不同覆盖点数乘 coverageWeight，再加终止失败奖励。达到 maxFindings 后不保留更多失败轨迹，但失败计数继续增长。

报告包含 `policy`、`best`、去重 `findings`、`coverage`、`episodes`、`steps`、`failures`、`trainingSamples`、`bestScore`、`backend` 和 `trainingBackend`。best 是评分最高的轨迹，也可能为 horizon 截断的 Running，不保证成功。

```squirrel
foreach (trace in trained.value.findings) {
    local reproduced = brain.replay(trace, environment, 0.00001);
    if (!reproduced.ok) throw reproduced.code;
}
```

回放使用实际动作和初始 seed，比较每步观察、合法动作、覆盖与终止状态，不依赖训练策略。数值使用显式 tolerance，其余字段精确匹配。

## API 快查

- `getName()`：模块名。
- `getBackends()`：Result，value 为实际可用后端标签数组。
- `run(config, environment)`：Result，value 为训练/搜索报告。
- `infer(policy, observation)`：Result，value 为 CPU 合法动作概率数组。
- `inferTensor(policy, observation)`：相同契约，使用 tensor provider。
- `inferGpu(policy, observation)`：相同概率契约，使用 GPU 计算图，无 CPU fallback。
- `act(policy, observation)`：Result，value 为最大概率合法动作 ID；平分时取合法目录中较早动作。
- `actTensor(policy, observation)`：相同决策规则，使用 tensor provider。
- `actGpu(policy, observation)`：相同决策规则，使用 GPU 计算图。
- `replay(trace, environment, tolerance)`：无 value 的 Result；明确返回执行错误或 divergence。

C++ 使用 run/infer/replay 和 Backend。`Codec.h` 统一处理 Config/Observation/Policy/Trace 与 owning Value 的转换，脚本和 JSON 保存共用；例如 `encodePolicy(policy).toJson()`、`decodePolicy(value)`。Policy、Trace、Report 的 schema 为 `evengine.agent.policy/trace/report`，版本 1；未知字段/版本拒绝，无旧的已发布版本需迁移。不提供原始内存存档或自动文件写入。

## 生命周期与限制

环境由调用方拥有，在 owner 线程同步执行。绑定仅在调用期间 root 回调表，返回数据独立拥有。run/replay 嵌套调用返回 Conflict；每次回调恢复 VM 栈，抛错后模块可再次使用。runner 不持锁调用未知代码，不缓存领域对象或 provider 指针到下一次环境回调。

reset 必须重建领域句柄、事件队列和 RNG。完成或失败后环境停留在最后执行状态，不自动恢复调用前状态。长回合会阻塞当前线程；实时帧推进、进程隔离、native 崩溃和卡死超时需要外部宿主。输入和预算有上限；Squirrel 公共 Value 转换最多 100000 个元素，超限返回失败，不发布 ok + null。

C++ 示例覆盖生产 grid 与 UI layout 算法；布局数值测试不代表 UI 点击分派或渲染验收。学习不保证在所有领域优于随机策略，应按多个 seed 比较。

## 构建

依赖初始化后运行 `make check/agent`。Windows 用 MSVC 环境，或包裹 CMake：

```powershell
.\cmake\with-msvc.cmd cmake -S test/agent -B build/agent -G Ninja -DCMAKE_BUILD_TYPE=Debug
.\cmake\with-msvc.cmd cmake --build build/agent -j 4
ctest --test-dir build/agent --output-on-failure
```

专用工程支持 EVE_ZEROERR_SOURCE、EVE_ECS_SOURCE、EVE_THIRD_PARTY_SOURCE。`EVE_AGENT_TENSOR=OFF` 验证缺少 tensor 的脚本路径；再关闭 EVE_AGENT_SCRIPT、EVE_AGENT_COMPOSITION 可验证纯核心。主引擎模块由 manifest 接线。
