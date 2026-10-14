# 场景关系摆放与相机遮挡约束设计

日期：2026-10-11。状态：分阶段推进；本文描述完整设计。
第一阶段的实际边界与验证见 [关系摆放实现与验证](2026-10-11-scene-relations-validation.md)。
第二阶段已提供静态不透明网格的独立候选遮挡测量，见 [遮挡接口与验证](2026-10-11-scene-visibility-validation.md)；
后续阶段已接入视觉关系持久化、原生网格/投影代次绑定及提交重新测量，见
[视觉关系与提交检查](2026-10-11-scene-visual-relations-validation.md)。
对象局部 local_box 表面区域和版本绑定现已实现，见 [区域验证](2026-10-11-scene-regions-validation.md)。
projected_overlap 现已实现，见 [投影重叠验证](2026-10-12-scene-projected-overlap-validation.md)。
alpha-test、polygon 区域及多对象联合搜索仍未实现。

## 目标与现有边界

AI 使用已有视觉工具理解参考图，向引擎提交对象之间的关系、相机和可调整范围。
引擎求解可执行的候选变换，提供真实帧及逐约束证据，再通过原生 Session 原子提交。
不开发图像分割、资产检索或新物理引擎。

已核对的复用点：

- ScenePhysicsPlacementBackend：私有 Box3D 世界、复合碰撞体、固定 tick、移动/旋转/缩放
  handle、射线表面对齐、候选 capture。selected 是动态体，非 selected 是静态障碍。
- SceneEditorSession：碰撞体缓存及 resourceKey 失效检查，当前版本入场，预览不修改正式
  场景，commitPhysicsPlacement 经 scene.transform.batch.set.v1 一次提交并可撤销。
- IRenderCapture / SceneInspect：已有 ID mask、depth/normal 导出入口；不假定其现有输出
  已具备候选隔离、持久对象映射、同帧一致性或完整遮挡分母。
- SceneAuthoringDocumentTarget：权威作者文档、版本化 snapshot 和正式历史。

当前限制必须显式处理：

- PhysicsPlacementFrame.colliding 是全世界存在 contact，正常支撑也会为 true；不能拿它
  判断穿插。settled 是接触/tick/速度启发式，不等于指定支撑者或抗倾倒稳定性证明。
- 当前选择集共享 handle，并默认保持组内布局；不能据此声称已支持独立对象联合优化。
- admission.excludeLocked 默认 true；新关系求解必须保留锁定环境作为静态障碍，只禁止
  将其列为可调整对象。相关规则留在 adapter 内，不隐式修改旧交互摆放的默认行为。

## 关系模型

RelationSpec 属于 scene editing 作者数据，使用稳定 scene ObjectId，不保存 ECS slot、
GPU 句柄或物理指针。关系不是 transform parent，也不自动创建运行时物理连接。

每条关系包含 relationId、类型、参与对象/区域、hard 或 soft、soft 的 priority 与 weight、
有单位的 tolerance，以及所需几何精度。硬约束按其容差验证，不允许以视觉改善抵消。
软目标按优先级分层，再在同层组合归一化误差，避免距离、角度、面积混为同一尺度。

第一批支持：

1. supported_on：subject 接触指定 support 的表面区域，接触法线与允许方向匹配；允许
   接触，限制穿透和支撑间隙。表面可以是命名资产区域或调用方提供的局部 patch。
2. clearance：指定对象对或障碍集合的最小距离；nonpenetration 作为全局硬策略，正常
   touching 不违规。物理 collider 的近似等级进入报告，不宣称 render mesh 精确距离。
3. visible_fraction：指定相机下，object/region 的遮挡可见比例至少达到阈值。
4. occludes：指定 occluder 对 target/region 的遮挡贡献位于给定区间；其他遮挡者另报。
5. projected_overlap：两个无外部遮挡的投影 mask 的重叠目标；不等同于 occludes。

后续类型：attached_to（贴面且指定法线/朝向）、contained_in（需要真正容纳区，而不是
渲染包围盒）、稳定性验证、朝向与空间间距。接口不得接受未实现类型后悄悄近似。

