# 虚拟几何体渲染（Nanite 风格）设计文档

> 模块：`virtualgeometry`（Native Vulkan 后端）
> 目标：在 EVEngine 上实现类似 UE5 **Nanite** 的虚拟化几何体渲染技术——
> 基于 **cluster DAG（有向无环图）+ GPU 驱动的可见性裁剪 + 软件光栅化 + visibility buffer**，
> 参考 UE5 公开算法与演讲资料，同时贴合本项目（无 mesh/task shader、无间接绘制、Vulkan 1.2）的实际能力。

## 1. 参考依据（公开资料）

| 资料 | 说明 |
|------|------|
| UE5 官方博客《Nanite: 实时渲染的虚拟化几何体》 | 概述 Nanite 的目标：虚拟化几何体，只渲染屏幕上有细节的部分，配合流送。 |
| SIGGRAPH 2021《Rendering Nanite》演讲 | 核心技术：cluster DAG、两级裁剪、软件光栅化、visibility buffer、材质后期解析。 |
| UE5 4.26 发布演讲 / 相关内部资料 | 细节：`~128` 三角形 cluster、LOD 层级、屏幕空间误差阈值、HZB 遮挡裁剪。 |
| Meshoptimizer（zeux/meshoptimizer） | `meshopt_buildMeshlets` 公开实现的 meshlet 化算法，作为 cluster 生成的参考。 |
| Frostbite GDC 2015《Moving Fortnite to a Virtualized GPU Pipeline》 | 虚拟化几何体与 GPU 驱动的思想来源之一。 |

核心思想（来自 SIGGRAPH 2021《Rendering Nanite》）：
1. **几何体分解为 cluster DAG**：每个 cluster 约 128 个三角形，以层次化的方式组织成 DAG。
2. **两级 GPU 裁剪**：第一遍裁剪在 DAG 上做 frustum + 遮挡 + LOD（屏幕空间误差）测试，
   得到一个"可见 cluster 列表"；第二遍只对这些可见 cluster 做三角形裁剪。
3. **软件光栅化**：小三角形（<= 某像素阈值）在 compute shader 里逐像素软件光栅化，
   大三角形走硬件光栅化。两者都写入 **visibility buffer**（每像素存实例/cluster/三角形 ID + 重心坐标）。
4. **材质后期解析**：visibility buffer 用一个全屏 pass 解析成最终着色的 GBuffer/颜色，
   避免对不可见几何做材质计算。

本模块是这一思想的**教学级忠实实现**，用软件光栅化（compute）覆盖大小三角形，
从而无需 mesh shader / indirect draw，也天然契合本引擎现有（`gpgpu`）compute 能力。

## 2. 总体架构

```
┌────────────────────────────── CPU（预处理，一次性） ──────────────────────────────┐
│  Mesh（顶点/索引）                                                                │
│    │  ClusterBuilder                                                              │
│    │    · 贪心 meshlet 化（目标 ~126 三角形 / cluster，缓存友好共享顶点）            │
│    ▼                                                                              │
│  Cluster 集合（LOD0）                                                              │
│    │  LodBuilder                                                                  │
│    │    · 迭代合并（cluster 简化 + 几何误差 E）生成层次 DAG                          │
│    │    · 每级记录：包围球、父指针、误差半径 r、误差 r_screen 常量                     │
│    ▼                                                                              │
│  VirtualGeometryAsset（可序列化）                                                  │
│    · 位置流、cluster 三角形/顶点索引、包围球、层次节点、每级误差                        │
└───────────────────────────────────────────────────────────────────────────────────┘
                          │ 上传为 SSBO
┌────────────────────────────── GPU（每帧） ────────────────────────────────────────┐
│  Pass A  vg_cull.comp   在 cluster DAG 上做 frustum+遮挡+屏幕空间误差(LOD) 裁剪      │
│                         输出可见 cluster 列表（append buffer + 原子计数器）          │
│  Pass B  vg_raster.comp 对每个可见 cluster 软件光栅化三角形 → visibility buffer      │
│                         （uvec2 ID + int 深度，atomicMin 深度测试）                 │
│  Pass C  vg_resolve     全屏解析 visibility buffer → 颜色（着色 / 调试可视化）        │
└───────────────────────────────────────────────────────────────────────────────────┘
```

## 3. 数据结构

### 3.1 Cluster（CPU / GPU 一致布局）

