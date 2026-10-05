# GPU Agents 实时模拟框架

> 状态：P2 已落地（editing/editor 文档与预览 + LifeField 材质绑定 + 表面 Capture）。日期：2026-10-05  
> 目标：基于 GPU Compute 架构搭建可扩展的 GPU Agents 实时模拟框架，覆盖鱼群、生命网格、鸟群与花瓣四类效果；模拟逻辑、环境数据与渲染表现解耦。  
> 关联：[`模块编排与裁剪架构.md`](./模块编排与裁剪架构.md)、[`领域短根继承与跨域组合架构.md`](./领域短根继承与跨域组合架构.md)、[`Result检查与不得丢弃返回值规范.md`](./Result检查与不得丢弃返回值规范.md)、[`superpowers/specs/2026-08-10-gpgpu-backend-abstraction-design.md`](./superpowers/specs/2026-08-10-gpgpu-backend-abstraction-design.md)、[`群体行为与流场模块设计.md`](./群体行为与流场模块设计.md)。

## 0. P0–P2 落地备注

已合入模块：

- `gpuagents`（`eve.GpuAgents()`）：`GpuAgentWorld` / `GpuAgentSimulation` / `EffectProfile` / `EffectBackend` / 四类 Solver / `AgentInstanceRenderer` / `SurfaceCapture` / `LifeFieldMaterialBinding`
- `gpuagents_editing`：`GpuAgentsDocumentTarget` + `gpuAgentsEffectSchema` + `GpuAgentsRuntimeApplier`
- `gpuagents_editor`（`eve.GpuAgentsEditorModule()`）：自动化 Target 工厂 + `GpuAgentsPreviewService`
- 标准化 `AgentState`（position / velocity / rotation / age / customData）
- 统一 SDF 障碍管线（静态场 + 动态球源，当前位置与速度外推位置双采样）
- CPU 参考求解器（可测、确定性/容差契约）；GPU 内核路径为 P1

测试：`test/gpuagents.cpp`、`test/editor_gpuagents.cpp`；示例：`examples/gpu-agents`；用户文档：`docs/usr/modules/gpuagents.md`。

**层**：宿主 `LAYER 5`，`DEPS gpgpu graphics`；editing `LAYER 6`；editor `LAYER 7`。P0/P2 步进不要求 Graphics 设备已初始化（纯 CPU 路径）；材质纹理上传在有 Graphics 时可选。

## 0.1 P2 备注（2026-10-05）

- **SurfaceCapture**：三角网格正交投影到 XZ 高度场，中心差分重建法线，写入 `SurfaceField`
- **LifeFieldMaterialBinding**：打包 `LifeFieldTexture` / `SurfaceDataTexture` RGBA8 + Origin/WorldSize/FieldResolution uniforms；可 `upload*` 到 Graphics
- **editing**：可逆 PropertySchema 文档，RuntimeApplier 投影到 Backend/World 表面域
- **editor**：自动化 `gpuagents-effect` Target；PreviewService 固定步长 scrub（含 LifeNetwork trail 能量）


## 1. 问题与非目标

### 1.1 要解决什么

- 多种大规模视觉 Agent（鱼/鸟/花瓣/生命网络）共享同一套缓冲、步进与实例渲染管线
- 环境语义（水流、风、目标、危险、表面、障碍）由 World 统一准备，Solver 只消费
- 算法可替换：换内核即可换效果，不必重写资源管理与渲染

### 1.2 非目标

| 排除项 | 原因 / 替代 |
|--------|-------------|
| RTS/寻路群体（流场 + 2D Boids） | 已有 `crowd` |
| RL / Tensor Agent | 已有 `agent` / `agent_tensor` |
| SPH / 体积流体 | 已有 `fluids` |
| 完整编辑器预览 UI | P2：`gpuagents_editing` / `gpuagents_editor` |
| GPU 内核 1:1 镜像 | P1：`GpuKernels` 镜像 CPU 参考 |

