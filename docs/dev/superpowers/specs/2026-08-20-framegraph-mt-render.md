# FrameGraph 多线程渲染迁移（基于 PR #134）

> 日期：2026-08-20 · 分支：`codex/framegraph-mt-render`（PR #134 的续作，
> 已含 Stage A + Stage B）
> 目标：把渲染管线切到 vkbuilder 的 FrameGraph 上，用 JobSystem 做帧数据准备与
> 并行命令录制，最终让主线程跑游戏脚本时不再同步占用渲染资源。

## 1. PR #134 审查结论

PR #134（`feat(thread): JobSystem with parallel_for, task_group, dependencies and
frame arena`）的**调度器部分质量良好**：JobSystem 抽象、frame arena、
`parallel_for` / `task_group`、24 个 zeroerr 用例都值得保留。但它把"并行渲染"
的实现建立在**二级命令缓冲（secondary command buffer）**上，这部分存在硬伤：

1. **录制不符合 Vulkan 状态机**。分支顶端的 `recordShadowCascadeSecondary` /
   `recordGBufferSecondary` 在二级 CB 里直接 `beginRenderPass`（`eInline`），且
   `vk::CommandBufferBeginInfo` 没有 `pInheritanceInfo`。对 `eSecondary` 缓冲，
   前者要求 `RENDER_PASS_CONTINUE` + 继承信息，否则 validation layers 报错，
   严重时驱动挂起/重置。分支里的提交 `1291df8` 曾尝试按规范修复
   （`RecordingSecondary` / `ReadySecondary` / `RenderPassExecutor` 结构），
   但随后被 `1e19cce` 整体 revert——说明这条路线没有走通，**不是继续补丁，而是
   换架构**。
2. **录制仍然主要在主线程**。`gfx.render3D()` 由 Squirrel 脚本同步调用
   （`load.nut` 的 `eve_frame()` → `eve_render()` → `gfx.present()`），
   `prepareFrame3D` 里主线程做了 ECS 快照后 `wait()` 等 JobSystem，之后 forward
   pass 和 layout 转换仍全部在 `currentPresentCb()` 上串行录制。JobSystem 只
   并行化了 CPU 数学，没有把"渲染"从主线程挪走。
3. **剔除回归**。prep 阶段把所有 item 都拿**主相机**的 `viewProj` 做视锥剔除
   （`sphereInFrustum(mainViewProj, ...)`），忽略了 `mr->camera` 指定的独立
   相机；G-buffer 与 shadow 的 per-cascade / per-camera 剔除也丢了。主相机看不到、
   但副相机在视野里的物体会被错误剔除。
4. **没有使用 FrameGraph**。pass 顺序、layout 转换、barrier 仍硬编码
   （`recordDeferredPassesParallel` + 手写 `begin*/end*SampledLayout`），这正是
   VKBuilder FrameGraph 要消灭的写法；`GenericImage::currentLayout` 单值跟踪在
   跨帧/跨线程下本来就容易错。
5. **真正的渲染线程解耦未做**。即使修好二级 CB，脚本线程仍然阻塞在整帧的
   acquire/record/submit/present 上。

结论：**保留 JobSystem，放弃二级 CB 路线，改为 FrameGraph 声明式 pass +
JobSystem 并行录制**。本分支已删除二级 CB 录制，恢复合法的串行录制路径，
并修复了上述剔除回归。

## 2. 目标架构（end state）

```
游戏线程 (Squirrel)                  渲染线程 (RenderThread)
─────────────────────                ──────────────────────
eve_update(dt)                       等待帧命令
  │                                   │
  ├─ 场景/逻辑修改 ──────────────────►│ 读取只读快照
  │                                   │
eve_render()                          │
  ├─ 2D/3D 绘制 → CPU 侧帧命令列表 ──►│ 消费 draw list
  └─ 立即返回（不碰 GPU）              │
                                      ├─ FrameGraph: addPass/compile（主线程外）
                                      ├─ record(jobSystemPassExecutor) ← JobSystem
                                      ├─ submit / present
                                      └─ 回到等待
