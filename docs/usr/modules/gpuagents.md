# GPU Agents（鱼群 / 生命网格 / 鸟群 / 花瓣）

**脚本入口：** `eve.GpuAgents()`

可扩展的 GPU Agents 实时模拟框架。模拟逻辑、环境数据与实例渲染拆为
**EffectProfile → EffectBackend → Solver → Renderer** 四层；`GpuAgentWorld`
统一注册后端、管理水流/风/目标/危险/表面与 SDF 障碍；`GpuAgentSimulation`
负责固定步长、双缓冲与重置。四类效果共享 `AgentState`
（position / velocity / rotation / age / customData）。

P0 为 CPU 参考求解器（可测、固定步长确定性）；GPU compute 镜像为 P1。

设计：[docs/dev/2026-10-05-gpu-agents-simulation-framework.md](../../dev/2026-10-05-gpu-agents-simulation-framework.md)

## 基本用法

```squirrel
local gpuAgents = eve.GpuAgents();
local world = gpuAgents.newWorld();
world.bakeEmptyObstacles(-10, -10, -10, 16, 16, 16, 1.0);
world.carveSphere(0, 0, 0, 2.0);
world.setWaterCurrent(0.2, 0.0, 0.0);

local school = gpuAgents.newBackend(0, 128); // 0=Fish, 1=Life, 2=Bird, 3=Petal
gpuAgents.spawnCloud(school, 64, 0.0, 1.0, 6.0, 2.0);
gpuAgents.registerBackend(world, "school", school);

// eve_update:
world.stepAll(dt);
local n = school.instanceCount(); // 实例矩阵已在 Backend 内同步
```

生命网格需先初始化表面：

```squirrel
world.initFlatSurface(-16, 0, -16, 32, 64);
local life = gpuAgents.newBackend(1, 64);
gpuAgents.spawnCloud(life, 32, 0, 0, 0, 4);
gpuAgents.registerBackend(world, "life", life);
```

## Effect kind

| kind | 含义 | 要点 |
|------|------|------|
| 0 | Fish | 3D Boids + 水流/深度/目标/危险；紧支撑权重 \(w(q)=(1-q)^2\) |
| 1 | LifeNetwork | 表面轨迹沉积/衰减/扩散；传感器跟随 |
| 2 | Bird | 邻域 + 升力/阻力/失速/侧倾/净空 |
| 3 | Petal | 被动刚体气动；落地 Settling；软边界回收 |

## 与 crowd / fluids 的边界

- `crowd`：2D 玩法群体与流场寻路
- `fluids`：SPH / 表面流体粒子
- `gpuagents`：视觉 FX Agents（可扩展到 GPU），不拥有玩法寻路真源
