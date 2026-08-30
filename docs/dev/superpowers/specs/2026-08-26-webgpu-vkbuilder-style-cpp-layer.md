# WebGPU 后端对标 vk-bootstrap：C++ 系统化封装层，编译期阻挡不可行的 API 调用

日期：2026-08-26
状态：已确认，待实现计划
分支：`codex/webgpu-vkbuilder`

## 背景与问题

Vulkan 后端依赖 `external/VKBuilder`（`vkbuilder.hpp`）：用 RAII 句柄（`GenericBuffer` /
`TextureImage2D` / `ColorAttachmentImage` / `DepthTarget`）、状态机（`UnboundSet` →
`BoundSet`）、Builder 链（`DescriptorSetBuilder` / `SamplerBuilder` / `PipelineBuilder`）把
**不可行 / 非法的 API 组合在编译期或类型层面阻挡**：`Device` 未建好前不能调设备级函数、
buffer 未 upload 不能 bind、错误参数无法拼出合法调用等。

而 WebGPU 后端（`src/modules/graphics/webgpu/Graphics.cpp`，4527 行）目前**没有**这套系统化
防护，直接在引擎层混用三层 API：

1. **原始 C API**（`WGPU*` 结构体 + `wgpuBufferMapAsync` / `wgpuSurfaceGetCapabilities` /
   `wgpuInstanceProcessEvents` 等自由函数）——624 处 `WGPU*` 用法；
2. **Dawn/emdawn 的 C++ RAII 包装**（`wgpu::Device` / `wgpu::Buffer` / `wgpu::RenderPipeline`）
   ——143 处 `.Get()`；
3. **两者互转**：107 处 `reinterpret_cast`（把 `WGPU*Descriptor` 当作 `wgpu::*Descriptor`
   传递，或反向），全靠 ABI 布局巧合成立。

由此产生四类"不可行 API 调用"在今天只是靠注释/经验规避，随时会炸：

| 问题 | 现状（Graphics.cpp） | 后果 |
|------|----------------------|------|
| **C↔C++ 结构体 ABI 混转** | 107 处 `reinterpret_cast`，`WGPUBufferDescriptor` ↔ `wgpu::BufferDescriptor` | 依赖 ABI 布局相同；结构体一改就静默错位，无编译期检查 |
| **默认值陷阱** | `WGPU*Descriptor d{}` 零初始化后手动设字段；`writeMask`/`sampleMask`/`alphaMode` 显式 `0` 或 `0xFFFFFFFF` | 零初始化 ≠ WebGPU 默认值（如 `writeMask=0` 全丢弃、`alphaMode=0` 为空槽），改动即出黑屏/全白 |
| **异步初始化无状态机** | `adapterReceived`/`deviceReceived`/`swapchainConfigured`/`initialized` 等 `std::atomic<bool>` + 忙等 `waitForAdapter/waitForDevice` | 状态靠运行时布尔拼凑，顺序错（先取 surface 再 request adapter）编译不过但运行时炸 |
| **不可行调用仅 stub** | `setMesh3DSSAO`/`beginDecalPass`/`supportsGBufferPost()==false` 空实现 | 引擎上层不知道哪些调用真正生效；静默吞掉 ≠ 明确拒绝 |

**目标**：为 WebGPU 后端建立一套**对标 vkbuilder 的 C++ 系统化封装层**，把"不可行的 API 调用
逻辑"在编译期（类型/签名/状态）阻挡住，而不是运行时靠断言或运气。这是纯增量重构——不改变
`eve::graphics::Graphics` 公共接口，不改脚本绑定，不改渲染结果。

## 对标矩阵（VKBuilder 支柱 → WebGPU 对应）

| VKBuilder 支柱 | WebGPU 对应方案 | 阻挡什么 |
|----------------|------------------|----------|
| RAII 资源句柄 | `wgpu::*` 已是 RAII；用 `webgpu::Resource` 家族统一封装 + 强类型持有 | 裸句柄泄漏 / 悬垂 |
| `UnboundSet`→`BoundSet` 状态机 | `webgpu::BindGroup` 的"可写→可读→已提交"类型态 | 绑定未完成就 draw |
| Builder 链默认合法 | `webgpu::PipelineBuilder` / `BindGroupLayoutBuilder` / `SamplerBuilder` / `TextureBuilder`，**默认值即合法值** | `writeMask=0` 黑屏 / `sampleMask=0` 丢弃 / `alphaMode` 空槽 |
| 初始化分步，非法顺序不可表示 | `webgpu::InitFlow`（`Instance→Adapter→Device→Surface→Configure`） | 在 device 前调 surface/提交队列 |
| 能力探测后再开功能 | `webgpu::Capabilities`（adapter/device limits 查询） | 请求超限或不可用的 sample count / format |
| 明确失败而非静默 | 每个 `Graphics` 空实现改抛 `Exception` + `supportsX()` 已返回 false | 上层误用不可行功能时获得确定性错误 |