RegionRef 使用 object + geometryRevision + regionId；区域在资产局部坐标中定义，随对象
变换投影。可由资产标签或 AI 给出局部 polygon/box；固定画面 polygon 是另一类
ScreenRegion，不能冒充窗户等物体附属部位。几何换版后引用失效，要求重新绑定。

## 遮挡的测量契约

对同一候选、相机、viewport、裁剪面和渲染参数：

- fullMask：目标单独参与深度测试时的像素集合，保留自身遮挡，移除外部遮挡者。
- visibleMask：全场景深度测试中目标实际可见的像素集合。
- visibleFraction = |visibleMask| / |fullMask|，即画面内、外部遮挡后的可见比例。
- 若要禁止移出画面，另加 in_frame 约束；裁出画面不算被其他对象遮挡。
- occludes(A,B)：在 B 的 fullMask 像素上，最前方 A 深度比 B 深度更近且超过 depth
  epsilon 的面积比例。分辨率、深度 epsilon、最小有效像素数写入证据。
- fullMask 为空、区域不可见或像素数过少返回 Unmeasurable，不能视为满足。

首版承诺 opaque 与 alpha-tested 几何，使用与实际渲染一致的 alpha cutoff；透明混合、
折射、体积效果等不能由单一前景 ID 准确表达，返回 Unsupported 或 Approximate，
hard 约束不能静默降低精度。ID 使用本帧 scene 对象映射，多 mesh 聚合到同一对象。
region 测量需要独立区域 coverage/depth，不使用矩形包围盒替代窗户 mask。

输出 FrameEvidence：candidate generation、sourceRevision、cameraFingerprint、
geometryFingerprint、frameId、viewport、映射与测量结果，及可选 PNG。
截图与 mask 来自同一 render submission。GPU readback 完成前不能发布最终报告。
预览使用可复用的独立渲染投影，不临时修改正式场景再恢复；截图证据必须来自引擎。

## AI 工具边界（拟议名称，尚非现有 API）

- eve_scene_relations：轻量查询或正式 upsert/remove 关系；持久修改走 Session 事务。
- eve_scene_solve_begin：提交 expectedRevision、关系补丁、相机引用、movable 集合、
  每对象允许的平移轴/范围、旋转轴/范围、缩放范围，及 seed/预算；返回 opaque jobToken。
- eve_scene_solve_status：返回进度、最佳 candidateToken 和逐关系报告，可请求证据截图。
- eve_scene_solve_commit：复核场景/资源/相机版本和最终硬约束，提交选定 candidateToken。
- eve_scene_solve_discard：取消 job 或释放候选。token 使用 generation/会话作用域，禁止
  把进程内 token 写入场景文件。结束 Session 时清理所有关联 job。

begin 可以携带临时关系，直到 commit 才选择将其持久化，避免试错污染正式文档。
昂贵求解/渲染通过 job 进行，不设置一个带默认参数的“便宜/昂贵同名”路径。

Result 区分工具失败和有效求解报告。报告状态至少包括 Feasible、BudgetExhausted、
Unmeasurable、Unsupported；预算耗尽或搜索失败不声称数学上无解。
每条关系返回 measured、required、residual、单位、精度等级、证据引用和违反对象。
放宽某限制或移动锁定对象只是建议，不能由求解器自行执行。

## 求解流程

1. 捕获 owning 文档、关系、碰撞体与相机快照，校验稳定身份、版本与 provider。
2. 构建可调整自由度；其他对象作为静态环境保留。不可调整对象不自动加入搜索集合。
3. 用支撑 patch、间距及已有姿态生成确定性种子候选；先做廉价几何筛选。
4. 对独立对象/显式组驱动已有物理预览 handle，固定 dt/tick，复用缓存，消除违规
   接触并检查指定支撑。对象对接触证据缺失时补齐 backend adapter，而非新建物理系统。
5. 仅渲染物理有效候选，测量实际可见率/遮挡率，沿允许自由度粗到细搜索。
6. 每轮物理修正后重新测量遮挡；视觉改善后重新检查物理，防止两者互相破坏。
7. 硬约束有效候选按 soft priority 排序，保持最佳结果；没有有效候选时只给诊断。
8. commit 再校验 token、scene revision、资产/相机身份，变换与关系补丁一次原子提交。

