# GPU-Driven 渲染改造：完整路线设计（阶段 0/1 详细 + 阶段 2/3/4）

日期：2026-08-19
状态：设计

## 背景

EVEngine 主渲染路径（`RenderSystem3D::render` → `vulkan::Graphics`）是 CPU 驱动的
immediate-mode：

- 每帧按 pass（shadow 3 级联 → GBuffer → forward/hair）遍历 ECS，CPU 做可见性检查、
  距离 LOD（`meshForDistance`）、模型矩阵计算。
- 每个 draw 在 CPU 上完整走一遍状态装配：复制 ~2KB 的 `Mesh3DUBO`（每 draw 一份
  host-visible UBO + `updateLocal`）、`mesh3dSetFor` 分配/写一套 descriptor set、
  `bindPipeline` + `bindDescriptorSets` + 按 mesh 绑 vertex/index buffer，然后
  `drawIndexed`。
- 每个 mesh 独立 vertex/index buffer，没有 GPU 可读的 mesh 表（bounds / indexRange）。
- 每个 texture 一个独立 descriptor set（`texSetLayout`），材质参数靠 `setMesh3D*`
  状态机逐 draw 切换，pipeline 状态爆炸。
- 一个有利的现状：shadow / GBuffer 已经走「CPU 列表延迟录制」
  （`shadowPassDraws` / `gbufferPassDraws` → `recordPendingShadowPasses` /
  `recordPendingGBufferPass`），forward 才是直接往 command buffer 里录。

目标形态见 `docs/dev/VIRTUAL_GEOMETRY.md` 的远期路线与
`docs/dev/3D渲染管线.md` 的现有数据流。本文档覆盖完整路线：

- **阶段 0（地基）**：bindless 资源模型 + GPU 资源表 + 每帧 GPU arena。渲染结果与现状
  完全一致，仍逐 draw，但不再有 per-draw descriptor 分配。
- **阶段 1（CPU 驱动 indirect draw）**：把不透明主几何的发射从逐 draw
  `vkCmdDrawIndexed` 换成 CPU 生成 `VkDrawIndexedIndirectCommand` 数组 +
  `vkCmdDrawIndexedIndirectCount`。剔除仍留在 CPU，但所有数据布局已经是
  「GPU 可写」的形态，为阶段 2（GPU cull）铺路。
- **阶段 2（GPU 剔除 + HZB）**：详细设计见文末。
- **阶段 3（visibility buffer + 材质解析 + VirtualGeometry 并入）**：详细设计见文末。
- **阶段 4（远期）**：设计要点见文末。

阶段 0/1 的接口预留（indirect / instance 由 GPU 写、bindless Set1、帧 slot 语义）
在阶段 2/3 中兑现，下文直接在这些接口上展开。

## 目标

1. 消除 per-draw 的 descriptor set 分配/写入、per-draw UBO 拷贝、per-draw 材质状态机
   切换。
2. 不透明主几何的 draw 发射改为 indirect + instancing（同 mesh 同材质合并一次 draw）。
3. 建立 GPU 侧唯一事实来源：instance / mesh / material 三张表 + bindless 资源数组。
4. 渲染结果与旧路径一致（同一套 shader 语义，仅绑定与发射方式变化）。
5. 全部改动可回退：能力探测失败或 `EVENGINE_GPU_DRIVEN=0` 时走旧路径，行为不回归。
6. 接口上为阶段 2 预留：indirect command / count 由 GPU 写、instance 由 GPU cull 写，
   阶段 1 的 CPU 生成路径只是「同一 buffer 的另一个写入者」。

## 非目标

- 不做 GPU 剔除 / HZB（阶段 2）。
- 不做 visibility buffer / 材质延迟解析 / VirtualGeometry 并入（阶段 3）。
- 不合并 mesh 顶点/索引到共享大 buffer（阶段 2/3 与 VG 一起评估）。
- 不迁移 hair / 半透明 / X-ray / 自定义 mesh shader / offscreen canvas 到新路径，
  这些永远保留旧 `drawMeshShader` 路径。
- 不做多线程录制（并行渲染由他人负责）；但阶段 0 的每帧 arena 与阶段 1 的
  「帧数据包」接口即为其预留 seam。
- 不引入 Vulkan 扩展依赖：所需能力（`drawIndirectCount`、descriptor indexing、
  nonUniformIndexing）在 Vulkan 1.2 core，只需显式 enable feature。

## 方案总览

```
RenderSystem3D::render
  │  collectInstances()：遍历 ECS 一次 → GpuInstance[] + 排序 key（CPU）
  │  buildIndirect()：按 (pipeline, material, mesh) 分桶 → IndirectCommand[] + count
  ▼
FrameArena（per-frame-slot 环形 buffer）
  ├── instance buffer（SSBO）        ← 阶段 2 由 cull compute 改写
  ├── indirect command buffer        ← 阶段 2 由 cull compute 生成
  ├── indirect count buffer          ← 阶段 2 由 cull compute 写
  └── 动态 UBO / push 兜底
  ▼
Bindless Set1（只读，帧内不变）
  ├── texture array（combined image sampler，descriptorCount = kMaxBindlessTextures）
  ├── mesh 表（SSBO）
  ├── material 表（SSBO）
  └── instance / indirect（SSBO，每帧按 slot 旋转）
  ▼
录制：每 pass（shadow / gbuffer / forward opaque）
  一次 bindPipeline + 一次 bindDescriptorSets + 每 bucket 一次
  vkCmdDrawIndexedIndirectCount
```

## 阶段 0 详细设计：bindless + 资源表 + 帧 arena

### 0.1 GpuDrivenCaps 能力探测

在 `vulkan::Graphics::initWithWindow` 的 physical device 阶段
（`src/modules/graphics/vulkan/Graphics.cpp` 中 `getFeatures()` 附近）增加统一探测，
结果存成员 `GpuDrivenCaps caps_`：

```cpp
struct GpuDrivenCaps {
    bool drawIndirect         = false;  // VkPhysicalDeviceFeatures
    bool drawIndirectCount    = false;  // Vulkan 1.2 core（原名 VK_KHR_draw_indirect_count）
    bool multiDrawIndirect    = false;
    bool descriptorIndexing   = false;  // VK_EXT_descriptor_indexing（1.2 core）
    bool runtimeDescriptorArray = false;
    bool shaderSampledImageArrayNonUniformIndexing = false;
    bool shaderStorageBufferArrayNonUniformIndexing = false;
    bool descriptorBindingPartiallyBound = false;
    bool descriptorBindingSampledImageUpdateAfterBind = false;
    bool descriptorBindingStorageBufferUpdateAfterBind = false;
    bool bufferDeviceAddress  = false;  // 可选，暂不依赖
    bool gpuDrivenAvailable() const { /* 以上全部为真 */ }
};
```

启用方式：`vk::PhysicalDeviceFeatures2` + `vk::PhysicalDeviceVulkan12Features`
（或等价 `VkPhysicalDeviceFeatures2` 链）。VKBuilder 当前只开了
`samplerAnisotropy`（[Graphics.cpp:434](C:/Users/xiaofans/.codex/worktrees/2656/EVEngine/src/modules/graphics/vulkan/Graphics.cpp:434)），
需要把 feature 链传进 `phys.createDevice()`。

