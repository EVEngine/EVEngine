# Control Rig 制作与动画工作流实施方案

日期：2026-10-22。状态：首版实施与验收完成（构建基线限制见执行记录）。

## 目标与完成标准

支持在引擎里创建角色控制器、组织控制层级与求解图、拖动控制器摆姿、记录控制器关键帧、预览时间轴、保存恢复 Rig 文档、烘焙为原生 AnimClip。运行时继续使用既有动画混合与蒙皮路径。交付包含可运行的原生编辑示例与引擎截图，不能用纯数据 API 代替可操作工具。

这是一套 EVEngine 原生实现，不承诺读取 UE RigVM/Control Rig 资产。首版节点包含 FK、Aim、Two Bone IK；控制器支持平移、四元数旋转、缩放与父空间。全身优化求解、面部专用绑定和 UE 资产转换不属于这些节点的实现。

## 现状与复用

- animation：AnimSkeleton、AnimPose、AnimClip、AnimGraph、AnimLayerMixer、IK 求解器。
- animation_editing：动画文档、稳定骨骼名称、图容器与运行时桥。
- animation_editor/editor/editing：UI 无关的编辑控制器、事务、撤销重做、图与时间轴展示协议。
- examples：负责布局和交互组装；不在 C++ 引擎中建立固定编辑器产品。
- 当前工作区已有大量其他任务修改，新增实现集中在 ControlRig* 文件；不回滚、不提交其他修改。

## 分阶段实施与验收

1. **Rig 数据与求解内核**：强类型定义、控制器层级、稳定 ID、编译为索引、FK/Aim/双骨 IK 有序求解、输入姿态组合、输出姿态、反向匹配。验证重复 ID、循环、缺失骨骼、非法变换、失败原子性。
2. **控制动画与持久化**：控制器 TRS 关键帧、确定性任意时间采样、版本化文档 codec、保持世界姿态的空间切换、两阶段烘焙为 AnimClip。验证保存恢复、未知版本/字段政策、端点与四元数插值、烘焙与直接求解一致。
3. **可组合编辑 SDK**：接入现有编辑 target/session/transaction；控制器和求解节点编辑、撤销重做、时间轴、Rig 加载保存与诊断；提供脚本 Result 投影。所有可变编辑状态只有文档一个 owner。
4. **原生可视化工具示例**：控制器形状与选择/拖动、变换 Inspector、层级、求解节点编辑、关键帧与播放、保存加载、烘焙回放。交互调用同一 C++ SDK，真实动画姿态驱动角色。
5. **集成验证与文档**：单模块与组合/失败路径测试、运行时裁剪证据、SHARED/OBJECT 链接、make check、示例 smoke、引擎自有截图/不同时间帧。逐项记录已完成、未通过及外部阻塞，不能把编写完成当作验收通过。

## 架构契约

- 运行时放 animation；编辑语义放 animation/editing；编辑器桥放 animation/editor。不得让运行时反向依赖 editor/UI，不新增顶层 rig 模块。
- R-FACE / R-BOUND：更新 animation 所属 module-interface catalogue。运行时直接引用 animation 自有数学/姿态符号；编辑桥的运行时依赖明确声明且裁剪时排除。可裁剪声明必须给出 L1/L2/L3/L4 对应证据。
- 连接四轴：单编辑器对单文档，已知消费者，同步原子修改；用直接强类型 API 与既有事务，不增注册表。逐帧求解使用编译索引，不按字符串查找、不查询模块单例。
- 文档拥有控制定义、求解图与轨道；编译结果为不可变派生值。运行实例拥有骨架副本和控制状态，不保留外部骨架/姿态/脚本临时指针。重新编译更换实例，旧实例仍拥有自己的快照；编辑 revision 检测过期提交。
- 非 ECS 文档/求解数据不创建 ECS System，也不引入统一角色父类。不存在新跨域长期裸指针 Link。
- 主线程编辑；纯求解不调用外部回调。时间由调用者传入，无全局时钟或 RNG；同一输入同一平台确定性，跨平台 TRS 误差以 1e-4 测试。
- 失败操作返回 [[nodiscard]] Result；输入先验证到 owning candidate，再发布。Doxygen 说明所有权、线程与重入；编译/烘焙为 builder 或 prepare/commit 两阶段并标注成本。
- 持久化 schema `eve.control-rig` v1；拒绝未知版本与未知结构字段，未来迁移显式增加；不序列化地址或运行时索引。
- Control Rig 是动画后处理：输入层混合姿态 → Rig 求解 → 输出姿态 → 现有蒙皮。编辑器可从已有 clip 反向匹配 FK 控制器，并烘焙为 clip；不能宣称任意非可逆图具有唯一反解。

