# 光线追踪能力与降级路径

**脚本入口：** `eve.RayTracing()`；`config.modules` 使用槽位 `"rayTracing"`。

配置 `rayTracing`，入口 `eve.RayTracing()`。它查询 Graphics 已注册的 IRayTracing
provider，不自己创建 GPU。先初始化窗口和 Graphics，再判断设备实际能力：

```squirrel
local tracing = eve.RayTracing();
local usable = tracing.isAvailable() && tracing.capsAvailable();
print(usable ? "ray tracing provider available\n" : "use raster lighting\n");
```

任务一是给图形设置页显示当前设备是否支持；任务二是在关卡资源创建前选择光照方案。
选择普通光栅方案后仍加载普通材质、阴影和环境光资源，不能只跳过整个 render。
缺少 provider 返回 false；检查构建 profile、设备扩展和 Graphics 初始化顺序后再报告不可用。
设备重建后重新查询，不能永久缓存第一次结果。

当前脚本只绑定 `isAvailable` 与 `capsAvailable`，没有脚本 BLAS/TLAS 构建或 dispatch
入口。需要自定义光追渲染时通过原生 IRayTracing 后端集成，不能在 EveScript 中照抄 Vulkan
或其他引擎的光追 API。实现见
[RayTracingModule.cpp](../../../src/modules/graphics/raytracing/RayTracingModule.cpp)，
后端契约见 [IRayTracing.h](../../../src/modules/graphics/IRayTracing.h)。