**回退**：`gpuDrivenAvailable() == false` 时 `RenderControl::isEnabled("gpuDriven")`
恒为 false，走旧路径；新接口全部 no-op。

### 0.2 Bindless 资源 Set（新增 `bindlessSetLayout`）

新建第二个 descriptor set 布局，与现有 per-frame set 分离（推荐两 set 模型，替代现有
`mesh3dSetLayout` 七 binding 逐 draw 更新）：

| Set | 内容 | 更新频率 |
|---|---|---|
| 0（`frameSetLayout`，保留/精简现有 `mesh3dSetLayout`） | camera/viewProj、clip、光照（沿用 `Lighting3DPack` / clustered SSBO）、shadow UBO、CSM array | 每帧或每 pass 一次 |
| 1（`bindlessSetLayout`，新增） | texture array、mesh 表、material 表、instance buffer、indirect buffer | 纹理/资源注册时；每帧按 slot 旋转 instance/indirect 绑定 |

`bindlessSetLayout` 绑定（GLSL 侧 `set = 1`）：

| Binding | 类型 | descriptorCount | 说明 |
|---|---|---|---|
| 0 | `combined image sampler` 数组 | `kMaxBindlessTextures`（默认 4096） | 所有 `GpuTexture` 注册进数组；`descriptorBindingPartiallyBound` + `descriptorBindingSampledImageUpdateAfterBind` |
| 1 | SSBO 数组 | `kMaxBindlessSsbo`（默认 64） | mesh 表 / material 表 / instance（每帧槽各占一项）/ indirect |
| 2 | SSBO（非数组，instance 动态基址） | 1 | 阶段 1 用 dynamic offset 或绑定对应 slot 的数组元素 |

实际布局在实现时收敛：**推荐 Set1 binding 1 用 SSBO 数组，元素 = 资源表类型**（mesh 表
下标 0、material 表下标 1、instance slot 2..2+frames-1、indirect slot 之后），shader
按约定下标访问。纹理数组使用 `nonUniformIndexing` 访问（`textures[nonuniform(idx)]`），
shader 侧对越界索引做 `clamp` 防御（`descriptorBindingPartiallyBound` 下空槽必须指向
占位资源）。

创建/销毁策略：
- 一个 `GpuTexture` 数组（vector 槽位），`GpuTexture::bindlessIndex` 保存注册序号；
  空槽填 `whiteTexture` 占位。
- 纹理销毁/重建时复用槽位或惰性追加（简单起见先追加，容量到顶后复用释放槽）。
- Set1 采用 `UPDATE_AFTER_BIND`：`newTexture` 在帧外/帧内都能注册，已绑定的 command
  无需重绑。若目标设备不支持 update-after-bind（回退项），则在每帧录制前整体重建
  Set1（纹理数量不大时成本可接受），并在 caps 里记录。

### 0.3 GPU 表：mesh / material

**Mesh 表**（只读 SSBO，std430）：

```glsl
struct GpuMeshRecord {
    vec4  boundsCenterRadius; // 模型空间包围球（cull 预留，阶段 2 使用）
    uint  vertexOffset;       // 顶点池偏移（阶段 0 为 0，per-mesh buffer）
    uint  vertexCount;
    uint  indexOffset;        // 阶段 0 为 0（per-mesh buffer 用 firstIndex=0）
    uint  indexCount;
    uint  indexType;          // 0 = u16，1 = u32
    uint  firstIndex;         // == indexOffset / indexType 尺寸；直接可填 VkDrawIndexedIndirectCommand
    uint  vertexBase;         // == vertexOffset
    uint  pad0;
    uint  pad1;
}; // 48B，便于 16B 对齐
```

`GpuMesh` 增加 `uint32_t gpuRecordIndex` 与（可选）CPU 侧 `GpuMeshRecord record_`；
上传 mesh 时（`uploadGpuMesh` / `uploadGpuMesh16`）计算记录并写入 mesh 表 buffer。
表 buffer 按帧槽双缓冲，但 mesh 表内容帧间不变——用 `gpuIdle` 槽上传一次即可，
只需在 `waitForSharedGpuResources` 之后更新（与现有 morph 更新同规则）。

**Material 表**（只读 SSBO，std430）：

```glsl
struct GpuMaterialRecord {
    vec4  tint;               // rgba
    vec4  pbr;                // x=metallic, y=roughness, z=receiveShadow, w=receiveLight
    vec4  texBomb;            // x=cellScale, y=strength, z=rotAmount
    vec4  parallax;           // x=scale, y=minLayers, z=maxLayers
    uvec4 textureSlots;       // xyzw = albedo / normal / height / env 的 bindless 下标（占位 slot 用 kInvalidTexture）
    uint  shadingModel;       // 0=pbr, 1=unlit, 2=hair, 3=custom
    uint  flags;              // castShadow / castOcclusion / ...
    uint  pad0, pad1;
}; // 96B
```

`Material` 保持现有脚本 API 不变，新增内部 `uint32_t gpuRecordIndex`；`set*` 方法标记
dirty，帧开始 `syncMaterialTable()` 全量/增量上传（材质数量少，全量上传最简单）。
`Material::bind(gfx)` 保留给旧路径。

### 0.4 FrameArena（每帧 GPU 分配器）

新类 `FrameArena`（`src/modules/graphics/vulkan/FrameArena.h/.cpp`），按 swapchain
帧槽（沿用 `kAsyncResourceCopies = 2`，阶段 2 可提到 3）旋转：

```cpp
class FrameArena {
public:
    struct Alloc { vk::DeviceSize offset; void *mapped; };
    Alloc alloc(vk::DeviceSize bytes, vk::DeviceSize align = 16);
    void reset();                     // 帧开始
    vk::Buffer buffer() const;
    vk::DeviceSize capacity() const;
private:
    vkb::GenericBuffer buf_;          // host-visible coherent（阶段 1 后期评估 device-local）
    vk::DeviceSize head_ = 0;
    std::vector<Alloc> live_;         // 调试：本帧分配清单
};
```

用途（阶段 1 起）：instance 数据、indirect command、count、以及将来 GPU cull 的输出
（cull 结果写入独立 buffer，由 cull pass 的原子计数器驱动，不属于 CPU arena）。

约束：
- 帧内不 realloc、不 shrink；容量不足时截断 + 告警（不抛异常，避免渲染中崩溃），
  截断策略：instance 超限丢弃多余实例并统计；indirect 超限停止追加。
- 分配在帧开始 reset；`Graphics::begin3DFrame` 内调用 reset 的时机必须晚于上一个
  占用该 slot 的帧 submit（现有帧槽机制已保证）。
- arena buffer 一个 pass 内只读，写只在帧开始（CPU 侧一次性 memcpy），避免半帧写入
  被 GPU 读到。

### 0.5 阶段 0 的落地顺序与验收

落地顺序（每步保持旧路径可用）：
1. caps 探测 + feature 启用。
2. `FrameArena` + mesh 表（先只建表，不消费）。
3. bindless Set1 + `GpuTexture::bindlessIndex` 注册，新增
   `createBindlessSetLayout()`；旧 `mesh3dSetFor` 保留。
4. material 表 + `syncMaterialTable()`。