```glsl
// GPU 端（std430）
struct VgCluster {
    uvec4 bounds;      // 包围球：x,y,z = 中心，w = 半径（用 float bit 打包）
    uvec2 triRange;    // 该 cluster 三角形在全局三角形流里的 [start, count)
    uvec2 vertRange;   // 顶点范围 [start, count)（局部索引 + 基偏移）
    uint lodLevel;     // 当前层次级别
    uint errorR;       // 简化误差（世界半径），用于屏幕空间误差计算
    uint errorRScreen; // 屏幕空间误差阈值常量（Nanite 做法，预计算避免每帧 sqrt）
    uint parent;       // 层次父节点（DAG 边），0xFFFFFFFF = 根
};
```

### 3.2 Visibility Buffer

```
pixel layout: uvec2 vgVisID   -> id.x = clusterIdx, id.y = triangleIdx
              uint  vgVisDepth -> 逆深度（1/z）整数化，用于 atomicMin 深度测试
              (重心坐标可选存 vec2/第三通道，用于材质插值)
```

软件光栅化时：
- 先把像素坐标变换到裁剪空间，求三角形的包围盒（可限制在屏幕内）。
- 对每个覆盖像素，用 2D 扫描线/重心测试求重心坐标。
- 深度用 `atomicMin(vgVisDepth, depthBits)` 做近处优先；同时用 `gl_InvocationID` 比较做 ID 写回。

### 3.3 层次 DAG

每个非叶节点是下一层若干 cluster 合并后的简化版本。可见性遍历从根开始：
- 若节点的屏幕空间误差 `<= 1px` 或包围球被遮挡 → 该分支停止（不再细化）。
- 否则下钻到子节点，直到叶（LOD0 全细节）。

屏幕空间误差计算（Nanite）：
```
screen_error_px = errorRScreen / (dist_to_sphere_center * proj_scale)
```
其中 `proj_scale = viewportHeight / (2 * tan(fovY/2))`，`dist` 是相机到 cluster 包围球中心的距离。
`errorRScreen` 在预处理阶段由世界空间误差半径 `r` 与投影参数折算。

## 4. 与 EVEngine 的集成

- 新模块 `virtualgeometry`（`create_module(EVVirtualGeometry virtualgeometry)`），
  复用 `gpgpu` 的 Vulkan compute 后端（`VulkanUtil`、`vkb::GenericBuffer`、`executeImmediately`）。
- 提供 `VirtualGeometry` Module 外观（`Module_REG`/`Module_IMPL` 注册进脚本命名空间，类名 `eve.VirtualGeometry`）。
- 预处理（CPU）由脚本调用 `vg.build(mesh, maxTriPerCluster, lodLevels)` 生成 asset 并上传 GPU。
- 每帧由脚本（或在 `RenderSystem3D` 内的可选路径）调用：
  - `vg.setCamera(view, proj, fov, viewport)`
  - `vg.update(camX,camY,camZ)` → dispatch cull + raster（同步，复用 gpgpu 队列）
  - `vg.resolve()` → dispatch resolve；结果经 staging 读回或作为 debug 视口上屏。

> 注：`gpgpu::dispatch` 为同步执行（独立 command buffer + queue），
> 因此本模块当前以"compute 生成 visibility buffer → 读回/上屏"作为演示路径，
> 后续可把 cull/raster 的 dispatch 直接录进渲染帧 command buffer 实现真正的帧内合成。

## 5. 文件清单

