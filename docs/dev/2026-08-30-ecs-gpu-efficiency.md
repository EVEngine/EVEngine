# ECS GPU 效率评估与驻留契约

日期：2026-08-30

## 结论

当前 ECS 可以把同构 float 组件批量交给 compute shader，但原默认路径不是 GPU-resident
仿真：每次 `update` 都逐对象、逐字段从 Squirrel 组件打包，分别上传每个 binding，
同步 dispatch，再回读并逐字段写回。它适合验证和 CPU 每帧必须观察结果的工作负载，
不适合把几十万到百万对象长期留在 GPU 上模拟。

本轮为 `eve.ShaderSystem` 增加逐 binding 驻留策略。首次 update 或实体结构 revision
变化时自动打包；稳态可以只 dispatch。CPU 修改后通过 `requestUpload` 建立显式上传点，
CPU 需要快照时通过 `requestReadback` 建立显式回读点。backend 的 upload/download/
dispatch counters 用于验证实际数据流，避免仅凭配置推断。

后续补全增加了连续 dirty range、跨 system resident buffer 共享和 Sequence 批量录制。
CPU 只改少数连续实体时不再全量打包；producer → consumer 的多个 kernel 可以零 CPU
往返地共享同一个 buffer，并在一次提交中依次执行。C++ View 路径增加编译期类型约束的
`EcsGpuWorkspace<Component>`，可复用 staging capacity 并进行范围传输。

进一步补全后，Sequence 在 Vulkan/WebGPU 上都有 `submitAsync`、`poll`、`wait` 和稳定状态；
Vulkan 由 fence 驱动，WebGPU 由 queue-completion future 驱动。脚本 binding schema 显式声明
`f32`、stride 与 read/write 权限，并可生成 GLSL/WGSL buffer declarations；write-only 输出
不再发生首次 CPU 上传。

Vulkan/WebGPU 渲染端新增 `GpuBuffer::residentView()` → `Graphics::gpuDrivenSubmitResident()`：
GPU system 可把已经按 `GpuInstance` 排序的常驻 storage buffer 直接绑定为实例表，只需 CPU
提供 O(bucket) 的 mesh/material 区间元数据并生成 indirect commands，不再回读或逐实体重打包。
接口用结构化状态明确报告 backend mismatch、非法范围、容量不足和资源未就绪；不会暗中退回
CPU 路径。

## 数据移动成本

若 Position 和 Velocity 都是两个 float，实体数为 `N`：

- 默认同步模式：每帧上传 `16N` bytes、回读 `16N` bytes，并执行 `8N` 次脚本字段
  读取/写入；一百万实体即每帧 32 MB 总传输和约八百万次 VM 字段操作。
- GPU-resident 模式：初始化或结构变化时上传 16 MB；无显式同步的稳态帧上传 0、
  回读 0，仅保留 compute dispatch。
- dirty range 模式：修改 `K` 个连续对象时只上传 `16K` bytes；结构变化会自动退化为
  全量上传，避免旧 View 索引写入错误对象。

这些是由布局和调用次数推导出的确定数据量，不是硬件耗时。正式性能结论必须同时报告
GPU、驱动、backend、构建类型、对象数、warmup、样本数和 p50/p95。

## GPU System 编写难度

脚本侧最小系统需要一段 compute kernel、实体 query、两个 `bindFields` 和一次 `update`，
入门门槛较低。当前仍需作者手动保证 GLSL/WGSL backend 源码、binding 布局、local size、
float-only 数据和同步时机一致，因此复杂系统的易用性为中等，而不是自动 ECS codegen。

驻留写法：

```squirrel
local sys = eve.ShaderSystem(Moveable, gpgpu, moveGlsl, 64)
sys.bindFields(0, "pos", ["x", "y"], false, false)
sys.bindFields(1, "vel", ["x", "y"], false, false)
sys.update(dt)

// CPU 改过 velocity 后：
sys.requestUpload(1)
// CPU 真正需要 position 快照时：
sys.requestReadback(0)
```

## System 契约

- Entity 范围：传给构造函数的脚本 Entity 类型及其派生类型。
- View：`eve.view(query)` 的稳定顺序快照；创建/销毁递增结构 revision 并触发重打包。
- 读取/写入：由 shader 与 `bindFields` 共同声明；引擎不从 GLSL 反射访问模式。
- 结构变化：system 自身不创建、销毁或增删组件；外部结构变化在下一次 update 前失效驻留打包。
- 事件：无。
- 服务：需要可用的 `Gpgpu` provider；缺失时不执行。
- 阶段：由游戏在 `eve_update` 中显式排序，调用线程为主/渲染线程。
- 权威状态：默认同步模式中 CPU 组件在 update 返回后可观察；驻留模式中 GPU buffer 是
  绑定字段的仿真权威，CPU 组件只是旧投影，直到显式回读。两端不得并发修改同一字段。