## 执行记录

- 已核查现有 Mixer、ConstraintStack、FootIK、ControlPose、AnimationClipEditor 和动画模块清单。
- 阶段 1–3 已实现：`ControlRig`、版本化 codec、`ControlRigDocument`、`ControlRigEditor` 与脚本绑定。复用既有编辑事务和 GraphDocument；没有新增 ECS 系统、全局注册表或固定编辑器产品。
- 阶段 4 已实现示例：真实 Knight/AnimSkin、控制层级、XY 拖动、XYZ 旋转、缩放、求解图编辑、关键帧、保存恢复、烘焙回放与 FK 姿态反向导入。已通过真实引擎帧验收。
- 阶段 5：SHARED 完整引擎及 animation/editor 测试程序链接通过；12 个 `animation.controlRig.*` / `editor.controlRig.*` 用例通过，包括真实 Squirrel VM 的数值返回类型和 owned Result 回归。覆盖 Aim/IK、输入姿态组合、四元数最短路径、烘焙端点、所有权、无效导入、图 revision、拖动事务和失败历史补偿。
- `make check CI_BASE=804540af60307c2eccc8f75ca831e87447d63ec2` 已通过（250 个脚本测试）。本机没有 GCC/Clang，仓库 nodiscard 编译诊断脚本按既有逻辑跳过；MSVC 严格警告构建已执行。
- provider-absent：minimal + animation_editor=ON + animation=OFF 的 `EVAnimation_editing`、`EVAnimation_editor` 编译通过；`ControlRigDocument.cpp` 被排除，绑定编译为空桥。这里只声明 L1 编译裁剪证据，不扩张为运行时行为证明。
- 验证使用从当前 HEAD `804540af60307c2eccc8f75ca831e87447d63ec2` 创建的隔离工作树，只同步本任务文件。主目录既有 common/Value 与命令行迁移尚不能完整编译；未将这些改动纳入本任务。
- 隔离基线的 SHARED 宿主启动缺失模块注册（包括 window/graphics），因此可视化验证改用 OBJECT。minimal 宿主仍引用 devtools 符号，验证组合显式启用 devtools；不修改独立的宿主/注册机制。
- OBJECT：minimal + animation + animation_editor + model3d + font + devtools，共 61 个模块，严格警告构建与 `eve` 链接通过；运行时不依赖引擎分组 DLL。
- 最终标准 smoke：`EVE=<OBJECT eve> MIN_RUN_SECONDS=2 RUN_SECONDS=10 EVE_CONTROL_RIG_PROOF=1 bash scripts/smoke_examples.sh control-rig-editor` 通过，无错误标记。验证模式经过实际磁盘保存/读取、烘焙、FK 反向匹配和撤销。
- 已检查引擎 readback 帧：`examples/control-rig-editor/evidence/pose-0.png`、`pose-1.png`、`baked-1.png`、`solve-graph.png`（生成物已忽略）。0 秒与 1 秒视口有 56,547 像素变化；直接求解与烘焙帧差异仅 1,455 像素，主要为回放时隐藏的控制框；姿态数值一致性由原生测试验证。
- 真实 UI 事件：点击 Save 成功写入原生保存目录；Delete Key 将胸部关键帧从 3 减至 2；点击 Undo 后保存的完整 JSON 与之前相等。普通文件热重载调用 `eve_after_reload` 从唯一文档 JSON 重建预览并再次输出 READY。撤销历史为会话内状态。
- 输入验证边界：当前 SDL 注入没有可靠驱动物理鼠标/ImGui 位置，因此不把注入返回成功当作真实拖动通过。视口拖动实现使用既有 `viewportActive`/坐标；预览不入历史、一次提交/撤销以及空间转换由专项测试覆盖。语义按钮输入已实测。
- 已修正实测发现的问题：严格 float 脚本参数、double 时间返回的脚本投影、GLB 均匀缩放的 `1e-5` 相对数值容差、原生保存目录初始化、高 DPI 面板布局与求解图分页、显式持久化文档根和重载回调。
- 不使用基线 `eve_restart` 作为验收路径：该旧基线软重启清理 `_async`/`_startup_t0` 后失败；普通文件热重载已单独验证。没有为此修改宿主层。
- 适用架构规范：Result/失败原子性、单权威文档、拥有骨架快照、无逐帧字符串查询、版本化 codec、现有事务及通用图复用、六面 catalogue 与裁剪证据均落实。无新增 ECS、Link 或跨线程回调；没有申请或采用架构例外。

