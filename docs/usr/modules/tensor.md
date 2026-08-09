# 张量模块

**脚本入口：** `eve.TF()`

执行 eager 张量运算，或用 Func 构图、编译并重复运行。

## 基本用法

```squirrel
local tf = eve.TF();
local a = tf.ones2(2, 3);
local b = tf.fill2(2, 3, 2.0);
local c = tf.add(a, b);
print(c.get2(0, 0) + "\n");
```

## 对象关系与调用时机

`TF` 创建 eager Tensor 或 Func；Func 中的 Tensor 是 symbolic；`compile()` 返回 CompiledFunction；run 接收 eager feed 并返回 eager 输出。shape 与 dtype 必须匹配。

## 目标导向指南

### 执行 eager 数值运算

用 `zeros/ones/fill/random` 创建 Tensor，通过 add、matmul、relu、reduce 等立即得到结果；先检查 shape/rank，元素调试用 `get1/get2/...`，批量逻辑不要逐元素跨脚本边界。

### 编译重复计算图

`tf.func()` 创建 Func，用 input 声明 placeholder，组合算子并 `setOutput()`，然后 `compile()`；根据输入数量调用 `run0()` 到 `run4()`，避免重复建图。

## 常见问题

- 对 symbolic Tensor 调用 `get()`。
- matmul 内维度不一致。
- 每帧重新 func/compile，而不是复用 CompiledFunction。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `abs()`、`add()`、`addInPlace()`、`addScalar()`、`addScalarInPlace()`、`arange()`、`clamp()`、`clone()`
- `compile()`、`constantScalar()`、`copyFrom()`、`cos()`、`div()`、`divScalar()`、`dot()`、`exp()`
- `eye()`、`fill()`、`fill1()`、`fill2()`、`fill3()`、`flatten()`、`func()`、`get()`
- `get1()`、`get2()`、`get3()`、`get4()`、`getDevice()`、`getDim()`、`getDim0()`、`getDim1()`
- `getDim2()`、`getDim3()`、`getDtype()`、`getName()`、`getPlaceholderCount()`、`getRandomSeed()`、`getRank()`、`getSize()`
- `input1()`、`input2()`、`input3()`、`input4()`、`isEager()`、`isSymbolic()`、`linspace()`、`log()`
- `matmul()`、`maximumScalar()`、`minimumScalar()`、`mulScalar()`、`mulScalarInPlace()`、`multiply()`、`multiplyInPlace()`、`neg()`
- `ones1()`、`ones2()`、`ones3()`、`powScalar()`、`rand1()`、`rand2()`、`randn1()`、`randn2()`
- `randomNormal1()`、`randomNormal2()`、`randomUniform1()`、`randomUniform2()`、`reduceMax()`、`reduceMean()`、`reduceMin()`、`reduceSum()`
- `relu()`、`reluInPlace()`、`reshape1()`、`reshape2()`、`reshape3()`、`reshape4()`、`run0()`、`run1()`
- `run2()`、`run3()`、`run4()`、`set()`、`set1()`、`set2()`、`set3()`、`set4()`
- `setOutput()`、`setRandomSeed()`、`sigmoid()`、`sin()`、`sqrt()`、`sub()`、`subScalar()`、`tanh()`
- `transpose()`、`where()`、`zeros1()`、`zeros2()`、`zeros3()`、`zeros4()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/tensor/`](../../../src/modules/tensor/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `tensor`。