验收：
- 新增纯 CPU 单测：`GpuMeshRecord` / `GpuMaterialRecord` 布局 static_assert（与 GLSL
  std430 一致）、mesh 表记录生成、bindless 槽位注册/复用、arena 分配对齐与回收。
- GPU 端到端：现有 graphics 测试全绿（渲染路径未切换，行为应零变化）。
- 运行时统计：`descriptorPool` 消耗不再随 draw 数增长（mesh3d 路径已验证切到 bindless
  后每帧分配集合数恒为常量）。

## 阶段 1 详细设计：CPU 驱动 indirect draw

### 1.1 实例收集（`RenderSystem3D::render` 重构）

新增收集阶段，只遍历 ECS 一次，替换 shadow / gbuffer / forward 三个 per-entity 循环：

```cpp
struct InstanceCollector {
    std::vector<GpuInstance> instances;   // CPU 镜像
    std::vector<SortKey>     keys;        // 与 instances 一一对应
};
InstanceCollector collectInstances(const Camera3D::Data &cam);
```

规则（与现有逻辑等价）：
- 可见性：`mr->visible`、xray 剔除（`xrayHighlight` 的实体**不进** GPU-driven 路径，
  仍走旧 `drawMeshShader`，因为需要二次绘制 + 场景深度采样）。
- LOD：沿用 CPU `meshForDistance`（阶段 2 才下沉），结果写 `instance.lod` / meshId。
- parts：`usesParts()` 的实体每个 part 一个实例（materialId 取自 part 或兜底
  `mr->material`）。
- hair / 半透明材质（`effectiveHair()`）不进 GPU-driven，走旧路径（透明排序保留）。
- `mr->camera` 非 default 的实体：阶段 1 仍走旧路径（多相机裁剪语义复杂，先不迁移）。
- 每个实例的 `flags` 记录 `castShadow` / `receiveShadow` / `castOcclusion` 等，供
  shadow / gbuffer 分 pass 过滤。

输出按 pass 分解（一次收集、三次投影）：
- shadow：过滤 `!castShadow`；LOD 可沿用主视图结果（阶段 1 简化，文档注明）。
- gbuffer：过滤 xray / hair / `!visible`。
- forward opaque：过滤 hair / xray。

### 1.2 Indirect 命令生成

新组件 `IndirectBuilder`（`src/modules/graphics/vulkan/IndirectBuilder.h/.cpp`），纯 CPU
可单测：

```glsl
// 与 VkDrawIndexedIndirectCommand 同布局（std430 / 20B）
struct GpuIndirectCommand {
    uint indexCount;      // 来自 mesh 表
    uint instanceCount;   // 该 bucket 合并的实例数
    uint firstIndex;      // 来自 mesh 表
    uint vertexOffset;    // 来自 mesh 表
    uint firstInstance;   // 该 bucket 在 instance buffer 中的基址
};
```

排序 key（CPU 生成，64 位）：

```
bit 63..56  pipelineId（shadow / gbuffer / forward / clustered / alphaCutout …）
bit 55..40  materialId（16 位）
bit 39..24  meshId（16 位）
bit 23..0   instance 序号（稳定序，避免 GPU/CPU 排序不一致）
```

生成算法：
1. 按 pass 过滤实例 → 按 key 稳定排序。
2. 连续区间内 `(pipelineId, materialId, meshId)` 相同 → 合并为一条 command，
   `instanceCount = 区间长度`，`firstInstance = 区间起始实例下标`。
3. 每条 command 同时写入 CPU 侧调试元数据（pipelineId/materialId/meshId），供
   `rtDraw` / 统计。
4. 每 pass 输出：`std::vector<GpuIndirectCommand>` + `uint32_t drawCount`。

### 1.3 录制路径

`vulkan::Graphics` 新增 GPU-driven 录制入口（旧接口不动）：

```cpp
// 帧数据包：CPU 侧已上传 instance buffer，indirect 命令已写入 arena
struct GpuDrivenFrameData {
    FrameArena *arena;                 // 已 reset + 写入
    vk::Buffer instanceBuffer;         // 或 arena 内 offset
    vk::Buffer indirectBuffer;         // 每 pass 的 command 数组
    vk::Buffer countBuffer;            // 每 pass 的 drawCount
    uint32_t   maxDraws[PassCount];
};
void beginGpuDrivenOpaque(const GpuDrivenFrameData &data);
void drawGpuDrivenPass(PassId pass, vk::Pipeline pipeline);   // 内部 per-bucket 循环
void endGpuDrivenOpaque();
```

对应三个录制点：
- `recordPendingShadowPasses`：shadow bucket 循环（alpha-cutout 仍是独立 pipeline
  bucket，pipeline 切换次数 = bucket 级，而非 draw 级）。
- `recordPendingGBufferPass`：gbuffer bucket 循环。
- forward：`drawMeshShader` 保留旧路径；新增 `drawMeshIndirect` 分支，在
  `beginSceneColorRenderPass` 内每 bucket 一次
  `vkCmdDrawIndexedIndirectCount(cb, indirectBuffer, offset, countBuffer, countOffset,
  maxDraws)`（Vulkan 1.2 core）。`drawIndirectCount` 不可用时回退固定
  `drawCount` 的 `vkCmdDrawIndexedIndirect`。

每次调用前只 bind 一次 pipeline + 一次 bindless Set1 + 每帧 Set0；push constants 收敛为
per-pass 常量（如 shadow 级联的 `lightVP`、gbuffer 的 clip）。

### 1.4 Shader 改动

`mesh3d.vert`（及 clustered 变体）改为从实例/资源表取数据：

```glsl
layout(set = 1, binding = 1) readonly buffer Instances { GpuInstance instances[]; };
layout(set = 1, binding = 1) readonly buffer Meshes     { GpuMeshRecord meshes[]; };
layout(set = 1, binding = 1) readonly buffer Materials  { GpuMaterialRecord materials[]; };
layout(set = 1, binding = 0) uniform sampler2D textures[];

void main() {
    uint inst = firstInstance + gl_InstanceIndex;          // push/UBO 提供 firstInstance
    GpuInstance gi = instances[inst];
    GpuMeshRecord m = meshes[gi.meshId];
    // 顶点位置从 per-mesh vertex buffer 取（阶段 0 布局不变）
    // model = gi.model；mvp = frameUBO.viewProj * model
    // albedo = texture(textures[nonuniform(materials[gi.materialId].textureSlots.x)], uv);
}
```

注意：
- `firstInstance` 从 push constants 或 Set0 传入（bucket 的 command 里已带
  `firstInstance`，但 shader 需要知道当前 bucket 的基址；推荐放 push constants）。
- 纹理访问必须 `nonuniform()` 包裹，且对 `kInvalidTexture` 做 clamp 到占位纹理下标。
- shadow / gbuffer 顶点 shader 同样改为实例驱动（`drawMeshShadow` / `drawMeshGBuffer`
  的 push 结构保留但数据改从表读）。
- hair / custom / xray shader 不改（旧路径）。
- 改完 shader 后运行 `scripts/compile_graphics_*_shaders.py` 重新生成 SPIR-V。

### 1.5 开关与回退

- 编译/运行开关：`EVENGINE_GPU_DRIVEN`（默认开启但受 caps 约束）；
  `RenderControl::setEnabled("gpuDriven", …)` 脚本可控。
