# Realtime fog (`graphics_fog`)

**脚本入口：** `eve.RealtimeFog()`

MAC 流体输运 + 世界空间光线步进 + Froxel 积分 + 解析体积光。艺术层只消费光学结果，不回写密度、速度或透射率。

```squirrel
local fog = eve.RealtimeFog().newSystem();
fog.configureDomain(16, 12, 16, -8, 0, -8, 8, 6, 8);
fog.seedHeightFog(0.8, 0.0, 0.35, 0.4);
fog.setQuality("enhanced");
local cfl = fog.stepSimulation(1.0 / 60.0);
```

`has_module("realtimeFog")` 在裁剪构建中检测本模块。

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
| `stepSimulation` | 固定子步推进，返回 CFL 数 |

C++ 主 API 见 `graphics/fog/FogSystem.h`。设计说明：[realtime-fog-system.md](../../dev/realtime-fog-system.md)。
