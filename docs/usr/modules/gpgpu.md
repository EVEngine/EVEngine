# GPU 计算模块

**脚本入口：** `eve.Gpgpu()`

创建 storage buffer 和 compute shader，绑定后调度 Vulkan compute。

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

## 对象关系与调用时机

`Gpgpu` 使用 Graphics 的 Vulkan 设备；ComputeShader 保存 SPIR-V 与 bindings；GpuBuffer 保存 storage/staging 数据。dispatch 前所有 binding 和 push constant 必须有效。

## 目标导向指南

### 批量缩放数组

先检查 `isAvailable()`，创建 storage buffer 并写入 float，创建 compute shader，binding 0 绑定 buffer、push constant 设置倍率，按 local size 计算 group 数后 `dispatch()`，最后读回结果。

### 避免 GPU 同步拖慢帧

频繁 readData 会等待 GPU；将连续计算保留在 device-local buffer，最终需要 CPU 结果时再读回。shader、buffer 和 binding 应复用，尺寸变化时才重建。

## 常见问题

- 未检查 `isAvailable()` 就创建资源。
- dispatch group 数按元素数而非 local size 取整。
- GPU 写完立即频繁 readback，造成同步停顿。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `bindBuffer()`、`clearBindings()`、`dispatch()`、`fillFloat32()`、`getBoundBuffer()`、`getFloat()`、`getName()`、`getSize()`
- `getUsage()`、`isAvailable()`、`newBuffer()`、`newShader()`、`newShaderFromSpvFile()`、`readData()`、`readFloat32()`、`setFloat()`
- `writeData()`、`writeFloat32()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/gpgpu/`](../../../src/modules/gpgpu/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `gpgpu`。
