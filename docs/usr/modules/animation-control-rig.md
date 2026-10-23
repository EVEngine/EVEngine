# Control Rig 制作

Control Rig 将控制器变换求解为原生 `AnimPose`。运行时属于 animation；文档和事务桥属于 animation/editing；UI 无关的编辑控制器属于 animation/editor。界面组装见 `examples/control-rig-editor`。

## 制作流程

1. 用 `AnimationEditorModule.createRig(targetId)` 创建脚本拥有的编辑器，检查 Result。
2. `initialize(skeleton)` 复制骨架并生成 FK 控制器，控制器局部变换以父控制器为参考。
3. 使用 `setControl` 或 `moveControl` 调整姿态；`previewMoveControl` 只更新派生预览，松开时再 `moveControl` 提交一次事务。调用 `seek` 可取消预览。
4. `addControl` 创建目标；`setNode` 创建 FK、Aim 或 TwoBoneIK 节点。节点 `after` 定义执行依赖，稳定声明顺序决定同优先级节点顺序。循环、缺失引用、非法链在发布前拒绝。
5. 用 `seek(seconds)` 设置时间；`setControl(..., key=true)` 插入或替换关键帧，`deleteKey` 删除当前精确时间的关键帧。平移/缩放线性插值，旋转走最短弧四元数插值，首尾之外保持端点。
6. `exportJson`/`importJson` 保存恢复 Rig 文档，`undo`/`redo` 使用既有本地编辑事务。
7. `keyFromPose(pose)` 将已有 Clip 采样姿态反向匹配到唯一满权重 FK 控制器，并在当前时间一次性记录所有控制器关键帧；一次撤销恢复整个导入。
8. `bakeInto(clip, sampleRate)` 完整生成候选 Clip 后原子替换目标。随后使用普通 `AnimPlayer`、`AnimLayerMixer` 和 `AnimSkin` 回放；烘焙 Clip 不依赖编辑器继续存在。

示例保存于 `fs.getSaveDirectory()` 指定的原生用户保存目录，文件名 `knight.rig.json`；状态栏显示绝对路径。普通脚本热重载只持久化文档 JSON，经 `eve_after_reload` 重建预览，撤销历史属于当前编辑会话。

所有会失败的方法都返回 `{ok, status, value?}`，必须先检查 `ok`。窗口和时间循环由调用者控制，不存在后台自增时钟。

## C++ 组合

`ControlRig::compile(definition, skeleton)` 返回拥有骨架副本的编译实例。`evaluate(seconds, mixedPose, output)` 可放在动画层混合后、蒙皮前；输入与输出可以是同一对象，失败时输出保持不变。求解热路径使用编译后的控制/骨骼索引，不按名字查询模块。

`ControlRigDocument` 复用 `IDomainOperationTargetStaging`、`LocalWorldAuthority` 与现有事务。`graph()` 将节点/依赖投影为 `GraphDocumentData`；`replaceGraph()` 在回写前检查域、版本、文档 revision、节点与边。图是派生编辑视图，文档始终是权威状态。

编译与烘焙为离线准备步骤，返回独立 owning artifact。编辑器不保留调用者的骨架、姿态、Clip 或 VM 临时地址。每个编辑器/运行实例由单线程使用；彼此独立的运行实例可并行求解。新文档重新编译，原实例仍拥有原快照，不产生悬空骨骼索引。

## 持久化与限制

- 文档：`eve.control-rig`，写入版本 2；版本 1 迁移为无限制，参考姿态取 initial。未知版本、未知字段、缺失字段、重复 ID、非有限值均拒绝。
- 变换：单位四元数、正的均匀缩放（容许 `1e-5` 相对导入误差）。非均匀缩放/剪切会明确拒绝，避免错误的父空间转换。
- Aim 使用骨骼局部 +Z；TwoBoneIK 使用直接 root-middle-tip 链，保留现有弯曲平面，不可达目标夹至可达距离。
- `matchFK` 只接受唯一、满权重 FK 绑定；它不是任意求解图的通用反解。
- 首版空间切换保持未动画化 Rig 的初始模型空间姿态；带关键帧的空间切换明确拒绝。
- 示例采用正视图 XY 拖动，Z 深度在 Inspector 调整；求解图面板可以创建、替换、删除节点和指定前置节点。
- 时间采样同一平台确定；跨平台浮点验收容差为 `1e-4`。不承诺 UE Control Rig/RigVM 资产兼容或全身多链优化求解。

## 验证入口

