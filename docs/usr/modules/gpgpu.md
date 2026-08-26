# GPU 计算模块

**脚本入口：** `eve.Gpgpu()` / `eve.EcsShaderSystem` / `eve.ShaderSystem`

创建 storage buffer 和 compute shader，绑定后通过当前 Vulkan 或 WebGPU 后端调度 compute。Vulkan 使用 GLSL，WebGPU 使用 WGSL；也可把 ECS System 写成 compute shader：打包组件 float 字段 → storage buffer → dispatch → 写回。

## 基本用法

```squirrel
local gpu = eve.Gpgpu();
if (gpu.isAvailable()) {
    local shader = gpu.newShader(shaderSource);
    local buffer = gpu.newBuffer(1024, "storage");
    shader.bindBuffer(0, buffer);
    gpu.dispatch(shader, 4, 1, 1);
}
```

## 用 shader 写 ECS System

```squirrel
class Moveable extends eve.Entity { pos = Position; vel = Velocity }

local sys = eve.ShaderSystem(Moveable, gpgpu, moveGlsl, 64)
sys.bindFields(0, "pos", ["x", "y"])
sys.bindFields(1, "vel", ["x", "y"])

eve_update = function(dt) { sys.update(dt) }
```

Compute shader 约定：

- `binding i`：对应 `bindFields(i, …)` 的 AoS float 流（按实体顺序紧密排列）
- `push_constant data[0]` = `dt`，`data[1]` = 实体数量

C++ 侧可用 `gpgpu::ShaderSystem` + `gpgpu/EcsGpu.h` 的 `packViewComponent` / `unpackViewComponent` 对 `ECS.hpp` View 做同样的打包与调度。

## Sequence：把多次调度合并为一次 GPU 提交

`eve.GpuSequence`（C++：`gpgpu::Sequence`）对标 Kompute 的 Sequence：在 Vulkan 与 WebGPU 上都把多个 buffer 拷贝和 compute dispatch 录制进**同一个 command buffer**，`submit()` 时一次提交、一次等待。对 AI 推理这类几十个 kernel 串行的负载，这能把几十次 record/submit/wait 往返压成一次。

```squirrel
local seq = gpu.newSequence();
seq.begin();
seq.recordUpload(inputBuffer, hostDataPointer, nbytes);   // 可多次
seq.recordDispatch(shaderA, groupsA);                    // 录制时 shader 的
seq.recordDispatch(shaderB, groupsB);                    // binding 已生效
seq.recordDownload(outputBuffer, stagingBuffer, nbytes); // staging 需 "staging" 用途
seq.submit();                                            // 一次提交，内部等待完成
local out = stagingBuffer.readFloat32s(count);
```

要点：

- `recordUpload` 会把数据先 memcpy 到内部 host-visible staging，调用返回后源指针即可失效。
- `recordDownload` 目标必须是 `newBuffer(n, "staging")`；`submit()` 返回后再读。
- 同一 Sequence 可循环复用：`submit()` 后再 `begin()`。
- Sequence 内部自动插入内存屏障，保证拷贝/计算/回读之间可见（单次提交没有 fence 兜底，这是与每次 dispatch 各提交一次的关键区别）。
- 录制期间 shader 的 descriptor set 会被延迟释放，`submit()` 完成后才回收，避免多 dispatch 引用被覆盖的 set。

引擎内部的 tensor 模块（`CompiledFunction` 的 GPU 执行）已经用 Sequence 实现整图单次提交：placeholder 上传 → 所有融合 kernel dispatch → 输出回读，一次 `submit()` 完成。

## 对象关系与调用时机

`Gpgpu` 使用当前 Graphics 后端设备；`ComputeShader` / `GpuBuffer` 为后端无关抽象。Vulkan 编译 GLSL 并保存 SPIR-V，WebGPU 直接创建 WGSL pipeline；`newShaderFromSpvFile` 是 Vulkan SPIR-V 兼容包装，WebGPU 不接受 SPIR-V。`EcsShaderSystem` / `eve.ShaderSystem` 负责 ECS 查询、打包与 dispatch。dispatch 前所有 binding 和 push constant 必须有效。

## 目标导向指南

### 批量缩放数组

先检查 `isAvailable()`，创建 storage buffer 并写入 float，创建 compute shader，binding 0 绑定 buffer、push constant 设置倍率，按 local size 计算 group 数后 `dispatch()`，最后读回结果。

### ECS 积分 / 粒子式批量更新

用 `eve.ShaderSystem` 绑定要读写的组件浮点字段，在 `eve_update` 调用 `update(dt)`。需要 CPU 侧逻辑（碰撞反弹等）时，可在 GPU 积分后再遍历 `entities()`。

### 避免 GPU 同步拖慢帧

频繁 readData / `setReadback(true)`（默认）会等待 GPU；将连续计算保留在 device-local buffer，最终需要 CPU 结果时再读回。shader、buffer 和 binding 应复用，尺寸变化时才重建。

## 常见问题

- 未检查 `isAvailable()` 就创建资源。
- dispatch group 数按元素数而非 local size 取整（`ShaderSystem` 已按 local size 处理）。
- GPU 写完立即频繁 readback，造成同步停顿。
- 组件字段不是 number：只有 float/int 字段可打包。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `begin()`、`bindBuffer()`、`clearBindings()`、`dispatch()`、`fillFloat32()`、`getBoundBuffer()`、`getFloat()`、`getName()`、`getSize()`
- `getUsage()`、`isAvailable()`、`newBuffer()`、`newShader()`、`newShaderFromBytecode()`、`newShaderFromSpvFile()`、`readData()`、`readFloat32()`、`setFloat()`
- `newSequence()`、`recordDispatch()`、`recordDownload()`、`recordUpload()`、`submit()`、`writeData()`、`writeFloat32()`、`packEcsFloats()`、`unpackEcsFloats()`
- `eve.EcsShaderSystem`：`setGpgpu` / `setShaderSource` / `ensureBuffer` / `dispatch` / …
- `eve.ShaderSystem`：`bindFields` / `setShaderSource` / `setReadback` / `update`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/gpgpu/`](../../../src/modules/gpgpu/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `gpgpu` / `ShaderECS`。
**示例：** [`examples/ecs/gpu_main.nut`](../../../examples/ecs/gpu_main.nut)、[`examples/basic/compute/ecs_move.comp`](../../../examples/basic/compute/ecs_move.comp)