- 回退条件（任一）：caps 不足、`xrayHighlight` 或 `mr->camera` 实体存在（阶段 1
  简化）、pass 列表包含未迁移 pass、arena 截断发生。
- 回退语义：整个 opaque 主路径退回旧逻辑，hair/xray 本就旧路径；不允许「部分实例
  新、部分旧」的混合（避免可见性语义分叉）。

### 1.6 调试与统计

- GPU 侧统计 buffer（每 pass `uvec4`：drawCount / instanceCount / triangleCount /
  截断标志），帧末读回，暴露 `Graphics::getGpuDrivenStats()`。
- 现有 `eve::debug::rtDraw` 保留：bucket 级调用（每个 command 一条记录），用于
  callgraph 对比。
- 验证模式 `EVENGINE_GPU_DRIVEN_VALIDATE=1`：同帧同时生成旧路径 + 新路径的
  command 列表（不执行），CPU 断言 bucket 覆盖的实例集合与旧逐 draw 集合一致。

## 数据布局汇总（实现蓝图）

```glsl
// set = 0，每帧（沿用 mesh3dFrameUbo 语义精简）
layout(set = 0, binding = 0) uniform FrameUBO {
    mat4  viewProj;
    mat4  view;
    vec4  cameraPos;      // xyz = eye，w = roughness
    vec4  clip;           // x = near, y = far
    vec4  ambient;        // rgb = ambient
    Light3DGpu lights[Lighting3DPack::kMaxLights];
};
layout(set = 0, binding = 1) uniform ShadowUBO { /* 现有 ShadowUBO */ };
layout(set = 0, binding = 2) uniform sampler2DArray csm;

// set = 1，bindless 只读
layout(set = 1, binding = 0) uniform sampler2D textures[kMaxBindlessTextures];
layout(set = 1, binding = 1) readonly buffer GpuTables {
    GpuMeshRecord     meshes[];     // [0..meshCount)
    GpuMaterialRecord materials[];  // [meshCount..+materialCount)
    GpuInstance       instances[];  // [..+instanceCount)
    GpuIndirectCommand indirect[];  // 追加段（阶段 2 由 GPU 写）
};
```

> 说明：`GpuTables` 用单块 SSBO 还是多块由实现定，spec 只约束字段与语义；
> 推荐多块独立 SSBO（mesh/material 静态、instance/indirect 每帧旋转），便于
> 阶段 2 只重绑 instance/indirect 两个块。

## 文件改动清单

| 文件 | 改动 |
|---|---|
| `src/modules/graphics/vulkan/Graphics.h/.cpp` | caps 探测 + feature 链、`bindlessSetLayout`、`GpuTexture::bindlessIndex`、`GpuMesh` 记录、`FrameArena` 成员、indirect 录制入口 |
| `src/modules/graphics/vulkan/FrameArena.h/.cpp`（新增） | 每帧环形分配器 |
| `src/modules/graphics/vulkan/IndirectBuilder.h/.cpp`（新增） | 排序 key + bucket 合并 + command 生成（纯 CPU，可单测） |
| `src/modules/graphics/RenderSystem3D.cpp` | `collectInstances` + `buildIndirect` 替换三个 per-entity 循环；hair/xray/custom 保留旧路径 |
| `src/modules/graphics/Graphics.h` | 能力门控接口声明（`supportsGpuDriven()` 等），WebGPU 后端返回 false |
| `src/modules/graphics/shaders/mesh3d.vert/.frag`、`mesh3d_clustered.*`、`mesh3d_gbuffer.*`、`mesh3d_shadow.*` | 实例/表驱动 + bindless 纹理 |
| `src/modules/graphics/RenderControl.cpp` | pass 表新增 `gpuDriven` 特性位（`compile()` 顺序不变） |
| `scripts/compile_graphics_*_shaders.py` | 重新编译受影响的 SPIR-V |
| `test/` | 新增：间接命令生成器、表布局、bindless 注册、GPU 端到端对比 |
| `docs/dev/3D渲染管线.md` | 更新数据流（收集 → 表 → indirect） |

## 验收标准

### 功能
- 全部现有 graphics / ClassicScenes 测试通过（linux-debug + xvfb）。
- 同一场景 `EVENGINE_GPU_DRIVEN=0/1` 截图逐像素一致（允许 MSAA/AA 配置相同前提下的
  浮点深度差异，禁止可见性差异）。
- shadow / gbuffer / forward 三个 pass 均走 indirect 后，draw call 数：
  - N 实例同 mesh 同材质 → 1 次 indirect draw（N 实例）；
  - 1000 实例混合 mesh/材质 → draw call 从 1000+ 降到几十（bucket 数）。
- hair / xray / 多相机实体在开关切换下输出一致。

### 性能（CI 可观测项）
- `descriptorPool` 分配不再随 draw 数线性增长。
- 大场景（≥1000 实例）CPU 帧时间下降：每帧 per-draw 的 UBO 拷贝与 descriptor 写
  从 O(draw) 降到 O(bucket)。
- 记录 `rtDraw` 统计：pipeline 切换次数从 O(draw) 降到 O(bucket)。

### 回归保护
- `EVENGINE_GPU_DRIVEN=0` 下零行为差异（旧路径代码不删除，只加分支）。
- 新增单测在 CI 上运行（纯 CPU 测试不依赖 GPU）。

## 风险与决策记录

1. **descriptor indexing 兼容性**：MoltenVK / 部分移动 GPU 对
   `nonUniformIndexing` / `updateAfterBind` 支持参差。决策：caps 严格探测，任一缺失
   即回退；shader 内 `clamp` + 占位资源防御；`UPDATE_AFTER_BIND` 缺失时每帧重建
   Set1（纹理注册数不大，成本可接受）。
2. **mesh 池合并**：阶段 0/1 不做。现有 `updateMeshVertices` / `bakeMeshMorph` 直接
   覆盖 per-mesh buffer，合并池会与这些路径冲突；GPU cull 需要的只是「表」而非
   「池」。合并留到阶段 2/3 与 VG 一并评估。
3. **forward 的旧路径共存**：`drawMeshShader` 保留，GPU-driven 只在
   `RenderControl` 的 `gpuDriven` 位开启且场景满足迁移条件时生效。禁止混跑，避免
   双路径可见性语义分叉。
4. **实例上限**：`kMaxInstances = 65536`、`kMaxBuckets = 65536` 起步，arena 按此
   预分配（instance 80B × 64K ≈ 5MB，indirect 20B × 64K ≈ 1.3MB，每帧槽一份）。
   超限截断 + 统计，不抛异常。
5. **多线程渲染边界**：本设计保证「收集 → 上传 → 生成 indirect」在单线程顺序完成，
   产出是自包含的帧数据包；`FrameArena` 的 per-frame-slot 语义即未来录制线程的
   输入边界，不与其冲突。

## 阶段 2 详细设计：GPU 剔除 + HZB

### 2.1 目标

- 把阶段 1 的 CPU 可见性检查 + 距离 LOD 换成 compute：frustum cull、HZB 遮挡剔除、
  LOD 选择全部在 GPU 完成。
- GPU 直接生成 indirect command + count（阶段 1 的 CPU 生成路径退化为验证/回退）。
- 主视图与 shadow 各有一条剔除链；hair / xray / 透明仍走 CPU 旧路径。
- 渲染结果与阶段 1 的 CPU 剔除一致（同规则镜像），仅可见性计算地点变化。

