# Time/Subscription 真实消费者迁移记录

日期：2026-08-26
范围：Particles、Effects、Animation，以及 Event poll listener 和 Animation clip reload listener。
状态：本切片已实现；Physics 仍由独立代理维护，replay 没有可安全接入的独立 runtime consumer。

## 时间入口

调度器向运行时模块注入 `eve::SimulationStep`：

```text
SimulationClock / replay scheduler
        └── SimulationStep{SimulationTick, Duration}
              ├── Particles::advance
              ├── effects::EffectContainer::advance
              └── animation::Animation::advance
```

三类入口都执行以下契约：

- `Duration` 必须非负；不能从模块内部读取 wall clock。
- 同一个 owner 的 tick 必须严格递增；回退或重复 tick 返回结构化 `Conflict`，且不推进状态。
- checked API 返回 `[[nodiscard]] Result`；旧 `float/double dt` facade 保留，并在实现内显式消费转换及推进结果。Animation 的 Tween、SpriteAnim、SpineAnim、AnimGraph、AnimLayerMixer、AnimSyncGroup、ControlAnim、ControlPose、MotionMatcher 和 AnimTrail 均以 `advance(SimulationStep)` 为真实实现入口；旧 `update(float)` 只向内转发。
- deterministic state 记录 `SimulationTick`。墙钟只能用于外部采样或性能统计，不能参与仿真状态或 snapshot key。
- scheduler 的 pause、rate、fixed-step 先在 `SimulationClock` 中决定；checked consumer 不再使用 Particles 的 legacy playback/fixed-step 配置改变注入步长。

Effects 的旧秒数 API 会转换为一个递增的兼容 tick，再走同一 checked path。Animation 的模块泵同样以 `Duration` 转换为兼容 tick。直接调用旧对象级 `update(float)` 的代码属于兼容边界，应逐步迁移到 scheduler path。`Animation::advance` 会先预检注册子对象的 tick，再以同一个 `SimulationStep` 驱动它们，避免一个子对象拒绝后 host 已推进前序子对象。

## CPU/GPU 可观察结果合同

- Particles CPU path：同一 seed、同一初始状态、同一 tick/duration，结果按现有 float 积分精度确定；测试比较位置、寿命和计数时使用显式 float tolerance。
- Particles resident GPU path：GPU 只在能力存在且 feature set 可表达时启用；GPU 计数/位置属于 tolerance-bounded observable，不宣称 bit-exact。能力不存在或提交失败时，状态通过已有可观测 backend/fallback 状态回到 CPU 语义，不能静默改变契约。
- Effects 与 Animation 当前是 CPU domain state，使用注入的整数纳秒 Duration 在模块边界转换到既有秒字段；tick 是恢复/调度身份，浮点值只作为领域计算值。

## Subscription 消费者

`eve::Observer` 新增 `notifyChecked`，它在无锁、快照派发的基础上捕获未知 callback 异常并继续派发后续 listener；返回的失败数必须显式消费。订阅者可以在回调中 dispose 自身/其他 token 或注册新 token。新注册只在下一次派发生效，已 dispose 的快照项会被跳过。

本切片接入两个此前仍是专用/裸回调生命周期的真实消费者：

1. `event::Event::subscribePoll`：统一 poll listener 的 move-only RAII 生命周期；兼容 `setPollObserver` 由内部持有一个 Subscription 投影。消息从队列移除后已经提交，listener 异常只增加可观测 failure count，不会让 `pollOwned()` 伪装成失败。
2. `animation::AnimClipRegistry::subscribeReload`：统一 EVA clip reload listener 生命周期；路径不存在仍会发出一次结构化 reload event，便于验证 capability/asset absence。adopt 完成后才派发，listener 异常不会回滚已经完成的 clip 更新。

此前已迁移的 `PropertyModel`、Definitions 和 PolicyRegistry 继续使用同一公共 token。高频 Physics contact 仍允许 batch/poll，不能为了生命周期统一而把高频数据逐条变成 callback。

## 粒子统计与资源轮询

`ParticleSimSystem::advance` 先在局部 `ParticleFrameStats` 中构建完整记录，只有整帧成功才替换公共快照；负 duration、重复 tick 或中途 checked 失败不会发布半帧计数。`ParticleEmitter::currentSimulationTick()` 通过 ECS 的 `ComponentRef` 只读查询，不再使用 `const_cast`。

粒子资源绑定继续允许无文件监听事件的宿主通过 mtime 轮询发现变更。这是明确的文件状态观测：`Resource::lastReloadObservation` 和 `getConfigReloadObservation()` 报告 `unbound`、`auto_reload_disabled`、`mtime_polling_unchanged`、`mtime_polling_reloaded`、`mtime_unavailable` 或 `mtime_polling_reload_failed`，供诊断、测试和脚本观测。

## 测试覆盖

`test/time_subscription_consumers.cpp` 覆盖：

- Particles checked step 忽略 emitter 私有 playback/fixed-step 配置、记录 tick、重复 tick 拒绝。
- Effects 和 Animation 的注入 Duration/tick 推进及可观察 tick。
- Event 无 listener/有 listener 的消费边界、callback exception、队列提交后仍返回消息。
- Event 与 clip registry 的 callback 内 dispose、re-subscribe，以及新增 listener 延迟到下一次 dispatch。
- clip reload provider/path absence 的 NoOp 结果。
- Animation 子对象的注入 step、重复 tick 拒绝，以及粒子 checked 失败时公共 `ParticleFrameStats` 保持上一个完整帧。

正式 Vulkan/third-party 全量构建不属于本切片验证范围；局部 C++20 语法、manifest、依赖图、分层和 `git diff --check` 必须在主代理收口阶段执行。
