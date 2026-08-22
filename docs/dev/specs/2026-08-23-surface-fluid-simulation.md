# 表面流体模拟（Surface Fluid Simulation）

EVEngine 的可交互表面流体方案：粒子被约束在模型的有符号距离场（SDF）上，在
重力切向分量驱动下沿表面流动，通过 PBF 密度约束、Akinci 式凝聚力/粘附力形成
液滴，用 Bingham 屈服应力模拟污泥，最后用屏幕空间表面重建（SSF）管线渲染。

## 背景与选型

实时可交互的表面流体，业界主流是粒子法（SPH / PBF）+ 屏幕空间流体渲染
（SSF）：

- 模拟：PBF（Position Based Fluids, Macklin & Müller 2013）稳定、GPU 友好；
  表面张力用 Akinci et al. 2013 的成对 cohesion/adhesion；粘稠/泥浆用
  Bingham 屈服应力（低剪切率下有效粘度激增，流体“冻结”堆叠）。
- 渲染：粒子 splat 到屏幕深度/厚度缓冲 → 双边滤波平滑（曲率流风格）→
  深度梯度重建法线 → 着色。高质量档可再叠加离屏 marching cubes，属于后续
  优化项。

参考：Screen Space Fluid Rendering with Curvature Flow (van der Laan 2009)、
NVIDIA GDC 2010 “Screen Space Fluid Rendering for Games”、Narrow-Band
Screen-Space Fluid Rendering (Oliveira & Paiva 2022)、Moro et al. 2007
（表面粒子流）。

## 模块结构

新模块 `fluids`（LAYER 5，依赖 `gpgpu` + `graphics`，在
`cmake/module_manifest.cmake` 注册）：

```
src/modules/fluids/
├── FluidMath.h             SPH 核函数（poly6/spiky/粘性/cohesion）+ 参数
├── FluidSdf.{h,cpp}        Mesh 体素化 SDF：解析球/平面 + 闭合三角网格烘焙
├── FluidSimulation.{h,cpp} CPU 参考求解器（网格邻居 + 密度 + PBF +
│                           XSPH 粘性 + Bingham + cohesion/adhesion + 表面投影）
├── FluidGpuKernels.h       GLSL 求解内核（clear/build/densityLambda/delta/apply/integrate）
├── FluidSurfaceRenderer.{h,cpp} SSF 渲染器（CPU 参考 + GPU 内核编排）
├── FluidSsfKernels.h       GLSL SSF 内核（clear/splat/smooth/normal/shade）
└── Fluids.{h,cpp}          模块工厂 + Squirrel 绑定（FluidSim / FluidSurface）
```

GPU 求解内核与 CPU 参考 1:1 镜像：统一定位网格（链表式 cellHead/cellNext，
atomicExchange 构建），8 个 SSBO 绑定、32 个 float push constant，布局在
`FluidGpuKernels.h` 顶部注释。无 GPU 或 shader 编译器时自动回退 CPU 求解器。

## 求解器数据流（每个 substep）

1. `clearGrid` + `buildGrid`：粒子入网格；
2. `integrate`：XSPH 粘性（Bingham 有效粘度）+ cohesion + adhesion + 重力 +
   阻尼 → 速度，预测位置，SDF 投影（推出表面、消除法向速度）；
3. PBF 循环（默认 2 次）：
   - `densityLambda`：poly6 密度 + spiky 梯度累加 + lambda；
   - `computeDelta`：Σ(λ_i+λ_j)·∇W / ρ0；
   - `applyDelta`：位置修正 + SDF 投影。

## SSF 渲染数据流

1. `clear`：深度（uint，0xFFFFFFFF 为空）、厚度、法线、颜色归零；
2. `splat`：每个粒子投影到屏幕，按粒子半径写像素 AABB：`atomicMin` 深度、
   `atomicAdd` 厚度（固定点 1/256）；
3. `smooth`：对深度做双边滤波（空间高斯 × 深度边缘 falloff），ping-pong
   双缓冲，可配置迭代次数；
4. `normal`：由深度梯度重建视图空间法线；
5. `shade`：water（蓝色基底 + Fresnel 边缘 + 高光）或 mud（棕色漫反射 ×
   厚度衰减 + 粗糙高光）。

## 脚本 API

```squirrel
local f  = eve.Fluids();
local sim = f.newSimulator(8192);
sim.setSdfSphere(0, 0, 0, 1, 32);      // 或后续接入任意 Mesh SDF
sim.setCohesion(0.5);
sim.setAdhesion(0.5);
sim.setPbfIterations(2);
sim.spawnDrop(0, 1.45, 0, 0.25, 200);

local r = f.newSurfaceRenderer(320, 240);
r.setCamera(0, 0, -2.6, 0, 0, 0, 0, 1, 0, 55);
r.setMode(1);                            // 0 水，1 泥
r.render(sim);
r.writePpm("fluid.ppm");                 // 调试输出
```

## 测试

`test/fluids.cpp`（17 个用例，按 CTest 进程隔离）：

- 数学核函数、SDF（球/平面/三角网格对比解析球）；
- CPU：表面下流、粘度阻尼、PBF 解压、cohesion 成团、adhesion 挂壁、
  Bingham 冻结低剪切运动、密度为正；
- GPU（headless Vulkan）：表面下流、PBF+cohesion 聚类、SSF 管线
  （单粒子深度≈相机距、法线朝向相机、厚度/透明度为正、PPM 导出）。

## 性能预算

粒子 2 万~8 万 + 4 次 PBF 迭代 + SSF 平滑 8~16 次，720p 目标 1~3 ms/帧。
粒子数、PBF/平滑迭代、分辨率全部可调；SSF 的窄带优化（NB-SSF）是后续
降本项。

## 已知后续工作（不在本 PR）

- 生产级 splat 渲染通道：需要给 graphics 2D 管线增加一个“存储缓冲 + 纹理”
  的自定义描述符集（或复用 gpgpu 缓冲作为 bindless SSBO），把 SSF 输出合成
  进主场景（GBuffer 深度/颜色做折射与遮挡），并支持鼠标拾取模型表面喷射。
- 网格 SDF 资产管线：把 `MeshSdf::makeFromTriangles` 接到 model3d 加载路径，
  支持任意模型与动态变换矩阵。
- 高质量档：离屏低分辨率 marching cubes 表面重建。