### 2.2 现状约束（影响设计的事实）

- 主 `vulkan::Graphics` 目前**没有任何 compute pipeline 基建**：AO/outline 是 fragment
  pass，cluster 光照在 CPU 构建后直接上传 SSBO。阶段 2 必须先补 compute 基础设施。
- `gpgpu` 模块的 compute 是同步 `executeImmediately`（独立 command buffer + queue），
  `ComputeShader` 没有「录制进外部 command buffer」的接口（见
  `src/modules/gpgpu/ComputeShader.h`），不能直接用于帧内序列化。VG 实验当初正是因此
  无法并入主帧。阶段 2 起在主 graphics 模块内自建 compute 录制能力，gpgpu 保持
  CPU 侧实验/工具用途。
- HZB 的来源是 GBuffer 的 D32 `hwDepth`（`storeOp=Store`、`ShaderReadOnlyOptimal`，
  见 `docs/dev/3D渲染管线.md` §2）。forward 的 scene depth 是 `DontCare`，不能当 HZB
  源；因此 `gpuDriven` 开启时会**强制开启 gbuffer**（与现有 `ao`/`gi`/`outline`
  强制 gbuffer 的模式一致，见 `RenderControl::setEnabled`）。
- 现有 LOD 数据在 `Renderable3D::MeshRenderer` 上（`lodMeshes[4]` +
  `lodDistances[3]`，按实体而非按 mesh），GPU 侧需要一张 **LodGroup 表**承接，
  而不是塞进 mesh 表。

### 2.3 Compute 基础设施（graphics 模块内）

新增 `src/modules/graphics/vulkan/ComputePass.h/.cpp`：

```cpp
class ComputePass {
public:
    bool create(vk::Device device, vk::PipelineLayout layout,
                const std::vector<uint32_t> &spv, uint32_t localSizeX);
    void record(vk::CommandBuffer cb, uint32_t groupsX, uint32_t groupsY = 1,
                uint32_t groupsZ = 1) const;
    vk::Pipeline pipeline() const;
};
```

- pipeline layout 复用 bindless Set1 + 每帧 Set0（阶段 0 产物），compute 只新增
  绑定约定（`set=0` 帧数据、`set=1` bindless 表）。
- SPIR-V 用现有 `scripts/compile_graphics_*_shaders.py` 模式新增
  `compile_graphics_compute_shaders.py`，输出 `.spv` + `_spv.inc` 内嵌数组。
- 帧内录制位置：`begin3DFrame()` 之后、`recordPendingShadowPasses()` /
  `recordPendingGBufferPass()` 之前的 compute 区段统一录制，用 pipeline barrier
  （buffer → compute 写 → draw 读；image → HZB 采样）表达依赖，不额外 submit。

### 2.4 HZB 生成

**来源**：GBuffer D32 → `R32F` mip 链，per-frame slot 双缓冲（cull 读的是上一帧
生成的 HZB，本帧在 GBuffer 后重建）。

**pass**（每帧一次，compute）：

1. mip0：从 GBuffer D32 采样写 `R32F`（近 = 0）。
2. mipN：读 mipN-1，2×2 **max** 降采样（存块内最远深度，遮挡测试保守），边界钳制。
3. 总 mip 数 `ceil(log2(max(w,h))) + 1`，每级一张 view 放入一个
   `sampler2D` 数组（或独立 view + 显式 lod 采样，推荐数组 view）。

**数据**：

```glsl
layout(set = 1, binding = 3) uniform sampler2D hzbMips;  // 或独立 view 数组
```

**遮挡测试规则**（与主流一致）：

- 把实例包围球投影到屏幕得到屏幕 AABB，取覆盖的 HZB texel。
- 若包围球**最近点深度 > 覆盖 texel 的 HZB 值**（即完全在已渲染几何之后）→ 剔除。
- 用 2×2 HZB texel 保守测试避免单 texel 采样洞；边界外视为可见。

**注意**：一帧延迟的遮挡剔除在相机快速移动时会产生"上一帧可见、本帧被遮挡"的
一帧残留/闪烁，主流引擎同样接受；用「膨胀包围球 + 帧间保守」缓解，先不做。

### 2.5 剔除链（cull → compact → emit）

每 pass 一条链，共用同一套 compute shader 变体（frustum 类型 + 过滤 flag 不同）：

```
Pass A  cull：       每 instance 一个线程 → frustum(+HZB)+LOD → append 到 visible[]
Pass B1 count：      每 visible 一个线程 → 按 bucket 原子计数
Pass B2 prefix：     单 workgroup 对 bucket 前缀和 → compactedBase[]、totalVisible
Pass B3 compact：    每 visible 一个线程 → 拷贝/补丁实例到 compacted[]（lod 已选）
Pass C  emit：       每 bucket 一个线程 → visible>0 时写 GpuIndirectCommand + 计数
```

**实例链配置**：

| 链 | frustum | 过滤 | 用途 |
|---|---|---|---|
| main | 相机 viewProj | `visible && !hair && !xray && !castShadowOnly` | gbuffer + forward 共用（bucket 内 pipelineId 区分） |
| shadow×3 | 各级联 lightVP | `castShadow` | shadow 每级联一条（初期），后续可合并为单 pass 多 frustum |

**关键点：阶段 1 的 CPU 排序保留**。instance 数组每帧按 key 排序后上传，cull 按
实例下标顺序处理，visible 列表天然有序 → 无需 GPU 排序，compaction 后每个 bucket
是连续段，emit 每 bucket 一条 command 即正确。CPU 排序成本保留（O(N log N)），
但这是阶段 2 里唯一留在 CPU 的 O(N) 以上工作；2.8 的 GPU 排序是消除它的可选步骤。

**LOD 下沉**（LodGroup 表）：

```glsl
struct GpuLodGroup {
    uvec4 lodMeshes;   // x/y/z/w = lod0..lod3 的 meshId（0xFFFFFFFF = 无）
    vec4  lodDist;     // x/y/z = lod0→1, 1→2, 2→3 的切换距离；w 保留
    uint  lodCount;
    uint  pad0, pad1, pad2;
};
```

`GpuInstance` 增加 `lodGroupId`（复用原 `pad` 字段，阶段 1 布局向后兼容：
原 `flags` 不变，`pad` 改名为 `lodGroupId`）。cull 阶段按 `meshForDistance` 相同规则
选 lod，`compacted.meshId = lodMeshes[level]`。屏幕空间误差 LOD（VG 的
`errorRScreen` 方案）作为后续细化，不阻塞本阶段。

### 2.6 GPU 写 indirect 的 buffer 布局

与阶段 1 完全同布局（`GpuIndirectCommand` 20B + count `uint32`），但写入者换成
emit pass。CPU `IndirectBuilder` 保留为：

- 验证模式：同帧对比 GPU 生成的 command 与 CPU 生成的 command（实例集合一致）。
- 回退模式：caps 不足 / 统计异常时切回阶段 1 的 CPU 生成。

### 2.7 帧顺序（`RenderControl::compile` 不变，录制顺序扩展）