## 2. 概念映射（Unreal 对照 → Eve）

| 外来概念 | Eve 对应 | 说明 |
|----------|----------|------|
| World Subsystem | `GpuAgentWorld` | 注册 Simulation、环境语义、障碍合成、资源准备 |
| Effect Profile | `EffectProfile` + 特化参数结构 | 数据资产式参数；无行为 |
| Effect Backend | `EffectBackend` | Profile → Solver 绑定；持有 Simulation |
| Solver | `IAgentSolver` + Fish/Life/Bird/Petal | 唯一推进真源 |
| Renderer | `AgentInstanceRenderer` | 只读 AgentState → 实例矩阵/自定义数据 |
| GPUAgentSimulation | `GpuAgentSimulation` | 固定步长、初始化、重置、双缓冲 |
| Landscape / Static Mesh 表面 | `SurfaceField`（高度/法线格） | Life Network 2.5D 数据源 |
| Niagara / 贴图粒子花瓣 | 被动刚体 Agent（线速度+角速度+朝向） | 非直接写速度的贴图粒子 |

## 3. 四层架构

```text
EffectProfile (data)
      │
      ▼
EffectBackend ──owns──► GpuAgentSimulation
      │                        │
      │                        ├── AgentState[] (ping/pong)
      │                        ├── fixed-step accumulator
      │                        └── ObstacleField view
      ▼
IAgentSolver (Fish | Life | Bird | Petal)
      │
      ▼
AgentInstanceRenderer ──► instance transforms / custom attrs
```

`GpuAgentWorld` 位于 Backend 之上：持有环境场、动态障碍源列表，并为多个 Simulation 提供只读环境快照。

### 3.1 共享 AgentState

```text
position   : vec3
velocity   : vec3
rotation   : quat (xyzw)
age        : float   (seconds)
customData : float[4]  (effect-specific; e.g. bank, trailId, phase, settled)
alive      : uint32    (0 = recycled slot)
```

类型差异只体现在 Solver 内核与 Profile；缓冲布局、World 注册与 Renderer 不变。

### 3.2 固定步长与双缓冲

- `GpuAgentSimulation::step(dt)` 累积时间，按 `fixedDt`（默认 1/60）子步进
- 每子步：`read = buffers[front]`，`write = buffers[1-front]`，Solver 写 write，再交换
- `reset()` 清空 alive；`initialize(count, seed, spawnFn)` 填充初始态

### 3.3 Result / 确定性

- 创建/配置 API 返回 `[[nodiscard]] Result<...>`；非法容量、空 Profile、未绑定 Solver 失败
- 确定性：同 Profile + 同 seed + 同固定步长序列 → CPU 路径 bit-stable（浮点运算顺序固定）
- GPU 路径（P1）契约为 `ToleranceBounded`

## 4. 四类效果

### 4.1 鱼群（3D Boids + 环境）

邻域权重紧支撑径向函数 \(w(q)=(1-q)^2\)，\(q=d/R\)；\(q\ge 1\) 时权重连续归零，避免近距离逆平方爆炸。

行为力：分离 / 聚合 / 对齐；可选水流、深度偏好、目标吸引、危险回避、障碍预测。

积分：半隐式 Euler（先加速度→速度，再用新速度更新位置）；限制 MaxSpeed、MaxAcceleration、MaxHorizontalTurn、MaxVerticalTurn。

Profile 关键参数：`SeparationRadius`、`CohesionRadius`、`AlignmentRadius`、`PerceptionFieldOfViewDegrees`、`MaxNeighborSamples` 及各权重。

### 4.2 生命网格（Life Network 2D）

Agent 运动约束在 `SurfaceField`（高度 + 法线）上。场纹理推进：轨迹沉积、衰减（`TrailDecayRate` / `FreshnessHalfLife`）、扩散（`DiffusionRate`）、危险场。

