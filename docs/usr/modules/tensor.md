# 张量模块

**脚本入口：** `eve.TF()`

执行 eager 张量运算，或用 Func 构图、编译并重复运行。编译管线采用类似
AITemplate 的静态编译思路：按图生成专用 GPU kernel、融合元素级算子链、静态
内存规划复用缓冲区、对 matmul 做自动调优。

## 基本用法

```squirrel
local tf = eve.TF();
local a = tf.ones2(2, 3);
local b = tf.fill2(2, 3, 2.0);
local c = tf.add(a, b);
print(c.get2(0, 0) + "\n");
```

```squirrel
// 编译一个可复用的“游戏 AI”策略网络：x @ W1 + b -> relu -> @ W2 -> softmax
local fn = tf.func();
local x  = fn.input1(4);
local h  = tf.matmul(tf.reshape2(x, 1, 4), W1);   // W1 等 eager 张量会被捕获为常量
h = tf.add(h, b1);
h = tf.relu(h);
h = tf.matmul(h, W2);
h = tf.softmax(h, 1);
fn.setOutput(h);
local compiled = fn.compile();                     // 融合 + 生成专用 kernel + 内存规划
local out = compiled.run1(state);                  // 每帧只做 feed + dispatch + 读回
```

## 对象关系与调用时机

`TF` 创建 eager Tensor 或 Func；Func 中的 Tensor 是 symbolic；`compile()` 返回
CompiledFunction；run 接收 eager feed 并返回 eager 输出。shape 与 dtype 必须匹配。

## AITemplate 风格编译管线

`compile()` 不再按“固定算子 + opcode”解释执行，而是：

1. **图优化**（`Optimizer`）：DCE、常量折叠、元素级链融合、matmul/conv 的
   bias + 激活后置融合、按生命周期做静态内存规划（不重叠的中间结果复用同一块
   显存）。
2. **专用 kernel 代码生成**（`KernelGen`）：每个融合组生成一份形状全部烘焙为
   常量的 GLSL；softmax/layernorm 使用两遍 kernel；SDPA 使用共享内存的融合
   attention；matmul 提供 naive 与 16x16 tiled 两种模板。
3. **进程内 GLSL→SPIR-V 编译**：Windows 上链接 Vulkan SDK 的 shaderc
   （glslang + SPIRV-Tools）静态库，不依赖外部 glslc.exe；找不到 shaderc 时回退
   到调用 glslc。
4. **matmul 自动调优**：编译时对 naive / tiled 两种变体各计时 5 次，选择更快者。
5. **整图单次提交**（`GpuBackend` + `gpgpu::Sequence`）：一次 `run()` 把
   placeholder 上传、所有融合组的 kernel dispatch、输出回读录制进同一个
   command buffer，`submit()` 一次完成——而不是每个 kernel 各提交等待一次。
   对 transformer 这类几十个 kernel 的推理图，可省掉几十次 GPU 提交往返。

当没有 Vulkan 设备或图无法降级到 GPU 时，`compile()` 自动回退到 CPU 参考解释器，
`getDevice()` 返回 `"gpu"` 或 `"cpu"`。

## 目标导向指南

### 游戏离线 AI（策略/行为模型）

用 `func()` 构图一次、每帧 `run`：`matmul` → `add`/`addScalar`（bias）→
`relu`/`gelu`/`silu` → `softmax` 得到动作概率，再用 `argmax` 取动作、用
`embedding` 查表。rank 3 的 `matmul` 支持批处理多个智能体。

### 常用 GPGPU 计算

广播二元运算（`[N,C] + [C]` 等）、`sumAxis/meanAxis/minAxis/maxAxis`、
`where`、`clamp` 都既支持 eager 也支持构图编译；大张量全量归约
（`reduceSum` 等）在超过 16384 元素时自动走 GPU reduction。

### 游戏中运行离线语音模型

语音模型常用算子都已内置：`conv1d`（特征提取）、`layernorm`/`rmsnorm`、
`gelu`/`silu`、`sdpa`/`sdpaMasked`（融合 attention，自带 softmax 与 mask）、
`embedding`、`slice`/`concat`/`permute`。`cast(x, "int32")` 得到索引张量供
`embedding` 使用。SDPA 限制 S ≤ 2048、D ≤ 512，超出自动回退 CPU。

### 快速计算地形地貌

`conv2d`/`maxpool2d`/`avgpool2d` 用于平滑、侵蚀、降采样；`resize2d`（最近邻 /
双线性）用于高度图放大；`where` + 比较组合实现掩膜；`randomUniform*` 提供种子化
噪声。这些都能构图后在 GPU 上批量处理整块地形。

### 大规模模拟

把模拟状态组织成批张量，用广播运算一次推进整批：`mulScalar`（时间步长）、
`add`/`sub`（状态更新）、`clamp`（边界）、`reduceSum`（统计）。把每帧固定形状
的更新写成 Func 编译一次，避免脚本侧逐元素循环。