```
begin3DFrame
  ├─ compute 区段：HZB 重建（用上一帧 GBuffer）
  ├─ compute 区段：main 链（cull→compact→emit）
  ├─ compute 区段：shadow 链×3
  ├─ recordPendingShadowPasses   （间接 draw 读 shadow 链命令）
  ├─ recordPendingGBufferPass    （读 main 链命令，pipelineId 过滤）
  └─ beginSceneColorRenderPass
       ├─ forward opaque：读 main 链命令（pipelineId 过滤）
       └─ hair：旧 drawMeshShader 路径（不变）
```

注意：shadow 链的 cull 使用**上一帧**相机/灯光数据生成的 frustum（灯光一般静止，
相机相对灯光变化小，一帧延迟可接受；需要严格时 shadow 链放本帧 CSM 构建之后）。

### 2.8（可选）GPU 排序，消除 CPU 排序依赖

当 CPU 排序成为瓶颈或实例需要无序追加（动态场景）时，加 GPU 基数排序（key = 阶段 1
的 64 位 sort key）：

- 新增 `radixSort.comp`：对 instance 下标按 key 排序（LSD，4 趟 16bit，每趟
  count/prefix/scatter，本地 shared memory 优化）。
- 排序后 cull/compact 流程不变；CPU 侧只保留「收集实例」。

### 2.9 回退与验证

- 回退链：GPU 剔除异常（统计校验失败 / caps 缺失）→ 阶段 1 CPU 生成 → 旧逐 draw。
- 验证：`EVENGINE_GPU_DRIVEN_VALIDATE=1` 下 GPU 与 CPU 各生成一份 command 列表，
  断言可见实例集合与 LOD 选择逐帧一致；差异即报。
- 统计 buffer 每链 `uvec4`：visibleInstanceCount / bucketCount / triangleCount /
  截断标志，帧末读回。

### 2.10 数据布局更新

```glsl
struct GpuInstance {          // 阶段 1 布局，pad 改名为 lodGroupId
    mat4  model;
    uint  meshId;
    uint  materialId;
    uint  flags;              // castShadow / receiveShadow / castOcclusion / ...
    uint  lodGroupId;         // 0xFFFFFFFF = 无 LOD
};

// 新增 buffer（bindless Set1 追加 binding）
layout(set = 1, binding = 3) uniform sampler2D hzb;             // R32F mip 链
layout(set = 1, binding = 4) readonly buffer LodGroups { GpuLodGroup lodGroups[]; };
layout(set = 1, binding = 5) buffer Visible { uint visibleCounter; uint visibleIds[]; };
layout(set = 1, binding = 6) buffer Compacted { GpuInstance instances[]; };
layout(set = 1, binding = 7) buffer IndirectCmds {
    uint drawCount;                       // 每链独立
    GpuIndirectCommand commands[];
};
```

### 2.11 文件改动清单（阶段 2）

| 文件 | 改动 |
|---|---|
| `src/modules/graphics/vulkan/ComputePass.h/.cpp`（新增） | compute pipeline 封装 |
| `src/modules/graphics/vulkan/Graphics.h/.cpp` | compute 区段录制、HZB 资源（per-slot R32F mip 链）、cull 链状态、统计读回 |
| `src/modules/graphics/vulkan/Culling.h/.cpp`（新增） | 剔除链（cull/count/prefix/compact/emit）的纯逻辑 + GPU 提交 |
| `src/modules/graphics/shaders/hzb_build.comp`、`cull.comp`、`compact.comp`、`emit.comp`（新增） | 阶段 2 compute shader |
| `src/modules/graphics/RenderSystem3D.cpp` | 实例收集保留，移除 CPU 可见性/LOD 分支（或保留作回退）；`lodGroupId` 填充 |
| `scripts/compile_graphics_compute_shaders.py`（新增） | compute SPIR-V 编译 |
| `test/` | 纯 CPU：LodGroup 选择镜像测试、compact/emit 逻辑测试；GPU：可见集合 parity |

### 2.12 验收标准（阶段 2）

- GPU 剔除 vs CPU 剔除：随机场景 200 帧可见实例集合一致（`VALIDATE` 模式零告警）。
- 1000+ 实例、50% 在 frustum 外的场景：CPU 侧剔除时间归零（无 per-instance 循环），
  总帧时间下降且随遮挡率增大收益增大。
- HZB 遮挡有效：大遮挡场景 draw 数比 frustum-only 进一步下降；无可见性错误
  （背对相机的被遮挡几何不出现）。
- shadow 三级联输出与阶段 1 一致（允许一帧延迟导致的边界差异，记录在案）。

### 2.13 风险与决策（阶段 2）

1. **一帧延迟遮挡**：相机快速运动时 HZB 滞后 → 闪烁/残影风险。决策：接受（主流
   一致），膨胀包围球缓解；`VALIDATE` 模式只比对 frustum+LOD 规则，不比对遮挡。
2. **shadow 剔除成本**：3 条链 = 3×cull 开销。决策：初期接受（shadow 链可只做
   frustum，不做 HZB/LOD 细化）；后续合并为单 pass 多 frustum 剔除。
3. **compaction 双倍实例拷贝**：compacted buffer 每帧拷贝可见实例（80B/个）。
   决策：接受，容量按 `kMaxInstances` 预算；与 VG 并入时统一评估 scratch 复用。
4. **compute 与 draw 的同步**：全部在一个 command buffer 内用 barrier 表达，禁止
   额外 submit，否则丢帧序语义（与现有 shadow/GBuffer 延迟录制同规则）。

## 阶段 3 详细设计：visibility buffer + 材质解析 + VirtualGeometry 并入

### 3.1 目标

- 不透明几何全部写入 visibility buffer，材质计算后置到全屏 resolve，替代阶段 2 的
  forward 材质循环。
- 硬件光栅与 VirtualGeometry 软件光栅统一写同一套 vis 附件，resolve 一视同仁。
- VG 从独立实验变成主帧的一部分：cluster DAG 剔除 + 软件光栅录制进主 command
  buffer，共享本设计的 bindless Set1 / HZB / 帧 slot 语义。

### 3.2 帧顺序变化

```
begin3DFrame
  ├─ compute：HZB 重建
  ├─ compute：main 剔除链 + shadow 剔除链（阶段 2）
  ├─ compute：VG cluster 剔除（硬件/软件分流，见 3.7）
  ├─ recordPendingGBufferPass
  │    ├─ 硬件光栅（普通实例）：写 GBuffer 原有附件 + vis 附件
  │    └─ 硬件光栅（VG 大三角形）：同 pass，cluster 间接 draw
  ├─ compute：VG 软件光栅（小三角形）→ vis 附件（atomic 深度合并）
  ├─ beginSceneColorRenderPass
  │    ├─ resolve（全屏）：vis → scene color（opaque）
  │    └─ hair / 透明：旧 drawMeshShader（depth 来源见 3.5）
  └─ AO / outline / FXAA 等后处理不变
```

### 3.3 Visibility buffer 布局

GBuffer 在现有 4 个附件（normal / depthColor / albedo / hwDepth）基础上增加：

| 附件 | 格式 | 内容 |
|---|---|---|
| `visID` | R32G32UI | x = geometryId（普通实例 = compacted 实例下标；VG = cluster 全局 id，高位打 type 标记）；y = triangleId |
| `visBary` | R16G16F | 重心坐标（u, v；w = 1-u-v 重建） |

