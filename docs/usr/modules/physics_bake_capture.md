# Physics Bake Capture / 刚体采集适配器

`physics_bake_capture` 是 L5 的可裁剪录制卫星，依赖 `physics` 与 `physics_bake`。
只有录制阶段需要它；纯姿态轨道与文件发布不依赖该适配器。

原生 `captureRigidFrame(world, bindings, seconds)` 同时支持 `World` 和 `World3D`。
每条 `RigidBakeBinding` 提供持久轨道 ID、当前 world handle 和 body handle。
函数只在调用期间解析活刚体，将姿态复制到拥有值的 `RigidTrackFrame`，不保留指针，
不调用 world.update，不生成时间或随机数，也不触发脚本回调。

2D 使用原始像素 XY 与弧度角生成的 Z 四元数，之后用 `PlanarPixels` 准备轨道；
3D 使用米与 XYZW 四元数，之后用 `Metres3D`。调用者按固定时间网格收集帧，再交给
`RigidPoseTrack::prepare` 完成时间、稳定身份集合和有限姿态的最终验证，再使用 save/load
保存与读取显式资产。带配方的准备及 loadForRecipe 负责缓存准入；
[表现适配器](physics_bake_graphics.md) 提供 2D/3D 目标提交，
[脚本录制](physics_bake_recording_script.md) 和 examples/physics-bake-replay 提供完整使用路径。
本采集模块自身不持有渲染对象。

## module-interface

- 原生调用面：两个 captureRigidFrame 重载，返回 checked Result<RigidTrackFrame>。
- Capability 输入/输出：无注册或查询；调用者显式传入世界。
- Link 数据：RigidBakeBinding 仅持有类型化 runtime handle 与独立持久 ID。物理世界拥有
  活刚体；源先销毁时返回 StaleHandle，绑定先销毁不影响源。世界恢复/重建后必须重建绑定，
  不能序列化运行时 handle。公开头中的 PhysicsHandles 是这个数据合同的类型依赖。
- ECS：不建立 System，也不在 View 迭代中进行结构变更。
- 事件/脚本/工具面：无新增事件、脚本绑定或工具协议。

线程归属为世界的 owner 线程，调用必须在 stepping 和回调之外。重复轨道 ID、重复 body、
nil ID、跨世界或过期句柄使整个捕获失败，不暴露部分帧；已冻结轨道与源生命周期独立。
捕获成本为复制全部绑定体姿态加身份校验，仅用于录制。裁剪声明仍需 manifest 构建与运行证据。
