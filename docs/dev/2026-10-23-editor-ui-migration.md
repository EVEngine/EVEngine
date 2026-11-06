# Audio / Animation 共用 UI 框架迁移

## 最新 dev 兼容调整（2026-10-25）

当前使用规范 `Result` 类型；Audio 绑定接入 `AudioEditorModule.cpp`，保留上游的共享绑定辅助函数。新增测试位于 `test/integration/editor-workspace/`，原有用例的调整移植到上游合并后的集成测试文件，不恢复旧路径或重复注册测试。独立 CPU 目标使用当前拆分/归并后的真实源文件，并编译检查两个领域的真实绑定入口。

当前 dev 的架构门禁以源码为准，不再使用已删除的 `scripts/architecture_contracts.json`；下文 catalogue 路径及旧验证数字属于原始实现基线的历史记录。冲突解决后的独立组合测试为 23/23，绑定生成器及架构检查器回归为 24/24。

## 开发入口

`src/modules/editor/presentation/WorkspacePresentation` 负责属性验证、拖动草稿、revision/context 冲突、单次提交和 action 路由。它不保存领域对象指针、不保存 history、不广播第二份领域状态。

领域提供 `IWorkspacePresentationSource` 的调用期适配器：AudioSourcePresentation 从 AudioSourceTarget schema 生成字段；AnimationClipPresentation 把 clip 设置和选中骨骼/关键帧映射为可编辑属性。write/action 继续调用原编辑器 canonical 操作与唯一 LocalTransactionBackend。

`src/scripts/editor_workspace_presenter.nut` 是共享 UI 宿主投影，随 bootstrap 嵌入 SDK。它统一创建 Inspector、挂载 Workspace 面板、消费 UI 事件、更新控件/撤销状态、处理快捷键与诊断。项目只提供其他面板配方、特殊选择操作和波形/骨骼/时间轴绘制。所有 `presentation*` 方法由同一个 C++ 绑定模板安装到两个领域类，使用公共 Result/Value 转换。

## 接入示例

```squirrel
view <- EditorWorkspacePresenter(ui, workspace, domainEditor);
view.mount(panelBuilders);  // Inspector 自动来自领域 schema
// 每帧：
view.update(keyboard, mouse.isDown(1), win.hasFocus());
// 退出：
view.close();
```

`presentationState()` 返回 revision/context/playing，用于廉价失效检测；`presentationSnapshot()` 返回 owning 行与历史可用性。`presentationEdit(path,value,true)` 暂存数值草稿，`presentationCommit()` 一次提交，`presentationCancel()` 取消；离散编辑传 false。`presentationAction(id)` 使用领域已有 history/transport，不创建第二条历史。失败统一返回结构化 Result。

拖动过程中暂停 transport，固定选中上下文；释放才写入文档，Esc/失焦/close 不写入。外部修改 revision 或选中骨骼、关键帧、播放位置会使旧草稿冲突。非法字段/类型/范围和只读字段不会提交。播放时只更新领域预览，不逐帧重建整个 Inspector；暂停或文档变化时同步。运行时 provider 不存在时原生编辑器保留 clock audition；示例按用户要求默认禁用音频输出，启动保持停止，Play/Pause 只推进静音可视播放头。原生 live audition 能力仍由领域显式 attach。

模板绑定依附 VM owned 领域对象，shared presenter 持有该脚本对象直到 close；native controller 每次只接受即时借用 source，因此无跨帧裸领域指针。close 取消草稿并隐藏宿主、解除脚本引用。

## 边界与验收口径

本切片不新增持久格式、ECS System 或 provider registry。以一对一高频且已知目标的调用期适配器满足 R-MECH；以 Result、明确 owner/lifetime、共享 contract tests 满足 API 与事务规范。module-interface 记录于唯一 architecture catalogue；未以 include 图声称 L2 裁剪。

下一切片继续统一异步 preview request 的序列/取消与 provider generation；本次沿用两个编辑器已有同步预览路径。多选 mixed 当前明确拒绝无单一值的写入；列表虚拟化、布局持久化和其他领域迁移属于后续切片。

独立 CPU + 真实 Squirrel UI 组合测试：

```sh
cmake -S test/editor_presentation -B build/editor-presentation \
  -DEVE_PRESENTATION_DEPENDENCIES=/absolute/path/to/third-party-install
cmake --build build/editor-presentation -j4
ctest --test-dir build/editor-presentation --output-on-failure
```

该目标编译 production editing/editor/领域适配器，不链接 GPU/audio runtime；UI journey 使用可观测的测试宿主，驱动真实 Squirrel presenter 与两个真实领域控制器。它不能替代引擎窗口 smoke 与截图。

一个 root presenter 消费一个 workspace 的 UI 全局事件队列；焦点丢失时排空旧事件并取消草稿，避免重新获得焦点后回放过期编辑。多 root 同时共享事件队列的 dispatcher 留待多文档宿主切片。

## 本批验证结果（2026-10-23，macOS）