```
src/modules/virtualgeometry/
  VirtualGeometry.h/.cpp          # 模块外观（脚本 API，类名 eve.VirtualGeometry）
  VirtualGeometryRenderer.h/.cpp  # 渲染器：build / setViewport / setCamera / update / resolve
  VirtualGeometryAsset.h          # 序列化资产（CPU 表示 + GPU 集群布局 VgGpuCluster）
  VirtualGeometryBackend.h        # 后端接口（vgCreate/vgUpload/vgUpdate/vgReadPixels）
  Builder.h/.cpp                  # 预处理：meshlet 化 + QEM LOD DAG（纯 CPU，可单测）
  LodSelection.h/.cpp             # CPU 参考实现：DAG LOD 选择（镜像 GPU cull 规则）
  shaders/
    vg_common.glsl                # 共享布局（绑定号 / 集群访问器 / 打包）
    vg_cull.comp                  # Pass A：DAG 可见性裁剪（frustum + LOD + 误差）
    vg_raster.comp                # Pass B：软件光栅化 → visibility buffer
    vg_resolve.vert/.frag         # Pass C：visibility buffer 解析（生产路径参考）
    *.spv / *_spv.inc             # 预编译 SPIR-V + 内嵌数组
  vulkan/
    VulkanVirtualGeometry.h/.cpp  # Vulkan 实现（基于 gpgpu 的 compute 后端）
    VirtualGeometryShaders.h      # 内嵌 .inc 的 SPIR-V
test/
  virtualgeometry_builder.cpp     # 预处理 + LOD 切换（CPU）单元测试
  virtualgeometry_gpu.cpp         # GPU 端到端 + 相机距离扫描测试
scripts/compile_virtualgeometry_shaders.py  # glslc 编译 → .spv/.inc
examples/virtualgeometry/demo.nut # 端到端示例（球体旋转 + LOD/裁剪统计）
docs/dev/VIRTUAL_GEOMETRY.md     # 本文档
```

## 6. 简化与取舍

- 使用**纯软件光栅化**覆盖所有三角形（不做大小三角形分流），保证无需 mesh/task shader。
- 遮挡裁剪使用上一帧的层次深度（此处用简单的包围球 + 可选的软件 HZB），作为简化。
- 不做几何流送（streaming），几何一次性上传；保留 DAG 结构便于后续接入页面流送。
- 材质解析仅做可视化（flat/cluster-id/深度）着色，不接入完整 PBR GBuffer，作为教学演示。

## 7. 后续路线

1. 帧内 command buffer 合成（cull/raster/resolve 直接录进渲染帧，消除同步读回）。
2. 接入硬件光栅化（大三角形路径）与 indirect draw 分流。
3. HZB 遮挡裁剪。
4. 几何流送（页面 + LRU）。

## 8. 构建与验证

在 macOS（Vulkan/MoltenVK）上完成真实构建与 GPU 测试：

```bash
# 初始化子模块
git submodule update --init --recursive
# 配置（可复用预编译 third-party；VKBuilder 头文件需指向子模块版本）
cmake -S . -B build/macosx -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_PLATFORM=macosx \
  -DEVENGINE_THIRD_PARTY_BINARY_DIR=<prebuilt-include/lib 根目录> -DBUILD_TESTING=ON
# 构建单测 + 引擎
cmake --build build/macosx --target unit_test eve -j4
# 运行虚拟几何体测试（CPU 预处理 + GPU 集成）
build/macosx/test/unit_test '--testcase=virtualgeometry.*'
```

验证结果（本机 arm64 / Release）：
- `EVVirtualGeometry` 模块编译通过；完整引擎 `eve` 链接成功。
- 单元测试 14/14 通过：
  - 预处理 5：空输入拒绝、LOD0 三角形全覆盖、簇尺寸有界、DAG 父子一致、
    LOD 三角形单调递减且误差递增。
  - **LOD 切换 5（CPU，`LodSelection` 镜像 GPU cull 的 accept 规则）**：
    - 近处全细节（LOD0）、远处粗 LOD，且随距离单调变粗；
    - LOD0 占比随距离单调下降，存在近/粗 LOD 混合的中间距离（渐变而非跳变）；
    - **无洞/无重叠**：任意距离下每条 DAG 分支恰选中一个节点（完整覆盖网格）；
    - 更低误差阈值在相同距离选中更多细节。
  - **GPU 端到端 4**：`buildIcosphere`、`cullRasterResolve`（裁剪→软件光栅化→
    visibility buffer→resolve）、**`lodTransitionSweep`（相机距离扫描：可见簇数随距离
    单调不增、近>远，且各距离像素覆盖完整无洞）**、模块创建。
- 着色器由 `scripts/compile_virtualgeometry_shaders.py` 用 glslc 编译为 SPIR-V（`.spv` + `.inc`）。

> 说明：复用外部预编译 third-party 时，需确保 `vkbuilder.hpp` 采用子模块（VKBuilder）版本，
> 不要被第三方 include 树里旧的同文件遮蔽（曾导致 `vkb::BoundSet` 未定义）。

已知问题/简化：visibility buffer 的像素 ID 采用单次 `atomicMin`（depth<<16 | clusterId），
不保留重心坐标与三角形 ID（解析阶段按簇着色）；遮挡裁剪暂未接入 HZB。