## 架构

```
src/modules/graphics/webgpu/
  wgpu_types.h            # 轻量工具：字符串视图、类型标签、枚举封装的统一 using
  Resource.h              # 资源 RAII 家族（沿用 wgpu::*，但收敛 .Get() 泄漏点）
  Capabilities.h/.cpp     # adapter/device 能力与 limits 的一次性查询缓存
  InitFlow.h/.cpp         # 分步初始化状态机（Instance→Adapter→Device→Surface→Configure）
  Builder.h/.cpp          # Pipeline/BindGroupLayout/Sampler/Texture Builder 链
  Graphics.cpp            # 改造为只消费上述层；消除全部 reinterpret_cast 与手写零初始化
```

### 分层与依赖

- 公共 `Graphics` 接口、`gpgpu/webgpu`、`imgui_impl_wgpu` 都不感知本层（仍用 `wgpu::*`）。
- `Builder.h`/`Capabilities.h`/`InitFlow.h` 是 `graphics/webgpu` 模块内部头，按
  AGENTS.md 跨模块规则不向上 `#include`。
- 新层只向下依赖 `wgpu::*`（webgpu_cpp.h），不引入新的 `reinterpret_cast` 之外的 ABI 假设。

## 关键设计

### 1. `InitFlow`：把"不可行的初始化顺序"变成编译期不可表示

`Graphics::initWithWindow` 今天把 adapter 回调、device 回调、surface 创建、configure 全部塞在一个
函数里，用 4 个 `std::atomic<bool>` 串起来。改为**类型驱动的一次性流程**：

```cpp
class InitFlow {
public:
    // 阶段对象只在上一阶段成功后才可取得；顺序由类型链保证。
    struct InstanceDone { wgpu::Instance instance; };
    struct AdapterDone { wgpu::Instance instance; wgpu::Adapter adapter; };
    struct DeviceDone {
        wgpu::Instance instance;
        wgpu::Adapter adapter;
        wgpu::Device device;
        wgpu::Queue queue;
        Capabilities caps;            // 建好 device 后立即缓存能力
    };
    struct SurfaceDone {
        wgpu::Instance instance;
        wgpu::Adapter adapter;
        wgpu::Device device;
        wgpu::Queue queue;
        wgpu::Surface surface;
        WGPUTextureFormat surfaceFormat;  // 来自 wgpuSurfaceGetCapabilities
        Capabilities caps;
    };
    struct Ready {                     // Configure 完成
        wgpu::Device device;
        wgpu::Queue queue;
        wgpu::Surface surface;
        WGPUTextureFormat surfaceFormat;
        Capabilities caps;
    };

    InstanceDone createInstance();            // 空实例；无 adapter 前不暴露设备 API
    AdapterDone requestAdapter(InstanceDone); // 仅从 InstanceDone 取得
    DeviceDone requestDevice(AdapterDone);    // 仅从 AdapterDone 取得
    SurfaceDone createSurface(DeviceDone, void* nativeWindow);
    Ready configure(SurfaceDone, int w, int h); // 仅从 SurfaceDone 取得
};
```

- **阻挡**：想跳步（拿 `SurfaceDone` 前就调 `configure`）、拿 `DeviceDone` 前就 `createSurface`
  都是**编译期**类型不匹配，直接编译失败。
- 回调转同步：保留现有 `wgpuInstanceRequestAdapter` + `ProcessEvents` 忙等语义（浏览器/Emscripten
  行为不变），但把布尔聚合成一个阶段结果，消除 `waitForAdapter/waitForDevice` 的散落状态。
- `Graphics` 持有 `Ready` 之后只暴露 `device()/queue()/surface()` 的 `const` 访问，杜绝中途改头。

### 2. `Capabilities`：能力探测后才允许请求资源

把 `wgpuAdapterGetLimits` / `wgpuDeviceGetLimits` / `wgpuSurfaceGetCapabilities` 的查询结果
**缓存成只读快照**，并暴露谓词：

```cpp
class Capabilities {
public:
    static Capabilities query(wgpu::Instance, wgpu::Adapter,
                              const wgpu::Surface &, WGPUTextureFormat defaultFormat);
    bool supportsSampleCount(uint32_t) const;
    bool supportsFormat(WGPUTextureFormat) const;
    uint32_t maxTextureDimension() const;
    uint32_t maxBindGroups() const;
    uint32_t maxDynamicUniformBuffers() const;
    // …更多 limits 谓词按需添加
};
```

