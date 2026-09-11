# Cloth 布料子模块

`physics_cloth` 是依赖 `physics`、`graphics` 与 `gpgpu` 的可选卫星模块。它独立拥有 2D、3D 与 GPU 布料运行时；刚体、流体项目可以只启用 `physics`，不必链接布料求解器和 GPU 布料实现。

```nut
local clothModule = eve.Cloth();
local cloth2d = clothModule.newCloth(18, 12, 14.0, 48.0, 36.0);
local cloth3d = clothModule.newCloth3D(16, 12, 0.4, -3.0, 3.2, -2.0);
local clothGpu = clothModule.newClothGPU(40, 30, 8.0, 40.0, 30.0);
```

`newCloth` 创建 CPU 2D 网格，`newCloth3D` 创建支持 XPBD、体积/压力、tether、skin/backstop、attachment、碰撞过滤和撕裂的 CPU 3D 网格，`newClothGPU` 创建 Vulkan compute 2D 网格。实例由调用者持有，结束使用时调用 `destroy()`；模块不保留跨帧的实例裸指针。

完整参数、交互和材质调节示例见 [Physics 模块](physics.md) 的 Cloth 章节。
