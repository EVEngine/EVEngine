# Audio / Animation 异步预览与 provider 生命周期

## 共享机制与领域接入

`editor/presentation/AsyncPreview<T>` 复用 `editing::TaskService`，负责请求替换、协作取消、provider generation、结果邮箱、失败与关闭状态。它不拥有领域文档、history、provider registry 或第二份已发布预览。每个领域 editor 独占一个隐藏的 preview session；provider 由该 session 与正在执行的工作共享拥有。

Audio 的宽度和循环区域变化提交波形后台任务；Animation 的 seek、选择、文档编辑、撤销重做、runtime clip 导入和播放时间变化提交姿态/叠加图任务。两个编辑器的 `update()` 在 owner thread 轮询结果。初始化同样异步，第一份结果到达前没有预览，不进行同步备用计算。Audio 示例继续保持静音，异步预览不会 attach live audition。

`requestPreview()` 是 expensive API 的请求阶段，复制一次 owning 输入后返回 Pending；`pollPreview()` 是结果阶段，不等待工作、不重新计算预览。Audio 的 PCM、请求宽度、循环参数与文档 revision；Animation 的完整目标文档、时间、骨骼层级与选择，均由工作拥有。工作不捕获 editor、临时 source adapter 或运行时骨骼指针。现有宽度/seek/编辑入口自动提交请求，成功表示输入已接受，预览稍后可见；不能再用返回成功推断预览已刷新。

Animation 编辑命令仍从权威文档采样当前变换，避免连续编辑依赖旧的后台显示结果。该采样复用领域原有算法，未新增插值实现或可写缓存。Inspector 的查询沿用领域 getter；完整展示预览及骨骼 overlay 在后台生成。

## 请求、generation 与取消

- `PreviewState.request` 是 session 内递增的请求序号；`providerGeneration` 从 1 开始，每次成功 `setPreviewProvider()` 均递增，包括重新设置同一 provider 实例。计数器耗尽会结构化拒绝，不回绕。
- 每次请求拥有独立、不可由外部伪造的 completion mailbox。替换请求、取消、provider 替换和关闭会丢弃 owner 对旧邮箱的引用；即使旧 provider 忽略取消并延迟返回，也无法重新成为当前结果。序号/generation 是 UI 观察值，不是虚构的 RuntimeHandle。
- `setPreviewProvider(shared_ptr<const I...PreviewProvider>)` 是直接、已知、一对一的依赖注入，不做按帧 registry 查询。绑定只更新依赖与 generation；调用方再 `requestPreview()` 发起昂贵工作。热重载或重新绑定必须经过该入口。
- `nullptr` 明确表示 provider 缺席，状态为 Unavailable；显式请求返回 Unsupported，保留最后成功预览，不隐式切换默认 provider。恢复 provider 后显式请求恢复预览。编辑与 undo 本身仍可使用。
- `cancelPreview()` 立即撤销待发布结果，保留已发布数据；后续输入变化或显式请求可以恢复。`closePreview()` 不可重开且幂等，释放 session 持有的 provider，拒绝后续请求与重绑。运行中工作持有自己的 provider 和输入直到返回。
- 默认 provider 的取消粒度为原生采样/波形计算边界，Animation 还逐骨骼检查取消。不会强行终止 C++ 线程。输入有界；自定义 provider 必须限制计算时间并检查 TaskContext。关闭立即禁止发布，析构再 join 协作工作，不对无期限阻塞 provider 承诺有界析构。

TaskService 为单 worker，当前请求不会并发执行；过期工作可能在更新请求提交后返回，之后才执行最新排队工作。每个 session 最多保留一个运行工作和一个最新排队工作。持续输入时保留上一份预览，停止变化后发布最新结果。没有另建线程池。

## 原子发布与状态

Audio 完成时验证捕获的文档 revision、PCM revision、期望宽度、asset 与结果状态，之后一起发布 waveform 与 viewport width。请求宽度与上次成功展示宽度是不同阶段的数据，不互相冒充。

Animation 完成时验证请求时间、选择、文档 revision，以及 overlay target/revision/status。pose 与 overlay 包含在同一 owning publication 中；验证全部通过才一起移动到领域展示成员。循环 clip 的 end time 允许采样到起点，不误判为过期请求。provider 返回错误、异常或不一致的 overlay 不会部分替换上一份展示。