- `createSceneColorResources`/`configureSurface` 用 `supportsSampleCount` 决定 MSAA，
  而不是硬编码 1/4。
- 自定义 shader / 多 bind group 路径用 `maxBindGroups` 防护。
- 只读、一次构造，杜绝中途查询不一致。

### 3. `Builder`：默认值即合法值，消除零初始化陷阱

把 Graphics.cpp 里手写 `WGPU*Descriptor` + `reinterpret_cast` 的地方收敛成 Builder 链。
**Builder 构造即写入合法默认**（对应 WebGPU 语义而非零值），用户只需覆盖要改的字段：

```cpp
class SamplerBuilder {
public:
    SamplerBuilder();                       // 默认：ClampToEdge + Linear + maxAnisotropy=1
    SamplerBuilder &filter(WGPUFilterMode mag, WGPUFilterMode min);
    SamplerBuilder &address(WGPUAddressMode u, WGPUAddressMode v, WGPUAddressMode w);
    SamplerBuilder &compare(WGPUCompareFunction cmp);   // 设了 compare 则必须是 Comparison 绑定
    SamplerBuilder &anisotropy(uint32_t a);
    wgpu::Sampler build(const wgpu::Device &) const;
};

class PipelineBuilder {                    // 默认 writeMask=All、sampleMask=0xFFFFFFFF、
                                           // alphaMode 不设空槽、blend 显式选择
public:
    PipelineBuilder(wgpu::Device, WGPUTextureFormat format);
    PipelineBuilder &blend(BlendMode mode); // 枚举驱动，杜绝手写 blend 状态错配
    PipelineBuilder &depth(WGPUCompareFunction, bool write);
    PipelineBuilder &sampleCount(uint32_t count);
    PipelineBuilder &cull(WGPUCullMode);
    wgpu::RenderPipeline build() const;
};
```

- `BindGroupLayoutBuilder` 承担 12/15 条 entry 的构造，按 `GpuShader` 的 `isMesh3D` /
  `isGbuffer` / `isShadow` 自动选绑定集合。
- **阻挡**：`writeMask`/`sampleMask`/`alphaMode` 的"默认丢帧"问题从源头消失——Builder 构造时
  就是正确值；`compare` 与绑定类型不一致时 `build()` 抛 `Exception`。

### 4. 空实现 stub 改为"明确拒绝"

对标 VKBuilder"不可用即不可调"，把 WebGPU 里 `setMesh3DSSAO` / `beginDecalPass` /
`drawDecal` / `endDecalPass` 这类空实现改为：

- 保持 `supportsGBufferPost()==false` / `supportsDecal()==false` 不变（上层本就不该调）；
- 若仍被调（误用），抛 `Exception("WebGPU: N 不受支持")` 而不是静默 `(void)`。
- 这属于**行为收紧**，需同步检索引擎上层是否真的会调这些（`RenderSystem3D` 等按
  `supportsX()` 分流）。若存在无条件调用，先修上层分流再收紧，避免回归。

## 实施顺序（PR 粒度，每个 PR 独立可编译可测）

1. **PR1：能力与工具层**（`wgpu_types.h`、`Capabilities`、`SamplerBuilder`）——纯新增，
   无行为变化；`Graphics` 先不消费。
2. **PR2：`InitFlow` 初始化状态机**——替换 `initWithWindow` 内部状态；对外 `Graphics`
   公共方法签名不变；浏览器/native 双端回归。
3. **PR3：`PipelineBuilder`/`BindGroupLayoutBuilder`**——替换 `make2D*/makeMesh3D*` 等
   手写 descriptor；覆盖默认值陷阱。
4. **PR4：空实现 stub 收紧**——`SSAO`/`decal` 明确抛错，先修上层分流。
5. **PR5：清理**——移除 Graphics.cpp 剩余 `reinterpret_cast` 与零初始化遗留；确认
   `rg -n reinterpret_cast` 在 `graphics/webgpu` 归零（imgui_impl_wgpu 单列是否纳入）。

## 实施记录（2026-08-26）

- **PR1 完成**：新增 `wgpu_types.h`、`Capabilities.h/.cpp`、`SamplerBuilder.h/.cpp`。
  `SamplerBuilder::build` 直接构造 `wgpu::SamplerDescriptor`（零初始化即 WebGPU 默认，
  无 reinterpret_cast）；`fromEngine` 精确复刻 `Graphics::makeSampler` 语义。
- **PR2 完成**：新增 `InitFlow.h/.cpp`，把 `Instance→Adapter→Device` 的异步 request +
  ProcessEvents 忙等封装成类型链（`InstanceDone→AdapterDone→DeviceDone`），跳步即编译失败。
  删除 `createInstanceAndAdapter/requestDevice/waitForAdapter/waitForDevice` 及
  `adapterReceived/deviceReceived/adapterError/deviceError`，改为持有 `Capabilities caps`。