传感：`SensorDistance` + `SensorAngle` 前方采样；权重 `TrailFollow` / `Nutrient` / `Repulsion` / `Danger`。`MinSurfaceNormalZ` 限制可行坡度。

输出给材质：`LifeFieldTexture`、`SurfaceDataTexture`、`Origin`、`WorldSize`、`FieldResolution`（P0 以 CPU 缓冲暴露）。

### 4.3 鸟群（飞行动力学）

在鱼群邻域模型上叠加相对升力、空气阻力、重力、失速恢复、侧倾与预测避让。

速度区间：`StallSpeed` / `CruiseSpeed` / `MaxSpeed`；`LiftCoefficient`、`GravityScale`、`AirDrag`；运动限制：`MaxClimbSpeed`、`MaxDescentSpeed`、`MaxTurnRate`、`MaxBankAngle`。

碰撞预测：`TimeToCollision`、`GroundClearance`、`CeilingClearance`；风：`UniformWindVelocityLocal`、`WindResponseTime`、`GustAcceleration`。

### 4.4 花瓣（被动刚体空气动力学）

每片花瓣持有线速度、角速度、朝向；由长度/宽度/厚度/质量/尺寸变异算惯性。相对气流 → 迎风面积、正面阻力、边缘阻力、升力、角阻力；压力中心偏移产生差异化翻滚。

生命周期、落地 Settling、软边界回收、地面接触、动态实体尾流（P0：球状尾流源写入 World 动态风扰动）。

## 5. 统一 SDF 障碍管线

```text
staticObstacleField  ─┐
                      ├─ min(distance) → ObstacleField
dynamicObstacleSources ─┘
```

Solver 同时采样当前位置 \(p\) 与外推位置 \(p + v\cdot t_{pred}\)：

1. 若距离 \(d < r_{\text{agent}}\)，用法向 \(\nabla d\) 推离位置
2. 速度分解为法向/切向，法向分量夹到非穿透，切向保留 → 沿表面滑移

鱼群、鸟群、花瓣共用；生命网格主要用表面场，仍可对危险 SDF 采样。

## 6. 模块边界

- **短根**：无统一 GameObject；`GpuAgentSimulation` 是模拟短根，效果差异在 Solver/Profile
- **跨模块**：渲染只消费 `AgentInstanceRenderer` 输出的缓冲；不 `#include` graphics 内部实现进 Solver
- **与 crowd 关系**：crowd 负责 2D 玩法群体；本模块负责 3D/2.5D 视觉 FX Agents，二者不互相包含
- **与 fluids 关系**：不复用 fluids 粒子；障碍体素可独立烘焙，后续可经 Capability 共享烘焙服务（非 P0）

## 7. 分期

| 阶段 | 内容 |
|------|------|
| **P0**（已落地） | 架构 + CPU 四 Solver + World/SDF + Instance 缓冲 + 测试 + 示例 |
| **P1** | GLSL compute 内核镜像、双缓冲 SSBO、Sequence 提交、GPU/CPU 容差对照 |
| **P2**（已落地） | editing/editor 预览、LifeField 材质绑定、表面 Capture |
| **P3** | 动态网格障碍烘焙、尾流耦合 particles、性能缩放档位 |

## 8. 验证清单

- [x] 鱼群：分离保持间距；聚合降低平均距离；障碍滑移不硬弹
- [x] 生命网格：沉积提高轨迹；衰减降低；传感器偏向高轨迹
- [x] 鸟群：速度夹在 Stall–Max；失速下方有抬头恢复趋势
- [x] 花瓣：落地后进入 Settling；软边界内回收
- [x] `ARCHITECTURE_BASE=HEAD make check/architecture-contracts`（源码契约）
- [x] P2：editing schema/undo、preview scrub、SurfaceCapture、LifeField RGBA pack
- [ ] P1：GPU 路径与 CPU 对照（未做）
- [ ] P2：Graphics depth-texture surface capture（CPU 三角正交投影已落地；深度纹理路径延后）
