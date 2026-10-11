# Realtime fog (`graphics_fog`)

**脚本入口：** `eve.RealtimeFog()`

MAC 流体输运 + 世界空间光线步进 + Froxel 积分 + 解析体积光。艺术层只消费光学结果，不回写密度、速度或透射率。

```squirrel
local fog = eve.RealtimeFog().newSystem();
fog.configureDomain(24, 12, 32, -16, -1, -48, 16, 10, 18);
fog.setOpticalProfile(0.04, 0.72, 0.82, 0.98, 0.15, 1.0, 0.55, 0.35);
fog.seedHeightFog(0.22, 0.0, 0.16, 0.45);
fog.setQuality("enhanced");
fog.setMainWind(0.5, 0.0, 0.1);
fog.setCurlStrength(0.4);
fog.setSphereInteractor(0.0, 1.0, -4.0, 1.0, 1.5, 0.0, 0.0);
local cfl = fog.stepSimulation(1.0 / 60.0);
vol.injectFroxelHeightFog(0.035, 0.72, 0.82, 0.98, 0.0, 0.16, -2.0, 10.0);
fog.injectToVolumetric(vol);   // additive MAC; does not clear height fog
vol.integrateFroxel(0.55, 0.62, 0.78, 1.0);
vol.uploadFroxel(gfx);
```

可运行示例：`examples/realtime-fog`。它复用 `examples/atmospheric-fog` 的视锥高度雾 + emissive proxy + `integrateFroxel`，再用 GPU `applyFog` 叠一层，MAC 只通过 `injectToVolumetric` 往同一网格里加密度。`has_module("realtimeFog")` 在裁剪构建中检测本模块。

## API 快查

| 绑定 | 作用 |
| --- | --- |
| `newSystem` | 创建 `FogSystem`，所有权交给脚本 VM |
| `qualityName` | 规范化质量档拼写 |
| `quality` | 当前质量档：`fast` / `enhanced` / `physical_reference` |
| `setQuality` | 切换质量档并作废 Beer cache |
| `simulationTime` | 已注入的模拟时间（秒） |
| `configureDomain` | 分配密度场与 MAC 网格（世界 AABB） |
| `seedHeightFog` | 高度带密度 + curl 初值 |
| `setMainWind` | 主风目标（m/s，经响应限速） |
| `setCurlStrength` | Curl 振幅（m/s） |
| `setCurlTimeScale` | Curl 时间尺度 |
| `setWindResponseRate` | 风向风速响应速率 |
| `setOpticalProfile` | 光学剖面：σ_t、albedo、AO、上下环境、各向异性 |
| `setSphereInteractor` | 球体固体代理（位置、半径、速度） |
| `clearInteractors` | 清空固体代理 |
| `stepSimulation` | 固定子步推进，返回 CFL 数 |
| `injectToVolumetric` | **叠加** MAC 密度到已有 froxel（不清空、不积分） |
| `syncToVolumetric` | 按质量档重建 atlas 并注入+积分（会清掉先前的高度雾） |

C++ 主 API 见 `graphics/fog/FogSystem.h`。设计说明：[realtime-fog-system.md](../../dev/realtime-fog-system.md)。
