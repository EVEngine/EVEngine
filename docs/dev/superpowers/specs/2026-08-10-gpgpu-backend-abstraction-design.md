# GPGPU 后端抽象设计（抽象基类 + Vulkan 派生）

日期：2026-08-10  
状态：已确认，待实现计划

## 背景与目标

当前 `ComputeShader` / `GpuBuffer` / `Gpgpu` 直接暴露并依赖 Vulkan 类型（`vk::*`、`vkb::Device`），公共头文件包含 `vkbuilder.hpp`。项目目前只有 Vulkan 一个图形后端，但未来可能扩展，因此需要把 GPGPU 模块改成**实现无关的公共接口**，同时保留现有脚本与 C++ 调用约定。

### 决策摘要

| 项 | 选择 |
|----|------|
| 抽象范围 | 完整抽象整个 GPGPU 模块 |
| 结构模式 | **方案 3：抽象基类 + Vulkan 派生资源** |
| 后端选择 | 自动匹配当前 Graphics 后端 |
| 着色器契约 | 新增通用源码/字节码接口；保留 SPIR-V API 作为兼容包装 |

## 架构

```
gpgpu/
  Gpgpu.h / Gpgpu.cpp              # Module 门面；工厂返回基类指针
  ComputeShader.h / .cpp           # 抽象基类（无 Vulkan 头）
  GpuBuffer.h / .cpp               # 抽象基类（无 Vulkan 头）
  vulkan/
    VulkanComputeShader.h/.cpp
    VulkanGpuBuffer.h/.cpp
    VulkanGpgpu.cpp                # 创建 / 编译 / dispatch 的 Vulkan 实现
    VulkanUtil.h/.cpp              # 现有 Vulkan 工具迁入
```

职责：

| 类型 | 公共层 | Vulkan 派生层 |
|------|--------|---------------|
| `ComputeShader` | `bindBuffer` / `setFloat` / `clearBindings` 等虚接口 | pipeline、descriptor、shader module |
| `GpuBuffer` | `writeData` / `readData` / `uploadBytes` 等虚接口 | `vk::Buffer` / device memory |
| `Gpgpu` | `newShader*` / `newBuffer` / `dispatch` | 按 Graphics 后端创建对应派生类并实现 dispatch |

公共头文件**不得**包含 Vulkan / vkbuilder 类型。

## 类接口

### `ComputeShader`（抽象基类）

```cpp
class ComputeShader {
public:
    static constexpr int kMaxBindings = 8;
    static constexpr int kMaxFloats = 32;
    static constexpr uint32_t kPushConstantBytes =
        uint32_t(kMaxFloats * sizeof(float));

    virtual ~ComputeShader() = default;

    virtual void bindBuffer(int binding, GpuBuffer *buffer) = 0;
    virtual GpuBuffer *getBoundBuffer(int binding) const = 0;
    virtual void setFloat(int index, float value) = 0;
    virtual float getFloat(int index) const = 0;
    virtual void clearBindings() = 0;

protected:
    // 共享 push-constant 状态，避免每个后端重复
    std::array<float, kMaxFloats> push_{};
};
```

- `flushDescriptors` 及 descriptor / pipeline 细节仅存在于 `VulkanComputeShader`，不对脚本暴露。
- 非拷贝；由调用方 / Squirrel 拥有，虚析构确保正确释放派生资源。

### `GpuBuffer`（抽象基类）

```cpp
class GpuBuffer {
public:
    virtual ~GpuBuffer() = default;

    virtual int getSize() const = 0;
    virtual std::string getUsage() const = 0;

    virtual void writeData(data::ByteData *data, int dstOffset = 0) = 0;
    virtual data::ByteData *readData(int srcOffset = 0, int size = -1) = 0;
    virtual void writeFloat32(int floatIndex, float value) = 0;
    virtual float readFloat32(int floatIndex) = 0;
    virtual void fillFloat32(float value) = 0;

    virtual void uploadBytes(const void *src, uint64_t nbytes,
                             uint64_t dstOffset = 0) = 0;
    virtual void downloadBytes(void *dst, uint64_t nbytes,
                               uint64_t srcOffset = 0) = 0;
};
```