## 本机已验证运行入口

验证工作树：`C:/Users/xiaofans/.codex/worktrees/control-rig-authoring/EVEngine`。
在该目录执行：

```powershell
cd examples/control-rig-editor
../../build/control-rig-object/src/engine/eve.exe run
```

正常开发环境也可使用根 Makefile 的 `make run/win32-debug GAME=examples/control-rig-editor`；本次主工作区尚有无关迁移未完成，因此交付运行证据使用隔离的 OBJECT 产物。日志位于主工作区 `build/control-rig-*.log`，未提交构建输出。


## Agent automation integration (2026-10-22)

1. Publish inputSchema metadata through the existing editing command registry and MCP discovery.
2. Register the live Rig target with its existing transaction backend; both UI and Agent share one document and history. Validate compiled preview before atomic publication.
3. Register a schema-described document replacement command (including controls, keys and solver nodes), using existing plan/commit revision checks. Explicitly enable automation after skeleton initialization.
4. Exercise discovery, inspect, plan, commit, UI/Agent mixed edits, undo/redo, invalid inputs, stale plans and both destruction orders. Run a real MCP edit/save/reload workflow against the example, focused native tests and source gate.

Contract: owner-thread synchronous calls, no callbacks under locks. Rig owns document/backend; coordinator borrows until unregister. A coordinator lifetime token prevents teardown access after host destruction; Rig destruction unregisters first. Existing schema eve.control-rig/1 remains authoritative; strict decoder rejects unknown fields. Command schema is discovery metadata, while the native decoder/compiler is the validation authority. No ECS or runtime RNG changes. Optional automation absence returns a structured failure; standalone authoring remains available.


### Agent integration verification

- SHARED strict MSVC build: animation/editor/scene test executables linked; 17 Rig tests and 10 existing Editor/MCP automation tests passed (27 total).
- OBJECT strict MSVC build: runnable example linked. Provider-absent profile: animation editing/editor targets compile with animation disabled (L1 only).
- Real TCP MCP: command/schema and target discovery, inspect, side-effect-free plan, commit, UI undo of Agent edit, Agent redo, UI save/load, Agent undo of UI import, UI redo and invalid-bone rejection passed. No eval-based editing.
- `examples/control-rig-editor/agent_demo.py` reproduces the live flow; `evidence/agent-result.json` records the receipt. Engine-owned before/after/restored frames were inspected; the pose region changed 41,066 pixels. UI retains the selected chest control and shows updated transform/keyframes.
- 5-second example smoke passed without error markers. Generic metadata remains optional for legacy commands; Rig advertises complete inputSchema and validates using the canonical decoder/compiler.
- Applicable architecture rules: structured checked Result, single authoritative document/history, borrowed registration lifetime and both destruction orders, atomic candidate publication, versioned strict persistence, optional-host failure, module-owned evidence. No exceptions requested or used. No ECS model, runtime module boundary or persistence version change.

Connection choice: each Rig has at most one active editor host (cardinality); registration, command discovery and edits are cold authoring operations (frequency); the owning Rig and coordinator know each other (observer knowability); publication and undo require atomic synchronous transactions (atomicity). Therefore registration uses the existing IEditingCommandRegistry capability once, then a borrowed coordinator/backend relationship with explicit lifetime tracking. Runtime pose evaluation retains compiled indices and performs no registry lookup.

- Additional lifecycle regression: external removal and same-ID replacement cannot be unregistered by the old Rig owner. Re-enabling after external removal re-registers the exact target/backend pair; replacement conflicts are explicit. Final 27-case suite and live MCP flow passed after this fix.

- Final source gate: `make check CI_BASE=804540af60307c2eccc8f75ca831e87447d63ec2` passed (250 repository script tests). Final lifecycle API changes also passed a separate incremental architecture and changed-line formatting recheck. Validation ran in the isolated task worktree to exclude unrelated in-progress main-workspace changes; native tests were focused, not the full host suite.

Environment limitation: the source gate's standalone nodiscard compiler diagnostic probe was automatically skipped because GCC/Clang was unavailable on PATH. Strict MSVC /W4 /WX SHARED and OBJECT compilation succeeded; this is not a claim that the skipped probe ran.