- **PR3 完成**：新增 `PipelineBuilder`/`BindGroupLayoutBuilder`，直接构造 C++ wrapper
  descriptor（默认即合法）。已迁移 8 个 bind-group-layout 函数、7 个 pipeline-layout 函数、
  3 个 2D pipeline 函数、`makeWgslModule`。
- **PR4 取消（已过时）**：dev 分支的 SSAO / decal 已是真实实现（`supportsGBufferPost()` /
  `supportsDecal()` 均返回 true，`setMesh3DSSAO`/`beginDecalPass`/`drawDecal` 有实质逻辑），
  无空实现 stub 可收紧。
- **PR5 部分完成**：`reinterpret_cast` 从 127 处降到 114 处（bind-group-layout /
  pipeline-layout / 2D pipeline / shader module 已归零）。剩余 114 处为 texture/buffer/
  bindgroup/renderpass 的 C→C++ descriptor 转换，ABI 稳定但改动面大，留待后续 PR 分批
  迁移并需在 webgpu-native 构建上回归。

### 后续待办（PR5 剩余）

- 迁移 `TextureDescriptor` / `BufferDescriptor` / `BindGroupDescriptor` /
  `RenderPassDescriptor` / `TexelCopy*` 的 `reinterpret_cast` 到 C++ wrapper descriptor。
- 迁移后必须在 `build/webgpu-native`（Dawn）与 `build/webgpu-web`（Emscripten）上回归
  parity 测试（`2026-08-24-webgpu-vulkan-render-parity.md`）。

### 编译验证（2026-08-26，Windows native Dawn）

`build/webgpu-native`（`BUILD_PLATFORM=webgpu`，MSVC Debug + Ninja）配置并构建成功：

- 配置需初始化 `external/*` submodules；本机 `EVENGINE_COMPILER_CACHE`（sccache）缓存损坏，
  以 `-DEVENGINE_COMPILER_CACHE=OFF` 关闭后正常。
- 编译期修 3 处错误：
  1. `sv()` 与 Graphics.cpp/Canvas.cpp 的匿名命名空间 `sv(const char*)` 重载歧义（C2668）
     → 删除本地重复的 `sv`，统一用 `wgpu_types.h`。
  2. `Capabilities.cpp` 丢失 `kDefaultSampleCount` → 恢复匿名命名空间常量。
  3. `PipelineBuilder::colorTarget/depth` 参数类型 C 枚举 `WGPUTextureFormat` 与 C++ 枚举
     `wgpu::TextureFormat` 无隐式转换（C2664）→ 改为接收 `WGPUTextureFormat` 内部
     `static_cast`。
- `EVGraphics` 模块与 `eve.exe`（70.1 MB）均成功编译链接（1633/1633）。

## 测试与回归

- 每个 PR 跑 `make test/linux-debug FILTER=webgpu*`（native Dawn）与浏览器构建
  `build/webgpu-web` 冒烟（`scripts/smoke_examples.sh`）。
- 关键验证点：
  - `InitFlow` 非法顺序为编译失败（用 `static_assert`/`delete` 明确"不可调"的 API）。
  - 默认值：不设 blend 的 `PipelineBuilder` 产生 Alpha 而非零掩码（对比提交前的黑屏复现）。
  - 能力：MSAA=4 场景在 `supportsSampleCount(4)==false` 的软渲设备上不崩、降级到 1。
  - 空实现 stub 收紧后，`RenderSystem3D` 在 decal/SSAO 关闭时不再触发。

## 明确不做（YAGNI）

- 不改 `eve::graphics::Graphics` 公共接口 / 脚本绑定 / 渲染结果。
- 不做 WebGPU→Vulkan 的统一抽象（现有 Vulkan 走 vkbuilder、WebGPU 走本层，各自保持）。
- 不把 `imgui_impl_wgpu.cpp` 纳入重构（第三方风格，保持独立）。
- 不引入跨模块新头依赖（严格按 AGENTS.md 分层）。

## 成功标准

1. `src/modules/graphics/webgpu` 中 `reinterpret_cast` 归零；`WGPU*Descriptor` 手写零初始化
   归零。
2. 不可行的初始化顺序、越能力请求、默认掩码陷阱在编译期或类型层面被阻挡。
3. 空实现 stub 不再静默吞调用；误用即确定性 `Exception`。
4. WebGPU 与 Vulkan 在既有 parity 测试上仍通过（`2026-08-24-webgpu-vulkan-render-parity.md`）。