状态有 Idle / Pending / Ready / Failed / Cancelled / Unavailable / Closed。异常转为结构化 Result。取消后无人消费的 Result 以明确原因释放，满足 Result observation 约束。`presentationState()` 为两个脚本类共同输出 preview/request/generation；共享 Inspector 显示 preview 状态。共享 `presentationClose()` 取消草稿和 preview；脚本宿主关闭时先停止 transport，再关闭预览并释放脚本引用。

## TaskService 所有权修复

新增 `TaskService::discard(TaskId)`：调用方放弃任务，排队工作被移除并释放捕获；运行工作被取消，返回后自动移除记录。已完成工作也可释放，避免逐帧预览累积 task records。未知 id 返回 NotFound。`waitIdle()` 仍计入被 discard 但尚未返回的运行工作。

provider 回调、捕获对象析构与领域发布均在 service/mailbox 锁之外。按帧 poll 只检查当前 mailbox，不查询 task 快照或 provider registry；TaskService 的任务身份管理只发生在请求/取消/释放边界。

## 边界与验证

六面边界、线程约束与成本形态记录在唯一 `scripts/architecture_contracts.json`。provider 接口位于领域自己的 editor 目录，外部依赖通过前置声明和私有 session 隐藏；未新增跨域向上 include、模块 boot list、ECS、持久格式或布局存档。

`test/editor_async_preview.cpp` 与原有共享 presentation 契约一起在完整宿主以及独立 CPU/Squirrel 组合目标运行。独立目标编译真实领域 editor/provider/shared task sources，不链接 Audio、Sound、Graphics 或动画 runtime 桥接，是此组合的 L2 裁剪证据；runtime-3d 的整个宿主 L2 裁剪不在声明内，manifest source-only 检查仅是 L1 证据。

测试使用可控、最长 3 秒阻塞的 provider 强制产生延迟旧结果，覆盖请求替换、同 revision 的 seek/选择变化、相同实例 provider 重绑、缺席/恢复、取消/幂等关闭、源销毁时 owning 输入、provider 异常、失效 payload、pose/overlay 原子拒绝与循环终点。旧 Squirrel UI journey 验证共享 close 会关闭 native preview。任务测试验证排队捕获及时释放、运行任务仍计入 idle，以及完成记录回收。

原有同步断言测试改为测试线程上的有界 owner pump；生产脚本和 `update()` 不等待。没有以测试 sleep 代替关键执行顺序：竞态用例依靠 provider 入场与释放栅栏；销毁用例的释放线程仅用于让阻塞 provider 从析构期间返回。

## 本批验证结果（macOS）

- 完整 Debug `eve` 与 `unit_test` 编译链接通过；两个领域各有 7 个共享 presentation 脚本方法，API catalogue 总计 10702 条。
- 独立 CPU/Squirrel 组合目标 17/17 通过，失败断言输出也会使其 CTest 失败。
- 完整宿主筛选 Audio / Animation / presentation / task 相关 52 项；逐项检查 verbose 日志后，51 项无断言问题，另 1 项既有 `editor.animation.clip_snapshot_and_retarget_preview_are_non_destructive` 发出 retarget 断言警告。它要求将 `mixamorig:Hips` 匹配到 `hips`，当前与 HEAD 相同的 normalized 实现并不剥离该前缀，导致 `map::at` 找不到 key。该文件与算法未在本批修改；未以跳过或 baseline 隐藏。现有完整宿主 CTest 对 CHECK 警告仍返回通过，因此这里不声称 52 项全部正确。
- 本批涉及的原生与脚本集成测试改为有界 owner pump 等待异步结果，并按 panel id/context 验证组成，按实际 track bone 找 pointer 行，不再假定 region 排序或旧的两骨骼 seed。
- 两个示例各运行 6 秒通过 smoke；Audio 首次尚未输出首帧，延长到 12 秒后再次通过并检查引擎自存渲染帧。音频保持静音。Animation 的引擎自存帧也已检查。生成的示例 PNG 已恢复，不提交自动截图。
- `TMPDIR=/private/tmp make check CI_BASE=HEAD` 通过，含 183 项脚本测试；`git diff --check` 通过。沿用本补丁开始前的 HEAD 作 diff base，未新增 allowlist 或基线。
- 适用的唯一状态 owner、Result、owner/worker 生命周期、两阶段成本 API、失败原子性、共享契约及 optional-provider 双配置均有实现或测试证据；无规范例外。没有 ECS 或持久格式变更。

最终渲染复核：静音 Audio 窗口在失焦时仍正确显示 `Preview ready`。共享 UI journey 也覆盖失焦状态更新与关闭后的 native Closed 状态。