原生测试：`animation.controlRig.*` 和 `editor.controlRig.*`；示例 smoke：`MIN_RUN_SECONDS=2 bash scripts/smoke_examples.sh control-rig-editor`。完整构建与截图的实际结果记录在开发实施方案中，源文件存在不等于运行验收完成。


## Agent editing through the existing Editor MCP

After initialize, call `rig.enableAutomation()` (the example does this automatically).
Discover the live `control-rig` target with `eve_editor_target_list`, then inspect it with
`eve_editor_inspect`. The `target.snapshot` is the canonical `eve.control-rig/2` document.
`eve_editor_commands` advertises `animation.rig.replace.v1` with its complete `inputSchema`.
Use a modified snapshot as the command payload, preserving the documented TRS array order.
Controls, keyframes and FK/Aim/TwoBoneIK nodes are replaced atomically as one undo step.

Prefer `eve_editor_plan` with `expectedRevision` from inspection, then `eve_editor_commit`;
a concurrent UI edit invalidates the plan. `eve_editor_execute` is the immediate path.
Both Agent undo/redo and UI undo/redo use the same existing history, including edits made
before automation was enabled. Compilation and pose evaluation precede publication;
invalid bones, graph cycles, transforms or schema fields do not mutate the document.
The UI refreshes its inspector, hierarchy, timeline and native skin on revision changes.

`disableAutomation()` removes discovery and invalidates retained plans without clearing
the authoring history. Destroying the Rig unregisters it; destroying the host first expires
its lifetime token. Re-enable against a new host to retain the same document/history.
Reinitialization while registered is rejected; disable first. Automation is optional and
returns an explicit unsupported result when no Editor host is available.

The command uses the existing `PropertySchema` projection shared by host catalogues and MCP.
Nested document schemas use its owned `documentSchema`, mutually exclusive with flat properties;
validation remains the registered planner's responsibility.

## 控制点自由度与关节范围

每个 control 的 `limits` 与关键帧一起持久化、撤销。平移 XYZ、旋转 XYZ 和统一缩放支持 `free`、`locked`、`limited`；locked 锁在参考值，limited 使用 `min/max`。例如肘部可以锁定三个平移轴、两个旋转轴和缩放，仅开放一个弯曲轴。自由度由开放轴推导，不另存一个可能失配的计数。

`reference` 是独立校准 TRS（同样为 10 个数），普通姿态编辑不改变它。平移范围是相对 reference 位置的父局部偏移；旋转公式为 `q = reference.q * (qz * qy * qx)`，固定轴 XYZ，单位度，规范分支 X/Z 为 [-180,180]、Y 为 [-90,90]。这不支持多圈旋转；接近欧拉奇点的复杂肩关节应采用合适的参考姿态。limited 必须包含中立值：平移/角度为 0，缩放比例为 1。

`extension=true` 使用参考位置方向作为滑动轴，将位置投影成 `reference.position * ratio`，ratio 限于 `minLength/maxLength`；1 是原骨长，范围 [1,1] 即禁止伸缩。它要求非零参考位置，并且 translation 三轴为 free，避免相互矛盾的平移/长度约束。此功能控制父子骨关节的间距；统一缩放另由 scale 限制。锁定平移但放开祖先缩放，仍会改变模型空间骨长，应按需要同时锁定相关 scale。

有效越界输入在保存时被投影，已有关键帧也随限位调整，整个修改可一次撤销。配置错误拒绝且不改变 revision、文档或预览。采样（含帧间插值）、拖拽预览、反向匹配与烘焙使用同一原生约束函数。FK 关联的受约束骨骼还在每个求解节点后受限，Aim/IK 不能绕过关节限位；因此 IK 目标可能无法精确到达。受约束 FK 必须是唯一满权重绑定，控制器父级对应骨骼父级，否则编译拒绝。重设父级前需解除限位，之后在新空间重新校准。

示例点击 Solve 面板的 **Control degrees of freedom / limits**，滚动编辑各轴模式和范围，点击 **Apply limits**；**Hinge X preset** 可快速建立单轴铰链。脚本通过 `setLimitsJson(controlId, limitsJson)` 调用相同 codec 和事务，Agent 修改 snapshot 中的 limits 后使用原有 replace 命令。命令名 `.v1` 表示操作协议版本，文档 schema 版本为 2。

实际流程测试：在示例运行时执行 `python agent_limits_demo.py --output ../../build/control-rig-limits`，覆盖越界平移/缩放、UI 设置铰链、Agent/UI 交叉撤销、非法范围原子拒绝及烘焙播放。