深度仍用现有 `hwDepth`（硬件光栅写；VG 软件光栅用 `atomicMin` 合并到同一 D32 不可行，
见 3.7 的深度合并方案）。`visID` 初始化清 `0xFFFFFFFF`（空像素），resolve 跳过。

### 3.4 硬件光栅写 visID

- 普通实例：fragment shader 输出 `geometryId = firstInstance + gl_InstanceIndex`、
  `triangleId = mesh.firstIndex + gl_PrimitiveID`；重心坐标由顶点 shader 输出
  per-vertex 常量（(1,0,0)/(0,1,0)/(0,0,1)）插值得到。
- GBuffer 顶点/片元 shader 增加 vis 输出变体（`mesh3d_gbuffer_vis.*`），旧
  `mesh3d_gbuffer.*` 保留（ao/gi 不需要 vis 时仍可用）。

### 3.5 Material resolve（延迟材质解析）

新增 `resolve_vis.frag`（全屏三角形，跑在 scene color pass 内）：

```glsl
// 每像素
uvec2 vis = texelFetch(visID, pixel);
if (vis.x == 0xFFFFFFFFu) discard;                 // 天空
vec2 bary = texelFetch(visBary, pixel).rg;

// 统一入口：普通实例与 VG cluster 都实现 fetchAttributes(geometryId, triId, bary)
// → pos/normal/uv（VG 走 cluster 位置流，普通实例走顶点池 SSBO）
GpuInstance inst = compacted[vis.x];               // 普通实例
MaterialRecord m = materials[inst.materialId];
// 复用 mesh3d.frag 的 PBR/IBL/CSM 光照代码（提取为共享 include pbr_shade.glsl）
```

- 阶段 3 的 resolve 需要**随机访问顶点属性** → 必须引入顶点池（见 3.6），这是对
  阶段 0/1「不合并 buffer」决策的正式修订：合并不再可选，而是 resolve 的前提。
- 深度：resolve 不写深度（GBuffer D32 已含 opaque 深度）。hair 的 depth 来源：
  scene color pass 开始时把 GBuffer D32 拷贝（或只读 depth attachment 复用）到
  scene color depth，hair draw 按现有深度语义测试。实现细节二选一，默认拷贝
  （`vkCmdCopyImage`，D32 → D32，render pass 外）。
- MSAA：scene color 保持 MSAA 时，resolve 用 sample-rate shading 在
  `gl_SampleID` 采样 vis 附件（vis 附件单采样，MSAA resolve 每 sample 读同一 vis
  像素即可）；hair 的 MSAA 行为不变。初始实现可要求 opaque GPU-driven 路径关闭
  MSAA（FXAA 兜底），作为简化项记录。

### 3.6 顶点池（修订阶段 0/1 决策）

- 新增全局顶点池（position + normal + uv 交错，`MeshVertex` 不变），buffer usage =
  `VERTEX_BUFFER | STORAGE_BUFFER`：硬件光栅当 VB 绑，resolve 当 SSBO 随机读。
- mesh 表增加 `vertexPoolOffset` / `indexPoolOffset`（阶段 0 的
  `vertexOffset/indexOffset` 变为真实池偏移）。
- 迁移策略：mesh 上传走池化分配；存量 mesh 惰性迁移（首次被 GPU-driven 使用/首次
  resolve 时入池）。`updateMeshVertices` / `bakeMeshMorph` 改走池内重写 +
  `waitForSharedGpuResources`（现有同步规则不变）。
- 池满：静态 mesh 与动态 mesh 分池（静态永不重写，可 device-local 优化）；动态池
  上限告警 + 回退。

### 3.7 VirtualGeometry 并入

**现状差距**（来自 `src/modules/virtualgeometry/`）：

- `vgUpdate` 走 gpgpu 同步 dispatch（`executeImmediately`），不录进主帧。
- buffer 归属单 renderer 自持（`VgState`），无共享几何层。
- 全软件光栅，无硬件/软件分流。
- 无 HZB 遮挡；vis buffer 只存 `depth<<16|clusterId`，无重心坐标/三角形 ID。
- resolve 只做 flat 可视化，不接材质。

**改造步骤**：

1. **compute 基建迁移**：VG 的 cull / raster shader 从 gpgpu 绑定迁到阶段 2 的
   `ComputePass` + bindless Set1（绑定号按 2.10 的约定扩展），cull/raster 录制进主帧
   compute 区段，同步靠 barrier。
2. **共享几何层**：新增 `src/modules/virtualgeometry/VirtualGeometryScene.h/.cpp`，
   持有 cluster pool（positions / triangles / clusters SSBO，跨 renderer 共享）、
   每帧 visible 列表、HZB 引用。`VirtualGeometryRenderer` 退化为薄门面（或由
   `VirtualGeometry` 模块脚本 API 直接操作共享层）。多 mesh 的 cluster 合并进同一
   池，`GpuMeshRecord` 增加 `vgAssetId` 索引。
3. **两路光栅分流**：cull 后按 cluster 的最大三角形投影面积分类（初期阈值 2px²，
   后续按三角形细分）：
   - 硬件路径：cluster 的三角形列表用顶点 shader 从 cluster 位置流取点（Vulkan 1.2
     无 mesh shader 时的标准替代：`gl_VertexIndex` 驱动 fetch，绑一个占位 VB 或
     不绑 VB），间接 draw 命令由 cull 的 emit 阶段生成（bucket = cluster）。
   - 软件路径：现 `vg_raster.comp` 语义保留，改为写统一 vis 附件 + `atomicMin`
     深度合并。
4. **深度合并**：软件光栅写 D32 需要 `atomicMin` 语义，D32 附件不支持原子写 →
   软件光栅先写 R32U 深度 buffer（packed 深度），帧末由 resolve 或专用 copy 与
   D32 合并；初期简化为软件光栅像素只写 vis 附件 + R32U 深度，resolve 里与 D32
   比较（resolve 已有逐像素流程，成本可接受）。
5. **HZB**：`vg_cull.comp` 增加 HZB 遮挡测试（现只有 frustum + 屏幕误差，
   见 [vg_cull.comp](C:/Users/xiaofans/.codex/worktrees/2656/EVEngine/src/modules/virtualgeometry/shaders/vg_cull.comp:20)）。
6. **vis 附件升级**：`vg_pack`（`depth<<16|clusterId`）废弃，改统一
   `visID/visBary` 布局；cluster 的三角形 id 与重心坐标进附件，材质 ID 由
   `vgAssetId → instance.materialId` 解析。
7. **resolve 统一**：`resolve_vis.frag` 对普通实例与 VG cluster 走同一套
   `fetchAttributes` 抽象（见 3.5）；VG 的 `vg_resolve` 实验 shader 退役。

### 3.8 混合场景规则

- 普通实例与 VG 资产可同帧共存：main 链 emit 的普通实例 bucket + VG 硬件 bucket +
  VG 软件列表按序录制，全部写同一 vis 附件，resolve 无感知。
- VG 资产属于不透明几何：hair / xray 仍旧路径；VG 的透明（无，cluster 无透明语义）
  不引入。
- `Renderable3D` 的 mesh 若已构建 VG asset，由 `mesh.gpuHandle` 旁的 `vgAssetId`
  标记，渲染时自动分流；无 asset 时普通路径。

