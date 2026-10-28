# 实时材质 Studio 设计

日期：2026-08-31

## 产品目标

材质 Studio 面向需要快速校色、检查 PBR 响应并安全发布到运行中对象的技术美术。单一核心任务是：
调整任意材质参数时立即看到与当前 revision 对应的预览，并把一次连续手势作为一次可撤销修改。

推荐宿主布局是“暗色校色台”：中央为大尺寸隔离预览，右侧按 Shading、Textures、Surface、
Parallax、Shadow 分组显示 schema 属性；顶部切换 sphere、cube、plane、自定义 mesh 与 environment；
底部持续显示文档 revision、预览 revision、发布状态和结构化诊断。主预览周围保持中性深灰，避免 UI
色彩影响材质判断。唯一强调色用于 active gesture 和成功发布状态，不用于装饰。

窄窗口下右侧 Inspector 移到预览下方；键盘焦点、搜索和诊断顺序由宿主 UI 负责，但字段、分组、
范围、枚举与资产过滤必须来自 `MaterialDocumentTarget::schema()`，不得复制第二份材质元数据。

## 状态与数据流

`MaterialStudioController` 是 UI 无关的交互编排器：

1. `beginInteraction(path)` 从 live document 创建 owned draft。
2. slider、color picker 或 asset picker 的中间值只写 draft。
3. `tick(monotonicMs)` 以宿主注入的单调时间按上限刷新隔离预览；默认约 30 Hz。
4. `commitInteraction()` 只把最终值生成一个 `DomainOperation`，经 transaction preflight 后原子发布。
5. `cancelInteraction()` 丢弃 draft，live runtime 从未被中间值污染。

预览请求包含不可变 snapshot、document revision、独立 scene ID、mesh/camera/environment 设置。
`MaterialPreviewService` 只发布 revision 匹配的成功 artifact；过期或失败结果不能覆盖当前预览。

## 权威、生命周期与失败

- `MaterialPublishingTarget` 是 authoring 文档的唯一权威，runtime sink 只是一次完整 candidate 的投影。
- Studio 拥有 draft 和可观察 UI 状态；target、transaction backend、preview service 与 renderer 均为同步借用。
- 所有 API 仅 owner thread 使用，不在 renderer/transaction callback 中重入。
- renderer 不访问 live scene；自定义 mesh 和 environment 通过 asset resolver 在隔离 scene 中解析。
- 非法字段值、跨字段错误、预览设置错误、事务冲突和 runtime 发布失败均返回结构化 `EditorResult`。
- commit 失败时保留 live 权威状态；宿主可展示诊断并允许用户重试或取消。

## 确定性与性能

- 控制器不读取 wall clock；宿主传入 monotonic milliseconds。
- 预览节流范围为 1–240 Hz，默认间隔 33 ms；rate-limit 返回 `NoOp`，不是失败。
- 每次手势只生成一个 transaction/history entry，避免 slider 拖动放大持久化与 runtime 上传成本。
- draft snapshot 是 owning value，不保存 GPU 指针或跨帧借用。

## 组合与裁剪

Studio 位于高层 `editor` 产品模块，复用 `editing`、transaction 和 material runtime sink；graphics runtime
不反向依赖 editor。UI host 可以是 ImGui、游戏内 UI 或自动化客户端。没有 renderer provider 时，文档编辑和
事务仍可工作，宿主应明确显示“preview unavailable”，不能伪造 fallback artifact。

## 验证矩阵

- 连续三个 draft 更新产生三个预览、零次 runtime publication，commit 后只增加一个 revision 与一次 publication。
- cancel 后 live revision/值不变，并可重新渲染 live preview。
- 单调时间节流、时间回退拒绝、非法 preview rate。
- 既有 material schema/validation/snapshot、preview stale publication、runtime sink rollback 测试继续覆盖底层契约。
- `check_architecture_contracts.py --base HEAD` 与 `module_depgraph.py --check` 必须通过。
