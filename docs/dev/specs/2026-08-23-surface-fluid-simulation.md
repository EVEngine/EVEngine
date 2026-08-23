# 表面流体模拟（Surface Fluid Simulation）

EVEngine 的可交互表面流体方案，目标是建筑、玻璃、角色、机械和植被上的水珠、
流痕与薄湿膜，而不是通用体积 3D 流体。动态模型以“三角形 id + 重心坐标”保存
稳定的材质空间地址；连续湿膜将使用表面场，凸起水滴使用表面粒子，脱离后转为
世界空间粒子。SDF 保留为世界空间撞击、重新附着与无网格数据时的降级碰撞表示。

## 动态表面架构

`FluidSurfaceBinding` 是动态表面的统一基础：

- 静态/刚体模型通过 `setTransform` 更新当前和上一帧世界空间顶点；
- 蒙皮、morph 和程序化植被通过 `setDeformedPositions` 提交变形后顶点；
- 水滴地址不保存世界位置，而保存三角形与重心坐标，因此随模型稳定运动；
- 三角形邻接负责跨面推进，开放边缘返回未消费位移供脱离逻辑使用；
- 表面点速度由当前/上一帧重心插值位置计算。

`SurfaceDropletSimulation` 是 CPU 参考水滴求解器：切向重力在表面参考系中积分，
模型加速度作为相对惯性参与运动；水滴在附着不足或到达开放边缘时输出
`DetachedDroplet` 世界空间状态；CPU 路径已支持合并、湿痕沉积与重新附着，GPU
镜像在后续阶段接入。

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
├── FluidSurfaceBinding.{h,cpp} 动态三角形表面地址、运动与拓扑迁移
├── SurfaceDropletSimulation.{h,cpp} 动态表面水滴 CPU 参考求解器
├── SurfaceFluidRenderData.{h,cpp} 局部切平面椭球实例与湿润 PBR 参数
├── SurfaceWetnessField.{h,cpp} 随网格变形的顶点湿膜、扩散与蒸发
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

原有 `test/fluids.cpp` 的 16 个球面流体用例保持不变；新增动态表面能力放在
`test/fluids_surface.cpp` 的 11 个独立用例中（均按 CTest 进程隔离）：

- 数学核函数、SDF（球/平面/三角网格对比解析球）；
- CPU：表面下流、粘度阻尼、PBF 解压、cohesion 成团、adhesion 挂壁、
  Bingham 冻结低剪切运动、密度为正；
- GPU（headless Vulkan）：表面下流、PBF+cohesion 聚类、SSF 管线
  （单粒子深度≈相机距、法线朝向相机、厚度/透明度为正、PPM 导出）。
- 动态表面：刚体/变形 pose、重心插值速度、跨三角形推进、开放边缘脱离、
  切向重力与表面加速度破坏附着。

## 性能预算

粒子 2 万~8 万 + 4 次 PBF 迭代 + SSF 平滑 8~16 次，720p 目标 1~3 ms/帧。
粒子数、PBF/平滑迭代、分辨率全部可调；SSF 的窄带优化（NB-SSF）是后续
降本项。

## 分阶段路线

1. **动态表面基础**：三角形/重心绑定、刚体和变形 pose、表面速度、跨面与开放
   边缘；CPU 参考水滴支持切向重力、摩擦、动态惯性和脱离。
2. **水滴系统**：GPU 常驻状态、邻域合并、接触角、尺寸相关附着、空间粒子撞击与
   重新绑定，并接入 model3d/animation 的 pose 数据。
3. **湿膜场**：独立 fluid UV/atlas 上的厚度、速度和 wetness；水滴轨迹沉积、
   蒸发/吸收、聚集再生水滴以及 chart 接缝通量。
4. **生产渲染**：湿润 PBR 材质、ellipsoid/sphere splat、场景深度/颜色合成、
   折射、吸收、IBL/SSR、运动向量和 TAA。
5. **质量与性能**：GPU compaction、稀疏图集、LOD/降频、窄带 SSF 与编辑器调试
   视图。

## 已实现的动态表面切片

- `FluidSurfaceBinding` 使用三角形编号和重心坐标保存水滴地址，统一计算刚体、
  蒙皮、形变及程序化动画的当前/上一帧 pose 和表面速度。
- `SurfaceDropletSimulation` 已具备相对于表面加速度的重力、接触角尺寸、保体积
  合并、开放边缘/惯性脱离、空间飞行及重新附着。
- `SurfaceWetnessField` 在材质空间保存逐顶点湿膜。水滴滑行时留下湿痕，湿痕可
  扩散和蒸发，不会因为模型平移、旋转或顶点形变而漂移。
- `SurfaceFluidRenderData` 把水滴转换为面积守恒的局部切平面椭球实例，速度只改变
  长宽比；同时把湿度映射为 roughness、specular、darkening 和法线强度，供
  Vulkan/WebGPU 湿润材质直接消费。
- `examples/surface-fluid-dynamic` 沿用原有球面参考场景，在球体轻微摆动时验证动态
  附着，并生成可复现的细水滴/湿痕参考帧。

![动态表面的水滴与湿痕](../../images/surface-fluid-dynamic.png)

## 已知后续工作

- 生产级 splat 渲染通道：需要给 graphics 2D 管线增加一个“存储缓冲 + 纹理”
  的自定义描述符集（或复用 gpgpu 缓冲作为 bindless SSBO），把 SSF 输出合成
  进主场景（GBuffer 深度/颜色做折射与遮挡），并支持鼠标拾取模型表面喷射。
- model3d/animation 接入：向 `FluidSurfaceBinding` 提供导入网格拓扑、专用 fluid UV
  以及 CPU/GPU 蒙皮后的当前/上一帧 pose。
- 高质量档：离屏低分辨率 marching cubes 表面重建。
