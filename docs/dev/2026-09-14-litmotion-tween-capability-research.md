# LitMotion 能力对照与 EVEngine Tween 演进方案

评估日期：2026-09-14  
范围：Unity [LitMotion](https://github.com/annulusgames/LitMotion)（v2 能力面）vs EVEngine `animation` 模块现有 Tween / ControlAnim  
目标：回答「引擎如何实现类似功能」，给出可落地的分层方案，而不是逐行移植 Burst/DOTS。

## 1. LitMotion 是什么

LitMotion 是面向 Unity 的高性能、零分配补间库（作者 Annulus Games，Magic Tween 的后继）。公开卖点：

- 一行创建并绑定任意值（Transform / Material / TMP / 任意字段）
- struct 驱动、DOTS Job + Burst 更新，创建与更新路径尽量零 GC
- `MotionBuilder` 链式配置（`WithEase` / `WithDelay` / `WithLoops` / `WithScheduler` / callbacks）
- `MotionHandle` 控制生命周期（`IsActive` / `Complete` / `Cancel` / await）
- `IMotionAdapter<TValue, TOptions>` 扩展插值类型
- 特殊运动：Punch / Shake、零分配文本动画
- v2：`LSequence`（Append / Join / Insert）、Inspector 可编辑的 LitMotion.Animation 包
- 与 UniTask / UniRx·R3 集成；设计哲学更倾向 async 组合，而不是把 Sequence 做成 DOTween 级复杂

典型 API：

```cs
LMotion.Create(Vector3.zero, Vector3.one, 2f)
    .WithEase(Ease.OutQuad)
    .WithLoops(2, LoopType.Yoyo)
    .WithDelay(0.2f)
    .BindToPosition(target);

LSequence.Create()
    .Append(LMotion.Create(0f, 1f, 1f).BindToPositionX(transform))
    .Join(LMotion.Create(0f, 1f, 1f).BindToPositionY(transform))
    .Insert(0f, LMotion.Create(0f, 1f, 1f).BindToPositionZ(transform))
    .Run();
```

核心拆解（实现层，非公开 API）：

| 概念 | 作用 |
|---|---|
| `MotionBuilder` | 配置态；`Bind` / `RunWithoutBinding` 才真正入库并播放 |
| `MotionHandle` | 轻量句柄（storage id + generation），无堆对象 |
| `MotionScheduler` | 接入 Update / FixedUpdate / Manual 等时钟 |
| `MotionAdapter` | 类型相关插值（float / Vector3 / Color / Quaternion…） |
| `MotionStorage` | 预扩容 dense 数组，避免运行时分配 |
| `ManagedMotionData` | 托管侧回调 / Bind 目标（与 Burst 友好的 POD 状态分离） |

设计哲学原文要点：struct 创建零分配、`EnsureStorageCapacity` 预热、入口统一为 `LMotion`（不做组件扩展方法）、复杂编排优先 UniTask/Rx 而非超重 Sequence。

## 2. EVEngine 现状（已有基础）

入口：`eve.Animation()` → `src/modules/animation/`。

### 2.1 Tween（已覆盖 LitMotion 的「基础补间」子集）

`Tween`（`Tween.h` / `Tween.cpp`）能力：

- 命名标量轨：`setFrom` / `setTo` / `setDelta`
- 角度最短路径：`setFromAngle` / `setToAngle` / `setDeltaAngle`
- `duration` / `delay` / `repeat`（含无限 `-1`）/ `yoyo`
- 缓动字符串：与 `Math.ease` 同集合（linear / Quad / Cubic / Sine / Expo）
- 生命周期：`start` / `pause` / `resume` / `stop` / `reset` + 状态查询
- 时间源：`advance(SimulationStep)`（确定性）；`update(dt)` 为兼容门面
- 模块泵：`Animation::advance` 批量推进注册中的 Tween / SpriteAnim / SpineAnim

脚本用法（文档 `docs/usr/modules/animation.md`）：

```squirrel
local anim = eve.Animation();
local move = anim.newTween(0.6);
move.setFrom("x", 0);
move.setTo("x", 200);
move.setEase("outQuad");
move.start();
anim.update(dt);
local x = move.get("x"); // 调用方自行写回目标
```

### 2.2 绑定模型：Pull，不是 Push

LitMotion 是 **Push Bind**：更新循环里 Adapter 把插值结果写进目标字段。

EVEngine 当前是 **Pull**：

- Tween 只维护命名属性当前值；
- `AvatarInstance::bindTween` 每帧从 Tween **拉取** `x/y/sx/sy`（及 3D / 同名参数）写回 Avatar；
- 其它目标（Scene 节点、`Sprite2D`、UI）需脚本 `tween.get("x")` 后手动赋值。

`docs/dev/2d-sprite-animation-api-evaluation.md` 已明确这一点：「Tween 足以计算平移、缩放和旋转角度，但只输出数值，调用者必须每帧手动写回目标。」

### 2.3 ControlAnim：接近 Punch/Shake 的物理感

`ControlAnim` 提供二阶 LTI / 阻尼弹簧 / PD，可 `impulse`，适合「被击弹开」「跟手弹簧」类运动。这与 LitMotion 的 Punch/Shake（阻尼振荡专用 Adapter）问题域重叠，但 API 形态不同（连续跟踪目标，而非有限时长的振荡补间）。

### 2.4 已有但不是 Tween 的邻近能力

| 能力 | 位置 | 与 LitMotion 关系 |
|---|---|---|
| 骨骼 Clip / Graph / 状态机 / Motion Matching | `Anim*` | 资产驱动动画，不是属性补间 |
| Sprite / Spine 播放器 | `SpriteAnim` / `SpineAnim` | 帧动画；可与 Tween 叠加变换 |
| AnimTrail | 轨迹采样 | 视觉效果，非补间内核 |
| Avatar `bindTween` | 唯一内置「写回绑定」 | 硬编码属性名，不可扩展 |

## 3. 能力对照表

| LitMotion 能力 | EVEngine 现状 | 差距 |
|---|---|---|
| Create(from,to,duration) + 链式 With-* | 命令式 setter | DX 差距大；语义可对齐 |
| Ease（含 AnimationCurve） | 字符串子集；无自定义曲线 | 缺 Bounce/Elastic/Back；无曲线资源 |
| Delay / Loops / Yoyo | 有 | 基本对齐（LoopType 仅 Yoyo 布尔） |
| Bind / BindToPosition 等 | 仅 Avatar pull；无通用 binder | **最大产品缺口** |
| MotionHandle（Complete/Cancel/await） | 对象指针 + start/stop | 无代次句柄；无 Complete 跳到终点；无 await |
| Scheduler（Update/Fixed/Manual） | `SimulationStep` 统一泵 | 够用；可不照搬多 Scheduler |
| Sequence Append/Join/Insert | 无 | 需编排层 |
| Punch / Shake | ControlAnim 可近似 | 缺一等公民有限时长振荡 API |
| 类型 Adapter（Vec2/3/Color/Quat） | 仅 float 命名轨 | 脚本靠多轨模拟；无 Color/Quat |
| 零分配 dense storage | `new Tween` 堆对象 + `vector<Tween*>` | C++ 热路径有分配；脚本 GC 另计 |
| UniTask / Rx | 无 | 可用脚本协程 / 回调 / Result 事件代替 |
| Inspector 可编辑 Animation | 无（有骨骼编辑器，非 Tween 编排） | 后置编辑器工作 |

## 4. 不要照搬的部分

1. **Burst / C# Job System**：Linux/Windows 原生引擎没有等价物。应用 C++ SoA + 紧凑 `MotionSlot` 数组 + `SimulationStep` 批量 `advance` 达到同类目标。
2. **组件扩展方法（`transform.DOMove` 风格）**：LitMotion 自己也不推荐；我们保持 `anim.newMotion(...)` / `LMotion` 式工厂入口，符合 `API-CONVENTIONS.md` 的模块工厂模式。
3. **闭包 Bind 作为唯一绑定方式**：Squirrel 闭包捕获会分配；应同时提供：
   - 无闭包的 **typed binder**（写 Scene 节点 / Sprite2D / UI 控件）；
   - 可选脚本回调（明确标注分配与可重入契约）。
4. **把 Sequence 做成 DOTween Pro 全集**：LitMotion 设计哲学也偏向 async 组合。我们优先做瘦 Sequence + 脚本侧顺序编排。
5. **丢弃 Result / 确定性时间**：新 API 必须走 `advance(SimulationStep)`，回调不得在持锁时调用未知脚本；符合架构门禁。

## 5. 推荐落点：在 `animation` 模块内演进，而不是新模块

理由：

- Tween 已注册在 `Animation` 泵里；时钟、测试、脚本入口已存在。
- 跨模块写回（Scene / Graphics UI）用 **binder 接口 + `eve::cap`**，避免 `animation` 直接 `#include` 上层模块（layering 规则）。
- 不新建 `eve.LitMotion()`，避免双轨 API；对外可保留 `Tween` 兼容，新增 `Motion` / `MotionSequence` 作为 LitMotion 对齐层。

建议类型划分：

```
animation/
  Tween.*              // 保留：命名标量轨（兼容）
  Motion.*             // 新增：typed value + handle + binder
  MotionEase.*         // 缓动表 + 可选 AnimationCurve 采样
  MotionSequence.*     // Append / Join / Insert
  binders/             // 仅接口 + 引擎内建 binder；上层模块注册
```

## 6. 目标 API 草图（Squirrel 友好）

### 6.1 单值 Motion（对齐 LMotion.Create）

```squirrel
local anim = eve.Animation();
local h = anim.motion(0, 200, 0.6)
    .ease("outQuad")
    .delay(0.1)
    .loops(2, "yoyo")
    .onComplete(function() { /* ... */ })
    .bindSpriteX(sprite);   // 内建 binder：每帧 push 到 Sprite2D.x

// 或手动 pull（兼容旧习惯）
local h2 = anim.motion(0, 1, 1.0).ease("linear").run();
local v = h2.value();
```

C++ 侧对应：

- `MotionBuilder`（或一次配置的 `MotionSettings`）在 `bind*` / `run` 时入库；
- 返回 `MotionHandle{storageId, generation}`；
- `handle.complete()` / `cancel()` / `isActive()` / `value()`。

### 6.2 Sequence

```squirrel
anim.sequence()
    .append(anim.motion(0, 1, 0.3).bindSpriteX(s))
    .appendInterval(0.1)
    .join(anim.motion(0, 1, 0.3).bindSpriteY(s))
    .insert(0.05, anim.motion(1, 0, 0.2).bindSpriteAlpha(s))
    .run();
```

约束对齐 LitMotion：禁止把「已在播放」或「无限 loop」的 motion 塞进 sequence。

### 6.3 Punch / Shake

```squirrel
anim.punch(0, 12, 0.4).frequency(18).damping(0.0).bindSpriteX(s);
anim.shake(Vec3(0,0,0), Vec3(0.2,0.2,0), 0.5).bindNodeLocalPos(node);
```

实现选项：

- **A（推荐先做）**：专用 Adapter，有限时长阻尼正弦（与 LitMotion 同语义，易测）；
- **B**：包装 `ControlAnim::impulse` + 自动衰减停止条件（更物理，难与时长精确对齐）。

### 6.4 跨模块 Binder（Capability）

```cpp
struct IMotionFloatSink {
    virtual ~IMotionFloatSink() = default;
    [[nodiscard]] virtual Result<void> write(float v) = 0;
};
// Scene / Graphics / UI / Avatar 各自 provide；animation 只 query
```

Avatar 现有 `bindTween` 可迁移为 `FloatSink` 实现，去掉硬编码属性名列表的扩展成本。

## 7. 存储与性能模型（C++）

对齐 LitMotion「零分配」意图的最小可行设计：

1. `MotionStorage`：按 value 类型分池（float / Vec2 / Vec3 / Color / Quat），`vector` 预留容量；
2. 槽位 = POD 状态（from/to/duration/elapsed/easeId/flags）+ 可选 `BinderId`；
3. `MotionHandle` = `{u32 index, u32 generation}`；销毁只增 generation，不立即压缩（或延迟 recycle）；
4. `Animation::advance` 单次遍历各池 `update` → `binder.write`；
5. 脚本仍可 `newTween` 走旧路径；新 API 默认走 storage，避免每条 motion 一次 `new`。

Squirrel 侧对象若必须存在，用轻量 handle wrapper（只持 `MotionHandle`），不要为每条运动分配完整 `Tween` 镜像。

确定性：所有时间推进只吃 `SimulationStep`；binder 写回失败返回 `Result`，可选 `cancelOnError`。

## 8. 分阶段落地（按风险 / 价值）

### Phase 0 — 文档与契约（本文件）

明确：兼容保留 `Tween`；新能力走 `Motion*`；跨模块只经 binder 接口。

### Phase 1 — DX 与绑定（最高价值）✅ 已落地（本分支）

- `MotionBuilder` + float/Vec2/Vec3 binder
- `MotionHandle`：complete / cancel / isActive
- 内建 binder：`Sprite2D` 变换、Scene 节点 local TRS、Avatar 参数（替换硬编码 pull）
- 回调：`onComplete` / `onCancel`（文档标明脚本闭包分配；C++ 用函数指针 + userData）
- 测试：`test/animation_motion.cpp` 过程隔离用例

### Phase 2 — Sequence + Ease 扩展 ✅ 已落地（本分支）

- `MotionSequence` Append / Join / Insert / AppendInterval
- Ease：Back / Elastic / Bounce（`evaluateMotionEase`）
- Squirrel：`newMotion` / `newMotionSequence` + Handle 包装
- 与 Phase 1 共用 `MotionRuntime` storage
- 测试：`test/animation_motion.cpp`（ease + sequence）

### Phase 3 — Punch / Shake + Color/Quat Adapter ✅ 已落地（本分支）

- 专用振荡 Adapter；Color/Quat 插值（Quat 用 slerp）
- 可选：文本 scramble（仅当 UI 文本模块有稳定 write sink）

### Phase 4 — 性能硬化 ✅ 已落地（本分支）

- `ensureCapacity`、池化 recycle、batch binder
- 基准：N=10k float motion update（对照当前 `vector<Tween*>`）
- 不引入第二套时钟

### Phase 5 — 编辑器（可选，对齐 LitMotion.Animation）

- 可序列化 `MotionSettings` / Sequence 资产
- 挂到现有 animation editor 工作区，而不是 Unity Inspector 仿品

## 9. 与现有架构规范的对齐清单

实施时必须满足（见 `docs/dev/重构代码质量与系统完整性规范.md` 等）：

- **Result**：`advance` / binder `write` / `complete` 失败不可丢弃；禁用模糊 `bool`。
- **所有权**：Motion 槽由 `Animation` 模块拥有；Handle 可失效；目标对象销毁时 binder 必须 stale-safe（generation 或 weak id）。
- **时间**：注入 `SimulationStep`；声明与 replay 的确定性契约（同一 tick 写回顺序稳定）。
- **回调可重入**：文档规定 binder/回调阶段禁止再 `sequence.run` 嵌套修改同一 storage（或 deferred queue）。
- **可选依赖**：无 Scene / 无 UI 的裁剪构建下，对应 binder 注册缺失时工厂返回明确 `Unavailable`；契约测双配置。
- **模块边界**：`scripts/module_depgraph.py --check`；animation 不 include scene/ui。
- **质量元数据**：新 TODO/FALLBACK 必须带 owner/issue/expiry。
- 交接前跑：`ARCHITECTURE_BASE=HEAD make check/architecture-contracts`。

## 10. 结论

- LitMotion 的「产品体验」= **Builder + Push Bind + Handle + 瘦 Sequence + 特殊运动**；「性能故事」= **struct storage + 预扩容 + 无闭包热路径**。
- EVEngine **已经具备** LitMotion 内核的约 40%：标量补间、delay/repeat/yoyo、确定性泵、Avatar 特例绑定、弹簧式 ControlAnim。
- **最大缺口是 Push 绑定与编排（Sequence）**，不是再写一个缓动函数表。优先做 Phase 1–2，即可在脚本侧达到「一行补间到 Sprite/节点」的体验；零分配存储属于 Phase 4 硬化，可与 API 解耦推进。
- 实现上应 **演进 `animation` 模块**，用 Capability binder 连接上层，而不是移植 Unity DOTS 或新开平行 Tween 库。

## 参考

- LitMotion README / Design Philosophy / Sequence / Binding 文档（annulusgames/LitMotion）
- `src/modules/animation/Tween.h`、`Animation.h`、`ControlAnim.h`
- `docs/usr/modules/animation.md`
- `docs/dev/2d-sprite-animation-api-evaluation.md`（Tween pull 模型既有结论）
- `src/modules/avatar/AvatarSkeletal.cpp`（`bindTween` pull 写回）
