# Physics Bake Particle Script

L6 可选粒子录制 VM 卫星；依赖 particles、通用配方 VM 和 particle_capture，
不依赖刚体、布料或绳索录制。纯播放不依赖此模块。

## module-interface

- 原生面：ParticleBakeRecording 注册，复用 prepare/finishParticleRecording 和 buildParticleTrack。
- Capability：无 provider 或查询。
- ECS/Link：发射器只在 begin/finish 调用期间借用；日志和归档不保存源指针或运行时实体句柄。
- 事件：消费 Sim 已记录的 owning 生灭/外观日志；无脚本回调或隐式 step。
- 脚本：eve.ParticleBakeRecording / eve.particleBakeRecording，RecordedParticleJournal 和 ParticleBakeArchive。
- 工具协议：无。完整 manifest 裁剪与可视化组合仍须独立验收。

所有调用在模拟/VM owner thread，处于模拟步和 sub-emitter 回调之外。调用者显式推进模拟，
同一归档的各发射器须从同一逻辑原点开始录制，使用协调的时间与已冻结完整配方。

`begin(emitter, maximumEvents)` 在 emitter Sim 中准备有界日志，成本线性于事件容量预留，
预算为 1..2000000。要求空的 CPU emitter，没有正在进行的录制；billboard/axis/stretched 且
sortMode=none。GPU、ribbon、排序模式不支持时明确拒绝，录制中策略漂移产生粘性错误，不静默丢弃事件。
失败保持已有日志。此操作不停止/重置源模拟，录制时的 RNG、配置和 step 仍由调用者提供。

`finish(emitter)` 将存活粒子在当前录制时间关闭，并转移完整日志，返回 VM-owned
RecordedParticleJournal。成本线性于存活粒子，之后向量转移为常数成本；不停止或重置实时模拟。
会先创建存储和 VM 返回对象再消耗日志；不完整/耗尽/无活动录制不能发布。
调用者若要完全切换到回放，应自行停止或重置源。journal 的 duration/eventCount 是常数成本查询。
停止或重置只退役活粒子，不删除 ECS 发射器；丢弃脚本引用也不等于删除原生实体。
原生宿主只有在所有借用方（含脚本别名和子发射器引用）结束使用后才能调用源的 `release()`。
归档和加载后的播放器不保留这些借用。当前脚本 Emitter 没有可安全使所有别名失效的删除接口，
因此不要通过外部原生删除后继续使用旧脚本对象。
duration 是覆盖累计 double 时间的最小不小于它的 float，避免向下舍入使末事件越界；
记录键仍使用原有 float 精度，无法区分的事件时间明确拒绝。

`newArchive(recipe)` 校验并复制完整 pixels2d 配方；费用为有界元数据验证和复制。
配方必须覆盖全部发射器、参数、后端和命名 RNG；后续修改配方不会修改已有归档。
`archive.addJournal(journal, emitterSessionUUID)` 共享持有不可变日志，UUID 是唯一且非 nil 的稳定会话身份。
同一日志不能重复加入，重复身份拒绝；最多 65536 个源和合计 2000000 个事件。
该创建阶段操作成本线性于已有源数，失败不改变归档。sourceCount 是常数成本查询。
日志包装器和源发射器释放后，归档仍拥有数据。

`archive.save(path)` 用所有日志的最大时长生成一份 ParticleTrack，并经公共协议发布；
成本线性于事件和轨迹排序，再执行文件写入/屏障，放在发布阶段。空归档拒绝；
空但完整的发射器日志允许。转换、IO 或已有目标失败不消耗归档，可修正路径后重试。
可重复保存到新路径；不会覆盖已有文件或自动重新模拟。加载使用
`eve.physicsBake.loadParticles(path, expectedRecipe)`，不再需要源模拟。
保存的是 sprite 数值和身份；材质由调用者绑定，脚本批量绘制通过
[Physics Bake Graphics Script](physics_bake_graphics_script.md) 的 `prepareParticles` / `draw` 接入。