多个独立对象的移动不能被强行塞进同一个组 handle。保留现有后端，以最小扩展提供
独立驱动组/对象能力，旧交互模式仍可使用共享 handle；共同复用 admission、collider、
固定步进与提交规划，不能出现第二套物理配置/提交实现。

预算明确 maxCandidates、physicsTicksPerCandidate、maxRenderedCandidates、
renderResolution、maxGpuReadbacks 和 cancellation；截图仅在选定候选请求时输出。
seed 与候选次序稳定；相同后端数值采用声明容差，不承诺跨 GPU/物理版本位级一致。
CPU 搜索可使用 owning 快照；Box3D/GPU 和提交按各自 owner-thread 契约执行。

## 持久关系和生命周期

作者文档加入 relations，升到 eve.scene.authoring v2；显式 v1→v2 migration 填空关系集，
未知字段/版本按既有严格规则拒绝，恢复先完整验证后发布。普通 hierarchy/live target
不悄悄变成 authoring owner：缺少持久关系能力时明确拒绝 save/upsert，临时求解可以
独立支持；完整 live 持久化需作为同一契约单独接通并测试。

对象删除时原子移除其关系，undo 一并恢复。资产换版使区域引用失效，保留诊断，不能
继续测量旧缓存。相机删除或换版使依赖候选失效。Session/world 任一先销毁都要取消
候选并清理投影；恢复/热重载从稳定身份重新建立 runtime Link，无跨帧裸指针。

持久关系是作者意图，不是每帧自动拉动物体的仿真系统。移动桌子后关系变 dirty，报告
受影响闭包；只有明确 repair/求解请求才移动允许对象，并纳入同一历史事务。
“随桌子移动”和“放置时由桌子支撑”是不同意图，不通过 supported_on 隐式混同。

## 模块与架构落点

- scene/editing：关系值、版本化 codec、正式命令与权威作者数据。
- scene/editor：SceneRelationPlacementCoordinator、job 生命周期、物理预览复用与规划。
- graphics/editing 或现有 authoring adapter：候选渲染、coverage/depth、GPU 证据。
- devtools：MCP 编解码和工具返回，不保存关系或第二套场景状态。

跨层调用由消费方接口及 capability provider 注入；不从 scene/editor 向 graphics 增加
反向 include。多态接口返回 owning Result；高频求解中不做每 tick 字符串 provider 查询。
暂不新增 ECS System，只在明确 authoring job 中运行。若后续需要 runtime 约束，另行
声明 ECS scope/view/read/write/phase，不能把编辑求解混入游戏帧。

更新 scene/graphics/devtools 模块六面 catalogue，公开 API 标注所有权、线程、成本与
重入性。图形 provider 缺席时物理关系仍可用，遮挡请求 Unsupported；物理缺席时，
请求硬物理保证拒绝。两种配置均需实测，不通过文档宣称可裁剪。

## 分阶段交付与验证

1. 关系数据与物理适配：supported_on、clearance、锁定障碍、对象对接触证据、范围和
   自由度；覆盖正常接触/违规穿透、资源失效、多对象独立驱动和一次撤销。
2. 原生遮挡证据：同帧 ID/depth 与完整目标分母、区域测量；用已知 opaque 几何测量
   0/25/50/100% 遮挡，覆盖画外、空 mask、alpha-test、unsupported 透明与多 mesh。
3. 联合求解与 MCP：可取消候选预览、粗到细搜索、证据和原子 commit；验证碰撞可行但
   遮挡失败、遮挡可行但碰撞失败、预算耗尽、版本冲突以及双方销毁顺序。

端到端验收：锁定房子/围墙/相机，仅移动树；树保持指定地面支撑且不穿墙，遮住房子
下部指定比例，窗户区域保持至少 95% 可见。比较前后 native PNG 与逐条实际测量。
另验收桌子移动后显式 repair 花盆关系，且不会移动未授权对象。

实现时运行定向契约测试、SHARED/OBJECT 链接与原生案例、provider present/absent
裁剪路径，以及 make check。此前的 59 条源门禁问题是独立未通过项，不能据此豁免
新实现或把设计文档当作实现验证。本文描述完整设计，实际编译/运行范围以各阶段验证文档为准。