```

关键契约（沿用 VKBuilder FrameGraph 的线程模型）：

- **构建与 `compile()` 在渲染线程**（当前阶段先留在主线程，下一阶段再搬）。
- **`record()` 内 pass 回调可并发**；回调只能写自己的 CB 和只读数据。
- **descriptor set 更新必须在 `record()` 之前**完成（`vkUpdateDescriptorSets`
  默认非线程安全）；引擎的 UBO ring 上传、`mesh3dSetFor` 惰性建 descriptor
  都要前移到帧数据准备阶段。
- **资源生命周期**：渲染线程可能在用某张 texture/mesh 时游戏线程删除它，
  需要引用计数 + 延迟销毁（`waitForSharedGpuResources` 只是兜底）。
- **每帧快照**：`FrameDrawList3D`（PR #134 已有）作为游戏线程写、渲染线程读的
  双缓冲结构，稳态不分配。

## 3. vkbuilder FrameGraph v1 的边界（已核对源码）

`external/VKBuilder/include/vkbuilder/framegraph.hpp`（commit 2bd5dd4）：

- 支持：声明式 `addPass().colorAttachment/depthAttachment/sample/read/write`、
  自动依赖边 + 拓扑排序、输出驱动 culling、layer 分层、attachment→sampled
  折叠进 RP finalLayout、其余转换合并为每 pass 一条 prologue barrier、
  RP/FB/CB 签名缓存、`record(executor)` 并行录制、`importSwapchain` 后自管
  acquire/submit/present。
- 限制（迁移时必须处理）：
  1. **pass/resource 集合跨帧稳定**：`addPass`/`createTexture` 只增不减，没有
     per-frame reset；"每帧重建图"指的是重新 `compile()` 规划，不能每帧新加
     pass。动态启停的 pass（如 GBuffer 关闭）用空 draw list + clear 表达。
  2. **没有 swapchain 时不推进帧槽**：`present()` 无 swapchain 直接 return，
     `frameSlot_` 固定为 0，ring 资源只会用第 0 份。要么让图 import swapchain
     自管整帧（推荐，终态如此），要么给 VKBuilder 打小补丁支持无 swapchain
     推进帧槽。
  3. **附件只能用一个 view**：`TextureDesc` 没有 `baseArrayLayer`/`layerCount`
     view 选择，2D array 附件会一次渲染全部 layer。CSM 的 `DepthArrayImage`
     （3 cascade 一层）无法直接建模为 3 个 per-cascade pass；两条出路：
     a. 上游 VKBuilder 增加 `baseArrayLayer`（推荐，改动 ~15 行）；
     b. shadow map 改为 3 张独立 2D depth + 改 mesh3d shader/descriptor
        （sampler2DArrayShadow → 3× sampler2DShadow）。
  4. **单 subpass、单 graphics queue**：无 async compute/transfer、无 transient
     内存别名、无 input attachment 特化——与本迁移无关，后续再补。
  5. **导入资源 framesInFlight 恒为 1**：`importTexture` 只挂一份物理对象；
     引擎现有的双缓冲 gbufferSlots/shadowMaps 要么迁成图内 `createTexture`
     （framesInFlight=2），要么保持引擎自管 + 每帧 import（会累积 handle，
     不可取）。

## 4. 分阶段实施计划

### Stage A（已落地）

- 从 PR #134 分支拉 `codex/framegraph-mt-render`，删除不合规的二级 CB 录制，
  恢复合法录制路径。
- 修复 prep 阶段剔除回归：per-item camera 视锥剔除（`culled`）、主相机剔除
  （`culledMain`，G-buffer 用）、per-cascade 掩码（`cascadeMask`，shadow 用）；
  相机 view-proj 在主线程预收集，worker 只读。
- 新增 `src/modules/graphics/vulkan/FrameGraphJobs.h`：把 JobSystem 接到
  `vkb::PassRecordExecutor` 的粘合层（layer 内 fork/join，layer 间顺序执行）。
- 新增 `test/render_graph.cpp`（零 GPU 依赖）：
  - 引擎目标 pass 拓扑（shadow×3 + gbuffer 同层并行 → forward → present）的
    规划断言：分层、依赖序、attachment→sampled 折叠（forward/present 零显式
    barrier）；
  - JobSystem executor 并行性与"每个 pass 恰好录一次"的断言；
  - 空 JobSystem 串行回退断言。

### Stage B（已落地，GPU 渲染已在本机验证）

- 把 G-buffer 迁进 FrameGraph（本分支已实现）：
  - **每个 in-flight slot 一个 `vkb::FrameGraph`**（`framesInFlight=1`），以
    `importTexture` 导入引擎自有的 normal/depthColor/albedo/hwDepth 图像——
    引擎保留所有权，`renderEntityIdMask` / `readGBufferToImageData` 等直连
    ColorTarget 的 readback/离屏路径完全不动。
  - `addPass("gbuffer")` 声明 3 个 colorAttachment + 1 个 depthAttachment，
    图自动规划 barrier 与 RP initial/final layout（清屏 +
    `ShaderReadOnlyOptimal`/`DepthStencilReadOnlyOptimal` 收尾）；手写的
    `beginColorAttachment/endSampledLayout` 从正常帧路径删除。
  - `recordPendingGBufferPass` 改为：`jobSystemPassExecutor` 录制 +
    `graph->submit()`，在 swapchain pass 之前单独提交（同队列，队列序保证
    forward 采样在 GBuffer 之后）。
  - **CB 复用安全**：framegraph v1 无 swapchain 时不推进帧槽，单图每帧复用
    同一个 CB 会与还在执行的上一帧竞争。按 present 槽位交替两个图后，每个
    CB 相隔两帧才复用，而 `Present::begin()` 会等当前槽位 fence（两帧前的
    提交，同队列上晚于 graph 提交）→ 无竞争。上游 VKBuilder 若补上
    swapchain-less 帧槽推进/自管 fence，可再塌缩回单图。
  - 管线兼容：gbuffer pipeline 仍基于引擎自建的 `gbufferRenderPass` 创建；
    图的 RP 与它在 format/samples/subpass attachment layout 上完全一致，
    Vulkan RP 兼容规则允许跨 RP 使用。

### Stage C（渲染线程解耦，核心收益）

- 新增 `RenderThread`：持有 swapchain + FrameGraph，`importSwapchain` 后
  `beginFrame/compile/record/submit/present` 全部在渲染线程。
- `gfx.render3D()` / `gfx.present()` 改为入队：游戏线程只写 `FrameDrawList3D` +
  2D batch 快照，立即返回；渲染线程消费上一帧快照（天然流水线并行，
  frame latency = 1）。
- descriptor 预更新：UBO ring 写入、descriptor set 创建/更新移到入队阶段；
  `record()` 回调只 bind。
- 资源生命周期：GPU 对象引用计数 + 延迟销毁队列（渲染线程 drain）。
- 脚本 API 语义调整：`render3D` 不再同步 present；`ui.dispatchEvents`、
  readback（MCP frame capture / RenderVision）改走渲染线程完成回调。

### Stage D（shadow/forward/present 全部进图 + 上游补丁）

- VKBuilder 增加 `TextureDesc::baseArrayLayer`（或 shadow 改 3 张 2D），CSM 3
  pass 进图（与 gbuffer 同层并行）——**shadow 部分已在本分支落地**（见下）。
- forward（场景色 + AO/outline + 2D/UI）与 present 作为图内 pass；
  `recordPending*Passes` 与手写 layout 跟踪全部删除。
- 评估 VKBuilder 无 swapchain 帧槽推进补丁，供引擎自管 present 的过渡期使用。

### 已落地的 Stage D（shadow 进图）

- 每个 present 槽位的 deferred FrameGraph 现在包含 **3 个 CSM cascade pass +
  G-buffer pass**（4 个声明式 pass，依赖无关、同一 layer）。
  CSM 是 2D array 深度图，每个 cascade 用 `layerView(c)` 作为附件视图导入，
  `arrayLayers=3` 让 barrier 覆盖整个数组；所有 cascade 结束于
  `DepthStencilReadOnlyOptimal`，forward pass（引擎侧）照常采样数组视图。
- `recordPendingShadowPasses` / `recordPendingGBufferPass` 删除，合并为
  `recordDeferredFrameGraph()`：图 record + `submit()`，在 swapchain pass 之前
  单独提交。
- **每槽每帧只录一次守卫**：`render3D` 可被脚本一帧调多次（测试里 3 次），
  同帧内不重复录制图 CB（否则 reset 在途 CB 会挂起驱动）。
- 录制默认**串行**：并行 executor（`jobSystemPassExecutor`）已就绪并通过 CPU
  压力测试，但 JobSystem 的 help-execution 与 frame arena 生命周期之间还有
  一个残余竞态（见下），修复前引擎路径用 `graph->record()`。

### JobSystem 竞态修复（PR #134 的调度器 bug）

- `State::completeJob` 原来在 completion callback / dependent release **之前**
  递减 `outstandingFrame`；`beginFrame/endFrame` 看到归零立即 `arena.reset()`，
  而 worker 还在用该 job（`fireCompletion`/`releaseDependents`）→ use-after-free，
  表现为 fork 出的 job 丢失（join 挂起）或崩溃。修复：递减移到完成回调之后。
- 新增 `render_graph.jobSystemGroupStress` /
  `render_graph.jobSystemEngineFramePattern` 两个压力测试作为回归覆盖。

### 并行录制的最终结论（WSL/TSan + Windows 驱动对照）

- **CPU 侧无数据竞争**：用 WSL + `-fsanitize=thread` 对完整帧模式
  （beginFrame → light/parallelFor → endFrame + 4 路 fork/join）、真实
  executor（`jobSystemPassExecutor`）与真实 framegraph 规划（header-only，
  直接包含）跑 3 万轮 × (硬件并发/4 worker)，零报告、零丢 job。
- **崩溃在驱动层**：Windows 上并行录制时，两个 worker 的 AV 位于
  `amdvlk64.dll` 内部，调用栈顶是 `vkb::FrameGraph::record` 的 recordOne；
  `device->waitIdle()` 后照崩（排除在途 CB 复用），且 Vulkan 规范允许并发
  录制不同 CB——结论是 AMDVLK 驱动的并发录制缺陷（NVIDIA/Intel 可能正常，
  需在 CI lavapipe 上验证）。
- 因此引擎默认**串行录制**（`graph->record()`）；并行 executor 保留并
  TSan 验证，启用条件：驱动兼容性确认或 VKBuilder 侧把 `cb.reset()` 提到
  主线程、worker 只做 `vkBeginCommandBuffer + 录制 + end`。

## 5. 本分支验证

- `cmake\with-msvc.cmd cmake.exe -G Ninja -DCMAKE_BUILD_TYPE=Debug
  -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
  -DEVENGINE_THIRD_PARTY_BINARY_DIR=<prebuilt install> -B build/win32-debug -S .`
- `cmake --build build/win32-debug --target unit_test`
- `ctest --test-dir build/win32-debug -R "render_graph|thread"`（本分支新增 + 既有
  JobSystem 用例）
- GPU 冒烟：`make run/win32-debug GAME=examples/basic`（Stage A 后应无行为回归；
  Stage B 起需在带 Vulkan 的 CI 上验证）。