### 3.9 数据布局更新

```glsl
// 统一 geometryId 编码（visID.x）
//  bit31      : 1 = VG cluster，0 = 普通实例
//  bit30..0   : compacted 实例下标 / 全局 clusterId

// GpuMeshRecord 追加
uint vgAssetId;      // 0xFFFFFFFF = 无 VG 资产
uint vertexPoolOffset;   // 顶点池偏移（取代阶段 0 的 vertexOffset 语义）
uint indexPoolOffset;
uint pad0;
```

### 3.10 文件改动清单（阶段 3）

| 文件 | 改动 |
|---|---|
| `src/modules/graphics/vulkan/Graphics.h/.cpp` | vis 附件 + resolve pipeline、顶点池、VG 录制集成 |
| `src/modules/graphics/vulkan/VertexPool.h/.cpp`（新增） | 顶点/索引池（静态/动态分池） |
| `src/modules/graphics/shaders/mesh3d_gbuffer_vis.*`、`resolve_vis.frag`、`pbr_shade.glsl`（新增/提取） | vis 写入与材质解析 |
| `src/modules/virtualgeometry/VirtualGeometryScene.h/.cpp`（新增） | 共享 cluster 层 + 帧内录制 |
| `src/modules/virtualgeometry/vulkan/VulkanVirtualGeometry.cpp` | gpgpu → ComputePass 迁移、HZB、vis 附件升级；`vgUpdate` 同步语义仅留测试 |
| `src/modules/virtualgeometry/shaders/vg_cull.comp` / `vg_raster.comp` / `vg_common.glsl` | 绑定迁移 + HZB + 新 vis 布局 |
| `scripts/compile_virtualgeometry_shaders.py` | 与新 compute 编译脚本对齐 |
| `test/` | VG 帧内端到端、resolve 与 forward 输出 parity、混合场景 |

### 3.11 验收标准（阶段 3）

- 普通实例场景：vis+resolve 路径与阶段 2 forward 输出逐像素一致（shading 同源：
  `pbr_shade.glsl` 共享）。
- VG 资产：cluster 场景（示例 `examples/virtualgeometry/demo.nut`）并入主帧后与
  独立实验输出一致（vis 区域覆盖、无洞），且与普通实例混合渲染无冲突。
- draw 统计：opaque 阶段 pipeline 切换次数降到常量级（硬件 bucket 数 + 1 resolve）；
  软件光栅只在 cluster 小三角形路径发生。
- 性能：大场景（远 LOD cluster + 大量实例）总帧时间显著低于阶段 2；CGI 验证
  `descriptorPool` 分配仍为常量。

### 3.12 风险与决策（阶段 3）

1. **软件光栅深度合并**：D32 无原子 → 临时 R32U 深度 + resolve 比较。风险：resolve
   带宽增加。决策：接受（初期），后续评估「软件光栅写 vis 后由硬件 depth prepass
   补深度」或专用 merge pass。
2. **顶点池迁移**：`updateMeshVertices` / morph 与池化冲突。决策：静态/动态分池 +
   惰性入池，动态池更新走现有 `waitForSharedGpuResources` 同步。
3. **MSAA**：初始 opaque GPU-driven 要求关闭 MSAA（FXAA 兜底），hair 不受影响；
   sample-rate resolve 作为后续细化。
4. **VG 的 gpgpu 依赖**：迁移后 `gpgpu` 模块仍保留（其它实验用），VG 不再依赖其
   同步 dispatch；旧 `VirtualGeometryRenderer` API 兼容层保留一个测试期。

## 阶段 4 详细设计（远期，设计要点）

### 4.1 GPU 排序（消除 CPU 排序）

- 实现 2.8 的 `radixSort.comp`（64 位 key，LSD 4 趟）。
- 收益：实例收集可无序追加（动态生成/流送直接追加），CPU 侧只剩收集与上传。
- 依赖：阶段 2 的 compact/emit 不变，排序结果喂 cull 或喂 emit。

### 4.2 Cluster DAG 流送（页面 + LRU）

- VG 资产拆页（如 64 cluster/页），按相机距离/可见性预测请求，transfer queue 异步
  上传，LRU 淘汰。
- `GpuMeshRecord` / cluster 表增加 `pageId` 与 resident 标记；cull 对非 resident
  页面走"占位 LOD"（父节点粗 cluster），避免缺页闪烁。
- 依赖：阶段 3 共享 cluster 层；与 `assets` 加载管线（`EveFileSystem`）衔接。

### 4.3 两阶段 indirect（实例 → cluster）

- 阶段 3 的 cull 是"每实例选 LOD cluster"；两阶段化后先实例级剔除（粗），再对
  每个可见实例做 cluster DAG 剔除（细，VG 的 `vg_cull` 即此），避免大场景下
  cluster 级遍历全量实例。
- 输出两级命令：实例 indirect（喂 cluster cull）+ cluster indirect（喂硬件/软件
  光栅）。

### 4.4 mesh / task shader 评估

- Vulkan 1.3 + `VK_EXT_mesh_shader` / MoltenVK 支持成熟后，评估用 task shader 做
  cluster 级 cull + mesh shader 做硬件光栅，替代 3.7 的"顶点 shader fetch + 软件
  光栅"混合路径。
- 决策点：跨平台（macOS MoltenVK）支持度不足时不迁移，维持 3.7 路径。

### 4.5 其它可选任务

- **Motion vector**：`GpuInstance` 增加 `prevModel`，resolve/后处理直接输出
  velocity buffer，为 TAA / 动态模糊铺路（与抗锯齿模块联动）。
- **visID 驱动的阴影**：shadow 只绘制 vis 附件中有像素的实例（读回可见集合或
  GPU 侧标记），省 shadow 带宽。
- **透明 GPU-driven**：hair/透明按深度排序 key（depth bucket + 材质 bucket）走
  indirect；设计在阶段 2 的 key 结构上扩展一位 depth 段，但不作为主线优先项。
- **GPU-driven 管线屏障**：用 pipeline barrier 最小化 + 阶段化录制，评估
  `VK_EXT_synchronization2` 简化依赖描述。

## 跨阶段依赖与顺序总表

| 阶段 | 依赖 | 核心产出 | 里程碑判断 |
|---|---|---|---|
| 0 | Vulkan 1.2 feature | bindless Set1、三张表、FrameArena | 旧渲染零变化，descriptor 分配恒定 |
| 1 | 0 | CPU 生成 indirect + instancing | draw call 降到 bucket 级，像素一致 |
| 2 | 1 | HZB + compute 剔除链 + GPU 写 indirect | 可见性计算零 CPU 成本，遮挡收益可测 |
| 3 | 2 | vis 附件 + resolve + VG 并入 + 顶点池 | 不透明几何完全 GPU-driven，VG 进主帧 |
| 4 | 3 | GPU 排序 / 流送 / 两阶段剔除 / mesh shader 评估 | 大世界 + 虚拟化几何 + 流送 |

> 里程碑建议：阶段 0+1 合并为一个 PR 交付；阶段 2 独立 PR（含 compute 基建）；
> 阶段 3 拆两个 PR（3.1-3.6 vis+resolve 先落地，3.7 VG 并入随后）。