### 跑一个真实的 mini LLM（TinyStories-gpt2-3M）

用预训练权重实测整个推理管线：`calum/tinystories-gpt2-3M`（8 层、d=64、16 头、
GPT-2 BPE 词表 50257，TinyStories 语料训练）。测试 [test/tensor_llm.cpp]
(../../../test/tensor_llm.cpp) 用 TF 算子逐层搭出完整 GPT-2 图：
`embedding`（token + 位置）→ 8 × { `layernormWB` → 多头 `sdpaMasked`（因果 mask）
→ 残差 → MLP(`gelu`) → 残差 } → `layernormWB` → `matmul(lm_head)` → `argmax`
贪婪采样。验证结果：

- 与 numpy 参考实现逐位置对比 softmax 概率：最大差 3e-7，top-1 一致 16/16；
- 编译路径（`func()` + `compile()`）与 eager 完全一致（logit 差 0），device 为
  cpu 或 gpu（有 Vulkan 窗口时）；
- 贪婪生成与 numpy 参考 24/24 token 一致，CPU eager 约 100 ms/token。

示例输出（prompt：*Once upon a time, there was a little*）：

> Once upon a time, there was a little girl named Lily. She loved to play with
> her toys and make things. One day, she found a big box in...

资产（约 47 MB）已 gitignore，按以下步骤重建：

```bash
# 1) 从 https://huggingface.co/calum/tinystories-gpt2-3M 下载 4 个文件到
#    test/assets/mini_llm/：pytorch_model.bin / config.json / vocab.json / merges.txt
#    （文件名带 tinystories_ 前缀）
# 2) 导出引擎权重包 + token 表 + numpy 参考输出：
python scripts/export_tiny_gpt2.py
# 3) 运行测试：
build/test/unit_test.exe --testcase="tensor.llm.*"
```

### 权重量化（fp16 / fp8 / fp4 / int8 / int4）

推理管线内置权重-only 量化：把 eager 权重打包成紧凑 dtype，构图时捕获为量化
Const，CPU 解释器与 GPU kernel 都在使用点反量化，激活保持 fp32。

```squirrel
local wq = tf.quantizeWeight(W, "int8", 64);   // 每 64 个元素一个 block scale
local wq = tf.quantizeWeight(W, "fp16");        // fp16 为纯 half 存储，无 scale
```

格式与布局（`Tensor.get(i)` 直接返回反量化后的值，`isQuantized()` 可查询）：

- `fp16`：IEEE half，2 字节/元素，无 scale；
- `fp8`：e4m3（1 字节）+ per-group block scale；
- `fp4`：e2m1（2 元素/字节）+ per-group block scale；
- `int8` / `int4`：对称量化 + per-group block scale（int4 两元素/字节）。

支持量化的使用点：matmul 的权重输入（B）、embedding 表、lm_head（transpose 后）。
GPU 侧在生成的 GLSL kernel 内联反量化（naive matmul 与 embedding），不落回
fp32 缓冲；LN 的 scale/bias 与线性 bias 建议保持 fp32（测试默认如此）。

TinyStories 模型全矩阵量化（group=64）实测（top-1 贪婪一致率 vs fp32，16 个
预测位；权重体积 vs fp32 27.6 MB）：

| dtype | 概率最大差 | top-1 | 体积 |
|-------|-----------|-------|------|
| fp16  | 0.0004    | 16/16 | 13.2 MB |
| int8  | 0.0103    | 16/16 | 7.0 MB |
| fp8   | 0.0716    | 15/16 | 6.6 MB |
| int4  | 0.1767    | 16/16 | 3.7 MB |
| fp4   | 0.3961    | 16/16 | 3.3 MB |

fp8/fp4 的 block scale 取 `maxAbs / 格式最大幅值`（e4m3=240、e2m1=6），让归一化
值铺满整个格式动态范围——若只归一化到 [0,1]，e2m1 会浪费 2/3/4/6 等电平，误差
会大一个数量级（早期版本的 0.94 就是这样来的）。
量化正确性由 `tensor.quant.*` 与 `tensor.llm.quantizedDtypes` 测试覆盖（含
GPU 编译路径），资产缺失时自动跳过。

## 常见问题

- 对 symbolic Tensor 调用 `get()`。
- matmul 内维度不一致；rank 3 批处理要求 batch 相同。
- 每帧重新 func/compile，而不是复用 CompiledFunction。
- GPU 编译需要已创建 Vulkan 窗口（`gpgpu.isAvailable()` 为真）；否则回退 CPU。
- `argmax`/`cast("int32")` 得到 int32 dtype 张量，脚本读取仍返回 float。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `Tensor`、
`Func`、`CompiledFunction`）的方法也列在这里。

### TF（`tf.` 方法）

