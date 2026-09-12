# 物理功能整合审查（2026-09-12）

## 范围与结论

本次审查覆盖 2D/3D rigid body、cloth、rope、soft body、surface fluid、生成式
collider、scene/editor、vehicle 与 pixelworld 消费端。当前实现已经具备多个可运行的
独立 solver，但“实现了 `ISimulationBackend`”不等于“已经通过统一 backend 选择器
装配”。本轮先合并能保持现有公开 API 的启动、诊断、失败原子性和契约测试；需要改变
快照 schema 或跨域身份模型的项目不得用兼容指针或第二份状态绕过。

## 本轮已收敛

- `physics_editor` 现在有生产 `SCRIPT/SLOT` 启动模块，负责向 live `Editor` 注册
  `physics.editing` provider，并在卸载时撤销 generation handle。测试从手工注册改为
  验证真实模块启动后的 factory lease。
- accelerator provider 创建失败或抛异常时仍显式回退 CPU，并保留原始结构化诊断；
  fallback warning 不再覆盖根因。
- `World3D::step` 仅在 backend step 成功后替换 contact event batch。失败 step 保留
  上一成功 tick 的事件和 `simulationTick`。
- CPU Cloth2D、Cloth3D、Rope3D、SoftBody3D 使用同一 conformance fixture，覆盖非法
  settings、重复 tick、失败 observation 不变和 duration/tick 单调推进。
- 2D/3D world snapshot 升级为 schema v2：envelope 写入非空稳定 world identity，拒绝
  foreign snapshot；已发布 v1 仅在 live body/topology 可用于补全缺失字段时迁移。restore 先在
  detached candidate world 中准备，失败不改变 live state；成功后一次换入并统一换代
  Body/Shape/Joint runtime handle，使跨域 link 明确失效。
- Vehicle 不再跨帧保存 `Body*`/`Body3D*`，改为 `PhysicsLink`、world generation 和弱生命期
  组合；已覆盖 detach、外部 body 销毁、vehicle-first/world-first 与 provider absent。
- 生成式 collider provider 可显式绑定调用方拥有的共享 `World3D`。prepare 只创建 disabled
  reservation，commit 才激活，rollback/clear 释放；world 先销毁后 publication 可检测为 stale。
- `scene_physics` 提供生产 `scenePhysics` 组合入口，把后续生成 collider 绑定到 gameplay
  `World3D`；`pixelworld_physics` 提供 `pixelworldPhysics` 脚本入口，示例按固定步同步权威
  PixelWorld 到 Box2D terrain projection。
- Cloth2D、Cloth3D、SurfaceFluid 的 selector 契约矩阵覆盖 provider present/absent、创建失败和
  step 失败；fallback 诊断携带稳定 domain，失败 observation 不推进。
- accelerator capability 支持多个独立 provider 注册：按稳定 priority 选择、一个 provider
  卸载不覆盖其他 domain、首个 provider 创建失败时可继续尝试后备 provider；旧的单 slot
  `IAcceleratorBackendProvider` 注册仍作为最后候选兼容。

## 尚未满足完整契约的 P0/P1 项

### 完整拓扑快照

schema v2 已能从 detached candidate 重建 2D circle/polygon/chain fixture，以及 3D shape、mesh、
heightfield 和 joint kind/local-frame topology；一次 swap 发布，旧 wrapper/link 失效。它仍未编码
2D edge、fixture material/filter/sensor/tag，以及 3D joint motor/limit/spring/threshold/
collide-connected、collision override 与完整 solver policy，因此当前契约是“可重建的有限 topology
checkpoint”，不能宣称完整 world save/replay。后续 schema 必须先补齐这些字段及迁移测试，再扩大
支持范围；不允许在 live world 上逐项修改后做 best-effort rollback。

### 独立 solver 的生产装配

ClothGPU 与 surface fluid 虽实现 backend contract，生产路径仍由各模块直接构造并持有
GPGPU/graphics 资源。多 provider registry 与独立 RAII 生命周期已经落地，因此 Cloth/Fluids
后续注册不会再互相覆盖；但现有公开 factory 返回具体 `Cloth`/`FluidSimulator` 类型，尚未把
domain state 与 accelerator/presentation adapter 分开，不能只注册一个 provider 就宣称生产
factory 已经过统一 selector。后续需完成该内部委托迁移，并补 CPU/GPU observable parity。

### Collider 消费链

`scenePhysics.bindGeneratedColliders()` 已提供显式 shared-world 生产入口；为兼容未迁移调用方，
未绑定时仍会为每个 artifact 创建私有 compatibility world，暂不能删除该路径。shared-world
restore 明确返回 Unsupported，需由调用方重新 publication，避免静默写入第二份状态。

PixelWorld physics 已有脚本模块和真实示例消费端；其 Box2D terrain cache 是权威 PixelWorld
的 projection，不持有第二份材质状态。

## 验收边界

本轮 source-only architecture contracts、module dependency、profile matrix、test manifest 和
`git diff --check` 必须通过；`physics-core-only` 必须独立用 GCC 编译。完整 Vulkan build、
GPU cloth/fluid runtime 与 framebuffer evidence 只有在 Vulkan SDK/ICD 可用时才可计为通过，
不得用静态检查或 mock provider 替代。