### `Gpgpu` 工厂签名

```cpp
bool isAvailable() const;

ComputeShader *newShader(const std::string &source);
ComputeShader *newShaderFromBytecode(const std::string &path);
ComputeShader *newShaderFromSpvFile(const std::string &path);  // 兼容 → bytecode

GpuBuffer *newBuffer(int byteSize, const std::string &usage = "storage");
void dispatch(ComputeShader *shader, int groupsX,
              int groupsY = 1, int groupsZ = 1);
```

- 工厂一律返回**基类指针**，实际对象为 Vulkan 派生类。
- `newShader`：通用源码；Vulkan 路径仍走现有 GLSL→SPIR-V（`glslc`）逻辑。
- `newShaderFromBytecode`：通用字节码文件；Vulkan 下按 SPIR-V 加载。
- `newShaderFromSpvFile`：保留为兼容包装，内部转调 `newShaderFromBytecode`；非 Vulkan 时明确报错「SPIR-V 仅支持 vulkan」。

## 后端匹配

- 在 `Graphics` 上增加轻量标识，例如 `virtual std::string getBackendName() const`，Vulkan 实现返回 `"vulkan"`。
- `Gpgpu` 读取当前 Graphics 后端名；仅当为 `"vulkan"` 且 device 已初始化时 `isAvailable()==true`。
- 不引入独立 GPGPU 后端注册工厂；后端跟随 Graphics。

## 数据流

1. 调用方通过 `Gpgpu::newShader*` / `newBuffer` 获得基类指针（内部 `new Vulkan*`）。
2. `bindBuffer` / `setFloat` 只更新抽象状态（bindings + push）。
3. `dispatch(shader, gx, gy, gz)`：
   - 空指针 / 无效 shader：与现有一致，直接 return；
   - 校验对象与当前后端匹配（`dynamic_cast` 到 Vulkan 派生类，或内部等价检查），不匹配则抛 `Exception`；
   - Vulkan：flush descriptors → bind pipeline / sets / push constants → dispatch → 同步等待（保持现有 sync 语义）。

## 错误处理

| 场景 | 行为 |
|------|------|
| Graphics 未就绪 / 非 Vulkan | `isAvailable()==false`；工厂抛 `Exception`（与现有 `requireVulkanGraphics` 一致） |
| SPIR-V API 在非 Vulkan | 明确异常：SPIR-V 仅支持 vulkan |
| binding / float 越界 | 保持现有静默忽略（本次不改行为） |
| `dispatch` 空指针 | 直接 return |

## 脚本绑定

- 继续绑定基类 `ComputeShader` / `GpuBuffer`。
- 虚析构保证 Squirrel 删除对象时走派生析构。
- 对外方法名与现有一致；可额外暴露 `newShaderFromBytecode`。

## 迁移影响

| 调用方 | 影响 |
|--------|------|
| Squirrel 脚本 | API 基本不变 |
| `tensor/GpuBackend` | 继续使用基类指针；不得再访问 `vk::*` 公共成员；改为虚方法 |
| 公共头依赖 | 去掉 Vulkan include，编译隔离更好 |
| 测试 | 现有 gpgpu / tensor GPU 路径行为等价，应可回归 |

## 明确不做（YAGNI）

- 不实现第二后端（Metal / D3D12），只留抽象边界。
- 不把 sync `dispatch` 改成异步队列。
- 不引入独立于 Graphics 的 GPGPU 后端注册系统。
- 不收紧越界静默忽略行为。

## 成功标准

1. `ComputeShader.h` / `GpuBuffer.h` / `Gpgpu.h` 公共头无 Vulkan / vkbuilder 依赖。
2. Vulkan 实现集中在 `gpgpu/vulkan/`。
3. 脚本与 `tensor::GpuProgram` 在 Vulkan 下行为与重构前等价。
4. 新增后端时，只需增加派生类 + Graphics 后端名分支，无需改脚本绑定表面。