- 完整 Debug 引擎与 unit_test 编译、链接通过；依赖使用与仓库 pin 一致的只读预构建库，下载依赖使用本机已缓存的相同源。
- 完整宿主中 `editor.presentation` / `editor.audio` / `editor.animation` 37 个 process-isolated CTest 用例通过。
- 无 GPU/audio runtime 的独立组合目标 5 个 CTest 用例通过，包含两个真实领域共享编辑契约、两个真实 Squirrel UI journey 和 audio loop 原子拒绝回归。
- 两个示例各运行 6 秒，满足最少 2 秒存活且没有脚本错误标记；原生渲染帧已检查。音频最后按静音配置复核，运行时 `isPlaying()` 为 false。
- `TMPDIR=/private/tmp make check CI_BASE=HEAD` 通过，含 183 项脚本测试。显式 TMPDIR 避开既有 uasset 测试对 macOS `/var` 与 `/private/var` 路径别名的假定；未修改该转换器。origin/dev 与当前分支之间既有 System catalogue 差异不属于本补丁，检查 base 使用本补丁开始前的 HEAD。
- API catalogue 生成 10700 个合同，0 unresolved、0 占位参数；两个领域各有 6 个 presentation 方法，模板定义没有暴露为错误的全局 API。生成器新增了真实 binder 展开与缺席 consumer 测试。
- 以唯一 domain/history owner、Result 与即时借用契约落实生命周期和失败语义；无新增运行时模块依赖、持久格式或 ECS 模型，无规范例外。异步 preview 与多 root dispatcher 不在本批完成声明内。

## 后续失败路径修复（2026-10-24）

Animation 的 presentation context 纳入目标身份，避免相同 revision、骨骼和播放位置的两个目标之间提交旧草稿；仍由原生 controller 统一拒绝冲突并消费草稿。

Audio 波形生成先构建候选结果，成功后才同时发布波形和 viewport width。超限、非有限数或整数转换越界的宽度请求不会清空上一次有效波形，不会改变布局、文档或 history。此修复沿用同步领域预览，不引入额外预览副本、调度器或 provider registry。

新增真实领域回归用例覆盖跨目标提交拒绝、失败重建后的预览/布局保留以及后续有效请求恢复；独立 CPU/Squirrel 组合目标目前 7 项通过。上述修改遵循单一状态 owner、Result 与失败原子性契约，没有规范例外。异步请求取消和 provider generation 尚未实现，仍是后续工作。

本次重新构建完整 Debug eve/unit_test 后，相关宿主 CTest 39/39 通过；独立组合 CTest 7/7 通过；`TMPDIR=/private/tmp make check CI_BASE=HEAD` 通过（183 项脚本测试）。

## 异步预览阶段

后续已接入 Audio / Animation 的异步预览取消与 provider generation。当前机制、生命周期与验证口径见 [异步预览实现](2026-10-24-async-editor-preview.md)；此前“异步未实现”的条目是上一批次的历史验收记录。

## 扩展编辑器契约测试（2026-10-24）

新增 AvatarDocumentEditor 与 VoxelCatalogEditor 的共同测试契约：`test/editor_extended_contracts.cpp` 包含 6 项真实原生编辑器用例，`test/editor_extended_script_contracts.cpp` 包含 2 项真实 ModuleManager/VM 绑定组合用例。

覆盖各实例 history 隔离、文档与完整可观察预览的 undo/redo 一致性、撤销后新编辑清除 redo、失败编辑保留 redo/文档/revision/预览、工作区注册冲突不发布半套面板，以及释放一个 VM 编辑器实例后另一个实例继续可用。脚本夹具先创建并保留基础 Editor 服务，再构造依赖该服务注册命令的领域模块。面板按稳定 id/context 验证，不依赖 region 排序；Avatar 图层查询按稳定 id 查找。

两者覆盖现有同步领域编辑与预览契约；共享 presentation/异步 provider 的生产接入仍以 Audio、Animation 为当前实现范围。本轮仅扩展测试和独立测试目标的真实 source 组合，没有新增生产 API、模块边界、持久格式或 ECS 变更。

新用例与本批涉及的旧用例使用 REQUIRE 强断言，原生 helper 接收当前 zeroerr TestContext，避免失败只报警告。旧 Voxel 射线测试修正了越界上一格的 canAttach 预期，同时补充模型内空格的可附加正向断言；不改变拾取实现。

独立 CPU 目标增加 Avatar/Voxel 的真实 editing/editor 源，继续不链接图形或领域 runtime。相同配置/构建/CTest 命令现在覆盖 23 项测试。两个真实 VM 用例在完整宿主中运行。

本批最终验证：完整 Debug unit_test 编译链接通过；`ctest --test-dir build/macosx-debug -R '^editor\.(contract|avatar|voxel)' --output-on-failure -V -j2` 的 27/27 通过，verbose 日志无 Assertion Failed 或运行时错误；独立 CPU 23/23 通过；`TMPDIR=/private/tmp make check CI_BASE=HEAD` 通过（183 项脚本测试），`git diff --check` 通过。适用的 Result 检查、唯一状态归属与生命周期测试要求均遵循，无规范例外。
