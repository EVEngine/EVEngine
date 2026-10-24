# Physics Bake Recording Script

L6 可选录制卫星依赖纯烘焙核心、播放 VM、变形捕获及 physics/cloth/rope 模块。纯播放不依赖此模块。

## module-interface

- 原生面：PhysicsBakeRecording 模块注册，使用既有 DeformRecording 与 captureDeformFrame。
- Capability：无 provider 或查询。
- ECS/Link：只在调用期间借用类型检查后的源；会话没有源对象、世界或 VM 指针。
- 事件：无回调、订阅或隐式物理 step。
- 脚本：eve.PhysicsBakeRecording / eve.physicsBakeRecording，VM-owned eve.DeformBakeRecording / eve.RigidBakeRecording。
- 工具协议：无。整个 manifest 的 L3/L4 裁剪仍待独立验收。

所有操作在模拟/VM owner thread、源未在 stepping 的时段执行。调用者显式推进物理与采样时间，
并提供完整 producer/backend/source/parameters/timing/RNG 的 eve.BakeRecipe。

`beginDeform(recipe, sampleBudget)` 复制当前配方并准备会话，预算为总顶点样本数 1..2000000。
成本线性于有界配方元数据，放在录制开始时；返回 checked VM-owned DeformBakeRecording。
配方后续修改不会改变会话。空、错误类型或非法预算返回诊断。

`captureCloth(cloth3D, seconds)` / `captureRope(rope3D, seconds)` 临时借用对应真实源，
复制位置及实际三角形/线段连接，再通过同一个原生 DeformRecording admission。
每次成本线性于源顶点和拓扑，带有分配；不调用 update、不保存源裸指针。
时间网格、固定拓扑、有限数和总预算不满足时不追加半帧。剪断或撕裂需要另一个片段，不能恢复旧连接来伪装固定拓扑。
源对象销毁不会使已经接受的帧失效。frameCount/sampleCount 是常数成本统计。

`save(path)` 冻结所有已录帧并经公共资产发布协议保存，成本线性于所有样本并执行文件写入与屏障，
只在发布阶段调用。空会话、已有目标、不可写路径等返回 checked 失败；不覆盖文件、不清空会话。
保存成功后可销毁录制会话，并用 eve.physicsBake.loadDeform(path, expectedRecipe) 加载独立播放器。
当前接入 Cloth3D/Rope3D 和下述 2D/3D 刚体；粒子录制由独立的
[physics_bake_particle_script](physics_bake_particle_script.md) 提供。本模块不承担渲染资源绑定。

## 刚体录制

`beginRigid(recipe, sampleBudget)` 返回拥有原生 RigidRecording 的 RigidBakeRecording，
预算计所有帧的姿态样本，总数 1..2000000。坐标取自已冻结配方；pixels2d 只接纳 World/Body，
metres3d 只接纳 World3D/Body3D。费用为有界元数据复制，放在会话创建阶段。

在第一帧之前，调用 `bindBody2D(world, body, uuid)` 或 `bindBody3D(world, body, uuid)`，
uuid 是非 nil 的稳定 PersistentId 字符串。每个会话仅绑定一个世界，最多 16384 个刚体，
重复刚体/身份或错误世界均拒绝；该设置阶段操作成本线性于已有绑定数。会话只保存拥有的
PersistentId 和带代际信息的 world/body handles，不保存源裸指针或延长源生命周期。

`capture2D(world, seconds)` / `capture3D(world, seconds)` 临时借用世界，按绑定批量捕获并向原生
录制会话追加；成本为线性捕获及身份排序，时间由调用者显式提供。第一帧成功后绑定列表锁定。
错误类型、坐标、世界或过期刚体返回 checked 失败，不改变已录帧。销毁刚体后创建替代刚体
不会让旧绑定自动指向新对象；恢复/重建世界后必须重新建立录制会话和绑定，运行时 handles 不持久化。
源先销毁时已录帧仍能保存；会话先销毁不会修改世界或刚体。

bindingCount/frameCount/sampleCount 为常数成本统计。save(path) 冻结并通过既有公共协议发布，
成本线性于所有样本并执行文件屏障，不覆盖已有文件，不清空会话。之后用
`eve.physicsBake.loadRigid(path, expectedRecipe)` 获取独立播放器；此时无需保留源世界。