- `abs()`、`add()`、`addScalar()`、`arange()`、`argmax()`
- `avgpool2d()`、`cast()`、`clamp()`、`concat2()`、`concat3()`、`concat4()`
- `constantScalar()`、`conv1d()`、`conv1dBias()`、`conv2d()`、`conv2dBias()`
- `cos()`、`div()`、`divScalar()`、`embedding()`、`exp()`、`eye()`
- `fill1()`、`fill2()`、`fill3()`、`fill4()`、`flatten()`、`func()`
- `gelu()`、`getRandomSeed()`、`layernorm()`、`layernormWB()`、`linspace()`
- `log()`、`logSoftmax()`、`matmul()`、`maxAxis()`、`maximumScalar()`
- `maxpool2d()`、`meanAxis()`、`minAxis()`、`minimumScalar()`
- `mulScalar()`、`multiply()`、`neg()`、`ones1()`、`ones2()`、`ones3()`
- `ones4()`、`ones5()`、`ones6()`、`permute2()`、`permute3()`、`permute4()`
- `permute5()`、`permute6()`、`powScalar()`、`rand1()`、`rand2()`、`rand3()`
- `quantizeWeight()`、`rand4()`、`randn1()`、`randn2()`、`randn3()`、`randn4()`、`randomNormal1()`
- `randomNormal2()`、`randomNormal3()`、`randomNormal4()`、`randomUniform1()`
- `randomUniform2()`、`randomUniform3()`、`randomUniform4()`
- `reduceMax()`、`reduceMean()`、`reduceMin()`、`reduceSum()`、`relu()`
- `reshape1()`、`reshape2()`、`reshape3()`、`reshape4()`、`reshape5()`
- `reshape6()`、`resize2d()`、`rmsnorm()`、`rmsnormW()`、`sdpa()`
- `sdpaMasked()`、`setRandomSeed()`、`sigmoid()`、`silu()`、`sin()`
- `slice()`、`softmax()`、`sqrt()`、`sub()`、`subScalar()`、`sumAxis()`
- `tanh()`、`transpose()`、`where()`、`zeros1()`、`zeros2()`、`zeros3()`
- `zeros4()`、`zeros5()`、`zeros6()`

量化相关：`quantizeWeight(t, "fp16"/"fp8"/"fp4"/"int8"/"int4", group)`；
`t.isQuantized()`；量化张量的 `get(i)` 返回反量化值。

### Tensor（`tf.*` 返回对象的 `t.` 方法）

- `abs()`、`add()`、`addInPlace()`、`addScalar()`、`addScalarInPlace()`
- `clone()`、`copyFrom()`、`cos()`、`div()`、`divScalar()`、`dot()`
- `exp()`、`fill()`、`flatten()`、`gelu()`、`get()`、`get1()`、`get2()`
- `get3()`、`get4()`、`get5()`、`get6()`、`getDevice()`、`getDim()`
- `getDim0()`、`getDim1()`、`getDim2()`、`getDim3()`、`getDim4()`、`getDim5()`
- `getDtype()`、`getRank()`、`getSize()`、`isEager()`、`isSymbolic()`、`log()`
- `matmul()`、`maximumScalar()`、`minimumScalar()`、`mulScalar()`
- `mulScalarInPlace()`、`multiply()`、`multiplyInPlace()`、`neg()`
- `powScalar()`、`reduceMax()`、`reduceMean()`、`reduceMin()`、`reduceSum()`
- `relu()`、`reluInPlace()`、`reshape1()`、`reshape2()`、`reshape3()`
- `reshape4()`、`reshape5()`、`reshape6()`、`set()`、`set1()`、`set2()`
- `set3()`、`set4()`、`set5()`、`set6()`、`sigmoid()`、`silu()`、`sin()`
- `sqrt()`、`sub()`、`subScalar()`、`tanh()`、`transpose()`

### Func（`tf.func()` 返回对象的 `fn.` 方法）

- `compile()`、`input1()`、`input2()`、`input3()`、`input4()`、`input5()`
- `input6()`、`setOutput()`

### CompiledFunction（`fn.compile()` 返回对象的 `c.` 方法）

- `getDevice()`、`getPlaceholderCount()`、`run0()`、`run1()`、`run2()`
- `run3()`、`run4()`、`run5()`、`run6()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。
- 张量支持 rank 1–6，dtype 为 float32 / int32；二元运算支持广播。
- GPU 路径：`compile()` 需要已初始化的 Vulkan Graphics（先创建窗口）。Windows
  上 GLSL→SPIR-V 由链接进引擎的 shaderc 静态库完成，不需要安装 glslc。

**源码：** [`src/modules/tensor/`](../../../src/modules/tensor/)（含
`Optimizer` 图优化/融合、`KernelGen` 专用 kernel 生成、`GpuBackend` GPU 运行时）
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `tensor`。