- 确定性：浮点结果为 tolerance-bounded；不承诺跨 GPU/backend bit-exact。
- 异步生命周期：Sequence、shader、buffer 在 `complete/failed` 前必须存活；pending Sequence
  拒绝 `begin`，析构会等待。`poll/wait` 在提交线程调用；WebGPU queue callback 只原子更新
  状态，不回调脚本或未知用户代码，因此不存在锁内 callback reentrancy。

## 大规模能力边界

在“结构稳定、热数据为紧凑 float、多个 tick 留在 GPU、消费端也能读取 GPU buffer”的条件下，
现有 storage-buffer compute 模型具备大批量模拟的数据路径。以下限制仍会阻止完整的百万对象
游戏闭环：

- `Gpgpu::dispatch` 当前同步等待，尚未与帧图异步调度整合；
- `ShaderSystem.record` 能把多个系统合并提交，Sequence 也能跨帧轮询完成状态；当前仍由
  调用者负责保存 Sequence、shader 和 buffer 生命周期，尚未接入全局 frame graph；
- 通用 GPU-driven 渲染现可直接消费 `ShaderSystem::getBuffer(binding)` 的 resident
  view；调用者仍需让 kernel 输出标准 80-byte `GpuInstance` 并维护连续、完整、按
  mesh/material 排序的 bucket 元数据；slice offset 必须按跨后端的 256-byte storage alignment
  对齐。Squirrel 已作为一等入口绑定 mesh/material 注册、类型化实例写入、resident bucket
  提交、启停和稳定状态；`eve.ShaderSystem` 也能直接提交其 resident binding；
- WebGPU 已有读取 80-byte `GpuInstance` 的 resident vertex pipeline，并复用现有 indirect
  buffer 与材质 bucket；固定版本 Dawn 的 WebGPU 专属 `GraphicsGpuDriven.cpp` 已实际编译，
  但完整 `EVGraphics` 被仓库既有 SDL Wayland `SDL_SysWMinfo::wl` 编译错误阻断，故端到端
  运行证据仍限于 Vulkan，不能把实测结果外推到 WebGPU；
- 脚本已有显式 `f32` schema、访问权和 GLSL/WGSL declaration codegen，但尚未根据完整
  kernel IR 自动生成算法主体，也尚不支持整数/向量压缩格式；
- C++ `EcsGpu.h` 已能复用 typed staging 并局部传输，但 ECS storage 本身仍不是可直接
  绑定的 GPU-native SoA storage。

因此当前答案是“核心批量计算可以，端到端高效大规模对象系统有条件成立”。本轮解决最主要的
稳态重复传输；后续优先级应是 resident buffer 到 indirect/instanced rendering 的直接连接、
异步 frame-graph 调度，以及 typed GPU component schema。

## 验证入口

- `ScriptECS.shaderSystemGpuResidentTransfers`：证明首次上传、稳态零上传/回读、显式同步和结构失效。
- `gpgpu.shaderSystem.ecsMove`：验证真实 compute 结果及 transfer/dispatch counters。
- `gpgpu.ecsPack.contiguousRange`：证明局部 pack/unpack 不覆盖范围外记录。
- `GpuDriven.residentInstanceBufferDirectSubmit`：在真实 Vulkan frame 中验证 resident storage
  buffer 直接绑定、indirect draw，以及 backend/stride/bucket 契约拒绝路径。
- `GpuDriven.squirrelResidentInstanceSubmit`：真实 Squirrel 脚本完成 mesh/material 注册、
  80-byte typed instance 写入并在 Vulkan frame 中提交一次 resident draw。
- `EVENGINE_ECS_MILLION_BENCHMARK=1` 运行
  `gpgpu.shaderSystem.millionResidentBenchmark`：输出一百万对象 resident tick 的 p50/p95、
  transfer counts 与零稳态传输证据；软件 Vulkan 数值仅用于回归，不代表独显预算。

本机 Lavapipe Debug 基线（2026-08-30，1,000,000 entities，3 warmup + 10 samples）为
p50 `0.753 ms`、p95 `0.903 ms`；13 个 resident tick 总计仅 2 次初始化上传和 1 次最终
验证回读，稳态上传/回读均为 0。该数据证明路径和复杂度不会随 CPU 字段同步增长，不能
外推为目标显卡的帧预算。
- `gpgpu.procgen.millionPointAcceptanceBenchmark`：已有百万点 GPU 融合基准，但它衡量 PointGraph，
  不能替代 ECS 脚本打包和驻留路径的硬件测量。
