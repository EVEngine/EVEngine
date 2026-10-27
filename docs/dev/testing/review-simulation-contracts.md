# 集成测试契约与重复开销复审：模拟、组合与导航

本轮只读审查，不删除、迁移或重写 C++、注册名、分类元数据，也不运行测试、构建或性能测量。建议是下一轮可审阅的契约设计，不能直接当作删除清单。

扫描范围为 77 个当前物理 CPP、1758 个 literal TEST_CASE 注册（含条件编译分支）。`map-navigation/` 当前没有 CPP；本轮未把已迁到 unit 的 map-navigation snapshot 再算作集成文件。下方附录覆盖全部物理文件；大型聚合逐 `Contract section`/namespace 列出职责和直接调用证据。没有用“无 GPU”推断“不是集成”，也没有用名字相似推断“可删除”。

## 优先建议

### R1：同一模拟不变量的逐标量框架断言，可保留全部采样而减少框架开销

- 证据：[physics_collision_regression.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/physics-simulation/physics_collision_regression.cpp:57)，`physics.collision.stack.remainsSupportedAfterTenSeconds`。真实 setup 是 1 个 World3D、地面和 8 个动态 box；执行 600 个 tick，每 tick 4 个 substep。循环对每个 body 做 x/y/z finite 和 y>0.35 共 4 个 REQUIRE：600×8×4=19200 次框架断言，尚未计最终位置/速度检查。
- 相互重叠的是同一个“10 秒内堆叠不穿透/不产生非数”不变量的报告机制；600 个时间样本本身不是冗余。建议保持全部 tick、body 和四个比较，记录首个坏 tick/body/坐标并立即终止危险模拟，再一次输出带上下文的失败；保留最终沉降位置、水平漂移和速度断言。不能仅检查最后一帧，也不能把真实接触换成 mock。
- 对照：[CrowdThread.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/crowd-runtime/CrowdThread.cpp:39) 的 `serialParallelContactAndReportsAreBitExact`、`predictiveAvoidanceMatchesSerialWithFourWorkers`、`predictiveVelocityAndBudgetAreBitExact` 也分别做 300/640 个 agent ×3 tick ×4/6 字段的串并行一致性检查。可复用“记录首差异”比较器，仍遍历全部 agent，保留统计报告、clock、budget 和失败后 retry 观察；不能把整群缩成一个 agent。
- 成本是实际框架调用数，不是已测得的 wall-clock 节省。JobSystem 的真实 worker、分区边界、join 必须保留。

### R2：RoadPreset history 通路重复，适合按投影字段列命名场景

- 直接比较了 [procgen_road_circular.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/procgen_road_circular.cpp:11) 的 `procgen.road.authoring.circularArcLimitsAndPresetUndo`，[procgen_road_controllers.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/procgen_road_controllers.cpp:10) 的 `procgen.road.authoring.outgoingControllerPresetSaveAndUndo`，[procgen_road_strength.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/procgen_road_strength.cpp:12) 的 `procgen.road.authoring.splineStrengthPresetOverrideSaveAndUndo`：均 create draft/document → beginEdit → prepare/commit → undo → capture → prepareRestore/commit → redo，最后观察一个 pointDefaults 字段。
- 相同机制是 history/candidate/restore 的往返；不同业务投影仍有独有价值：`circularFromEnd`、`pathControl`、`splineStrength` 不能互相替代。建议一个“preset point-default publication”任务，三行具名参数保留全部字段断言，以 `splineStrengthPresetOverrideSaveAndUndo` 作为完整通路骨架；每行使用独立可变 document，至多共享不可变 draft 和进程。
- setup 是内存 network、document、候选和快照克隆，没有真实 VM、窗口或 GPU，不能声称节省三次显卡初始化。字段 admission/几何纯函数边界值宜用直接值验证；每个字段至少一次真实 document→network 投影仍保留。
- 必须独立保留 `procgen_road_presets.cpp` 中 cycle/stale owner/late failure atomicity、history-root migration、explicit false/empty override、unbinding 和 redo-preservation；这些不是上述 happy round-trip 的重复。`surfacePath.retainsPresetHistoryAndMigratesMissingPolicy` 的缺字段旧版本迁移也不能被三个 happy row 覆盖。

### R3：材质缺纹理发布失败可统一矩阵；三条 slot 路由不可丢

- [gameplay_composition_graphics_editor_material_target_tests.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/gameplay_composition_graphics_editor_material_target_tests.cpp:500) `editor.material.missing_vegetation_detail_texture_rejects_atomically`、[gameplay_composition_graphics_editor_material_target_tests.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/gameplay_composition_graphics_editor_material_target_tests.cpp:615) `...missing_vegetation_vertex_texture_rejects_atomically`、[gameplay_composition_graphics_editor_material_target_tests.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/gameplay_composition_graphics_editor_material_target_tests.cpp:704) `...missing_vegetation_motion_texture_rejects_atomically` 的 body 均创建 MaterialDocumentTarget、Renderable3D、Material、SelectiveMaterialAssets fake、RuntimeSink 和 PublishingTarget，再 reloadSnapshot；共同终局仅是 NotFound、revision 不变、未出现 PbrSurface。
- 差异是实际 detail/vertex/motion resolver 分支，因此“删到一个失败 case”不成立。建议统一成发布缺依赖的三行具名场景，保留每个配置函数、拒绝开关和 slot 路由；共享 immutable 配置和 fixture 构造逻辑，确保每行 Material/target/registry 独立或显式重置。当前 body 未显式销毁新建 Renderable，依赖独立测试进程；合并 row 前必须在 sink/target 最后使用之后按 ECS owner 规则销毁该实体，不能让上一行对象进入下一行。代表保留 detail row 的完整发布调用，另外两行仍经过真实 RuntimeSink。
- 这三条没有打开窗口/提交 GPU；真实成本是 ECS Renderable 创建、材质/快照拥有数据及进程启动。fake 已存在，不能再以“mock renderer”代替真实发布适配器并声称覆盖相同契约。
- 当前三个失败观察都从 fresh/empty Material 出发，不证明“已有有效发布在后续失败时保留”。若下一轮减量，应把一个代表失败接在成功发布后，检查旧 surface/资源句柄和 revision 均保持；不要为三种缺资源重复三份完整成功前奏。已有各类型成功投影断言仍不可省。

### R4：多个粒子 skin 场景重复加载同一 glTF，可以共享只读模型，而非删掉容量/过滤语义

- [gameplay_composition_graphics_graphics_vegetation_tests.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/gameplay_composition_graphics_graphics_vegetation_tests.cpp:2739) `particles.skin.filterByIndexAndClearFilter` 与 [gameplay_composition_graphics_graphics_vegetation_tests.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/gameplay_composition_graphics_graphics_vegetation_tests.cpp:2771) `particles.skin.emitFromSkinRespectsBuffer` 都调用 loadCesiumMan，定位 skinned mesh、导入 AnimSkeleton、创建 AnimSkin/AnimPose、applyBindPose/computeWorld，再创建 emitter。辅助函数 [gameplay_composition_graphics_graphics_vegetation_tests.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/gameplay_composition_graphics_graphics_vegetation_tests.cpp:2295) 会 setIdentity、setupWriteDirectory、mount 同一个 CesiumMan 目录并 newModelDataFromFile。
- 重复的是资源准备；两条断言不是重复：前者观察 bone filter 命中以及 clear-filter 后 emit32，后者观察容量10对 emit100 的截断。可同一个 skin emission 任务共享 pin 住的 ModelData/skin/只读 bind pose，每个 subcase 新建且按 owning API 清理 emitter；detach 只证明解除绑定，不能未经API验证当作销毁。至少一次真实 importer→skin→emitter 路径保留；大量 index/阈值输入可使用小型拥有 mesh/skin 测试值，但不能只留一个 mock 结果。
- `clearRestoresBoneOriginSpawn`、attach plane、followRotation 的来源生命周期/坐标投影不能因为也加载 CesiumMan 就被删除。`clearSkinSource` 与 detach 的先后清理需保留。
- 另有执行可见性问题：这些 case 在缺 glTF 时 `if (!ensureSkinnedAssets()) return`。本轮不增加新 soft skip；下一轮应让测试报告明确区分“资源缺失未执行”和“通过”，性能比较也必须排除不对称 early-return。不能把未加载模型的快跑当成优化。

### R5：重复 restore 三次没有额外泄漏观察，需明确其合同或合并重复验证

- [physics_snapshot3d_ownership.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/physics-simulation/physics_snapshot3d_ownership.cpp:28) `physics.snapshot3d.candidateOwnershipCommitAndRollback` 先对相同 captured snapshot 构造同样 unsupported 第二 joint，连续三次 restore 失败，每次仅看旧 body lookup 和旧 joint validity；随后相同有效 snapshot 连续 restore 三次，最后检查旧 handles 全失效、重拍数量 2 bodies/1 shape/1 joint。
- 必须保留“成功分配第一个候选 joint 后第二个失败”的 late rollback，以及成功 restore 对 body/shape/joint/handle 的整体失效；这比纯 schema 拒绝更强，不能 mock World3D。
- 但 pass=3 本身没有 native allocation/registry-count/泄漏或候选销毁计数差异。建议同任务保留 1 次失败＋1 次成功的基础观察，将“反复恢复不累积后端对象”明确成有计数/稳定快照证据的重复生命周期 subcase；不要把三次相同 REQUIRE 当成已经做了泄漏测试。是否缩次数要由后续故障注入/生命周期证据决定，本轮不删。
- 对照 rigid-backend 聚合中的 snapshot topology cases 主要观察 world identity/旧 handles；不能据同样出现 !isValid 就认定覆盖了上述 late allocation rollback。

### R6：线程失败 phase 矩阵可复用已 join 的池，但 owner-destruction 场景不得共享

- [CrowdThread.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/crowd-runtime/CrowdThread.cpp:210) `crowd.thread.partialVelocityFailureRollsBackClockAndMotion` 对 failPhase {1,2} 每行重新创建 3-worker JobSystem 和 scheduler，同时建300 agents、snapshot、reference restore、失败后 retry 比较。两个 phase 分别对应不同结构阶段，不能删成一个。
- 在不破坏 owner 生命周期前提下，可共用任务级 JobSystem，逐 row 新建 FailAfterBody wrapper/Crowd/reference；每行仍验证该 phase 的 calls、clock/motion 未发布，随后同 tick retry 成功且与 reference 相等。共享前提是所有 worker 已 join，不能让上行失败任务残留到下行。
- `workerFailureJoinsAndInFlightOwnerSurvivesRelease` 故意在 worker 内 jobs.reset()，并检验 completed==18、expired/stopped 不再执行，必须使用其独立 ownership setup；绝不能为了复用池把这个 destruction 场景并入普通 bit-exact fixture。

## 已核实相似但必须保留的跨域失败路径

- [gameplay_composition_graphics_gameplay_action_tests.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/gameplay_composition_graphics_gameplay_action_tests.cpp:3612)：`gameplayAction.costFailureIsAtomicBeforeActive` 检查 affordability 失败使 reserve=0/effect prepare=0；`effectPrepareFailureDoesNotReserveCost` 检查 effect prepare=1、resource reserve=0；`commitFailureCompensatesResourceAndPreparedEffect` 检查 resource reserve=1、effect/resource rollback=1、无 debit；`successCommitsCostBeforeNonfailableEffect` 则要求 debit、effect commit=1、rollback=0。setup 外形高度相似，失败阶段和观察值互斥，不能合成只检查 Failed 的一个测试。可统一 fixture builder，所有注入点/观察仍保留。
- [gameplay_composition_graphics_rts_script_tests.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/gameplay_composition_graphics_rts_script_tests.cpp:1666) 与 [gameplay_composition_graphics_rts_script_tests.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/gameplay_composition_graphics_rts_script_tests.cpp:1716)：两条 scene editor physics placement 都执行30次 update 和 commit，但一个是两个不同未选中父节点下的兄弟对象（观察local x=3/4），另一个选中的 parent 和 child 一起移动（观察parent local7、child local1）。后者防止使用 parent 的旧世界矩阵，前者防止混用父坐标；不是重复，可共享 snapshot 查询助手，不能删任一拓扑。
- `physics.forces.batchRejectsInvalidTailBeforeWaking`、`batch2DRevalidatesWholeBatchAtCommit` 分别观察 Box3D 与 Box2D 的 awake/velocity 原子性；前者还覆盖 prepare overflow 和 commit 前销毁第二 body。两个后端不得互相替代。`batchMoveAndForeignWorldCannotDuplicateSubmission` 的 move-consumption、foreign-world identity 又是不同阶段。
- [world_sweep.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/projectile-behavior/world_sweep.cpp:34)：Box3D/Jolt 两条 contract 共用 helper，但分别调用真实 backend。world token restore 的 failure 不失效/成功失效、world.destroy/reset，以及257-hit预算和 carrier pierce+snapshot+失败后 age 不变，均有独有观察；不能拿公式 mock 合并。257个 box 是硬查询预算边界的一侧，减少到个位数将不再触发当前分支。
- Ocean 的 GPU unavailable、CPU/GPU parity/stale token、large 128/256 band、renderer wake/floating-origin、VM GC ownership、spray provider retirement 的 backend状态、句柄时序和像素输出不同。大分辨率 parity 值得单独测成本，但不能按“都比较水面”删除。`gpuSpectrumReportsUnavailableProvider` 没有打开窗口，不能与有 device 的测试共享 Graphics 状态，否则反而会失去缺 provider 前置条件。
- `weather.lightning weapon endpoints pulse and stale entity` 将 pulse invalid-dt/stop 与 ECS world reset 后 trigger 失败和析构安全放在一次真实视觉准备后；已经共享 setup。可用局部值测试扩展 dt 矩阵，但保留 stale Renderable owner 的失败和 fx.reset 析构路径。

## 判定与后续验证要求

本轮没有确认可直接删除的完整 integration case。确认的优化机会主要是重复资源准备、重复框架报告、字段矩阵的 history scaffolding 和重复循环缺乏独立可观察量。不要把“可以用更少 setup 表达”误写成“业务断言重复可删”。

若后续实施，先为拟减的每一条记录：独有 precondition、provider/VM/backend、故障注入阶段、状态/事件/句柄观察，以及两种销毁顺序。每个命名 row 必须失败能定位，不能只累计总布尔。共享 fixture 必须明确成功和失败后的重置；在 zeroerr 中 SUB_CASE 即时执行，外层 fixture 不自动重放。不能捕获 move-only owner 的值副本，也不能在一个 subcase 销毁另一个依赖的 provider。

没有执行计时，因此这里不把 GPU/window/worker 创建次数换算成收益；默认运行的进程成本、import cache 行为、framework logging 和并行调度开销需用相同可执行路径、资源、环境及完整失败统计测量。本文也不认可从缺资源 early-return 得到的“加速”。

## 全部审查文件与 namespace 职责清单

下面是对全部源码正文和 literal case body 的静态索引；每项给实际 include/API/断言侧证，供定位任务边界，不把 include 图当作可裁剪证明。大型聚合按其保留的独立 namespace/Contract section 展开；同一个 namespace 可包含多个不同生命周期案例。只有上方逐 body 对比的事项具有“可合并开销”的人工结论；未列为发现的任务并非已证明不存在任何重复。


### physics-simulation

#### [climbing_foot_contacts.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/physics-simulation/climbing_foot_contacts.cpp:1)

- 当前规模：344 行；8 literal注册。
- 实际任务：攀爬接触/锚点→真实碰撞世界、移动/旋转 body、foot/hand 独立接触及 corner 胶囊约束。
- `本文件任务`（当前起始 1 行，8 注册）：
  - 参与接口正文证据：climbing、physics；实际调用 `advance`, `newBoxShape`, `nodeRef`, `isBound`, `restore`, `newWorld3D`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`climbing.footContacts.narrowWallSupportsOnlyOneFoot`；`climbing.footContacts.overheadLedgeLeavesFeetFree`；`climbing.footContacts.ladderSocketsFollowMovingBody`。
  - 含否定谓词的观察所在场景：`climbing.footContacts.narrowWallSupportsOnlyOneFoot`；`climbing.footContacts.overheadLedgeLeavesFeetFree`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [climbing_grounding.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/physics-simulation/climbing_grounding.cpp:1)

- 当前规模：86 行；3 literal注册。
- 实际任务：Climbing ECS motion system→生产胶囊 motor；低台阶、高台阶和低顶空间，含 settle trajectory。
- `本文件任务`（当前起始 1 行，3 注册）：
  - 参与接口正文证据：climbing、physics；实际调用 `body`, `newBody`, `newBoxShape`, `state`, `intent`, `newCapsuleShape`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`climbing.grounding.lowStepUsesProductionMotor`；`climbing.grounding.tallStepRemainsBlocked`；`climbing.grounding.lowCeilingRejectsStepWithoutPublishingRaisedPose`。

#### [physics_buoyancy_geometry.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/physics-simulation/physics_buoyancy_geometry.cpp:1)

- 当前规模：113 行；3 literal注册。
- 实际任务：浸没面积/体积公式与实际2D/3D浮力抵消重力混合；公式row与force→backend step职责不同。
- `本文件任务`（当前起始 1 行，3 注册）：
  - 参与接口正文证据：physics；实际调用 `step`, `newBody`, `setDensity`, `getLinearVelocityY`, `newCircleFixture`, `getAngularVelocity`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`physics.forces.immersionHalfDryFullAndThickness`；`physics.forces.immersionMatchesIndependentSliceIntegration`；`physics.forces.geometricBuoyancyBalancesGravity2DAnd3D`。
  - 含否定谓词的观察所在场景：`physics.forces.immersionHalfDryFullAndThickness`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [physics_collision_regression.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/physics-simulation/physics_collision_regression.cpp:1)

- 当前规模：100 行；4 literal注册。
- 实际任务：真实CCD/动态对/长期堆叠/查询triangle identity；长时间支持性不能由短期raycast替代。
- `本文件任务`（当前起始 1 行，4 注册）：
  - 参与接口正文证据：physics；实际调用 `getX`, `newBody`, `newBoxShape`, `newSphereShape`, `setLinearVelocity`, `updateFull`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`physics.collision.ccd.sphereStopsAtThinWall`；`physics.collision.ccd.dynamicPairMaintainsOrderWithFourSubsteps`；`physics.collision.stack.remainsSupportedAfterTenSeconds`。

#### [physics_coupling3d.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/physics-simulation/physics_coupling3d.cpp:1)

- 当前规模：233 行；7 literal注册。
- 实际任务：3D传动/耦合→真实joint/body状态、周期/失败配置与拓扑有效性。
- `本文件任务`（当前起始 1 行，7 注册）：
  - 参与接口正文证据：physics；实际调用 `step`, `sample`, `getLinearVelocityY`, `isValid`, `newBody`, `newRevoluteJoint`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`physics.coupling3d.gearTracksMultipleTurnsAndSourceDeath`；`physics.coupling3d.pulleyConservesWeightedLength`；`physics.coupling3d.slackRopeDoesNotPush`。
  - 含否定谓词的观察所在场景：`physics.coupling3d.gearTracksMultipleTurnsAndSourceDeath`；`physics.coupling3d.pulleyConservesWeightedLength`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [physics_coupling3d_script.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/physics-simulation/physics_coupling3d_script.cpp:1)

- 当前规模：104 行；2 literal注册。
- 实际任务：Squirrel耦合binding→真实3D bodies/joints及脚本引用生命周期。
- `本文件任务`（当前起始 1 行，2 注册）：
  - 参与接口正文证据：physics；实际调用 `addFunc`, `newBody`, `addClass`, `newSphereShape`, `setAngularVelocity`, `run`。
  - setup正文迹象：真实VM、body/world backend。
  - 任务观察样本：`physics.coupling3d.scriptOwnershipAndTypeSafety`；`physics.coupling3d.scriptWorldFirstInvalidation`。

#### [physics_field_set.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/physics-simulation/physics_field_set.cpp:1)

- 当前规模：76 行；2 literal注册。
- 实际任务：FieldSet筛选/叠加→实际ForceBatch/awake/step和无部分发布。
- `本文件任务`（当前起始 1 行，2 注册）：
  - 参与接口正文证据：physics；实际调用 `raw`, `prepare`, `newBody`, `IsAwake`, `commit`, `getLinearVelocityX`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`physics.forces.fieldSetFiltersAndSuperposes2D`；`physics.forces.fieldSet3DOrderAndAtomicFailures`。
  - 含否定谓词的观察所在场景：`physics.forces.fieldSetFiltersAndSuperposes2D`；`physics.forces.fieldSet3DOrderAndAtomicFailures`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [physics_force_application.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/physics-simulation/physics_force_application.cpp:1)

- 当前规模：224 行；9 literal注册。
- 实际任务：SI force/torque/batch→Box2D/Box3D质量、惯量、wake和真实tick，包含late stale link。
- `本文件任务`（当前起始 1 行，9 注册）：
  - 参与接口正文证据：physics；实际调用 `newBody`, `step`, `getLinearVelocityX`, `commit`, `raw`, `newCircleFixture`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`physics.forces.application2DIsMeterIndependent`；`physics.forces.application3DForceAndOffCenterTorque`；`physics.forces.applicationRejectsStaticWithoutMutation`。
  - 含否定谓词的观察所在场景：`physics.forces.application2DIsMeterIndependent`；`physics.forces.application3DForceAndOffCenterTorque`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [physics_force_receipt.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/physics-simulation/physics_force_receipt.cpp:1)

- 当前规模：175 行；3 literal注册。
- 实际任务：load receipt准备/提交/回执→物理世界真实状态及重复/失效处理。
- `本文件任务`（当前起始 1 行，3 注册）：
  - 参与接口正文证据：physics；实际调用 `status`, `step`, `commit`, `newBody`, `raw`, `SetAutoClearForces`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`physics.forces.receiptRejectsWrongStepAndRetriesBackendOnce`；`physics.forces.receiptRestoreAndDestroyCancelPendingOnly`；`physics.forces.receiptRequiresAutomaticForceClearing2D`。
  - 含否定谓词的观察所在场景：`physics.forces.receiptRejectsWrongStepAndRetriesBackendOnce`；`physics.forces.receiptRestoreAndDestroyCancelPendingOnly`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [physics_force_script.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/physics-simulation/physics_force_script.cpp:1)

- 当前规模：253 行；3 literal注册。
- 实际任务：Squirrel force DTO/binding→真实world/body/tick及结构化失败。
- `本文件任务`（当前起始 1 行，3 注册）：
  - 参与接口正文证据：physics；实际调用 `addClass`, `run`, `compileSource`, `addFunc`, `getLinearVelocityY`, `newBody`。
  - setup正文迹象：真实VM、body/world backend。
  - 任务观察样本：`physics.forces.scriptModelsProjectResults`；`physics.forces.scriptFieldSetsOwnValuesAndBatchRights`；`physics.forces.scriptWingAssembliesControlBothDimensions`。

#### [physics_joint2d.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/physics-simulation/physics_joint2d.cpp:1)

- 当前规模：219 行；9 literal注册。
- 实际任务：SI Joint2D factory/handle→Box2D joint/world和属性有效性。
- `本文件任务`（当前起始 1 行，9 注册）：
  - 参与接口正文证据：physics；实际调用 `isValid`, `raw`, `setMotor`, `GetJointCount`, `step`, `destroyChecked`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`physics.joint2d.revoluteMotorLimitsAndBodyTeardown`；`physics.joint2d.prismaticDistanceWheelAndWeld`；`physics.joint2d.pulleyConservesWeightedLength`。
  - 含否定谓词的观察所在场景：`physics.joint2d.revoluteMotorLimitsAndBodyTeardown`；`physics.joint2d.pulleyConservesWeightedLength`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [physics_joint2d_script.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/physics-simulation/physics_joint2d_script.cpp:1)

- 当前规模：51 行；1 literal注册。
- 实际任务：Joint2D脚本参数/结果→实际body/joint ownership。
- `本文件任务`（当前起始 1 行，1 注册）：
  - 参与接口正文证据：physics；实际调用 `addFunc`, `newBody`, `addClass`, `raw`, `GetJointCount`, `newCircleFixture`。
  - setup正文迹象：真实VM、body/world backend。
  - 任务观察样本：`physics.joint2d.scriptResultAndOwningRelease`。

#### [physics_joint2d_snapshot.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/physics-simulation/physics_joint2d_snapshot.cpp:1)

- 当前规模：422 行；9 literal注册。
- 实际任务：2D joint版本化restore→backend joint topology、stale handles及脚本引用。
- `本文件任务`（当前起始 1 行，9 注册）：
  - 参与接口正文证据：common、physics；实际调用 `set`, `raw`, `restore`, `getId`, `make`, `isValid`。
  - setup正文迹象：真实VM、body/world backend。
  - 任务观察样本：`physics.joint2d.snapshotAllKindsPreserveParametersAndInvalidateLinks`；`physics.joint2d.snapshotRestoredMotorActuallyDrivesAndHonorsLimit`；`physics.joint2d.snapshotGearPreservesOriginalPhaseAndDependencyTeardown`。
  - 含否定谓词的观察所在场景：`physics.joint2d.snapshotAllKindsPreserveParametersAndInvalidateLinks`；`physics.joint2d.snapshotGearPreservesOriginalPhaseAndDependencyTeardown`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [physics_merge_contract.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/physics-simulation/physics_merge_contract.cpp:1)

- 当前规模：140 行；4 literal注册。
- 实际任务：新旧factory共同identity/snapshot、3D新增joint属性以及Jolt力/快照可移植性。
- `本文件任务`（当前起始 1 行，4 注册）：
  - 参与接口正文证据：physics、vehicle；实际调用 `newBody`, `snapshot`, `runtimeHandle`, `isValid`, `findJoint`, `restore`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`physics.merge.planarFactoriesShareIdentityAndSnapshot`；`physics.merge.spatialAddedJointKindsRetainParameters`；`physics.merge.joltKeepsPortableSnapshotsAndAcceptsForces`。
  - 含否定谓词的观察所在场景：`physics.merge.planarFactoriesShareIdentityAndSnapshot`；`physics.merge.joltKeepsPortableSnapshotsAndAcceptsForces`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [physics_mover_grounding.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/physics-simulation/physics_mover_grounding.cpp:1)

- 当前规模：84 行；1 literal注册。
- 实际任务：owned capsule mover→支持面探测/sensor/ignoredBody与真实碰撞；具名subcase隔离world。
- `本文件任务`（当前起始 1 行，1 注册）：
  - 参与接口正文证据：physics；实际调用 `moveCapsuleOwned`, `newBody`, `newBoxShape`, `setSensor`, `getMoverGroundDot`, `setPosition`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`physics.mover.grounding.supportAndFilteringContract`。
  - 含否定谓词的观察所在场景：`physics.mover.grounding.supportAndFilteringContract`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [physics_simulation_graphics_box3d_tests.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/physics-simulation/physics_simulation_graphics_box3d_tests.cpp:1)

- 当前规模：4488 行；127 literal注册。
- 实际任务：多职责聚合；按下列namespace分别审阅其调用和失败观察，不以graphics文件名替代任务判定。
- `action collision ignore` / `test_action_collision_ignore`（当前起始 3 行，3 注册）：
  - 参与接口正文证据：action、physics；实际调用 `newBody`, `isBodyPairCollisionEnabled`, `enter`, `newWorld3D`, `activeCount`, `pairCount`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`actionCollisionIgnore.overlapRestoresPhysicsAuthorityAndSurvivesStaleEntity`；`actionCollisionIgnore.preservesExternalDisableAndCleansUpDestroyedBodies`；`actionCollisionIgnore.destructorRestoresAndWorldMayDieFirst`。
  - 含否定谓词的观察所在场景：`actionCollisionIgnore.overlapRestoresPhysicsAuthorityAndSurvivesStaleEntity`；`actionCollisionIgnore.preservesExternalDisableAndCleansUpDestroyedBodies`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `box2d` / `test_box2d`（当前起始 181 行，14 注册）：
  - 参与接口正文证据：graphics、physics、window；实际调用 `newBody`, `newRectangleFixture`, `newWorld`, `getY`, `update`, `getX`。
  - setup正文迹象：body/world backend、实际window/headless。
  - 任务观察样本：`box2d.module.meter`；`box2d.world.createAndGravity`；`box2d.body.fallsUnderGravity`。
  - 含否定谓词的观察所在场景：`box2d.fixture.sensor`；`box2d.query.rayCastAndAABB`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `box3d` / `test_box3d`（当前起始 567 行，64 注册）：
  - 参与接口正文证据：physics；实际调用 `getId`, `newBody`, `newWorld3D`, `newSphereShape`, `newBoxShape`, `setTag`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`box3d.world.createAndGravity`；`box3d.body.fallsUnderGravity`；`box3d.terrainLoadGravity.waitsThenRestoresOriginalScale`。
  - 含否定谓词的观察所在场景：`box3d.terrainLoadGravity.waitsThenRestoresOriginalScale`；`box3d.body.dampingGravitySleepMassAndCenters`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `climbing` / `test_climbing`（当前起始 2344 行，16 注册）：
  - 参与接口正文证据：animation、climbing、physics；实际调用 `advance`, `upsertAction`, `tryBegin`, `probe`, `phase`, `drainEvents`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`climbing.probeReturnsOwningHandlesAndRestoresPhysicsQueryState`；`climbing.selectionIsDeterministicAcrossDefinitionInsertionOrder`；`climbing.selectionProfileWeightsAndInputModeDriveQuantizedCost`。
  - 含否定谓词的观察所在场景：`climbing.executionRejectsDuplicateTicksAndSupportsExplicitCancellation`；`climbing.invalidPelvisMotionDoesNotConsumeSimulationTick`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `climbing anchor execution` / `test_climbing_anchor_execution`（当前起始 2853 行，5 注册）：
  - 参与接口正文证据：climbing、physics；实际调用 `reservationCount`, `phase`, `tryBeginAnchor`, `poseFor`, `transitionAnchor`, `isBound`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`climbing.anchorExecution.shimmyAndCornerKeepOneAtomicReservation`；`climbing.anchorExecution.beamPoleAndBarUseExplicitStablePhases`；`climbing.anchorExecution.ladderMountClimbAndDismountShareOneExecution`。
  - 含否定谓词的观察所在场景：`climbing.anchorExecution.reloadAndDestructionCannotLeakOccupancy`；`climbing.anchorExecution.snapshotTransfersOrReacquiresOneExclusiveClaim`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `climbing anchor graph` / `test_climbing_anchor_graph`（当前起始 3266 行，4 注册）：
  - 参与接口正文证据：climbing、physics；实际调用 `runtimeHandle`, `nodeRef`, `resolveNode`, `reserve`, `release`, `reservationCount`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`climbing.anchorGraph.resolvesMovingBodyAndBidirectionalEdges`；`climbing.anchorGraph.reservationSlotsAndReloadAreTransactional`；`climbing.anchorGraph.targetDestructionMakesResolutionStale`。
  - 含否定谓词的观察所在场景：`climbing.anchorGraph.reservationSlotsAndReloadAreTransactional`；`climbing.anchorGraph.targetDestructionMakesResolutionStale`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `climbing anchor route` / `test_climbing_anchor_route`（当前起始 3439 行，4 注册）：
  - 参与接口正文证据：climbing、physics；实际调用 `ref`, `planRoute`, `error`, `code`, `reservationCount`, `reload`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`climbing.anchorRoute.shortestTieUsesStableNodeIdentity`；`climbing.anchorRoute.fullNodeReroutesWithoutMutatingOccupancy`；`climbing.anchorRoute.filtersBoundsAndGenerationAreExplicit`。
  - 含否定谓词的观察所在场景：`climbing.anchorRoute.filtersBoundsAndGenerationAreExplicit`；`climbing.anchorRoute.enforcesAuthoredCapabilityTags`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `climbing definition policy` / `test_climbing_definition_policy`（当前起始 3597 行，2 注册）：
  - 参与接口正文证据：climbing、physics；实际调用 `newBody`, `setProfile`, `newWorld3D`, `newCapsuleShape`, `newBoxShape`, `getId`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`climbing.definitionPolicy.enforcesSourceTagsAndCandidateBudget`；`climbing.definitionPolicy.commandIsTransactionalAndCameraCueIsOwningOutput`。
  - 含否定谓词的观察所在场景：`climbing.definitionPolicy.commandIsTransactionalAndCameraCueIsOwningOutput`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `climbing ecs` / `test_climbing_ecs`（当前起始 3698 行，4 注册）：
  - 参与接口正文证据：climbing、physics；实际调用 `climbingState`, `climbingCandidates`, `newBody`, `isBound`, `climbingIntent`, `values`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`climbing.ecs.composesWithExistingRootAcrossFixedPhases`；`climbing.ecs.rejectsStalePhysicsLinkWithoutMutatingDerivedCandidates`；`climbing.ecs.profileDrivesCollisionConstrainedGroundAndAirLocomotion`。
  - 含否定谓词的观察所在场景：`climbing.ecs.composesWithExistingRootAcrossFixedPhases`；`climbing.ecs.rejectsStalePhysicsLinkWithoutMutatingDerivedCandidates`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `climbing events` / `test_climbing_events`（当前起始 3965 行，3 注册）：
  - 参与接口正文证据：climbing、physics；实际调用 `tryBegin`, `drainEvents`, `upsertAction`, `phase`, `cancel`, `executionId`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`climbing.events.detachBeforeReentrantConsumerMutation`；`climbing.events.snapshotPreservesUndeliveredOrder`；`climbing.events.capacityBackpressureIsAtomic`。
  - 含否定谓词的观察所在场景：`climbing.events.capacityBackpressureIsAtomic`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `climbing input` / `test_climbing_input`（当前起始 4094 行，3 注册）：
  - 参与接口正文证据：climbing、physics；实际调用 `isZero`, `setProfile`, `executionId`, `newWorld3D`, `newBody`, `newBoxShape`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`climbing.selection.commitsExecutionAndConsumesInputExactlyOnce`；`climbing.selection.failedPrepareLeavesInputAndAuthorityUnchanged`；`climbing.selection.appliesProfileCoyoteTicksAtTheBoundary`。
  - 含否定谓词的观察所在场景：`climbing.selection.commitsExecutionAndConsumesInputExactlyOnce`；`climbing.selection.failedPrepareLeavesInputAndAuthorityUnchanged`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `climbing parkour` / `test_climbing_parkour`（当前起始 4200 行，2 注册）：
  - 参与接口正文证据：climbing、physics；实际调用 `newBody`, `newBoxShape`, `upsertAction`, `probe`, `tryBegin`, `advance`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`climbing.parkour.slideUsesCompactCapsulePath`；`climbing.parkour.wallRunChoosesStableSideAndDoesNotEmitLand`。
  - 含否定谓词的观察所在场景：`climbing.parkour.wallRunChoosesStableSideAndDoesNotEmitLand`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `climbing performance` / `test_climbing_performance`（当前起始 4299 行，1 注册）：
  - 参与接口正文证据：climbing、physics；实际调用 `newBody`, `newBoxShape`, `newWorld3D`, `climbingState`, `clearTelemetry`, `telemetry`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`climbing.performance.reportsOrdinaryCandidateDenseAndActiveP50P95`。
- `climbing physics policy` / `test_climbing_physics_policy`（当前起始 4442 行，2 注册）：
  - 参与接口正文证据：physics；实际调用 `newBody`, `newBoxShape`, `moveCapsuleOwned`, `newWorld3D`, `runtimeHandle`, `getId`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`climbing.physics.ownedMoverSlopePolicyIsPerCall`；`climbing.physics.broadPhaseOwnsStableFilteredShapeEvidence`。
  - 含否定谓词的观察所在场景：`climbing.physics.ownedMoverSlopePolicyIsPerCall`；`climbing.physics.broadPhaseOwnsStableFilteredShapeEvidence`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [physics_simulation_graphics_physics_rigid_backend_tests.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/physics-simulation/physics_simulation_graphics_physics_rigid_backend_tests.cpp:1)

- 当前规模：3963 行；94 literal注册。
- 实际任务：多职责聚合；按下列namespace分别审阅其调用和失败观察，不以graphics文件名替代任务判定。
- `climbing prediction` / `test_climbing_prediction`（当前起始 3 行，4 注册）：
  - 参与接口正文证据：climbing、physics；实际调用 `error`, `upsertAction`, `requestFor`, `tryBeginPredicted`, `pose`, `phase`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`climbing.prediction.acceptsOnlyServerReprobedExactCandidate`；`climbing.prediction.mismatchCorrectsWithoutStartingClientTarget`；`climbing.prediction.tickLeadAndWireSchemaAreBounded`。
  - 含否定谓词的观察所在场景：`climbing.prediction.tickLeadAndWireSchemaAreBounded`；`climbing.prediction.decisionRejectsMalformedOrFutureRuntimeSnapshot`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `climbing reload` / `test_climbing_reload`（当前起始 164 行，2 注册）：
  - 参与接口正文证据：climbing、physics；实际调用 `definitionGeneration`, `tryBegin`, `snapshot`, `snapshotJson`, `reloadProfile`, `restoreJson`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`climbing.reload.isAtomicAndPinsActiveDefinition`；`climbing.reload.jsonProfileAndRuntimeRoundTrip`。
  - 含否定谓词的观察所在场景：`climbing.reload.isAtomicAndPinsActiveDefinition`；`climbing.reload.jsonProfileAndRuntimeRoundTrip`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `climbing services` / `test_climbing_services`（当前起始 266 行，6 注册）：
  - 参与接口正文证据：climbing、common、physics、cap；实际调用 `error`, `code`, `setProfile`, `publishForCapability`, `has_value`, `toInt`。
  - setup正文迹象：真实VM、body/world backend。
  - 任务观察样本：`climbing.services.requiredStaminaAbsenceIsObservableAndTransactional`；`climbing.services.presentStaminaCommitsExactlyOnceWithExecution`；`climbing.services.staminaAdapterIdentityMismatchIsTransactional`。
  - 含否定谓词的观察所在场景：`climbing.services.requiredStaminaAbsenceIsObservableAndTransactional`；`climbing.services.staminaAdapterIdentityMismatchIsTransactional`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `climbing snapshot` / `test_climbing_snapshot`（当前起始 643 行，3 注册）：
  - 参与接口正文证据：physics、climbing；实际调用 `snapshot`, `restore`, `upsertAction`, `executionId`, `set`, `error`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`climbing.snapshot.roundTripRestoresPinnedExecution`；`climbing.snapshot.supportsNMinusOneAndPinsActiveDefinitionAcrossReload`；`climbing.snapshot.futureVersionAndStaleLinkFailuresAreAtomic`。
  - 含否定谓词的观察所在场景：`climbing.snapshot.futureVersionAndStaleLinkFailuresAreAtomic`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `container adapters` / `test_container_adapters`（当前起始 747 行，2 注册）：
  - 参与接口正文证据：building、common、game_event、vehicle、container；实际调用 `snapshot`, `revision`, `enter`, `seats`, `descriptor`, `placeAt`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`container.adaptersShareParityAndCanonicalLifecycleEnvelope`；`container.adaptersFailureInjectionLeavesMembershipUnchanged`。
  - 含否定谓词的观察所在场景：`container.adaptersShareParityAndCanonicalLifecycleEnvelope`；`container.adaptersFailureInjectionLeavesMembershipUnchanged`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `fluids volume coupling` / `test_fluids_volume_coupling`（当前起始 910 行，3 注册）：
  - 参与接口正文证据：fluids、physics；实际调用 `step`, `newWorld3D`, `newBody`, `contacts`, `newBoxShape`, `restore`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`fluids.volumeCoupling.attachmentReturnsGravityLoadAndTorque`；`fluids.volumeCoupling.extremeMassRatioUsesObservableVelocityLimits`；`fluids.volumeCoupling.sdfObstacleTransfersImpulseToDynamicBody`。
  - 含否定谓词的观察所在场景：`fluids.volumeCoupling.extremeMassRatioUsesObservableVelocityLimits`；`fluids.volumeCoupling.sdfObstacleTransfersImpulseToDynamicBody`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `gameplay control climbing` / `test_gameplay_control_climbing`（当前起始 1032 行，2 注册）：
  - 参与接口正文证据：climbing、physics、cap；实际调用 `executionId`, `isZero`, `has_value`, `upsertAction`, `gameplayDomain`, `availableGameplayActions`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`gameplay.control.climbingReprobesAuthorityForPlayerAndAutomation`；`gameplay.control.climbingRejectsUnauthorizedCharacterWithoutProbeMutation`。
  - 含否定谓词的观察所在场景：`gameplay.control.climbingReprobesAuthorityForPlayerAndAutomation`；`gameplay.control.climbingRejectsUnauthorizedCharacterWithoutProbeMutation`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `gts terrain collider` / `test_gts_terrain_collider`（当前起始 1158 行，1 注册）：
  - 参与接口正文证据：procgen；实际调用 `getBody`, `addVertex`, `replace`, `getBodyCount`, `addTriangle`, `getRevision`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`procgen.physics.gtsTerrainCollider.replacesAtomicallyAndSurvivesWorldFirstTeardown`。
  - 含否定谓词的观察所在场景：`procgen.physics.gtsTerrainCollider.replacesAtomicallyAndSurvivesWorldFirstTeardown`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `physics artifact provider` / `test_physics_artifact_provider`（当前起始 1220 行，6 注册）：
  - 参与接口正文证据：physics、procgen、artifact、cap；实际调用 `prepare`, `rayCast`, `isHandleLive`, `getBodyCount`, `isBox3DBacked`, `bindWorld`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`physics.artifact.stageUsesRealBox3DAndRayQuery`；`physics.artifact.rollbackAndValidationLeaveNoCollider`；`physics.artifact.restoreRebuildsBox3DAndInvalidatesOldHandle`。
  - 含否定谓词的观察所在场景：`physics.artifact.rollbackAndValidationLeaveNoCollider`；`physics.artifact.restoreRebuildsBox3DAndInvalidatesOldHandle`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `physics consumer integration` / `test_physics_consumer_integration`（当前起始 1458 行，3 注册）：
  - 参与接口正文证据：physics、pixelworld、scene、scene_physics、pixelworld_physics；实际调用 `expect`, `boundWorld`, `clear`, `isInvalid`, `bindGeneratedColliders`, `runtimeHandle`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`physics.consumer.sceneRuntimeBindsGeneratedCollidersToGameplayWorld`；`physics.consumer.sceneRuntimeObservesDestroyedGameplayWorldAsStale`；`pixelworld_physics.moduleCreatesProductionTerrainProjection`。
  - 含否定谓词的观察所在场景：`physics.consumer.sceneRuntimeObservesDestroyedGameplayWorldAsStale`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `physics contact query` / `test_physics_contact_query`（当前起始 1510 行，3 注册）：
  - 参与接口正文证据：本section复用上文头/共用fixture；需沿实际helper确认provider；实际调用 `queryBodyContactsOwned`, `runtimeHandle`, `newBody`, `newBoxShape`, `update`, `getId`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`physics.contacts.ownedCoverageAndLifetime`；`physics.contacts.truncationIsExplicit`；`physics.contacts.rejectsForeignStaleAndInvalidInputs`。
  - 含否定谓词的观察所在场景：`physics.contacts.ownedCoverageAndLifetime`；`physics.contacts.rejectsForeignStaleAndInvalidInputs`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `physics core boundary` / `test_physics_core_boundary`（当前起始 1584 行，8 注册）：
  - 参与接口正文证据：physics、cap；实际调用 `diagnostics`, `step`, `code`, `observation`, `newBody`, `kind`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`physics.core.backendContract.isHeadlessAndObservable`；`physics.core.backendContract.cpuAndMockShareObservableRules`；`physics.core.backendFallbackIsStructuredAndObservable`。
  - 含否定谓词的观察所在场景：`physics.core.backendFallbackIsStructuredAndObservable`；`physics.core.world3dFailedStepPreservesPreviousContactEvents`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `physics destruction` / `test_physics_destruction`（当前起始 1860 行，4 注册）：
  - 参与接口正文证据：physics、schema；实际调用 `newWorld3D`, `boneState`, `applyField`, `boneLink`, `resolve`, `isEdgeBroken`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`physics_destruction.strainFieldBreaksEdgeAndDetachesBones`；`physics_destruction.instanceDestroyReleasesBodiesBeforeWorld`；`physics_destruction.worldDestroyFirstLeavesStaleLinks`。
  - 含否定谓词的观察所在场景：`physics_destruction.strainFieldBreaksEdgeAndDetachesBones`；`physics_destruction.instanceDestroyReleasesBodiesBeforeWorld`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `physics destruction p3` / `test_physics_destruction_p3`（当前起始 2018 行，6 注册）：
  - 参与接口正文证据：physics、schema；实际调用 `applyField`, `boneState`, `isEdgeBroken`, `newWorld3D`, `step`, `boneClusterId`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`physics_destruction_p3.edgeBreakBudgetDefersAndDrains`；`physics_destruction_p3.clusterBreakEventsAcrossMembership`；`physics_destruction_p3.sleepBudgetDefersRemainder`。
- `physics joint frames` / `test_physics_joint_frames`（当前起始 2303 行，2 注册）：
  - 参与接口正文证据：physics；实际调用 `newBody`, `arraySize`, `setRotation`, `newRevoluteJoint`, `localFrames`, `snapshot`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`physics.joints.localFramesAreOwnedAndBodyRelative`；`physics.joints.rotatedFramesSurviveSnapshotRestore`。
  - 含否定谓词的观察所在场景：`physics.joints.localFramesAreOwnedAndBodyRelative`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `physics jolt cooking` / `test_physics_jolt_cooking`（当前起始 2376 行，1 注册）：
  - 参与接口正文证据：physics；实际调用 `getMass`, `setDensity`, `getLocalCenterX`, `getInertiaYY`, `setType`, `createShape`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`physics.jolt.deferredDensityParity`。
  - 含否定谓词的观察所在场景：`physics.jolt.deferredDensityParity`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `physics mechanism` / `test_physics_mechanism`（当前起始 2451 行，4 注册）：
  - 参与接口正文证据：physics；实际调用 `newBody`, `newBoxShape`, `getKind`, `updateFull`, `newWorld3D`, `setDrive`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`physics.joint.weldMotorParallelAndFilterAreUsable`；`physics.mechanism.shaftDriveSpinsRotor`；`physics.mechanism.ratchetBlocksReverseSpin`。
  - 含否定谓词的观察所在场景：`physics.mechanism.shaftDriveSpinsRotor`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `physics mechanism2d` / `test_physics_mechanism2d`（当前起始 2579 行，4 注册）：
  - 参与接口正文证据：physics；实际调用 `newBody`, `newRectangleFixture`, `getKind`, `updateFull`, `newWorld`, `setMeter`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`physics.joint2d.distanceRevoluteWeldMotorAndGearAreUsable`；`physics.mechanism2d.shaftDriveSpinsRotor`；`physics.mechanism2d.ratchetBlocksReverseSpin`。
  - 含否定谓词的观察所在场景：`physics.mechanism2d.shaftDriveSpinsRotor`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `physics rigid backend` / `test_physics_rigid_backend`（当前起始 2718 行，5 注册）：
  - 参与接口正文证据：common；实际调用 `isValid`, `error`, `destroy`, `newBody`, `getX`, `code`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`physics.backend.bodyDispatchDoesNotRequireNativeBody`；`physics.backend.jointDispatchDoesNotRequireNativeConstraint`；`physics.backend.parentDestructionInvalidatesBodyAndJoint`。
  - 含否定谓词的观察所在场景：`physics.backend.bodyDispatchDoesNotRequireNativeBody`；`physics.backend.jointDispatchDoesNotRequireNativeConstraint`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `physics rope` / `test_physics_rope`（当前起始 3101 行，2 注册）：
  - 参与接口正文证据：physics；实际调用 `newWorld3D`, `newBody`, `setCollideWorld`, `setRadius`, `setGravity`, `update`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`physics.rope.continuousCollisionStopsAtTriangleMesh`；`physics.rope.dynamicRigidBodyReceivesReactionImpulse`。
- `physics trajectory collision` / `test_physics_trajectory_collision`（当前起始 3144 行，5 注册）：
  - 参与接口正文证据：common、physics；实际调用 `advance`, `arm`, `registerCollider`, `newWorld3D`, `bindPoseSource`, `newBody`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`trajectoryCollision.sphereSweepHitsAndDedupsAgainstWorld3D`；`trajectoryCollision.dualSocketCapsuleSweepsWeaponBlade`；`trajectoryCollision.singleBoneCapsuleUsesLocalHalfHeight`。
  - 含否定谓词的观察所在场景：`trajectoryCollision.rejectsInvalidCatalogAndSurvivesWorldDestroy`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `physics world snapshot topology` / `test_physics_world_snapshot_topology`（当前起始 3368 行，11 注册）：
  - 参与接口正文证据：common、physics；实际调用 `newBody`, `snapshot`, `runtimeHandle`, `restore`, `isValid`, `findBody`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`physics.snapshot_v2_persists_2d_fixture_geometry_and_rejects_unknown_fields_atomically`；`physics.snapshot_v2_rebuilds_mesh_heightfield_and_joint_sources_from_detached_state`；`physics.snapshot_restored_world_keeps_live_step_driver`。
  - 含否定谓词的观察所在场景：`physics.snapshot_v2_persists_2d_fixture_geometry_and_rejects_unknown_fields_atomically`；`physics.snapshot_v2_rebuilds_mesh_heightfield_and_joint_sources_from_detached_state`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `pixelworld physics` / `test_pixelworld_physics`（当前起始 3659 行，6 注册）：
  - 参与接口正文证据：physics；实际调用 `expect`, `setMaterial`, `sync`, `rayCast`, `bodyCount`, `sourceRevision`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`pixelworld_physics.fragment_decomposition_and_sleep_settlement_are_transactional`；`pixelworld_physics.both_destruction_orders_and_restore_staleness_are_safe`；`pixelworld_physics.terrain_cache_rebuilds_dirty_chunks_and_rolls_back_budget_failure`。
  - 含否定谓词的观察所在场景：`pixelworld_physics.fragment_decomposition_and_sleep_settlement_are_transactional`；`pixelworld_physics.both_destruction_orders_and_restore_staleness_are_safe`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `targeting` / `test_targeting`（当前起始 3852 行，1 注册）：
  - 参与接口正文证据：physics、sensing、cap；实际调用 `code`, `query`, `index`, `addWorld`, `expect`, `has_value`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`targeting.physicsAdapterRequiresOneActiveWorld`。
  - 含否定谓词的观察所在场景：`targeting.physicsAdapterRequiresOneActiveWorld`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [physics_snapshot3d_ownership.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/physics-simulation/physics_snapshot3d_ownership.cpp:1)

- 当前规模：766 行；13 literal注册。
- 实际任务：候选world/joint/shape restore原子提交→native ownership/旧句柄/轨迹/接触/脚本引用。
- `本文件任务`（当前起始 1 行，13 注册）：
  - 参与接口正文证据：common、physics；实际调用 `set`, `snapshot`, `newBody`, `restore`, `newSphereShape`, `isValid`。
  - setup正文迹象：真实VM、body/world backend。
  - 任务观察样本：`physics.snapshot3d.candidateOwnershipCommitAndRollback`；`physics.snapshot3d.preservesIndependentJointFrames`；`physics.snapshot3d.jointParametersRoundTripAllKinds`。
  - 含否定谓词的观察所在场景：`physics.snapshot3d.candidateOwnershipCommitAndRollback`；`physics.snapshot3d.preservesIndependentJointFrames`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [physics_vehicle_tires.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/physics-simulation/physics_vehicle_tires.cpp:1)

- 当前规模：202 行；6 literal注册。
- 实际任务：Vehicle ECS/Rig→2D chassis/tire forces、实际接触/驱动，非单独轮胎公式。
- `本文件任务`（当前起始 1 行，6 注册）：
  - 参与接口正文证据：physics、vehicle；实际调用 `suspension`, `step`, `input`, `getLinearVelocityX`, `raw`, `getAngle`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`physics.vehicleTires2D.forcesDriveAndSteerWithoutVelocitySetters`；`physics.vehicleTires2D.pendingRetryCannotPublishOrDoubleApply`；`physics.vehicleTires2D.brakeAndAirborneRespectContactCapacity`。
  - 含否定谓词的观察所在场景：`physics.vehicleTires2D.pendingRetryCannotPublishOrDoubleApply`；`physics.vehicleTires2D.brakeAndAirborneRespectContactCapacity`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [physics_vehicle_tires3d.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/physics-simulation/physics_vehicle_tires3d.cpp:1)

- 当前规模：262 行；10 literal注册。
- 实际任务：Vehicle/Rig→3D悬挂/轮胎/底盘真实力、转向和失败提交。
- `本文件任务`（当前起始 1 行，10 注册）：
  - 参与接口正文证据：physics、vehicle；实际调用 `step`, `suspension`, `getLinearVelocityZ`, `input`, `getLinearVelocityY`, `getLinearVelocityX`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`physics.vehicleTires3D.suspensionMobilityDrivesAndSteersByForces`；`physics.vehicleTires3D.dynamicGroundReceivesOppositeImpulse`；`physics.vehicleTires3D.relativePointVelocityIncludesMovingGroundSpin`。
  - 含否定谓词的观察所在场景：`physics.vehicleTires3D.airborneAndZeroFrictionCannotDriveChassis`；`physics.vehicleTires3D.rejectedStepAndInvalidWheelAreAtomic`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [physics_wing_assembly.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/physics-simulation/physics_wing_assembly.cpp:1)

- 当前规模：127 行；4 literal注册。
- 实际任务：WingAssembly载荷组合→实际2D/3D body与提交后的力矩/运动。
- `本文件任务`（当前起始 1 行，4 注册）：
  - 参与接口正文证据：physics；实际调用 `prepare`, `getLinearVelocityY`, `newBody`, `commit`, `getLinearVelocityX`, `getAngularVelocityX`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`physics.forces.wingAssembly2DUsesRotatedPointVelocity`；`physics.forces.wingAssembly3DControlSurfacesAndWind`；`physics.forces.wingAssemblyFailureIsAtomic`。
  - 含否定谓词的观察所在场景：`physics.forces.wingAssemblyFailureIsAtomic`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

### projectile-behavior

#### [world_sweep.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/projectile-behavior/world_sweep.cpp:1)

- 当前规模：171 行；4 literal注册。
- 实际任务：world query token→Box3D/Jolt sweep→weapon carrier hit/pierce；restore/destroy/预算完整闭包。
- `本文件任务`（当前起始 1 行，4 注册）：
  - 参与接口正文证据：common、physics、weapon；实际调用 `query`, `queryToken`, `bind`, `getId`, `restore`, `snapshot`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`carrierWorldSweep.box3dContractAndRestore`；`carrierWorldSweep.joltContractAndRestore`；`carrierWorldSweep.bodyDestructionAndBudget`。
  - 含否定谓词的观察所在场景：`carrierWorldSweep.bodyDestructionAndBudget`；`carrierWorldSweep.carrierPierceSnapshotAndFailure`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

### crowd-runtime

#### [CrowdScript.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/crowd-runtime/CrowdScript.cpp:1)

- 当前规模：317 行；1 literal注册。
- 实际任务：一次真实VM/bindings fixture，14具名脚本场景；共享setup与脚本局部状态。
- `本文件任务`（当前起始 1 行，1 注册）：
  - 参与接口正文证据：crowd；实际调用 `run`, `compileSource`, `expect`, `addTable`, `addClass`, `addFunc`。
  - setup正文迹象：真实VM。
  - 任务观察样本：`crowd.script.runtimeBindingContracts`。

#### [CrowdThread.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/crowd-runtime/CrowdThread.cpp:1)

- 当前规模：306 行；7 literal注册。
- 实际任务：真实JobSystem/Crowd scheduler→串并行bit exact、失败join/owner释放、各phase rollback/retry。
- `本文件任务`（当前起始 1 行，7 注册）：
  - 参与接口正文证据：crowd、thread；实际调用 `getAgentState`, `advance`, `setWorkScheduler`, `setClampToField`, `load`, `setDefaultSpeed`。
  - setup正文迹象：native worker。
  - 任务观察样本：`crowd.thread.serialParallelContactAndReportsAreBitExact`；`crowd.thread.predictiveAvoidanceMatchesSerialWithFourWorkers`；`crowd.thread.workerFailureJoinsAndInFlightOwnerSurvivesRelease`。
  - 含否定谓词的观察所在场景：`crowd.thread.predictiveAvoidanceMatchesSerialWithFourWorkers`；`crowd.thread.workerFailureJoinsAndInFlightOwnerSurvivesRelease`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

### rts-crowd

#### [CrowdMotion.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/rts-crowd/CrowdMotion.cpp:1)

- 当前规模：248 行；6 literal注册。
- 实际任务：RTS ECS单位/命令→Crowd真实worker/gather/project adapter及partial failure。
- `本文件任务`（当前起始 1 行，6 注册）：
  - 参与接口正文证据：..、crowd、rts、thread；实际调用 `motion`, `crowd`, `setWorkScheduler`, `release`, `orders`, `enqueue`。
  - setup正文迹象：native worker。
  - 任务观察样本：`rts.thread.scriptCrowdExecutionRequiresConfiguredWorldAndScheduler`；`rts.crowd.parallelConsumerMatchesSerialMotionAndArrival`；`rts.crowd.parallelConsumerRejectsExpiredProviderWithoutPublishingMotion`。
  - 含否定谓词的观察所在场景：`rts.thread.scriptCrowdExecutionRequiresConfiguredWorldAndScheduler`；`rts.crowd.parallelConsumerRejectsExpiredProviderWithoutPublishingMotion`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [rts_crowd_flow_navigation.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/rts-crowd/rts_crowd_flow_navigation.cpp:1)

- 当前规模：877 行；23 literal注册。
- 实际任务：RTS orders/groups→Crowd/map flow，field共享/到达/障碍/移动目标与保存恢复。
- `test_rts_crowd_flow_navigation_composition` / `test_rts_crowd_flow_navigation_composition`（当前起始 6 行，18 注册）：
  - 参与接口正文证据：crowd、rts、map；实际调用 `motion`, `crowd`, `observation`, `getCrowdFlowNavigationStatus`, `request`, `assign`。
  - setup正文迹象：真实VM。
  - 任务观察样本：`rts.crowd.sharedFlowStartsOldestRequestBeforeNewLowerKey`；`rts.crowd.failedPumpAndSameOwnerInputRepairDoNotRefundTickQuota`；`rts.crowd.sharedFlowWorkServesPersistentGoalsWhileLowerKeysChurn`。
  - 含否定谓词的观察所在场景：`rts.crowd.failedPumpAndSameOwnerInputRepairDoNotRefundTickQuota`；`rts.crowd.reconfigureAndClearReenableCannotRefundFailedTickWork`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `test_rts_crowd_flow_groups_composition` / `test_rts_crowd_flow_groups_composition`（当前起始 653 行，5 注册）：
  - 参与接口正文证据：crowd、rts、map；实际调用 `generation`, `steerRoute`, `requestRoute`, `motion`, `pump`, `replaceGroups`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`rts.crowd.groupWrongSlotStartsBoundedTerminalRouteAndKeepsItAcrossRefresh`；`rts.crowd.groupProjectionRejectsDuplicatesAtomicallyAndOldRouteEpoch`；`rts.crowd.movementGroupSharesBarrierRouteAndArrivesAtDistinctSlots`。
  - 含否定谓词的观察所在场景：`rts.crowd.groupProjectionRejectsDuplicatesAtomicallyAndOldRouteEpoch`；`rts.crowd.groupOldOrderAndReusedEntitySlotCannotReuseMemberProjection`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [rts_crowd_registry.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/rts-crowd/rts_crowd_registry.cpp:1)

- 当前规模：341 行；12 literal注册。
- 实际任务：typed registry/provider→Crowd/map/surface投影，销毁双序、swap-pop/stale、VM facade。
- `test_rts_crowd_registry_composition` / `test_rts_crowd_registry_composition`（当前起始 5 行，6 注册）：
  - 参与接口正文证据：rts、crowd；实际调用 `crowd`, `motion`, `release`, `getAgentCount`, `setCrowdProvider`, `newUnit`。
  - setup正文迹象：真实VM。
  - 任务观察样本：`rts.crowd.providerDestructionIsObservableAndSafeInBothOrders`；`rts.crowd.invalidClockRejectsBeforeBindingOrIntentMutation`；`rts.crowd.scriptRelocationUsesCheckedOwnerPlacement`。
  - 含否定谓词的观察所在场景：`rts.crowd.providerDestructionIsObservableAndSafeInBothOrders`；`rts.crowd.invalidClockRejectsBeforeBindingOrIntentMutation`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `test_rts_crowd_surface_adapter_composition` / `test_rts_crowd_surface_adapter_composition`（当前起始 151 行，5 注册）：
  - 参与接口正文证据：map、crowd、rts；实际调用 `getFieldStatus`, `setTopology`, `configureCrowdSurfaceTracking`, `setNavigationProvider`, `step`, `captureNavigationGrid`。
  - setup正文迹象：真实VM。
  - 任务观察样本：`rts.crowd.mapAdapterPreservesAdjacencyAndDestinationEntryCosts`；`rts.crowd.mapAdapterInflatesNavigationAndPublishesRawPhysicalGeometry`；`rts.crowd.surfaceTrackingPublishesOnlyOnChangedInputsAndRejectsBeforeSimulation`。
  - 含否定谓词的观察所在场景：`rts.crowd.mapAdapterInflatesNavigationAndPublishesRawPhysicalGeometry`；`rts.crowd.surfaceTrackingPublishesOnlyOnChangedInputsAndRejectsBeforeSimulation`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `test_rts_crowd_zero_delta_composition` / `test_rts_crowd_zero_delta_composition`（当前起始 310 行，1 注册）：
  - 参与接口正文证据：crowd、rts；实际调用 `motion`, `crowd`, `orders`, `enqueue`, `getAgentState`, `getNamedAgentIndex`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`rts.crowd.zeroDeltaReconcilesOnlyReachedGoals`。
  - 含否定谓词的观察所在场景：`rts.crowd.zeroDeltaReconcilesOnlyReachedGoals`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [rts_formation.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/rts-crowd/rts_formation.cpp:1)

- 当前规模：498 行；13 literal注册。
- 实际任务：formation/attack slots/group orders→ECS/Crowd/weapon/suppression实际组合。
- `test_rts_formation_composition` / `test_rts_formation_composition`（当前起始 9 行，1 注册）：
  - 参与接口正文证据：rts；实际调用 `motion`, `newUnit`, `crowd`, `commandUnits`, `findUnit`, `orders`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`rts.formation.largeTranslatedGridDoesNotReverseMarchingRows`。
- `test_rts_attack_slots_composition` / `test_rts_attack_slots_composition`（当前起始 38 行，4 注册）：
  - 参与接口正文证据：rts；实际调用 `navigation`, `motion`, `newUnit`, `crowd`, `step`, `orders`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`rts.attackSlots.frontPositionsAreUniqueAndRearCannotFire`；`rts.attackSlots.targetDestructionAndReplacementOrderExpireReservations`；`rts.attackSlots.waitingMembersNeverProduceRealWeaponFire`。
  - 含否定谓词的观察所在场景：`rts.attackSlots.frontPositionsAreUniqueAndRearCannotFire`；`rts.attackSlots.targetDestructionAndReplacementOrderExpireReservations`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `test_rts_group_attack_move_composition` / `test_rts_group_attack_move_composition`（当前起始 219 行，4 注册）：
  - 参与接口正文证据：rts；实际调用 `motion`, `newUnit`, `orders`, `current`, `submitMovementGroup`, `snapshotState`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`rts.groups.attackMoveAdmissionAndRestorePreserveOrders`；`rts.groups.attackMoveReplayV6RoundTripsAndRejectsV5CombinationAtomically`；`rts.groups.attackMoveEngagedMemberDoesNotHoldBackAdvancingMember`。
  - 含否定谓词的观察所在场景：`rts.groups.attackMoveAdmissionAndRestorePreserveOrders`；`rts.groups.attackMoveReplayV6RoundTripsAndRejectsV5CombinationAtomically`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `test_rts_combat_suppression_aura_composition` / `test_rts_combat_suppression_aura_composition`（当前起始 337 行，1 注册）：
  - 参与接口正文证据：rts、weapon、sensing、combat；实际调用 `release`, `morale`, `definition`, `state`, `faction`, `combat`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`rts.combat.suppressionAuraUsesLiveProviderState`。
- `test_rts_performance_semantics_composition` / `test_rts_performance_semantics_composition`（当前起始 400 行，3 注册）：
  - 参与接口正文证据：rts、crowd；实际调用 `expect`, `durability`, `crowd`, `combat`, `rally`, `cleanupDestroyed`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`rts.performanceCasualtyBatchRepairsOwnedReferencesWithoutCrowdProvider`；`rts.performanceCasualtyBatchCrowdFailureLeavesReferencesAndRootsIntact`；`rts.performanceNestedPassengerCleanupKeepsRecursiveLifecycle`。
  - 含否定谓词的观察所在场景：`rts.performanceCasualtyBatchRepairsOwnedReferencesWithoutCrowdProvider`；`rts.performanceCasualtyBatchCrowdFailureLeavesReferencesAndRootsIntact`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

### rts-unit-lifecycle

#### [rts_unit_lifecycle_tactics_rts_tests.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/rts-unit-lifecycle/rts_unit_lifecycle_tactics_rts_tests.cpp:1)

- 当前规模：3223 行；64 literal注册。
- 实际任务：多职责聚合；按下列namespace分别审阅其调用和失败观察，不以graphics文件名替代任务判定。
- `rts crowd` / `test_rts_crowd`（当前起始 3 行，6 注册）：
  - 参与接口正文证据：crowd、rts；实际调用 `motion`, `crowd`, `orders`, `release`, `expect`, `getNamedAgentIndex`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`rts.crowd.arrivalPreservesResolvedSeparation`；`rts.crowd.holdOrderResistsPushAndReplacementReleasesIt`；`rts.crowd.predictiveCrossingSurvivesEcsProjection`。
  - 含否定谓词的观察所在场景：`rts.crowd.holdOrderResistsPushAndReplacementReleasesIt`；`rts.crowd.stepBudgetFailureReturnsResult`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `rts crowd production` / `test_rts_crowd_production`（当前起始 189 行，3 注册）：
  - 参与接口正文证据：rts、production；实际调用 `motion`, `stepScript`, `findUnit`, `scriptResource`, `rally`, `unitCount`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`rts.crowd.productionPushPublishesPeerMotion`；`rts.crowd.blockedProductionRetainsPaymentAndIdentityForRetry`；`rts.crowd.scriptProfileCrossingReachesBothDestinations`。
  - 含否定谓词的观察所在场景：`rts.crowd.blockedProductionRetainsPaymentAndIdentityForRetry`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `rts formation` / `test_rts_formation`（当前起始 374 行，6 注册）：
  - 参与接口正文证据：rts；实际调用 `orders`, `current`, `motion`, `exportText`, `release`, `findUnit`。
  - setup正文迹象：真实VM。
  - 任务观察样本：`rts.formation.mixedRadiiFitAndSelectionOrderIsStable`；`rts.formation.invalidSelectionLeavesCommandsUntouched`；`rts.formation.rotationAndColumnPreserveAnchorAndSpacing`。
  - 含否定谓词的观察所在场景：`rts.formation.invalidSelectionLeavesCommandsUntouched`；`rts.formation.rotationAndColumnPreserveAnchorAndSpacing`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `rts groups` / `test_rts_groups`（当前起始 593 行，12 注册）：
  - 参与接口正文证据：rts、crowd、map；实际调用 `motion`, `navigation`, `orders`, `newUnit`, `step`, `snapshotState`。
  - setup正文迹象：真实VM。
  - 任务观察样本：`rts.groups.pacingWorksWithAndWithoutCrowd`；`rts.groups.movingSlotsAssembleBeforeArrival`；`rts.groups.passageReleasesAndRestoresFormation`。
  - 含否定谓词的观察所在场景：`rts.groups.movingSlotsAssembleBeforeArrival`；`rts.groups.opposingSquadsReserveWholeCorridor`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `rts orders` / `test_rts_orders`（当前起始 1241 行，13 注册）：
  - 参与接口正文证据：rts、crowd、map；实际调用 `motion`, `orders`, `release`, `command`, `navigation`, `enqueue`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`rts.facadeCommandsContainmentAndCloakUseOwnedStableRoots`；`rts.formationPlannerIsDeterministic`；`rts.commandFanOutUsesGenericOrdersAndFormation`。
  - 含否定谓词的观察所在场景：`rts.facadeCommandsContainmentAndCloakUseOwnedStableRoots`；`rts.commandFanOutUsesGenericOrdersAndFormation`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `rts production` / `test_rts_production`（当前起始 1795 行，12 注册）：
  - 参与接口正文证据：rts、action、production、economy、resource；实际调用 `rally`, `release`, `motion`, `production`, `faction`, `containment`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`rts.buildingTouchesConstructionDropoffAndRally`；`rts.aiRequestsProductionAndLaunchesAttackMove`；`rts.transportBoardingUsesGenerationCheckedContainment`。
  - 含否定谓词的观察所在场景：`rts.buildingTouchesConstructionDropoffAndRally`；`rts.aiRequestsProductionAndLaunchesAttackMove`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `rts restore` / `test_rts_restore`（当前起始 2388 行，12 注册）：
  - 参与接口正文证据：rts、weapon、combat；实际调用 `motion`, `newUnit`, `combat`, `apply`, `supply`, `rally`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`rts.buildingAmmoProductionPurchasesFiniteStockAndSnapshotsFractionalProgress`；`rts.orderAndProductionAdaptersRoundTripCanonicalQueueSnapshots`；`rts.rootSnapshotRestoresStableRelationshipsAndRejectsTopologyMismatch`。
  - 含否定谓词的观察所在场景：`rts.orderAndProductionAdaptersRoundTripCanonicalQueueSnapshots`；`rts.rootSnapshotRestoresStableRelationshipsAndRejectsTopologyMismatch`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

### gameplay-composition

#### [climbing_contact_presentation.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/climbing_contact_presentation.cpp:1)

- 当前规模：53 行；1 literal注册。
- 实际任务：Runtime loader/脚本固定步接触→presentation blend，释放及原子拒绝。
- `本文件任务`（当前起始 1 行，1 注册）：
  - 参与接口正文证据：common；实际调用 `parent_path`, `runSource`, `substr`, `is_open`, `rdbuf`, `str`。
  - setup正文迹象：真实VM、模型/文件导入。
  - 任务观察样本：`climbing.contactPresentation.fixedStepBlendReleasesAndRejectsAtomically`。

#### [climbing_pose_contacts.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/climbing_pose_contacts.cpp:1)

- 当前规模：159 行；7 literal注册。
- 实际任务：ClimbingContact目标→Animation skeleton/pose手脚链，断链/非数拒绝前不修改pose。
- `本文件任务`（当前起始 1 行，7 注册）：
  - 参与接口正文证据：animation、climbing；实际调用 `addBone`, `getWorldPositionX`, `code`, `getWorldPositionY`, `getWorldPositionZ`, `setBindPosition`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`climbing.poseContacts.nonHandContactsDoNotPinReleasedHands`；`climbing.poseContacts.disabledHandsAreNoOp`；`climbing.poseContacts.rejectsNonfiniteInputBeforeChangingEitherHand`。
  - 含否定谓词的观察所在场景：`climbing.poseContacts.nonHandContactsDoNotPinReleasedHands`；`climbing.poseContacts.rejectsNonfiniteInputBeforeChangingEitherHand`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [gameplay_composition_action_preview_profile_tests.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/gameplay_composition_action_preview_profile_tests.cpp:1)

- 当前规模：36 行；1 literal注册。
- 实际任务：ActionPreviewProfileStore→真实临时目录save/load及资产identity/missing结果。
- `本文件任务`（当前起始 1 行，1 注册）：
  - 参与接口正文证据：action、editor；实际调用 `load`, `save`。
  - setup正文迹象：模型/文件导入。
  - 任务观察样本：`actionPreviewProfile.sidecarStoreRoundTripsAndRejectsIdentityMismatch`。
  - 含否定谓词的观察所在场景：`actionPreviewProfile.sidecarStoreRoundTripsAndRejectsIdentityMismatch`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [gameplay_composition_action_upper_body_orientation_tests.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/gameplay_composition_action_upper_body_orientation_tests.cpp:1)

- 当前规模：168 行；3 literal注册。
- 实际任务：ActionTimeline/Montage→animation layer/compensation顺序和mask/provider策略。
- `本文件任务`（当前起始 1 行，3 注册）：
  - 参与接口正文证据：action、animation；实际调用 `addBone`, `compose`, `world`, `applyBindPose`, `setLocalRotation`, `setLayerRole`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`actionMontage.upperBodyProfileReplacementIsTransactional`；`actionMontage.upperBodyCompensationObservesProviderMaskAndFullBodyPolicy`；`actionMontage.upperBodyCompensationRunsBeforeAdditiveLayersDeterministically`。
  - 含否定谓词的观察所在场景：`actionMontage.upperBodyProfileReplacementIsTransactional`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [gameplay_composition_graphics_animation_inertializer_tests.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/gameplay_composition_graphics_animation_inertializer_tests.cpp:1)

- 当前规模：4543 行；129 literal注册。
- 实际任务：多职责聚合；按下列namespace分别审阅其调用和失败观察，不以graphics文件名替代任务判定。
- `action editor adapter` / `test_action_editor_adapter`（当前起始 3 行，2 注册）：
  - 参与接口正文证据：action、common、editor、action_editor、cap；实际调用 `invoke`, `timeline`, `targetId`, `describe`, `format`, `nanoseconds`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`actionEditor.seedTargetExposesCanonicalProperties`；`actionEditor.module_registers_automation_target_and_property_command`。
- `action game event` / `test_action_game_event`（当前起始 79 行，1 注册）：
  - 参与接口正文证据：action、game_event；实际调用 `has_value`, `dispatch`, `setEnabled`, `lastSequence`, `format`, `status`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`actionGameEvent.validatesAndAppendsToPersistentLog`。
  - 含否定谓词的观察所在场景：`actionGameEvent.validatesAndAppendsToPersistentLog`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `action montage` / `test_action_montage`（当前起始 169 行，17 注册）：
  - 参与接口正文证据：action、animation、editor；实际调用 `play`, `present`, `addBone`, `local`, `erase`, `pose`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`actionMontage.schemaV5RoundTripAndLegacyMigration`；`actionMontage.settingsUpdateIsValidatedAndPreservesPreparedPlayback`；`actionMontage.sectionSplitUpdateIsAtomicAndPreservesPreparedPlayback`。
  - 含否定谓词的观察所在场景：`actionMontage.settingsUpdateIsValidatedAndPreservesPreparedPlayback`；`actionMontage.sectionSplitUpdateIsAtomicAndPreservesPreparedPlayback`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `action preview controller` / `test_action_preview_controller`（当前起始 937 行，7 注册）：
  - 参与接口正文证据：action、common、editor；实际调用 `previewTime`, `has_value`, `play`, `stepFrames`, `playing`, `refresh`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`actionPreviewController.explicitlyReportsMissingRootMotionProvider`；`actionPreviewController.samplesAndDrawsOwningRootMotionPath`；`actionPreviewController.routesPresentationAndStateBoundaryCues`。
  - 含否定谓词的观察所在场景：`actionPreviewController.prepareFailureLeavesTransportUnchanged`；`actionTimelineEditor.rejectsStaleAndForgedPreviewPlans`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `action timeline` / `test_action_timeline`（当前起始 1183 行，10 注册）：
  - 参与接口正文证据：action、editor；实际调用 `target`, `timeline`, `toValue`, `activeEditor`, `advance`, `undo`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`actionTimeline.versionedRoundTripAndDeterministicSampling`；`actionTimeline.disabledItemsPersistAndDoNotProjectRuntimeState`；`gameplayAction.advanceProjectsAuthoredTimelineBoundaries`。
  - 含否定谓词的观察所在场景：`actionTimeline.versionedRoundTripAndDeterministicSampling`；`actionTimeline.disabledItemsPersistAndDoNotProjectRuntimeState`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `action timeline payload editor` / `test_action_timeline_payload_editor`（当前起始 1646 行，3 注册）：
  - 参与接口正文证据：action、editor；实际调用 `target`, `payload`, `revision`, `undo`, `seconds`, `timeline`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`actionTimelinePayloadEditor.patchPreservesUnknownFieldsAndRejectsAtomically`；`actionTimelinePayloadEditor.vfxClipFitCommandsAreAtomicAndUndoable`；`actionTimelinePayloadEditor.vfxClipFitRejectsWrongShapeAndOutOfRangeWithoutMutation`。
  - 含否定谓词的观察所在场景：`actionTimelinePayloadEditor.patchPreservesUnknownFieldsAndRejectsAtomically`；`actionTimelinePayloadEditor.vfxClipFitRejectsWrongShapeAndOutOfRangeWithoutMutation`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `action timeline widget` / `test_action_timeline_widget`（当前起始 1802 行，13 注册）：
  - 参与接口正文证据：action、editor、cap；实际调用 `target`, `timeline`, `selectItem`, `clear`, `handleShortcut`, `undo`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`actionTimelineWidget.projectsDrawsAndHitTestsSemanticItems`；`actionTimelineWidget.projectsSemanticResourceThumbnailsWithoutPersistingThem`；`actionTimelineWidget.usesRegistryLabelAndExplicitCustomVisualFallback`。
  - 含否定谓词的观察所在场景：`actionTimelineWidget.projectsBoundedAudioWaveformWithoutOwningAudioState`；`actionTimelineWidget.drawsAndTransactionallyInspectsParameterCurveKeys`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `agent composition` / `test_agent_composition`（当前起始 2246 行，2 注册）：
  - 参与接口正文证据：..、agent、grid、ui；实际调用 由fixture/自由函数实现，不能仅凭无方法调用判为local。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`agent.productionGridGame`；`agent.productionUiLayout`。
- `agent tensor` / `test_agent_tensor`（当前起始 2288 行，1 注册）：
  - 参与接口正文证据：agent、cap；实际调用 `resize`, `status`, `code`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`agent.tensorParityAndProviderLifetime`。
  - 含否定谓词的观察所在场景：`agent.tensorParityAndProviderLifetime`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `animation action preview` / `test_animation_action_preview`（当前起始 2333 行，3 注册）：
  - 参与接口正文证据：action、animation、editor；实际调用 `sampleRootMotion`, `addPositionKey`, `setDuration`, `setLoop`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`animationActionPreview.samplesRegisteredClipWithoutRetainingIt`；`animationActionPreview.rejectsMissingAmbiguousAndInvalidRootClips`；`animationActionPreview.clipDestructionBecomesObservableNotStale`。
  - 含否定谓词的观察所在场景：`animationActionPreview.rejectsMissingAmbiguousAndInvalidRootClips`；`animationActionPreview.clipDestructionBecomesObservableNotStale`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `animation inertializer` / `test_animation_inertializer`（当前起始 2403 行，40 注册）：
  - 参与接口正文证据：animation、common、data、filesystem、model3d、animation_editing、editing；实际调用 `addBone`, `setBindPosition`, `update`, `apply`, `applyBindPose`, `pose`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`animation.inertializer.ownsHistoryAndHonorsBoneDurations`；`animation.inertializer.invalidInputsLeaveTransitionAndOutputUnchanged`；`animation.inertializer.interruptionPreservesMovingRotationAndVelocity`。
  - 含否定谓词的观察所在场景：`animation.inertializer.invalidInputsLeaveTransitionAndOutputUnchanged`；`animation.orientationWarping.invalidInputsAndDisabledApplyLeavePoseUnchanged`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `animation smr neural retarget` / `test_animation_smr_neural_retarget`（当前起始 3444 行，2 注册）：
  - 参与接口正文证据：animation、cap；实际调用 `addBone`, `findBone`, `setBindPosition`, `addRotationKey`, `setDuration`, `setSampleRate`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`animation.smr.neural.tensorBackendRewritesClip`；`animation.smr.neural.fallsBackToClassicalWhenDisabled`。
- `attribute domain adapters` / `test_attribute_domain_adapters`（当前起始 3529 行，1 注册）：
  - 参与接口正文证据：attributes、card、common、rts、vehicle、weapon；实际调用 `stats`, `attributes`, `state`, `modifierAt`, `release`, `health`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`attributes.domainAdapters.projectSelectedStats`。
  - 含否定谓词的观察所在场景：`attributes.domainAdapters.projectSelectedStats`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `attributes rpg unification` / `test_attributes_rpg_unification`（当前起始 3663 行，4 注册）：
  - 参与接口正文证据：attributes、rpg；实际调用 `addModifier`, `modifierAt`, `setBase`, `getFinal`, `addAttributeModifier`, `getFinalAttribute`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`attributes.canonicalOperationOrderAndMetadata`；`attributes.generatedIdsAreSetLocalAndCopyDeterministic`；`attributes.rpgFacadeUsesTheSameAttributeSetTruth`。
  - 含否定谓词的观察所在场景：`attributes.rpgFacadeUsesTheSameAttributeSetTruth`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `audio` / `test_audio`（当前起始 3776 行，23 注册）：
  - 参与接口正文证据：action、audio、common、filesystem、scene、sound、data、cap、editing、script；实际调用 `isPlaying`, `getSourceCount`, `data`, `pump`, `newSource`, `play`。
  - setup正文迹象：真实VM、模型/文件导入。
  - 任务观察样本：`audio.device`；`audio.staticSource.playStop`；`audio.actionBlockProviderOwnsRealSourceEnterExit`。
  - 含否定谓词的观察所在场景：`audio.staticSource.playStop`；`audio.actionPreviewRetainsAdvanceSeeksRefreshAndStopsOnExit`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [gameplay_composition_graphics_buildingfx_tests.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/gameplay_composition_graphics_buildingfx_tests.cpp:1)

- 当前规模：4185 行；94 literal注册。
- 实际任务：多职责聚合；按下列namespace分别审阅其调用和失败观察，不以graphics文件名替代任务判定。
- `buildingfx` / `test_buildingfx`（当前起始 3 行，16 注册）：
  - 参与接口正文证据：building、common、graphics；实际调用 `sync`, `setGridPlane`, `attach`, `detach`, `placeEdge`, `isVisualVisible`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`buildingfx.sync.lifecycle`；`buildingfx.surfacePoseUsesExactAnchorAndFrame`；`buildingfx.edgeVisualUsesMidpointLengthAndThickness`。
  - 含否定谓词的观察所在场景：`buildingfx.sync.lifecycle`；`buildingfx.edgeCurveRasterUsesAuthoritativeTopologyVisuals`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `card payment composition` / `test_card_payment_composition`（当前起始 670 行，5 注册）：
  - 参与接口正文证据：card、economy、rts、transaction、resource、production、orders、action；实际调用 `state`, `expect`, `credit`, `account`, `registerCardsFromJson`, `newCard`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`card.customResourceCostComposesWithRtsAccountAndEffects`；`card.rtsPaymentRejectsDuplicateEffectsBeforeMutation`；`card.customPaymentRejectsInvalidCostAndMissingAccount`。
  - 含否定谓词的观察所在场景：`card.customResourceCostComposesWithRtsAccountAndEffects`；`card.rtsPaymentRejectsDuplicateEffectsBeforeMutation`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `climbing animation` / `test_climbing_animation`（当前起始 851 行，12 注册）：
  - 参与接口正文证据：animation、climbing、common、filesystem、model3d、physics；实际调用 `addBone`, `addPositionKey`, `setDuration`, `addEvent`, `setBindPosition`, `addClip`。
  - setup正文迹象：真实VM、body/world backend、模型/文件导入。
  - 任务观察样本：`climbing.animation.extractsRootMotionAndSemanticNotifies`；`climbing.animation.graphProjectionTriggersOnceAndDegradesExplicitly`；`climbing.animation.bindingValidatesClipIdentityAndResolvesRootBone`。
  - 含否定谓词的观察所在场景：`climbing.animation.bindingValidatesClipIdentityAndResolvesRootBone`；`climbing.animation.rejectsInvalidGraphAndDuplicatePlayerTicks`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `combat action damage` / `test_combat_action_damage`（当前起始 1366 行，1 注册）：
  - 参与接口正文证据：action、combat；实际调用 `has_value`, `status`, `dispatch`, `setEnabled`, `enabled`, `diagnostics`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`combatActionDamage.validatesRoutesAndAtomicallyMutatesResolvedState`。
  - 含否定谓词的观察所在场景：`combatActionDamage.validatesRoutesAndAtomicallyMutatesResolvedState`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `combat action windows` / `test_combat_action_windows`（当前起始 1454 行，2 注册）：
  - 参与接口正文证据：action、combat；实际调用 `dispatch`, `setEnabled`, `has_value`, `activeCount`, `isInvulnerable`, `activeHitboxes`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`combatActionWindows.pairHitboxInvulnerabilityAndInterruptCleanup`；`combatActionWindows.armAndDisarmMeleeHitboxRuntime`。
  - 含否定谓词的观察所在场景：`combatActionWindows.pairHitboxInvulnerabilityAndInterruptCleanup`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `combat arena foundation` / `test_combat_arena_foundation`（当前起始 1627 行，1 注册）：
  - 参与接口正文证据：action、combat；实际调用 `has_value`, `format`, `registerSubject`, `registerOwner`, `setPosition`, `dodge`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`combatArenaFoundation.composesSlicesABCDEF`。
- `combat cancel resolver` / `test_combat_cancel_resolver`（当前起始 1741 行，1 注册）：
  - 参与接口正文证据：action、combat；实际调用 `has_value`, `setEnabled`, `enter`, `format`, `push`, `addEdge`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`combatCancelResolver.consumesBufferAgainstCancelAndComboGraph`。
- `combat damage` / `test_combat_damage`（当前起始 1825 行，9 注册）：
  - 参与接口正文证据：combat、settlement；实际调用 `apply`, `recoverPoise`, `previewSettlement`, `has_value`, `configure`, `configureSettlementRules`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`combatDamage.defaultRuleCommitsHealthPoiseReactionAndKnockback`；`combatDamage.providerPathIsObservableAndPreparedBeforeCommit`；`combatDamage.settlementRulesDriveTheRuntimeUsedByRts`。
  - 含否定谓词的观察所在场景：`combatDamage.providerPathIsObservableAndPreparedBeforeCommit`；`combatDamage.poiseRecoveryIsExplicitCheckedAndClamped`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `combat feel cancel guard` / `test_combat_feel_cancel_guard`（当前起始 2069 行，1 注册）：
  - 参与接口正文证据：combat、action；实际调用 `has_value`, `setEnabled`, `match`, `dispatch`, `format`, `push`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`combatFeelCancelGuardAndComboGraphCompose`。
  - 含否定谓词的观察所在场景：`combatFeelCancelGuardAndComboGraphCompose`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `combat framework composition` / `test_combat_framework_composition`（当前起始 2170 行，1 注册）：
  - 参与接口正文证据：combat、effects、tags、weapon、action；实际调用 `has_value`, `registerTag`, `apply`, `advance`, `format`, `registerDefinition`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`combatFramework.composesTagsPlayerAbilityBuffDamageAttributesAndProjectile`。
- `combat loop` / `test_combat_loop`（当前起始 2264 行，5 注册）：
  - 参与接口正文证据：combat、action；实际调用 `registerSubject`, `format`, `advance`, `state`, `setCharacters`, `setMelee`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`combatLoop.requiresCharacterAndMelee`；`combatLoop.warpsAttackerTowardLockAndFramesCamera`；`combatLoop.appliesMeleeDamageFeelAndHitstopFreeze`。
  - 含否定谓词的观察所在场景：`combatLoop.requiresCharacterAndMelee`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `combat navigation` / `test_combat_navigation`（当前起始 2428 行，5 注册）：
  - 参与接口正文证据：combat、map；实际调用 `setBlocked`, `setTopology`, `has_value`, `steer`, `state`, `registerSubject`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`combatNavigation.pathfinderRoutesAroundBlockedCells`；`combatNavigation.locomotionFollowsDetourAndArrives`；`combatNavigation.replansAfterCanonicalGridMutation`。
  - 含否定谓词的观察所在场景：`combatNavigation.unreachableGoalAbortsLocomotionAtomically`；`combatNavigation.rejectsInvalidGridProjection`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `combat target enemy ai` / `test_combat_target_enemy_ai`（当前起始 2548 行，3 注册）：
  - 参与接口正文证据：combat、action；实际调用 `has_value`, `setPosition`, `format`, `nextIntent`, `registerOwner`, `band`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`combatTargetEnemyAiAndBodyPartDamage`；`combatEnemyAi.telegraphThenRecoverArePunishable`；`combatEnemyAi.midAndFarBandsSteerTowardTarget`。
  - 含否定谓词的观察所在场景：`combatEnemyAi.telegraphThenRecoverArePunishable`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `container transfer` / `test_container_transfer`（当前起始 2723 行，6 注册）：
  - 参与接口正文证据：card、common、inventory、container；实际调用 `revision`, `getSlotInstanceId`, `identity`, `contains`, `code`, `registerCardsFromJson`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`container.zoneUsesStrongCoordinateSpacesAndValidatesShapes`；`container.cardDeckHandDiscardTransferEmitsOneStrongEvent`；`container.cardTransferRejectsFilterAndDuplicateWithoutMutation`。
  - 含否定谓词的观察所在场景：`container.zoneUsesStrongCoordinateSpacesAndValidatesShapes`；`container.cardTransferRejectsFilterAndDuplicateWithoutMutation`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `definition runtime` / `test_definition_runtime`（当前起始 2975 行，3 注册）：
  - 参与接口正文证据：card、common、definitions、effects、definition；实际调用 `state`, `reload`, `identity`, `error`, `code`, `isValid`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`definitionRuntime.policiesAndGenerationQualifiedStaleChecks`；`definitionRuntime.cardAdapterProjectsTypedIdentityAndReloads`；`definitionRuntime.effectAdapterKeepsTypedPolicyAndRejectsActiveReload`。
  - 含否定谓词的观察所在场景：`definitionRuntime.policiesAndGenerationQualifiedStaleChecks`；`definitionRuntime.cardAdapterProjectsTypedIdentityAndReloads`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `definition runtime skill vehicle` / `test_definition_runtime_skill_vehicle`（当前起始 3164 行，2 注册）：
  - 参与接口正文证据：common、rpg、vehicle、condition、definition、definitions；实际调用 `state`, `identity`, `restore`, `definition`, `snapshot`, `error`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`definitionRuntime.skillAdapterUsesCanonicalRegistryAndAtomicSnapshot`；`definitionRuntime.vehicleAdapterProjectsEntityAndRejectsStaleSnapshot`。
  - 含否定谓词的观察所在场景：`definitionRuntime.skillAdapterUsesCanonicalRegistryAndAtomicSnapshot`；`definitionRuntime.vehicleAdapterProjectsEntityAndRejectsStaleSnapshot`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `definition runtime weapon` / `test_definition_runtime_weapon`（当前起始 3309 行，2 注册）：
  - 参与接口正文证据：weapon、definitions、definition；实际调用 `state`, `definitionHandle`, `insert`, `restore`, `replace`, `reload`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`definitionRuntime.weaponTypedStateSnapshotReloadAndStale`；`definitionRuntime.weaponRejectWhileActiveIsAtomic`。
  - 含否定谓词的观察所在场景：`definitionRuntime.weaponTypedStateSnapshotReloadAndStale`；`definitionRuntime.weaponRejectWhileActiveIsAtomic`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `dialogue localization` / `test_dialogue_localization`（当前起始 3412 行，3 注册）：
  - 参与接口正文证据：dialogue、i18n、dnut；实际调用 `set`, `clear`, `validateLocalization`, `importCsv`, `resolveDuration`, `exportMissingCsv`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`dialogueLocalization.roundtripMissingAndVoiceManifest`；`dialogueLocalization.rejectsPartialDuration`；`dialogueLocalization.validatesExactRuntimeLocaleCoverage`。
  - 含否定谓词的观察所在场景：`dialogueLocalization.validatesExactRuntimeLocaleCoverage`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `dialogue payment` / `test_dialogue_payment`（当前起始 3503 行，4 注册）：
  - 参与接口正文证据：attributes、dialogue、economy、statepatch、dnut；实际调用 `getBase`, `status`, `credit`, `setBase`, `executeStateMutation`, `has`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`dialoguePayment.composesMoneyReputationAndStatePatch`；`dialoguePayment.failureDoesNotDebitOrPublishState`；`dialoguePayment.specRejectsUnknownAndNonPositiveFields`。
  - 含否定谓词的观察所在场景：`dialoguePayment.failureDoesNotDebitOrPublishState`；`dialoguePayment.specRejectsUnknownAndNonPositiveFields`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `dialogue proc` / `test_dialogue_proc`（当前起始 3651 行，12 注册）：
  - 参与接口正文证据：avatar、dialogue、scene、i18n；实际调用 `setVarValue`, `pickLineWithParams`, `evalConditionData`, `loadPoolsFromData`, `hasVar`, `playLineWithParams`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`dialogueProc.varsScopes`；`dialogueProc.conditions`；`dialogueProc.poolLoading`。
  - 含否定谓词的观察所在场景：`dialogueProc.varsScopes`；`dialogueProc.conditions`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [gameplay_composition_graphics_editor_camera_target_tests.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/gameplay_composition_graphics_editor_camera_target_tests.cpp:1)

- 当前规模：4339 行；127 literal注册。
- 实际任务：多职责聚合；按下列namespace分别审阅其调用和失败观察，不以graphics文件名替代任务判定。
- `editor behavior graph` / `test_editor_behavior_graph`（当前起始 3 行，5 注册）：
  - 参与接口正文证据：crowd、npc_ai；实际调用 `applyDomainOperation`, `findPin`, `createNode`, `canConnect`, `connect`, `makeSetPath`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.behavior.graph_compiles_deterministic_tree`；`editor.behavior.graph_rejects_cycles_or_unreachable_nodes`；`editor.crowd.agent_zone_path_operations_are_reversible`。
  - 含否定谓词的观察所在场景：`editor.behavior.graph_rejects_cycles_or_unreachable_nodes`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor biome rules editor` / `test_editor_biome_rules_editor`（当前起始 174 行，5 注册）：
  - 参与接口正文证据：common、editor、procgen、biome_editor、editing、cap；实际调用 `invoke`, `pointCount`, `getPanelContext`, `previewRevision`, `revision`, `layerDensity`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.biome.rules_editor_installs_workspace_and_previews_deterministically`；`editor.biome.rules_editor_exclusions_reduce_points_and_reject_invalid_density`；`editor.biome.properties_are_reversible_and_snapshot_load_is_atomic`。
  - 含否定谓词的观察所在场景：`editor.biome.rules_editor_exclusions_reduce_points_and_reject_invalid_density`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor building target` / `test_editor_building_target`（当前起始 362 行，16 注册）：
  - 参与接口正文证据：building、editing；实际调用 `applyDomainOperation`, `revision`, `makePlace`, `preflight`, `getBuildingCount`, `getBuildingId`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`editor.building.preview_reports_snap_footprint_and_occupancy`；`editor.building.place_move_remove_are_exactly_reversible`；`editor.building.undo_restore_conflicts_instead_of_overwriting_occupancy`。
  - 含否定谓词的观察所在场景：`editor.building.preview_reports_snap_footprint_and_occupancy`；`editor.building.place_move_remove_are_exactly_reversible`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor building tool` / `test_editor_building_tool`（当前起始 1095 行，2 注册）：
  - 参与接口正文证据：building、editor、editing；实际调用 `dispatchPointer`, `revision`, `lastReceipt`, `edgeCurveGroup`, `compensate`, `hasPointerCapture`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`editor.building.curve_tool_routes_drag_as_one_authority_transaction`；`editor.building.curve_tool_cancel_and_miss_do_not_mutate`。
  - 含否定谓词的观察所在场景：`editor.building.curve_tool_routes_drag_as_one_authority_transaction`；`editor.building.curve_tool_cancel_and_miss_do_not_mutate`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor camera target` / `test_editor_camera_target`（当前起始 1299 行，27 注册）：
  - 参与接口正文证据：camera、daynight、decal、fluids、graphics、hd2d、image、particles、ui、weather、editing、particles_editing；实际调用 `code`, `makeSet`, `revision`, `applyDomainOperation`, `snapshotValue`, `loadSnapshot`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.camera.rigs_are_property_editable_reversible_and_persistent`；`editor.camera.timeline_has_stable_keys_reference_safety_and_scrub`；`editor.camera.invalid_snapshot_and_runtime_generation_are_atomic`。
  - 含否定谓词的观察所在场景：`editor.v2.graph_core_is_domain_neutral_and_revisioned`；`editor.v2.material_compile_failure_keeps_last_successful_preview`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor core v2` / `test_editor_core_v2`（当前起始 2103 行，17 注册）：
  - 参与接口正文证据：editor、map、scene、editing、asset；实际调用 `code`, `query`, `publish`, `append`, `applyDomainOperation`, `allowsCommand`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.v2.strong_ids_and_values`；`editor.v2.idsUseCommonUuidAdapterWithLegacyParse`；`editor.v2.host_profile_is_an_execution_boundary`。
  - 含否定谓词的观察所在场景：`editor.v2.strong_ids_and_values`；`editor.v2.idsUseCommonUuidAdapterWithLegacyParse`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor definition target` / `test_editor_definition_target`（当前起始 2639 行，8 注册）：
  - 参与接口正文证据：definitions、editor、orders、production、schema、social；实际调用 `applyDomainOperation`, `setJson`, `code`, `snapshotValue`, `loadSnapshot`, `validate`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.definition.schema_form_exposes_field_metadata_and_reversible_edits`；`editor.definition.validates_schema_and_cross_references`；`editor.definition.snapshot_load_is_atomic`。
  - 含否定谓词的观察所在场景：`editor.queue.inspector_copies_orders_without_borrowing_runtime_storage`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor dialogue graph` / `test_editor_dialogue_graph`（当前起始 2838 行，4 注册）：
  - 参与接口正文证据：dialogue；实际调用 `createNode`, `compile`, `build`, `makeNode`, `findPin`, `makeRouteNode`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.dialogue.graph_compiles_nodes_routes_and_localization_metadata`；`editor.dialogue.graph_rejects_bypassed_or_incomplete_routes`；`editor.dialogue.graph_builds_real_conversation_document`。
  - 含否定谓词的观察所在场景：`editor.dialogue.graph_rejects_bypassed_or_incomplete_routes`；`editor.dialogue.subtitle_preview_checks_safe_area_readability_and_lip_envelope`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor extension v2` / `test_editor_extension_v2`（当前起始 2981 行，12 注册）：
  - 参与接口正文证据：audio、common、editor、graphics、physics、editing、domain_gizmo_editing；实际调用 `authoringTarget`, `code`, `updateInteraction`, `tick`, `revision`, `makeCreateBinding`。
  - setup正文迹象：真实VM。
  - 任务观察样本：`editor.v2.game_extension_injects_same_tool_into_editor_and_game`；`editor.v2.extension_load_exception_rolls_back_partial_registration`；`editor.script.composes_heightmap_target_field_tool_and_transaction`。
  - 含否定谓词的观察所在场景：`editor.input_map.rejects_dangling_delete_and_captures_intentional_input`；`editor.material.studio_coalesces_live_gesture_into_one_transaction`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor fluid target` / `test_editor_fluid_target`（当前起始 3471 行，6 注册）：
  - 参与接口正文证据：fluids、fluids_editing；实际调用 `snapshotValue`, `applyDomainOperation`, `settings`, `makeSet`, `previewBudget`, `create`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.fluids.settings_are_reversible_persistent_and_budgeted`；`editor.fluids_factory_creates_all_schema_targets`；`editor.fluids_factory_loads_volume_snapshot_atomically`。
  - 含否定谓词的观察所在场景：`editor.fluids_factory_creates_all_schema_targets`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor gpuagents` / `test_editor_gpuagents`（当前起始 3609 行，4 注册）：
  - 参与接口正文证据：gpuagents、gpuagents_editor；实际调用 `applyDomainOperation`, `makeSet`, `settings`, `lifeFieldPixels`, `sampleHeight`, `sampleNormal`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`gpuagents.surface_capture_builds_height_and_normals`；`gpuagents.life_field_material_binding_packs_rgba`；`editor.gpuagents_document_schema_set_undo_and_runtime_apply`。
- `editor housegen target` / `test_editor_housegen_target`（当前起始 3715 行，3 注册）：
  - 参与接口正文证据：procgen、housegen；实际调用 `makeCreateComponent`, `applyDomainOperation`, `snapshotValue`, `publish`, `layout`, `toJson`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.housegen.kit_supports_incremental_authoring_properties_and_snapshot`；`editor.housegen.preview_is_deterministic_revision_safe_and_failure_atomic`；`editor.housegen.invalid_snapshot_does_not_partially_replace_document`。
  - 含否定谓词的观察所在场景：`editor.housegen.kit_supports_incremental_authoring_properties_and_snapshot`；`editor.housegen.preview_is_deterministic_revision_safe_and_failure_atomic`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor localization` / `test_editor_localization`（当前起始 3810 行，7 注册）：
  - 参与接口正文证据：i18n、profiler；实际调用 `code`, `ingest`, `setVariant`, `history`, `addRow`, `snapshotValue`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.localization_reports_completeness_placeholders_and_voice_metadata`；`editor.localization_snapshot_round_trip_is_atomic_and_deterministic`；`editor.localization_rejects_duplicate_keys_and_invalid_voice_status`。
- `editor map` / `test_editor_map`（当前起始 3935 行，1 注册）：
  - 参与接口正文证据：map、map_editing；实际调用 `getTile`, `revision`, `dirtyRegion`, `newLayer`, `writeInt`, `record`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.map.tileLayerTargetUsesLiveRevision`。
- `editor map document` / `test_editor_map_document`（当前起始 3965 行，5 注册）：
  - 参与接口正文证据：map、editing；实际调用 `applyDomainOperation`, `makeSetRoad`, `mapLayers`, `revision`, `code`, `mapRoads`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.map.road_mesh_publication_and_object_import_are_revision_safe`；`editor.map.layers_are_ordered_reversible_and_protect_nonempty_content`；`editor.map.road_spline_and_placements_support_create_update_undo`。
- `editor material batch target` / `test_editor_material_batch_target`（当前起始 4178 行，5 注册）：
  - 参与接口正文证据：graphics、editing；实际调用 `revision`, `makeSet`, `applyDomainOperation`, `render`, `targetId`, `publish`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.material.batch_reports_mixed_and_commits_one_atomic_publication`；`editor.material.batch_sink_failure_preserves_every_document`；`editor.material.preview_uses_immutable_isolated_revisioned_request`。
  - 含否定谓词的观察所在场景：`editor.material.batch_sink_failure_preserves_every_document`；`editor.material.preview_uses_immutable_isolated_revisioned_request`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [gameplay_composition_graphics_editor_material_target_tests.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/gameplay_composition_graphics_editor_material_target_tests.cpp:1)

- 当前规模：4420 行；102 literal注册。
- 实际任务：多职责聚合；按下列namespace分别审阅其调用和失败观察，不以graphics文件名替代任务判定。
- `editor material target` / `test_editor_material_target`（当前起始 3 行，24 注册）：
  - 参与接口正文证据：common、editor、graphics、physics、editing；实际调用 `makeSet`, `read`, `snapshotValue`, `code`, `applyDomainOperation`, `reloadSnapshot`。
  - setup正文迹象：真实VM。
  - 任务观察样本：`editor.material.schema_matches_core_material_authoring_surface`；`editor.material.vegetation_alpha_properties_are_reversible_and_persistent`；`editor.material.legacy_snapshots_migrate_with_vegetation_defaults`。
  - 含否定谓词的观察所在场景：`editor.material.translucency_mask_requires_orm_atomically`；`editor.material.vegetation_stages_snapshot_reaches_bound_runtime_material`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor mesh modifier editor` / `test_editor_mesh_modifier_editor`（当前起始 1110 行，4 注册）：
  - 参与接口正文证据：editing、procgen；实际调用 `makeNode`, `createNode`, `findPin`, `getActivePanel`, `activateTool`, `canConnect`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.meshModifierEditor.composesAllToolsAndTracksExternalRevisions`；`editor.meshModifierGraph.reflectsCompilesAndPreviewsFusedGraph`；`editor.meshModifierGraph.rejectsWrongDomainAndPinTypes`。
  - 含否定谓词的观察所在场景：`editor.meshModifierEditor.composesAllToolsAndTracksExternalRevisions`；`editor.meshModifierGraph.rejectsWrongDomainAndPinTypes`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor network telemetry` / `test_editor_network_telemetry`（当前起始 1242 行，2 注册）：
  - 参与接口正文证据：network；实际调用 `ingest`, `samples`, `code`, `diagnostics`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.network.telemetry_computes_rates_and_bounds_history`；`editor.network.telemetry_handles_reset_and_reports_health`。
- `editor offscreen preview` / `test_editor_offscreen_preview`（当前起始 1299 行，7 注册）：
  - 参与接口正文证据：graphics、image、spritestack、virtualgeometry、editing、model3d；实际调用 `makeSet`, `code`, `applyDomainOperation`, `revision`, `targetId`, `snapshotValue`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.preview.offscreen_artifacts_are_bounded_revision_safe_and_releasable`；`editor.spritestack.preset_is_reversible_and_atomic`；`editor.spritestack.bake_is_deterministic_and_reports_layer_artifacts`。
- `editor particle graph` / `test_editor_particle_graph`（当前起始 1576 行，3 注册）：
  - 参与接口正文证据：particles；实际调用 `makeNode`, `createNode`, `compile`, `contains`, `preview`, `findPin`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.particles.graph_compiles_real_emitter_modules`；`editor.particles.preview_is_revision_tagged_and_budget_bounded`；`editor.particles.graph_rejects_cycles_missing_modules_and_bad_properties`。
  - 含否定谓词的观察所在场景：`editor.particles.preview_is_revision_tagged_and_budget_bounded`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor pcg graph` / `test_editor_pcg_graph`（当前起始 1697 行，6 注册）：
  - 参与接口正文证据：procgen；实际调用 `makeNode`, `domain`, `snapshot`, `createNode`, `compile`, `findPin`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.pcgGraph.reflectsNodesAndCompilesPointGraphAsset`；`editor.pcgGraph.rejectsInvalidBlackboardParameterBindings`；`editor.pcgGraph.rejectsCyclesAndWrongPinTypes`。
  - 含否定谓词的观察所在场景：`editor.pcgGraph.rejectsInvalidBlackboardParameterBindings`；`editor.pcgGraph.rejectsCyclesAndWrongPinTypes`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor physics asset` / `test_editor_physics_asset`（当前起始 1915 行，6 注册）：
  - 参与接口正文证据：physics、editing；实际调用 `authoringTarget`, `resolve`, `shape`, `publish`, `code`, `raw`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`editor.physics.asset_resolver_validates_triangle_geometry_and_uri_lookup`；`editor.physics.asset_resolver_rejects_out_of_range_indices_and_builds_height_metadata`；`editor.physics.asset_resolver_validates_2d_polygon_and_chain_metadata`。
  - 含否定谓词的观察所在场景：`editor.physics.live_shape_publication_swaps_after_build_and_undoes`；`editor.physics.live_2d_polygon_publication_swaps_after_build_and_undoes`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor procgen script editor` / `test_editor_procgen_script_editor`（当前起始 2165 行，4 注册）：
  - 参与接口正文证据：editor、procgen、procgen_editing、procgen_editor；实际调用 `revision`, `pointCount`, `previewRevision`, `loadModule`, `getFloat`, `isDirty`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.procgen.script_editor_installs_workspace_and_commits_params`；`editor.procgen.script_editor_keeps_preview_on_failed_rebuild_and_stale_publish`；`editor.procgen.script_editor_reload_drops_unknown_and_fills_defaults`。
  - 含否定谓词的观察所在场景：`editor.procgen.script_editor_installs_workspace_and_commits_params`；`editor.procgen.script_editor_keeps_preview_on_failed_rebuild_and_stale_publish`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor property model` / `test_editor_property_model`（当前起始 2289 行，16 注册）：
  - 参与接口正文证据：editor、editing、property_access；实际调用 `position`, `read`, `schema`, `write`, `refresh`, `code`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.property_model_routes_runtime_writes_through_command_intent`；`editor.property_model_value_conversion_preserves_nested_data`；`editor.property_model_reads_one_atomically_refreshed_snapshot`。
  - 含否定谓词的观察所在场景：`editor.v2.property_model_uses_transaction_backend_for_commit_undo_and_redo`；`editor.v2.property_model_failed_commit_keeps_target_unchanged_and_is_discardable`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor runtime manifest v2` / `test_editor_runtime_manifest_v2`（当前起始 2941 行，12 注册）：
  - 参与接口正文证据：common、editor、graphics、map、weather、editing、lighting_editing、script；实际调用 `unregisterCommand`, `publish`, `commandService`, `run`, `compileSource`, `snapshot`。
  - setup正文迹象：真实VM、body/world backend、模型/文件导入。
  - 任务观察样本：`editor.v2.runtime_manifest_is_explicit_and_closes_asset_dependencies`；`editor.v2.runtime_manifest_rejects_developer_features`；`editor.v2.script_discovers_plans_and_executes_registered_command`。
  - 含否定谓词的观察所在场景：`editor.v2.runtime_manifest_is_explicit_and_closes_asset_dependencies`；`editor.session.adoptsCleanExternalChangesAndProtectsDirtyEdits`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor scene component payload` / `test_editor_scene_component_payload`（当前起始 3468 行，7 注册）：
  - 参与接口正文证据：audio、graphics、scene、editing；实际调用 `targetId`, `revision`, `code`, `makeSet`, `registerProvider`, `propertyProvider`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.scene.component_payload_registry_routes_schema_transactions_and_undo`；`editor.scene.component_payload_registry_rejects_mixed_and_stale_references`；`editor.scene.component_bindings_reuse_audio_physics_and_material_targets`。
  - 含否定谓词的观察所在场景：`editor.scene_import.presets_expand_and_custom_edits_are_reversible`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor scene runtime` / `test_editor_scene_runtime`（当前起始 3974 行，2 注册）：
  - 参与接口正文证据：scene；实际调用 `applyDomainOperation`, `findById`, `setTree`, `makeRename`, `getParentById`, `hasNode`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.scene.live_host_applies_incremental_hierarchy_and_trs_without_losing_links`；`editor.scene.live_host_staged_create_delete_and_conflict_are_atomic`。
  - 含否定谓词的观察所在场景：`editor.scene.live_host_staged_create_delete_and_conflict_are_atomic`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor scene target v2` / `test_editor_scene_target_v2`（当前起始 4070 行，6 注册）：
  - 参与接口正文证据：editing；实际调用 `readTransform`, `plan`, `code`, `sceneObject`, `undo`, `describe`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.v2.scene_transform_supports_atomic_multi_object_trs_edits`；`editor.v2.scene_document_and_runtime_targets_are_tool_isomorphic`；`editor.v2.scene_targets_expose_stable_capability_ids`。
- `editor skeleton overlay` / `test_editor_skeleton_overlay`（当前起始 4315 行，3 注册）：
  - 参与接口正文证据：animation；实际调用 `build`, `addBone`, `setLocalPosition`, `getBoneCount`, `applyBindPose`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.animation.skeleton_overlay_builds_lines_axes_constraints_retarget_and_mask`；`editor.animation.skeleton_overlay_rejects_cycles_missing_parents_and_budget`；`editor.animation.runtime_skeleton_adapter_uses_world_pose_and_stable_names`。
  - 含否定谓词的观察所在场景：`editor.animation.skeleton_overlay_builds_lines_axes_constraints_retarget_and_mask`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [gameplay_composition_graphics_editor_tests.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/gameplay_composition_graphics_editor_tests.cpp:1)

- 当前规模：5092 行；117 literal注册。
- 实际任务：多职责聚合；按下列namespace分别审阅其调用和失败观察，不以graphics文件名替代任务判定。
- `dialogue state integration` / `test_dialogue_state_integration`（当前起始 3 行，5 注册）：
  - 参与接口正文证据：dialogue、dnut_interpreter、statepatch、transaction、dnut、condition、test_support；实际调用 `set`, `pushBack`, `reasonCode`, `evaluate`, `passed`, `asString`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`dialogueState.conversationLocalsDoNotBecomeWorldState`；`dialogueState.usesSharedConditionExplanation`；`dialogueState.statePatchTransactionFailureDoesNotPartiallyModify`。
  - 含否定谓词的观察所在场景：`dialogueState.conversationLocalsDoNotBecomeWorldState`；`dialogueState.usesSharedConditionExplanation`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editing contracts` / `test_editing_contracts`（当前起始 346 行，10 注册）：
  - 参与接口正文证据：animation、avatar、building、camera、crowd、definitions、editing、editor、fluids、graphics、hd2d、i18n、map、network、particles、physics、procgen、profiler、scene、social、ui、virtualgeometry、voxel、material_editing、physics_editing、scene_editing、animation_editing、avatar_editing、building_editing、graphics_editing、ui_editing、fluids_editing、voxel_editing、detail；实际调用 `canonicalFormat`, `acquire`, `registerProvider`, `query`, `unload`, `extensionProviders`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editing.contracts.ids_values_and_editor_compatibility`；`editing.contracts.property_path_is_textual`；`editing.contracts.structured_result`。
  - 含否定谓词的观察所在场景：`editing.contracts.ids_values_and_editor_compatibility`；`editing.contracts.structured_result`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editing satellite contract suite` / `test_editing_satellite_contract_suite`（当前起始 684 行，2 注册）：
  - 参与接口正文证据：audio、physics、audio_editing、physics_editing、editing、editor；实际调用 `extensionProviders`, `revision`, `targetId`, `authoringTarget`, `applyDomainOperation`, `makeSet`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editing.satellite.audio_and_physics_share_atomic_publication_contract`；`editing.satellite.audio_and_physics_publish_open_factory_providers`。
- `editor` / `test_editor`（当前起始 813 行，35 注册）：
  - 参与接口正文证据：common、editor、map、procgen、procgen_editing、heightmap_target、level_editor、editing；实际调用 `toTable`, `toInt`, `getGid`, `addTool`, `diagnostics`, `dispatchPointer`。
  - setup正文迹象：真实VM、模型/文件导入。
  - 任务观察样本：`editor.session.routes_tool_lifecycle_and_capture`；`editor.session_tool_reentrancy_cannot_apply_stale_pointer_capture`；`editor.session_destructor_contains_throwing_tool_callbacks`。
  - 含否定谓词的观察所在场景：`editor.session.routes_tool_lifecycle_and_capture`；`editor.session_tool_reentrancy_cannot_apply_stale_pointer_capture`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor animation clip` / `test_editor_animation_clip`（当前起始 2244 行，12 注册）：
  - 参与接口正文证据：animation、common、animation_editor、editor、cap、editing；实际调用 `invoke`, `applyDomainOperation`, `compile`, `addBone`, `keyCount`, `build`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.animation.clip_timeline_supports_stable_reversible_tracks_events_and_masks`；`editor.animation.clip_snapshot_and_retarget_preview_are_non_destructive`；`editor.animation.clip_builds_real_runtime_asset_and_rejects_missing_bones`。
  - 含否定谓词的观察所在场景：`editor.animation.clip_editor_pointer_selects_bone_and_rejects_invalid_settings`；`editor.animation.clip_editor_keys_edits_and_deletes_joint_transform`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor archspace target` / `test_editor_archspace_target`（当前起始 2634 行，4 注册）：
  - 参与接口正文证据：archspace、editor、archspace_editor、cap；实际调用 `invoke`, `document`, `bakeMesh`, `applyDomainOperation`, `snapshotValue`, `nodeCount`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`archspace.document.bootstrap_room_opening_and_mesh_bake`；`archspace.editing.room_property_undo_and_snapshot_are_atomic`；`archspace.document.opening_cutout_changes_mesh_and_helpers_work`。
  - 含否定谓词的观察所在场景：`archspace.document.bootstrap_room_opening_and_mesh_bake`；`archspace.editing.room_property_undo_and_snapshot_are_atomic`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor archspace tools` / `test_editor_archspace_tools`（当前起始 2815 行，3 注册）：
  - 参与接口正文证据：archspace、archspace_editing、editing、editor；实际调用 `dispatchPointer`, `revision`, `document`, `addTool`, `activateTool`, `descriptor`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.archspace.wall_draw_tool_commits_one_authority_transaction`；`editor.archspace.opening_and_item_tools_commit_through_authority`；`editor.archspace.wall_draw_cancel_does_not_mutate`。
  - 含否定谓词的观察所在场景：`editor.archspace.wall_draw_tool_commits_one_authority_transaction`；`editor.archspace.wall_draw_cancel_does_not_mutate`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor audio editor` / `test_editor_audio_editor`（当前起始 3035 行，8 注册）：
  - 参与接口正文证据：audio、audio_editor、cap、editing、editor、audio_editing；实际调用 `invoke`, `read`, `applyDomainOperation`, `targetId`, `makeSet`, `inspect`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.audio.adapter_registers_commands_and_owns_headless_targets`；`editor.audio.property_command_keeps_bound_document_histories_independent`；`editor.audio.effects_are_typed_reversible_ordered_and_assignable`。
  - 含否定谓词的观察所在场景：`editor.audio.import_diagnostics_rejects_nonfinite_and_reports_silence`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor audio source script` / `test_editor_audio_source_script`（当前起始 3298 行，4 注册）：
  - 参与接口正文证据：editor、editing；实际调用 `toTable`, `bus`, `code`, `validate`, `snapshotValue`, `targetId`。
  - setup正文迹象：真实VM。
  - 任务观察样本：`editor.audio.source_editor_script_composes_workspace_and_transactions`；`editor.audio.source_schema_covers_playback_spatial_attenuation_and_routing`；`editor.audio.source_edits_are_reversible_and_cross_validated`。
  - 含否定谓词的观察所在场景：`editor.audio.mixer_bus_hierarchy_is_cycle_safe_and_reversible`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor audio transport` / `test_editor_audio_transport`（当前起始 3478 行，3 注册）：
  - 参与接口正文证据：audio、editing；实际调用 `bind`, `code`, `setLoop`, `play`, `update`, `generate`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.audio.transport_wraps_custom_loop_and_rejects_stale_revision`；`editor.audio.transport_validates_seek_loop_and_rebinding`；`editor.audio.waveform_builds_bounded_envelopes_and_loop_diagnostics`。
  - 含否定谓词的观察所在场景：`editor.audio.transport_wraps_custom_loop_and_rejects_stale_revision`；`editor.audio.waveform_builds_bounded_envelopes_and_loop_diagnostics`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor automation v2` / `test_editor_automation_v2`（当前起始 3577 行，12 注册）：
  - 参与接口正文证据：common、graphics、scene、editing、cap、scene_editor、material_editor；实际调用 `invoke`, `meshRenderer`, `applyDomainOperation`, `readTransform`, `findById`, `capture`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.v2.automation_capability_discovers_executes_plans_and_cancels`；`editor.automation.editing_commands_keep_level_histories_independent`；`editor.v2.automation_edits_material_through_the_same_transaction_path`。
  - 含否定谓词的观察所在场景：`editor.prefab_nested_dependencies_detect_cycles_and_refresh_revision_pins`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor avatar document editor` / `test_editor_avatar_document_editor`（当前起始 4083 行，2 注册）：
  - 参与接口正文证据：avatar、avatar_editor、editor；实际调用 `getPanelContext`, `previewRevision`, `revision`, `layerCount`, `layerZ`, `previewY`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.avatar.document_editor_installs_workspace_and_reverses_layer_z`；`editor.avatar.document_editor_rejects_referenced_parameter_delete`。
  - 含否定谓词的观察所在场景：`editor.avatar.document_editor_rejects_referenced_parameter_delete`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor avatar editor` / `test_editor_avatar_editor`（当前起始 4139 行，2 注册）：
  - 参与接口正文证据：avatar、avatar_editing、editing、editor、avatar_editor、cap；实际调用 `invoke`, `layers`, `applyDomainOperation`, `makeCreateLayer`, `registerEditingTarget`, `unregisterEditingTarget`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.avatar.automation_edits_layers_through_the_same_transaction_path`；`editor.avatar.automation_owns_headless_targets_for_agent_lifecycle`。
- `editor avatar target` / `test_editor_avatar_target`（当前起始 4214 行，3 注册）：
  - 参与接口正文证据：avatar、graphics、editing；实际调用 `instance`, `applyDomainOperation`, `makeCreateParameter`, `code`, `makeCreateLayer`, `schema`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.avatar.layers_and_parameters_have_dynamic_reversible_inspectors`；`editor.avatar.expressions_protect_referenced_channels_and_snapshot_is_atomic`；`editor.avatar.runtime_publishes_metadata_and_preserves_generation_on_asset_failure`。
- `editor host package` / `test_editor_host_package`（当前起始 4327 行，5 注册）：
  - 参与接口正文证据：action、editing、editor、fluids、voxel、action_editor、cap、fluids_editor、voxel_editor；实际调用 `invoke`, `isMounted`, `newHost`, `workspace`, `undo`, `autoMount`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`editor.host.mounts_action_package_panels_and_exports_schemas`；`editor.host.reject_incomplete_package`；`editor.host.attach_filters_commands_and_skips_incomplete`。
  - 含否定谓词的观察所在场景：`editor.host.mounts_action_package_panels_and_exports_schemas`；`editor.host.reject_incomplete_package`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor script component payload` / `test_editor_script_component_payload`（当前起始 4496 行，7 注册）：
  - 参与接口正文证据：common、editor、scene、editing、scene_editor、cap；实际调用 `componentPayloads`, `code`, `makeSet`, `propertyProvider`, `operationTarget`, `revision`。
  - setup正文迹象：真实VM。
  - 任务观察样本：`editor.scene.script_component_payload_reads_declared_fields`；`editor.scene.script_component_write_is_undoable_and_visible_to_script`；`editor.scene.script_component_planner_refuses_invalid_edits`。
  - 含否定谓词的观察所在场景：`editor.scene.script_component_payload_reads_declared_fields`；`editor.scene.script_component_planner_refuses_invalid_edits`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [gameplay_composition_graphics_gameplay_action_tests.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/gameplay_composition_graphics_gameplay_action_tests.cpp:1)

- 当前规模：4574 行；117 literal注册。
- 实际任务：多职责聚合；按下列namespace分别审阅其调用和失败观察，不以graphics文件名替代任务判定。
- `editor spline path binder` / `test_editor_spline_path_binder`（当前起始 3 行，19 注册）：
  - 参与接口正文证据：editing、procgen；实际调用 `applyDomainOperation`, `points`, `makeSetPoint`, `makeSetSettings`, `revision`, `snapshotValue`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.splinePathBinder.refreshesAllBoundPointsAtomically`；`editor.splinePathBinder.staleSourcePreservesDocumentAndLinkForReload`；`editor.splinePathBinder.rejectsDestroyedPointAndWrongDocument`。
  - 含否定谓词的观察所在场景：`editor.splinePathBinder.staleSourcePreservesDocumentAndLinkForReload`；`editor.splinePathBinder.rejectsDestroyedPointAndWrongDocument`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor stylize mesh vfx` / `test_editor_stylize_mesh_vfx`（当前起始 579 行，7 注册）：
  - 参与接口正文证据：stylize；实际调用 `applyDomainOperation`, `passes`, `makeSet`, `snapshotValue`, `makeCreate`, `code`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.stylize_mesh_vfx edits undoes and persists canonical assets`；`editor.stylize_mesh_vfx rejects invalid edits atomically`；`editor.stylize_mesh_vfx preview publication preserves the prior generation`。
  - 含否定谓词的观察所在场景：`editor.stylize_mesh_vfx rejects invalid edits atomically`；`editor.stylize_mesh_vfx preview publication preserves the prior generation`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor surface fluid preview` / `test_editor_surface_fluid_preview`（当前起始 728 行，6 注册）：
  - 参与接口正文证据：fluids；实际调用 `build`, `makeSet`, `applyDomainOperation`, `settings`, `code`, `loadSnapshot`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.surface_fluid.preview_replays_forward_and_backward_scrub_deterministically`；`editor.surface_fluid.preview_rejects_stale_invalid_and_unbounded_requests`；`editor.surface_fluid_exposes_reversible_droplet_wetness_and_material_properties`。
  - 含否定谓词的观察所在场景：`editor.surface_fluid.preview_replays_forward_and_backward_scrub_deterministically`；`editor.surface_fluid_snapshot_load_is_atomic_and_material_warnings_are_structured`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor task diagnostic v2` / `test_editor_task_diagnostic_v2`（当前起始 872 行，13 注册）：
  - 参与接口正文证据：common、editor、graphics、editing；实际调用 `publish`, `validate`, `registerPanel`, `snapshot`, `toInt`, `setRequest`。
  - setup正文迹象：真实VM、body/world backend。
  - 任务观察样本：`editor.v2.task_queue_is_queryable_and_cancellable`；`editor.v2.material_compile_runs_on_background_task_service`；`editor.v2.validation_and_diagnostics_are_extension_owned`。
  - 含否定谓词的观察所在场景：`editor.v2.material_compile_runs_on_background_task_service`；`editor.v2.json_rejects_invalid_and_out_of_range_input`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor texture recipe target` / `test_editor_texture_recipe_target`（当前起始 1223 行，2 注册）：
  - 参与接口正文证据：procgen；实际调用 `makeSet`, `applyDomainOperation`, `targetId`, `read`, `code`, `snapshotValue`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.texture_recipe.reflects_schema_and_reverses_parameters`；`editor.texture_recipe_snapshot_is_atomic_and_preview_is_deterministic`。
- `editor transaction consumer` / `test_editor_transaction_consumer`（当前起始 1289 行，12 注册）：
  - 参与接口正文证据：editor、map、transaction、editing、level_editing；实际调用 `code`, `getGid`, `commit`, `revision`, `append`, `preflight`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.transactionConsumer.commitUndoRedoAndDryRun`；`editor.transactionConsumer.authorityPreflightRejectsWithoutCommit`；`editor.transactionConsumer.secondParticipantFailureCompensatesEditor`。
  - 含否定谓词的观察所在场景：`editor.transactionConsumer.authorityPreflightRejectsWithoutCommit`；`editor.transactionConsumer.secondParticipantFailureCompensatesEditor`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor ui document` / `test_editor_ui_document`（当前起始 1874 行，8 注册）：
  - 参与接口正文证据：editor、ui、editing；实际调用 `code`, `widget`, `applyDomainOperation`, `themes`, `toTable`, `makeCreate`。
  - setup正文迹象：真实VM。
  - 任务观察样本：`editor.ui.style_inspector_preview_pick_and_anchor_gizmo_are_revision_safe`；`editor.ui.document_hierarchy_operations_are_reversible_and_cycle_safe`；`editor.ui.multi_selection_layout_inspector_is_atomic_and_reports_mixed_values`。
- `editor ui skin preview` / `test_editor_ui_skin_preview`（当前起始 2232 行，2 注册）：
  - 参与接口正文证据：ui、editing；实际调用 `applyDomainOperation`, `makeCreate`, `widget`, `build`, `makeSetContent`, `contains`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.ui.skin_content_is_reversible_and_snapshot_v2_round_trips`；`editor.ui.skin_preview_resolves_assets_crops_cover_and_clips_to_parent`。
  - 含否定谓词的观察所在场景：`editor.ui.skin_preview_resolves_assets_crops_cover_and_clips_to_parent`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor voxel catalog` / `test_editor_voxel_catalog`（当前起始 2333 行，10 注册）：
  - 参与接口正文证据：common、editor、voxel、voxel_editing、voxel_editor、cap；实际调用 `invoke`, `makeCreate`, `voxelCount`, `makeCreateModel`, `findModel`, `applyDomainOperation`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.voxel.catalog_classifies_filled_and_partial_occupancy`；`editor.voxel.catalog_snapshot_roundtrip_preserves_occupancy`；`editor.voxel.catalog_join_sockets_match_opposite_faces`。
  - 含否定谓词的观察所在场景：`editor.voxel.catalog_empty_canvas_attaches_first_in_bounds_cell`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `editor workspace layout shell` / `test_editor_workspace_layout_shell`（当前起始 2573 行，3 注册）：
  - 参与接口正文证据：ui、editor、ui_editor；实际调用 `registerPanel`, `setTree`, `tree`, `withText`, `activatePanel`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`editor.ui.layout.widgets_compose_fixed_chrome`；`editor.ui.layout.workspace_shell_maps_regions_to_dock_areas`；`editor.ui.layout.workspace_shell_single_panel_skips_tabs`。
- `effects domain adapters` / `test_effects_domain_adapters`（当前起始 2741 行，9 注册）：
  - 参与接口正文证据：card、rts、vehicle、weapon、effects、settlement；实际调用 `target`, `apply`, `advance`, `resolve`, `setJson`, `snapshot`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`effects.domainAdapters.shareLifecycleAndPeriodicParity`；`effects.domainAdapters.failureIsAtomicAndRestoreMakesHandlesStale`；`effects.domainAdapters.keepDomainStrategies`。
  - 含否定谓词的观察所在场景：`effects.domainAdapters.failureIsAtomicAndRestoreMakesHandlesStale`；`effects.card.deathTransitionFiresOnlyOnTheAliveToDeadEdge`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `game save integration` / `test_game_save_integration`（当前起始 3168 行，1 注册）：
  - 参与接口正文证据：common、inventory、rpg、tactics；实际调用 `has_value`, `setVariable`, `addCell`, `newSide`, `add`, `position`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`gameSave.rpgAndTacticsRestoreTogether`。
- `gameplay action` / `test_gameplay_action`（当前起始 3266 行，12 注册）：
  - 参与接口正文证据：action、common、rpg、weapon、condition、resource、sensing；实际调用 `advance`, `phase`, `submit`, `status`, `has_value`, `code`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`gameplayAction.phasePipelineUsesInjectedSimulationTime`；`gameplayAction.conditionFailureOwnsFailedTransition`；`gameplayAction.targetFailureStopsBeforeActive`。
  - 含否定谓词的观察所在场景：`gameplayAction.conditionFailureOwnsFailedTransition`；`gameplayAction.targetFailureStopsBeforeActive`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `gameplay control rpg product` / `test_gameplay_control_rpg_product`（当前起始 3853 行，3 注册）：
  - 参与接口正文证据：common、inventory、rpg、cap；实际调用 `submitGameplay`, `getVariable`, `countItem`, `observeGameplay`, `gameplayDomain`, `code`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`gameplay.control.rpgProductRunsShopLootAndQuestThroughCanonicalTransactions`；`gameplay.control.rpgProductDetectsMutationOutsideTheAdapter`；`gameplay.control.rpgControlFactoryReturnsStructuredOwnership`。
  - 含否定谓词的观察所在场景：`gameplay.control.rpgProductRunsShopLootAndQuestThroughCanonicalTransactions`；`gameplay.control.rpgProductDetectsMutationOutsideTheAdapter`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `gameplay control weapon` / `test_gameplay_control_weapon`（当前起始 4080 行，3 注册）：
  - 参与接口正文证据：common、inventory、transaction、weapon、cap；实际调用 `format`, `countItem`, `addItem`, `has_value`, `gameplayDomain`, `availableGameplayActions`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`gameplay.control.weaponUsesCanonicalActionAndAtomicAmmoPayment`；`gameplay.control.routerDispatchesAmongTwoWeaponInstances`；`gameplay.control.weaponRejectsUnauthorizedWielderWithoutCharging`。
  - 含否定谓词的观察所在场景：`gameplay.control.weaponUsesCanonicalActionAndAtomicAmmoPayment`；`gameplay.control.routerDispatchesAmongTwoWeaponInstances`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `gpuagents` / `test_gpuagents`（当前起始 4303 行，7 注册）：
  - 参与接口正文证据：gpuagents；实际调用 `simulation`, `states`, `configure`, `initialize`, `step`, `aliveCount`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`gpuagents.obstacle_resolve_keeps_tangential`；`gpuagents.fish_separation_and_cohesion`；`gpuagents.life_trail_deposit_and_follow`。

#### [gameplay_composition_graphics_graphics_vegetation_tests.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/gameplay_composition_graphics_graphics_vegetation_tests.cpp:1)

- 当前规模：3787 行；108 literal注册。
- 实际任务：多职责聚合；按下列namespace分别审阅其调用和失败观察，不以graphics文件名替代任务判定。
- `graphics authoring projection` / `test_graphics_authoring_projection`（当前起始 3 行，2 注册）：
  - 参与接口正文证据：graphics、scene；实际调用 `transform`, `localBounds`, `computeBounds`, `markSubtreeDirtyById`, `findById`, `setTree`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`graphics.authoring.bounds_use_vertices_and_clear_after_invalid_upload`；`graphics.authoring.scene_link_preserves_reflection_shear_and_direct_setter_mode`。
  - 含否定谓词的观察所在场景：`graphics.authoring.bounds_use_vertices_and_clear_after_invalid_upload`；`graphics.authoring.scene_link_preserves_reflection_shear_and_direct_setter_mode`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `graphics primitive cache` / `test_graphics_primitive_cache`（当前起始 83 行，7 注册）：
  - 参与接口正文证据：common、graphics、image；实际调用 `commands`, `render`, `statistics`, `add`, `update`, `triangles`。
  - setup正文迹象：真实VM。
  - 任务观察样本：`GraphicsPrimitives.sceneCacheOnlyRebuildsUpdatedSlots`；`GraphicsPrimitives.sceneCacheReusesGeometryAcrossCameras`；`GraphicsPrimitives.sceneCacheInvalidationIsTransactional`。
  - 含否定谓词的观察所在场景：`GraphicsPrimitives.sceneCacheReusesGeometryAcrossCameras`；`GraphicsPrimitives.sceneCacheInvalidationIsTransactional`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `graphics raytracing` / `test_graphics_raytracing`（当前起始 432 行，3 注册）：
  - 参与接口正文证据：common、graphics、cap；实际调用 `caps`, `isAvailable`, `addTriangleMesh`, `status`, `code`, `rebuildScene`。
  - setup正文迹象：实际window/headless。
  - 任务观察样本：`graphics.raytracing.capabilityAbsentOrPresent`；`graphics.raytracing.probeAndUnsupportedOps`；`graphics.raytracing.moduleScriptFacade`。
  - 含否定谓词的观察所在场景：`graphics.raytracing.probeAndUnsupportedOps`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `graphics vegetation` / `test_graphics_vegetation`（当前起始 513 行，24 注册）：
  - 参与接口正文证据：graphics、image；实际调用 `replace`, `sample`, `revision`, `pbrSurface`, `initHeadless`, `adoptMotionHighlights`。
  - setup正文迹象：实际window/headless。
  - 任务观察样本：`graphics.vegetation.mesh_highlight_ownership_and_failure`；`graphics.vegetation.extras_gpu_atlas_tracks_revision_and_rejects_half_overflow`；`graphics.vegetation.gpu_field_binding_is_coherent_stale_safe_and_atomic`。
  - 含否定谓词的观察所在场景：`graphics.vegetation.extras_gpu_atlas_tracks_revision_and_rejects_half_overflow`；`graphics.vegetation.gpu_field_binding_is_coherent_stale_safe_and_atomic`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `graphics vegetation gizmo` / `test_graphics_vegetation_gizmo`（当前起始 1303 行，18 注册）：
  - 参与接口正文证据：asset、graphics、image、asset_graphics；实际调用 `replace`, `newTexture`, `setTangentFrame`, `uploadRgba8MipChain`, `revision`, `build`。
  - setup正文迹象：body/world backend、实际window/headless。
  - 任务观察样本：`graphics.vegetation.gizmo_preserves_rotated_shapes_channels_and_revision`；`graphics.vegetation.gizmo_rejects_stale_revision_and_budget_without_partial_output`；`graphics.vegetation.mask_orm_uses_green_ao_alpha_smoothness_and_retains_blue`。
  - 含否定谓词的观察所在场景：`graphics.vegetation.gizmo_preserves_rotated_shapes_channels_and_revision`；`graphics.vegetation.mask_orm_rejects_malformed_linear_inputs`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `kaykit combat assets` / `test_kaykit_combat_assets`（当前起始 1852 行，1 注册）：
  - 参与接口正文证据：action、animation、editor、filesystem、model3d；实际调用 `getDuration`, `getBoneCount`, `has_value`, `getMeshCount`, `newModelDataFromFile`, `validate`。
  - setup正文迹象：模型/文件导入。
  - 任务观察样本：`kaykitCombatAssets.importRigClipsAndDriveActionEditorPreview`。
  - 含否定谓词的观察所在场景：`kaykitCombatAssets.importRigClipsAndDriveActionEditorPreview`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `map tile editing` / `test_map_tile_editing`（当前起始 2092 行，2 注册）：
  - 参与接口正文证据：map、map_editing、editing；实际调用 `getTile`, `setVisible`, `writeInt`, `fill`, `setTile`, `readInt`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`map.editing.preservesFlipBitsWhenRestoringFieldValue`；`map.editing.terrainBrushPreservesManualNeighbors`。
- `npc ai navigation` / `test_npc_ai_navigation`（当前起始 2133 行，2 注册）：
  - 参与接口正文证据：npc_ai；实际调用 `tick`, `erase`, `registerTaskService`, `registerBehavior`, `createAgent`, `snapshot`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`npc_ai.navigationTaskCompletesAsynchronously`；`npc_ai.navigationTicketIsAbandonedWhenStateExits`。
- `particles attach extra` / `test_particles_attach_extra`（当前起始 2254 行，21 注册）：
  - 参与接口正文证据：animation、ik、particles、model3d、filesystem、test；实际调用 `newEmitter`, `detach`, `getCount`, `getX`, `initStraightPose`, `sim`。
  - setup正文迹象：模型/文件导入。
  - 任务观察样本：`particles.attach.resetClearsParticlesKeepsAttach`；`particles.attach.moveToOverwrittenBySync`；`particles.attach.applyConfigPreservesAttach`。
  - 含否定谓词的观察所在场景：`particles.attach.layerAndVisibleIndependent`；`particles.attach.cesiumManBoneByNameIfPresent`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `particles attach more` / `test_particles_attach_more`（当前起始 2843 行，20 注册）：
  - 参与接口正文证据：model3d、filesystem、test；实际调用 `getX`, `detach`, `newEmitter`, `sim`, `getCount`, `getY`。
  - setup正文迹象：模型/文件导入。
  - 任务观察样本：`particles.attach.emitterLifeExpiresWhileAttached`；`particles.attach.bufferCapUnderRateOnBone`；`particles.attach.rebindDifferentBone`。
  - 含否定谓词的观察所在场景：`particles.attach.emitterLifeExpiresWhileAttached`；`particles.skin.clearRestoresBoneOriginSpawn`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `particles attach skin` / `test_particles_attach_skin`（当前起始 3415 行，8 注册）：
  - 参与接口正文证据：model3d、filesystem、test；实际调用 `computeWorld`, `newEmitter`, `getCount`, `addBone`, `getY`, `detach`。
  - setup正文迹象：body/world backend、模型/文件导入。
  - 任务观察样本：`particles.attach.boneFollowsPose`；`particles.attach.byNameAndOffset`；`particles.attach.followRotation`。
  - 含否定谓词的观察所在场景：`particles.attach.boneFollowsPose`；`particles.attach.byNameAndOffset`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [gameplay_composition_graphics_procgen_core_boundary_tests.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/gameplay_composition_graphics_procgen_core_boundary_tests.cpp:1)

- 当前规模：4108 行；118 literal注册。
- 实际任务：多职责聚合；按下列namespace分别审阅其调用和失败观察，不以graphics文件名替代任务判定。
- `particles dynamic bones` / `test_particles_dynamic_bones`（当前起始 3 行，20 注册）：
  - 参与接口正文证据：animation、ik、particles；实际调用 `getX`, `newEmitter`, `getY`, `detach`, `getCount`, `createBone`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`particles.attach.spineBoneFollowsAndEmits`；`particles.attach.spineDynamicEmitAcrossAnim`；`particles.attach.spineFollowRotation`。
  - 含否定谓词的观察所在场景：`particles.attach.spineBoneFollowsAndEmits`；`particles.attach.nullAndOutOfRangeDisable`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `particles effect asset` / `test_particles_effect_asset`（当前起始 625 行，3 注册）：
  - 参与接口正文证据：action、common、filesystem、particles、scene、cap；实际调用 `getEmitterCount`, `prepare`, `present`, `mountRealDirectory`, `getEmitterByName`, `naturalDuration`。
  - setup正文迹象：模型/文件导入。
  - 任务观察样本：`particles.effectAsset.playbackLabAssetIsConsumable`；`particles.actionBlockProviderOwnsRealVfxEnterUpdateExit`；`particles.actionPreviewRetainsScrubsRebuildsAndCleansVfx`。
  - 含否定谓词的观察所在场景：`particles.actionPreviewRetainsScrubsRebuildsAndCleansVfx`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `pcg car camera` / `test_pcg_car_camera`（当前起始 849 行，15 注册）：
  - 参与接口正文证据：camera、graphics、image、procgen、scene、ui；实际调用 `meshAt`, `update`, `addVertex`, `configure`, `getIndexCount`, `appendLevel`。
  - setup正文迹象：真实VM、实际window/headless。
  - 任务观察样本：`camera.pcgCar.appliesExactDefaultsAndInput`；`camera.pcgCar.rejectsBadProfilesWithoutMutation`；`camera.pcgCar.rearLockUsesInjectedTargetYaw`。
  - 含否定谓词的观察所在场景：`camera.pcgCar.rejectsBadProfilesWithoutMutation`；`camera.pcgFree.captureMoveSprintScrollAndRelease`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `pcg photo mode apply` / `test_pcg_photo_mode_apply`（当前起始 1317 行，26 注册）：
  - 参与接口正文证据：common、daynight、graphics、os、procgen、ui、weather、cap；实际调用 `setFloat`, `executeRegistered`, `compile`, `setBool`, `applyPhotoModeField`, `getCommandCount`。
  - setup正文迹象：真实VM。
  - 任务观察样本：`ui.pcgPhotoModeApply.compilesStableTypedDomainCommands`；`ui.pcgPhotoModeApply.rollsBackInReverseAfterFailure`；`ui.pcgPhotoModeApply.isAtomicAndCapabilityAbsenceIsObservable`。
  - 含否定谓词的观察所在场景：`ui.pcgPhotoModeApply.rollsBackInReverseAfterFailure`；`ui.pcgPhotoModeApply.isAtomicAndCapabilityAbsenceIsObservable`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `physics accelerator domains` / `test_physics_accelerator_domains`（当前起始 2208 行，10 注册）：
  - 参与接口正文证据：physics、cap；实际调用 `diagnostics`, `message`, `error`, `observation`, `details`, `step`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`physics.accelerator.satellitesReportAbsentProviderPerDomain`；`physics.accelerator.satellitesSelectPresentProvider`；`physics.accelerator.satellitesPreserveCreateFailure`。
  - 含否定谓词的观察所在场景：`physics.accelerator.satellitesSelectPresentProvider`；`physics.accelerator.satellitesStepFailureDoesNotAdvanceObservation`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `physics destruction graphics` / `test_physics_destruction_graphics`（当前起始 2494 行，3 注册）：
  - 参与接口正文证据：physics、window、graphics、physics_editing、editing；实际调用 `boneState`, `draw`, `newWorld3D`, `begin3DFrame`, `lastActiveDrawCount`, `lastSleepBatchCount`。
  - setup正文迹象：实际window/headless。
  - 任务观察样本：`physics_destruction_editing.fractureRecipeSchemaMirrorsDefaults`；`physics_destruction_graphics.drawsActiveAndBatchesSleepingBones`；`physics_destruction_graphics.staleWorldRejectsDraw`。
  - 含否定谓词的观察所在场景：`physics_destruction_graphics.staleWorldRejectsDraw`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `physics editor boot` / `test_physics_editor_boot`（当前起始 2635 行，1 注册）：
  - 参与接口正文证据：common、editor、physics、physics_editing；实际调用 `extensionProviders`, `acquire`, `query`, `createCollider`, `targetId`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`physics.editor.bootPublishesEditingFactory`。
- `physics simulation backend satellites` / `test_physics_simulation_backend_satellites`（当前起始 2662 行，5 注册）：
  - 参与接口正文证据：fluids、physics；实际调用 `observation`, `step`, `isZero`, `nanoseconds`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`physics.backend.cpuCloth2DConforms`；`physics.backend.cpuCloth3DConforms`；`physics.backend.cpuRope3DConforms`。
- `pixelworld editor` / `test_pixelworld_editor`（当前起始 2741 行，5 注册）：
  - 参与接口正文证据：pixelworld、ui；实际调用 `expect`, `draft`, `has_value`, `definition`, `fingerprint`, `replaceMaterial`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`pixelworld_editor_material_draft_is_transactional_and_codec_canonical`；`pixelworld_editor_reaction_and_phase_operations_validate_whole_catalog`；`pixelworld_editor_widget_tree_exposes_visual_material_and_rule_controls`。
  - 含否定谓词的观察所在场景：`pixelworld_editor_material_draft_is_transactional_and_codec_canonical`；`pixelworld_editor_reaction_and_phase_operations_validate_whole_catalog`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `procgen artifact publish` / `test_procgen_artifact_publish`（当前起始 2879 行，7 注册）：
  - 参与接口正文证据：graphics、procgen、transaction、artifact、cap；实际调用 `publish`, `partCount`, `setInt`, `execute`, `has_value`, `code`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`procgen.artifactPublish.missingCapabilityIsExplicitAndAtomic`；`procgen.artifactPublish.capabilitiesCommitHexAndCastle`；`procgen.artifactPublish.adapterFailureRollsBackWithoutStoreState`。
  - 含否定谓词的观察所在场景：`procgen.artifactPublish.missingCapabilityIsExplicitAndAtomic`；`procgen.artifactPublish.adapterFailureRollsBackWithoutStoreState`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `procgen core boundary` / `test_procgen_core_boundary`（当前起始 3158 行，23 注册）：
  - 参与接口正文证据：common、map、physics、procgen、scene、cap、graphics、artifact；实际调用 `setInt`, `child`, `setString`, `setSeed`, `setSize`, `addVertex`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`procgen.core.cpuSurfaceIsBackendNeutral`；`procgen.params.typedStorageConversionsAndCanonicalEncoding`；`procgen.params.dimensionsHaveDedicatedSemantics`。
  - 含否定谓词的观察所在场景：`procgen.dynamicMeshUvPaint.moduleHandleDetectsRelease`；`procgen.e2e.providerFailureLeavesNoHalfState`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [gameplay_composition_graphics_procgen_road_editing_tests.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/gameplay_composition_graphics_procgen_road_editing_tests.cpp:1)

- 当前规模：2475 行；45 literal注册。
- 实际任务：多职责聚合；按下列namespace分别审阅其调用和失败观察，不以graphics文件名替代任务判定。
- `procgen simulation` / `test_procgen_simulation`（当前起始 3 行，5 注册）：
  - 参与接口正文证据：graphics、map、procgen、window、script；实际调用 `isBound`, `view`, `getCell`, `getLength`, `getIndexCount`, `positions`。
  - setup正文迹象：实际window/headless。
  - 任务观察样本：`procgen.simulation.dungeonDelve`；`procgen.simulation.crystalCave`；`procgen.simulation.caveExpedition`。
- `resource account` / `test_resource_account`（当前起始 933 行，21 注册）：
  - 参与接口正文证据：attributes、card、common、economy、inventory、orders、production、rts、transaction、weapon、resource、container、action、condition；实际调用 `code`, `credit`, `getBase`, `rollback`, `reserve`, `commit`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`resource.account.attributeSetMatchesCommonDebitCreditReservationContract`；`resource.account.economyLedgerMatchesCommonDebitCreditReservationContract`；`resource.account.multiResourceDebitFailureIsAtomicForBothAdapters`。
  - 含否定谓词的观察所在场景：`resource.account.multiResourceDebitFailureIsAtomicForBothAdapters`；`resource.account.rollbackReleasesReservationWithoutChangingBalance`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `rpg crafting` / `test_rpg_crafting`（当前起始 1784 行，3 注册）：
  - 参与接口正文证据：inventory、rpg；实际调用 `countItem`, `setId`, `addItem`, `expect`, `taskCount`, `advance`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`rpg.craftingAlchemyPaysQueuesRetriesAndSettlesOnce`；`rpg.craftingRejectsInsufficientIngredientsWithoutQueueMutation`；`rpg.craftingRejectsAggregateOutputOverflowBeforePayment`。
  - 含否定谓词的观察所在场景：`rpg.craftingAlchemyPaysQueuesRetriesAndSettlesOnce`；`rpg.craftingRejectsInsufficientIngredientsWithoutQueueMutation`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `rpg equipment` / `test_rpg_equipment`（当前起始 1918 行，3 注册）：
  - 参与接口正文证据：inventory、rpg；实际调用 `syncEquipModifiers`, `getFinalAttribute`, `stackAt`, `countItem`, `clearAllItemStats`, `setId`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`rpg.equipment.statSyncAppliesAndRemoves`；`rpg.loot.rollGuaranteedAndSeeded`；`rpg.loot.rollDirectWithRng`。
- `rpg quest reward` / `test_rpg_quest_reward`（当前起始 2037 行，3 注册）：
  - 参与接口正文证据：rpg、inventory；实际调用 `getState`, `countItem`, `getVariable`, `setVariable`, `activate`, `notify`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`rpg.questReward.commitsAllOwnersBeforePublishingInventoryEvents`；`rpg.questReward.capacityFailureLeavesEveryOwnerAndObserverUntouched`；`inventory.addBatch.rejectsStalePreparedStateWithoutPublishing`。
  - 含否定谓词的观察所在场景：`rpg.questReward.capacityFailureLeavesEveryOwnerAndObserverUntouched`；`inventory.addBatch.rejectsStalePreparedStateWithoutPublishing`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `rpg save prepare` / `test_rpg_save_prepare`（当前起始 2159 行，1 注册）：
  - 参与接口正文证据：rpg、inventory；实际调用 `setVariable`, `getVariable`, `bind`, `setContentVersion`, `snapshotJson`, `prepareRestoreSnapshotJson`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`rpg.saveSession.preparedRestorePublishesOnlyAtCommit`。
- `rpg shop transaction` / `test_rpg_shop_transaction`（当前起始 2190 行，4 注册）：
  - 参与接口正文证据：rpg、inventory；实际调用 `countItem`, `getVariable`, `setVariable`, `addItem`, `setId`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`rpg.shop.buyAndSellPublishOnlyCommittedFinalState`；`rpg.shop.failuresLeaveCurrencyInventoryAndObserversUntouched`；`rpg.shop.catalogueStrictReplacementIsOrderedAndAtomic`。
  - 含否定谓词的观察所在场景：`rpg.shop.failuresLeaveCurrencyInventoryAndObserversUntouched`；`rpg.shop.catalogueStrictReplacementIsOrderedAndAtomic`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `rpg world interaction` / `test_rpg_world_interaction`（当前起始 2313 行，5 注册）：
  - 参与接口正文证据：rpg、inventory；实际调用 `isObjectConsumed`, `consumeObject`, `getState`, `getVariable`, `countItem`, `activate`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`rpg.worldInteraction.commitsWorldQuestStateAndInventoryBeforePublishing`；`rpg.worldInteraction.inventoryFailureLeavesEveryOwnerUntouched`；`rpg.worldInteraction.rejectsAlreadyConsumedObjectWithoutDuplicateRewards`。
  - 含否定谓词的观察所在场景：`rpg.worldInteraction.inventoryFailureLeavesEveryOwnerUntouched`；`rpg.worldInteraction.rejectsAlreadyConsumedObjectWithoutDuplicateRewards`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [gameplay_composition_graphics_rts_combat_tests.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/gameplay_composition_graphics_rts_combat_tests.cpp:1)

- 当前规模：3894 行；62 literal注册。
- 实际任务：多职责聚合；按下列namespace分别审阅其调用和失败观察，不以graphics文件名替代任务判定。
- `rts combat` / `test_rts_combat`（当前起始 3 行，25 注册）：
  - 参与接口正文证据：rts、weapon、sensing、combat、resource、map；实际调用 `release`, `motion`, `combat`, `durability`, `definition`, `tactics`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`rts.autoCombatUsesCanonicalSensingWeaponAndDamage`；`rts.shieldsRegenerateOnlyAfterDamageCooldown`；`rts.moraleAuraAcceleratesSuppressionRecovery`。
  - 含否定谓词的观察所在场景：`rts.autoCombatUsesCanonicalSensingWeaponAndDamage`；`rts.moraleAuraAcceleratesSuppressionRecovery`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `rts combat 3` / `test_rts_combat_3`（当前起始 1749 行，6 注册）：
  - 参与接口正文证据：rts、weapon、sensing、combat、definitions；实际调用 `release`, `definition`, `durability`, `combat`, `state`, `motion`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`rts.suppressionAndEscortFacadesRoundTripDedicatedTacticalState`；`rts.automaticTargetPoliciesAndWeaponPreferencesOverrideDistanceButExplicitAttackStillWins`；`rts.weaponTargetDomainAndTagsFilterAutomaticAndExplicitAttacks`。
  - 含否定谓词的观察所在场景：`rts.facadeHealsCombatRootsAndAppliesCanonicalStatusDefinitions`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `rts composition` / `test_rts_composition`（当前起始 2149 行，14 注册）：
  - 参与接口正文证据：rts、crowd、weapon、sensing、combat、action、map、definitions；实际调用 `release`, `motion`, `tactics`, `combat`, `faction`, `expect`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`rts.compositionRootsAndBatchViews`；`rts.baseViewIncludesSubclassButNotOtherRoot`；`rts.typedLinkDetectsStaleTarget`。
  - 含否定谓词的观察所在场景：`rts.typedLinkDetectsStaleTarget`；`rts.coordinatedVolleyHoldsAndReleasesGroupWhileExplicitAttackBypassesSchedule`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `rts economy` / `test_rts_economy`（当前起始 2836 行，15 注册）：
  - 参与接口正文证据：rts、resource、combat、weapon、map、definitions、production；实际调用 `release`, `supply`, `motion`, `worker`, `faction`, `orders`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`rts.autoAssignedWorkerGathersAndCreditsCanonicalAccount`；`rts.workerAssignmentFacadeMaintainsMiningLinksAndAutoAssignmentAtomically`；`rts.constructionRepairAndCaptureUseTypedBuildingTargets`。
  - 含否定谓词的观察所在场景：`rts.workerAssignmentFacadeMaintainsMiningLinksAndAutoAssignmentAtomically`；`rts.constructionRepairAndCaptureUseTypedBuildingTargets`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `rts economy 2` / `test_rts_economy_2`（当前起始 3750 行，2 注册）：
  - 参与接口正文证据：rts、weapon；实际调用 `supply`, `navigation`, `definition`, `orders`, `motion`, `addUnitAmmoSupply`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`rts.facadeMigratesMobileSupplyReserveAmmoAndAutoResupplyControls`；`rts.facadeExposesManualBuildersAndFactionWorkforceAutomation`。
  - 含否定谓词的观察所在场景：`rts.facadeMigratesMobileSupplyReserveAmmoAndAutoResupplyControls`；`rts.facadeExposesManualBuildersAndFactionWorkforceAutomation`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [gameplay_composition_graphics_rts_script_tests.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/gameplay_composition_graphics_rts_script_tests.cpp:1)

- 当前规模：4479 行；121 literal注册。
- 实际任务：多职责聚合；按下列namespace分别审阅其调用和失败观察，不以graphics文件名替代任务判定。
- `rts script` / `test_rts_script`（当前起始 3 行，26 注册）：
  - 参与接口正文证据：rts、settlement、combat；实际调用 `motion`, `has_value`, `stepScript`, `newFaction`, `newFactionUnit`, `scriptResource`。
  - setup正文迹象：真实VM、模型/文件导入。
  - 任务观察样本：`rts.sandboxScriptCompilesThroughEveScriptFrontend`；`rts.scriptFogQueriesResolveCreatedFactionIdentity`；`rts.scriptFacadeKeepsFactionIdentityAcrossFrameCallbacks`。
  - 含否定谓词的观察所在场景：`rts.legacySandboxContentMaterializesEveryArchetype`；`rts.scriptConstructionAtomicallyPaysBuildsAndCompletesCanonicalOrder`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `scene authoring` / `test_scene_authoring`（当前起始 1134 行，4 注册）：
  - 参与接口正文证据：scene、editing、scene_editor、scene_editing；实际调用 `saveJson`, `execute`, `undo`, `snapshot`, `redo`, `makeRestore`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`scene.authoring.document_atomic_spawn_presentation_roundtrip_history`；`scene.authoring.rejected_source_schema_camera_and_parent_preserve_state`；`scene.authoring.ground_and_gap_solve_world_geometry_under_reflected_parent`。
  - 含否定谓词的观察所在场景：`scene.authoring.rejected_source_schema_camera_and_parent_preserve_state`；`scene.authoring.ground_and_gap_solve_world_geometry_under_reflected_parent`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `scene editing commands` / `test_scene_editing_commands`（当前起始 1325 行，2 注册）：
  - 参与接口正文证据：editing、scene；实际调用 `applyDomainOperation`, `plan`, `snapshotValue`, `makeRestore`, `sceneObject`, `revision`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`scene.editing.commands_hierarchy_plans_are_reversible_and_backend_neutral`；`scene.editing.snapshot_restore_is_validated_atomic_and_reversible`。
  - 含否定谓词的观察所在场景：`scene.editing.commands_hierarchy_plans_are_reversible_and_backend_neutral`；`scene.editing.snapshot_restore_is_validated_atomic_and_reversible`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `scene editor live session` / `test_scene_editor_live_session`（当前起始 1418 行，3 注册）：
  - 参与接口正文证据：scene、scene_editor、scene_editing、editing；实际调用 `findById`, `execute`, `saveJson`, `setTree`, `undo`, `restoreJson`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`scene.editor.live_session_preserves_links_rejects_divergence_and_stale_host`；`scene.editor.live_restore_reorders_hierarchy_atomically_and_can_reopen`；`scene.editor.detached_root_keeps_world_transform`。
  - 含否定谓词的观察所在场景：`scene.editor.live_session_preserves_links_rejects_divergence_and_stale_host`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `scene editor session` / `test_scene_editor_session`（当前起始 1499 行，9 注册）：
  - 参与接口正文证据：scene_editor；实际调用 `saveJson`, `execute`, `beginPhysicsPlacement`, `code`, `commitPhysicsPlacement`, `undo`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`scene.editor.session_create_restore_and_undo_share_one_history`；`scene.editor.physics_placement_commits_selected_objects_as_one_undo_step`；`scene.editor.physics_placement_cancel_is_non_destructive`。
  - 含否定谓词的观察所在场景：`scene.editor.session_create_restore_and_undo_share_one_history`；`scene.editor.physics_placement_commits_selected_objects_as_one_undo_step`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `scene follow player` / `test_scene_follow_player`（当前起始 1758 行，20 注册）：
  - 参与接口正文证据：common、scene、script、editing；实际调用 `step`, `placementFrame`, `setHandlePosition`, `addBookmark`, `getBookmarkCount`, `code`。
  - setup正文迹象：真实VM。
  - 任务观察样本：`scene.followPlayer.matchesPcgOffsetsAndWaterThreshold`；`scene.followPlayer.scaleNoPlayerAndAtomicValidation`；`scene.locationProfile.oneShotLocationAndFailureAtomicity`。
  - 含否定谓词的观察所在场景：`scene.followPlayer.scaleNoPlayerAndAtomicValidation`；`scene.locationProfile.oneShotLocationAndFailureAtomicity`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `scene projected overlap` / `test_scene_projected_overlap`（当前起始 2237 行，4 注册）：
  - 参与接口正文证据：common、editing、scene、scene_editor、scene_editing、cap；实际调用 `execute`, `saveJson`, `snapshot`, `restoreJson`, `undo`, `geometryRevision`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`scene.projected_overlap.schema_v4_migration_roundtrip_delete_and_invalid_restore`；`scene.projected_overlap.target_planning_includes_partner_and_reference_region`；`scene.projected_overlap_uses_pair_fraction_independent_of_visibility_and_rejects_bad_evidence`。
  - 含否定谓词的观察所在场景：`scene.projected_overlap.schema_v4_migration_roundtrip_delete_and_invalid_restore`；`scene.projected_overlap_uses_pair_fraction_independent_of_visibility_and_rejects_bad_evidence`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `scene regions` / `test_scene_regions`（当前起始 2399 行，4 注册）：
  - 参与接口正文证据：scene_editor、scene_editing；实际调用 `execute`, `snapshot`, `saveJson`, `geometryRevision`, `undo`, `makeRestore`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`scene.regions.geometry_revision_excludes_trs_and_material_but_invalidates_recipe`；`scene.regions.roundtrip_delete_undo_and_v2_migration_preserve_intent`；`scene.regions.invalid_bounds_unknown_fields_and_conflicting_identity_are_atomic`。
  - 含否定谓词的观察所在场景：`scene.regions.geometry_revision_excludes_trs_and_material_but_invalidates_recipe`；`scene.regions.roundtrip_delete_undo_and_v2_migration_preserve_intent`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `scene relations` / `test_scene_relations`（当前起始 2561 行，11 注册）：
  - 参与接口正文证据：scene、scene_editing、cap；实际调用 `execute`, `saveJson`, `beginRelationPlacement`, `snapshot`, `commitPhysicsPlacement`, `checkRelationPlacement`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`scene.relations.roundtrip_delete_undo_and_v1_migration_are_atomic`；`scene.relations.reject_unknown_missing_and_duplicate_without_mutation`；`scene.relations.contact_evidence_distinguishes_support_penetration_and_unmeasurable`。
  - 含否定谓词的观察所在场景：`scene.relations.reject_unknown_missing_and_duplicate_without_mutation`；`scene.relations.contact_evidence_distinguishes_support_penetration_and_unmeasurable`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `schema production integration` / `test_schema_production_integration`（当前起始 2933 行，2 注册）：
  - 参与接口正文证据：definitions、policyregistry、schema；实际调用 `insert`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`schema.productionBoundary.validatesDefinitionsBeforeMutation`；`schema.productionBoundary.validatesPolicyMetadataBeforeMutation`。
  - 含否定谓词的观察所在场景：`schema.productionBoundary.validatesDefinitionsBeforeMutation`；`schema.productionBoundary.validatesPolicyMetadataBeforeMutation`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `settlement replay composition` / `test_settlement_replay_composition`（当前起始 2993 行，6 注册）：
  - 参与接口正文证据：card、combat、rpg、settlement、tactics、effects；实际调用 `effectAt`, `has_value`, `apply`, `configure`, `digest`, `settle`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`settlement.replay.rpgLifestealChainReplaysThroughDomainAdapter`；`settlement.replay.rtsDamageBatchRetainsAndVerifiesCanonicalAudits`；`settlement.replay.cardPeriodicRestoreRetainsAndVerifiesCanonicalAudit`。
  - 含否定谓词的观察所在场景：`settlement.replay.cardProjectionFailureDoesNotCommitLifecycleCandidate`；`settlement.replay.tacticsCommitFailureRollsBackDomainState`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `snapshot consumers` / `test_snapshot_consumers`（当前起始 3371 行，5 注册）：
  - 参与接口正文证据：authority、common、game_event、production、statepatch、transaction、test_support、definitions；实际调用 `snapshotJson`, `restoreSnapshot`, `error`, `code`, `snapshot`, `commit`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`snapshot.consumers.eventStreamEnvelopeRoundTripAndGeneratorSurvives`；`snapshot.consumers.transactionLedgerEnvelopeMigratesAtomically`；`snapshot.consumers.statePatchEnvelopeRejectsHashAndRevisionMismatch`。
  - 含否定谓词的观察所在场景：`snapshot.consumers.eventStreamEnvelopeRoundTripAndGeneratorSurvives`；`snapshot.consumers.transactionLedgerEnvelopeMigratesAtomically`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `snapshot envelope` / `test_snapshot_envelope`（当前起始 3662 行，4 注册）：
  - 参与接口正文证据：authority、definitions；实际调用 `restoreSnapshot`, `snapshotJson`, `code`, `error`, `snapshot`, `migrate`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`snapshot.envelope.canonicalHashRoundTripAndNoWallClock`；`snapshot.migration.rejectsUnknownNewVersionAndHashMismatch`；`snapshot.consumers.authorityAndDefinitionsRoundTripAndMetadata`。
  - 含否定谓词的观察所在场景：`snapshot.migration.rejectsUnknownNewVersionAndHashMismatch`；`snapshot.consumers.authorityAndDefinitionsRoundTripAndMetadata`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `softbody volume` / `test_softbody_volume`（当前起始 3865 行，12 注册）：
  - 参与接口正文证据：asset、graphics、physics、window、schema、physics_editing、editing；实际调用 `getParticleY`, `update`, `toValue`, `getParticleCount`, `setDeformationResistance`, `getVolumeRatio`。
  - setup正文迹象：body/world backend、实际window/headless、模型/文件导入。
  - 任务观察样本：`softbody.volume.creationRejectsInvalidLattice`；`softbody.volume.definitionRoundTripsAndCreatesRuntime`；`softbody.volume.definitionRejectsUnknownFieldsAndVersions`。
  - 含否定谓词的观察所在场景：`softbody.volume.creationRejectsInvalidLattice`；`softbody.volume.definitionRejectsUnknownFieldsAndVersions`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `standard combat abilities` / `test_standard_combat_abilities`（当前起始 4194 行，3 注册）：
  - 参与接口正文证据：action、combat；实际调用 `contains`, `registerDefinition`, `asString`, `validate`, `insert`, `format`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`standardCombatAbilities.providesTwentyUniqueValidatedArchetypes`；`standardCombatAbilities.registerGrantAndActivateThroughSharedController`；`standardCombatAbilities.roundTripEveryArchetypeThroughVersionedJsonAssets`。
- `stylize action attack vfx` / `test_stylize_action_attack_vfx`（当前起始 4261 行，6 注册）：
  - 参与接口正文证据：action、stylize、cap；实际调用 `runtime`, `activeCount`, `registerRecipeJson`, `dispatch`, `advanceHandlers`, `has_value`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`stylize.action.attack_vfx binding validates recipe xor uri`；`stylize.action.attack_vfx camera executor routes to optional sink`；`stylize.action.attack_vfx runtime plays with soft-skipped missing backends`。
  - 含否定谓词的观察所在场景：`stylize.action.attack_vfx binding validates recipe xor uri`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [gameplay_composition_graphics_stylize_attack_vfx_layers_tests.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/gameplay_composition_graphics_stylize_attack_vfx_layers_tests.cpp:1)

- 当前规模：1837 行；41 literal注册。
- 实际任务：多职责聚合；按下列namespace分别审阅其调用和失败观察，不以graphics文件名替代任务判定。
- `stylize attack vfx audio decal` / `test_stylize_attack_vfx_audio_decal`（当前起始 3 行，4 注册）：
  - 参与接口正文证据：common、decal、stylize、cap；实际调用 `activeCount`, `start`, `code`, `advance`, `stop`, `play`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`stylize.attack_vfx.layers audio prefab executors register`；`stylize.attack_vfx.layers decal solid executor`；`stylize.attack_vfx.layers stylizeAction play advance api`。
  - 含否定谓词的观察所在场景：`stylize.attack_vfx.layers audio prefab executors register`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `stylize attack vfx layers` / `test_stylize_attack_vfx_layers`（当前起始 176 行，8 注册）：
  - 参与接口正文证据：common、graphics、image、stylize、window、cap；实际调用 `hasUniform`, `newMeshShader`, `start`, `stop`, `setWindowSettings`, `newCanvas`。
  - setup正文迹象：实际window/headless。
  - 任务观察样本：`stylize.attack_vfx.layers mesh style and skill executors start`；`stylize.attack_vfx.layers trail and distortion executors`；`stylize.attack_vfx.layers water whip starts mesh trail camera distortion`。
- `tactics action` / `test_tactics_action`（当前起始 584 行，3 注册）：
  - 参与接口正文证据：action、common、tactics；实际调用 `advance`, `addCell`, `submit`, `position`, `turn`, `has_value`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`tactics.moveCommitsThroughSharedActionRuntime`；`tactics.moveActionPrepareFailureDoesNotMutateBoard`；`tactics.moveActionRejectsStaleExpectedRevision`。
  - 含否定谓词的观察所在场景：`tactics.moveActionPrepareFailureDoesNotMutateBoard`；`tactics.moveActionRejectsStaleExpectedRevision`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `tactics rpg integration` / `test_tactics_rpg_integration`（当前起始 753 行，1 注册）：
  - 参与接口正文证据：tactics、settlement；实际调用 `has_value`, `addCell`, `newSide`, `newUnit`, `setClampMax`, `magnitude`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`tactics.rpgSettlementOutcomeDefeatsUnitAndCompletesObjective`。
- `tactics settlement` / `test_tactics_settlement`（当前起始 867 行，3 注册）：
  - 参与接口正文证据：settlement、tactics；实际调用 `settle`, `settleAtomic`, `setClampMax`, `magnitude`, `configure`, `configureSettlementRules`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`tactics.settlement.committedAbilityUsesSharedRules`；`tactics.settlement.cellOnlyAbilityUsesExplicitStableTarget`；`tactics.settlement.areaAbilityUsesAtomicBatchAndRollsBackAllTargets`。
  - 含否定谓词的观察所在场景：`tactics.settlement.areaAbilityUsesAtomicBatchAndRollsBackAllTargets`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `time subscription consumers` / `test_time_subscription_consumers`（当前起始 1001 行，7 注册）：
  - 参与接口正文证据：animation、common、effects、particles、platform_event；实际调用 `advance`, `dispose`, `subscribePoll`, `setFrom`, `setTo`, `start`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`time.checkedConsumers.useSchedulerTickWithoutPrivateRate`；`time.checkedConsumers.effectsAndAnimationRecordTick`；`time.checkedConsumers.animationChildrenUseInjectedStep`。
  - 含否定谓词的观察所在场景：`time.checkedConsumers.useSchedulerTickWithoutPrivateRate`；`time.checkedConsumers.animationChildrenUseInjectedStep`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `ui motion bind` / `test_ui_motion_bind`（当前起始 1220 行，3 注册）：
  - 参与接口正文证据：ui、animation；实际调用 `meta`, `expect`, `has_value`, `ease`, `bind`, `advance`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`UI.motion.bindHostPosAndOpacity`；`UI.motion.staleHostCancelsOnError`；`UI.motion.bindHostSizeOverlayAndItemPos`。
  - 含否定谓词的观察所在场景：`UI.motion.bindHostPosAndOpacity`；`UI.motion.staleHostCancelsOnError`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `ui presentation` / `test_ui_presentation`（当前起始 1379 行，3 注册）：
  - 参与接口正文证据：property_access、ui；实际调用 `read`, `write`, `has_value`, `tree`, `isDirty`, `findById`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`ui.presentation.dynamic_model_validates_and_notifies`；`ui.presentation.generated_view_binds_two_way`；`ui.presentation.component_tracks_model_revision`。
  - 含否定谓词的观察所在场景：`ui.presentation.dynamic_model_validates_and_notifies`；`ui.presentation.generated_view_binds_two_way`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。
- `vehicle physics` / `test_vehicle_physics`（当前起始 1597 行，9 注册）：
  - 参与接口正文证据：physics、vehicle；实际调用 `update`, `registerVehiclesFromJson`, `newVehicle`, `findBody`, `getPhysicsSpace`, `newWorld`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`vehicle.physics2d.wheelDrivesAndSteers`；`vehicle.physics2d.trackRotatesInPlace`；`vehicle.physics3d.suspensionDrives`。
  - 含否定谓词的观察所在场景：`vehicle.physicsLink.detachInvalidatesBodyAndProjection`；`vehicle.physicsLink.externalBodyDestructionBecomesStale`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [procgen_road_authoring.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/procgen_road_authoring.cpp:1)

- 当前规模：137 行；2 literal注册。
- 实际任务：Road authoring document/编辑command/terrain provider→实际road projection/history/writeback；具体字段与故障阶段见下列case及API。。
- `本文件任务`（当前起始 1 行，2 注册）：
  - 参与接口正文证据：editing、procgen；实际调用 `edges`, `height`, `snapshotValue`, `revision`, `addNode`, `loadSnapshot`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`procgen.road.authoring.bridgeTunnelIntervalsKeepTerrainEvenNearItsSurface`；`procgen.road.authoring.intervalsSplitAndSnapshotsUndoWithoutDuplication`。
  - 含否定谓词的观察所在场景：`procgen.road.authoring.bridgeTunnelIntervalsKeepTerrainEvenNearItsSurface`；`procgen.road.authoring.intervalsSplitAndSnapshotsUndoWithoutDuplication`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [procgen_road_circular.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/procgen_road_circular.cpp:1)

- 当前规模：42 行；1 literal注册。
- 实际任务：Road authoring document/编辑command/terrain provider→实际road projection/history/writeback；具体字段与故障阶段见下列case及API。。
- `本文件任务`（当前起始 1 行，1 注册）：
  - 参与接口正文证据：procgen；实际调用 `commit`, `network`, `edges`, `beginEdit`, `prepare`, `undo`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`procgen.road.authoring.circularArcLimitsAndPresetUndo`。
  - 含否定谓词的观察所在场景：`procgen.road.authoring.circularArcLimitsAndPresetUndo`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [procgen_road_connections.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/procgen_road_connections.cpp:1)

- 当前规模：143 行；2 literal注册。
- 实际任务：Road authoring document/编辑command/terrain provider→实际road projection/history/writeback；具体字段与故障阶段见下列case及API。。
- `本文件任务`（当前起始 1 行，2 注册）：
  - 参与接口正文证据：editing、procgen；实际调用 `snapshotValue`, `addNode`, `revision`, `loadSnapshot`, `addEdge`, `nodes`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`procgen.road.authoring.customConnectionSnapshotUndoAndSplitRebindStableIds`；`procgen.road.authoring.closedBeamConnectionValidatesTheEntireSeamLoop`。
  - 含否定谓词的观察所在场景：`procgen.road.authoring.customConnectionSnapshotUndoAndSplitRebindStableIds`；`procgen.road.authoring.closedBeamConnectionValidatesTheEntireSeamLoop`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [procgen_road_controllers.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/procgen_road_controllers.cpp:1)

- 当前规模：36 行；1 literal注册。
- 实际任务：Road authoring document/编辑command/terrain provider→实际road projection/history/writeback；具体字段与故障阶段见下列case及API。。
- `本文件任务`（当前起始 1 行，1 注册）：
  - 参与接口正文证据：procgen；实际调用 `commit`, `network`, `edges`, `beginEdit`, `prepare`, `undo`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`procgen.road.authoring.outgoingControllerPresetSaveAndUndo`。

#### [procgen_road_extensions.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/procgen_road_extensions.cpp:1)

- 当前规模：544 行；7 literal注册。
- 实际任务：Road authoring document/编辑command/terrain provider→实际road projection/history/writeback；具体字段与故障阶段见下列case及API。。
- `本文件任务`（当前起始 1 行，7 注册）：
  - 参与接口正文证据：editing、procgen；实际调用 `snapshotValue`, `height`, `addNode`, `capture`, `addEdge`, `getPositionX`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`procgen.road.authoring.prefabDeformsSourceTrianglesNormalsUvsAndPersistsCuts`；`procgen.road.authoring.multiArmFacilityRoutesSaveUndoAndMigrateSplitIdentities`；`procgen.road.authoring.prefabStartMiddleEndRepeatsWeldsAndReversesWithoutLosingAssets`。
  - 含否定谓词的观察所在场景：`procgen.road.authoring.prefabDeformsSourceTrianglesNormalsUvsAndPersistsCuts`；`procgen.road.authoring.multiArmFacilityRoutesSaveUndoAndMigrateSplitIdentities`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [procgen_road_full_roll.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/procgen_road_full_roll.cpp:1)

- 当前规模：85 行；1 literal注册。
- 实际任务：Road authoring document/编辑command/terrain provider→实际road projection/history/writeback；具体字段与故障阶段见下列case及API。。
- `本文件任务`（当前起始 1 行，1 注册）：
  - 参与接口正文证据：procgen；实际调用 `edges`, `setEdgeControlPoints`, `reverseEdge`, `evaluateResult`, `commit`, `network`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`procgen.road.authoring.fullRollPreservesReferenceRotationAndAtomicHistory`。
  - 含否定谓词的观察所在场景：`procgen.road.authoring.fullRollPreservesReferenceRotationAndAtomicHistory`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [procgen_road_junction_style.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/procgen_road_junction_style.cpp:1)

- 当前规模：95 行；1 literal注册。
- 实际任务：Road authoring document/编辑command/terrain provider→实际road projection/history/writeback；具体字段与故障阶段见下列case及API。。
- `本文件任务`（当前起始 1 行，1 注册）：
  - 参与接口正文证据：editing、procgen、procgen_editing；实际调用 `positions`, `indices`, `addNode`, `spans`, `status`, `describe`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`procgen.road.junctionStyle.saveRestoreAndUndoReproduceGeometry`。

#### [procgen_road_noop.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/procgen_road_noop.cpp:1)

- 当前规模：231 行；5 literal注册。
- 实际任务：Road authoring document/编辑command/terrain provider→实际road projection/history/writeback；具体字段与故障阶段见下列case及API。。
- `本文件任务`（当前起始 1 行，5 注册）：
  - 参与接口正文证据：procgen、editing；实际调用 `addNode`, `applyDomainOperation`, `revision`, `code`, `laneLinkCount`, `addEdge`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`procgen.road.editingNoOp.buildersReturnSuccessAndPreserveAuthor`；`procgen.road.editingNoOp.sameEndpointStillPlansMissingTurnsAndUndo`；`procgen.road.editingNoOp.commandAdaptersOmitNoChangeOperations`。
  - 含否定谓词的观察所在场景：`procgen.road.editingNoOp.buildersReturnSuccessAndPreserveAuthor`；`procgen.road.editing.connectAllTurnsIsAtomicAndUndoable`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [procgen_road_parameter_sampling.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/procgen_road_parameter_sampling.cpp:1)

- 当前规模：77 行；1 literal注册。
- 实际任务：Road authoring document/编辑command/terrain provider→实际road projection/history/writeback；具体字段与故障阶段见下列case及API。。
- `本文件任务`（当前起始 1 行，1 注册）：
  - 参与接口正文证据：procgen；实际调用 `edges`, `addNode`, `commit`, `network`, `addEdge`, `erase`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`procgen.road.authoring.parameterScanPolicySaveUndoAndLegacyMigration`。
  - 含否定谓词的观察所在场景：`procgen.road.authoring.parameterScanPolicySaveUndoAndLegacyMigration`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [procgen_road_position_split.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/procgen_road_position_split.cpp:1)

- 当前规模：94 行；2 literal注册。
- 实际任务：Road authoring document/编辑command/terrain provider→实际road projection/history/writeback；具体字段与故障阶段见下列case及API。。
- `本文件任务`（当前起始 1 行，2 注册）：
  - 参与接口正文证据：procgen；实际调用 `applyDomainOperation`, `addNode`, `nodeCount`, `edgeCount`, `nodeResult`, `edgeResult`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`procgen.road.editing.positionSplitUndoRedoKeepsProjectedPointAndIds`；`procgen.road.editing.connectNodeAtPositionSnapsToVisibleCurve`。
  - 含否定谓词的观察所在场景：`procgen.road.editing.connectNodeAtPositionSnapsToVisibleCurve`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [procgen_road_presets.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/procgen_road_presets.cpp:1)

- 当前规模：370 行；7 literal注册。
- 实际任务：Road authoring document/编辑command/terrain provider→实际road projection/history/writeback；具体字段与故障阶段见下列case及API。。
- `本文件任务`（当前起始 1 行，7 注册）：
  - 参与接口正文证据：procgen；实际调用 `network`, `edges`, `beginEdit`, `prepare`, `commit`, `capture`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`procgen.road.authoring.presetV1RollDefaultsMigrateWithoutChangingSavedBehavior`；`procgen.road.authoring.sharedPresetsPropagateWithExplicitOverridesAndBake`；`procgen.road.authoring.presetCandidatesRejectCyclesStaleOwnersAndLateFailuresAtomically`。
  - 含否定谓词的观察所在场景：`procgen.road.authoring.presetCandidatesRejectCyclesStaleOwnersAndLateFailuresAtomically`；`procgen.road.authoring.presetExplicitFalseAndEmptyArraysRoundTripAndUnbindingRestoresSource`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [procgen_road_profile_editing.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/procgen_road_profile_editing.cpp:1)

- 当前规模：1340 行；26 literal注册。
- 实际任务：Road authoring document/编辑command/terrain provider→实际road projection/history/writeback；具体字段与故障阶段见下列case及API。。
- `本文件任务` / `test_procgen_road_editing`（当前起始 1 行，26 注册）：
  - 参与接口正文证据：editing、procgen；实际调用 `addNode`, `applyDomainOperation`, `edgeResult`, `edges`, `validate`, `laneLinkCount`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`procgen.road.editing.pointProfilesRoundTripMigrateAndRejectAtomically`；`procgen.road.editing.pointProfilesUndoRestoresExactAuthorValues`；`procgen.road.editing.nodeMoveReanchorsIncidentEdges`。
  - 含否定谓词的观察所在场景：`procgen.road.editing.pointProfilesRoundTripMigrateAndRejectAtomically`；`procgen.road.editing.edgeControlPointsCommitAtomically`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [procgen_road_recipe_snapshot.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/procgen_road_recipe_snapshot.cpp:1)

- 当前规模：55 行；1 literal注册。
- 实际任务：Road authoring document/编辑command/terrain provider→实际road projection/history/writeback；具体字段与故障阶段见下列case及API。。
- `本文件任务`（当前起始 1 行，1 注册）：
  - 参与接口正文证据：editing、procgen、procgen_editing；实际调用 `edges`, `setInt`, `canonicalString`, `positions`, `indices`, `setEdgeStyle`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`procgen.road.authoring.recipeSnapshotConsumesEditorAuthorDataAndLOD`。

#### [procgen_road_reference.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/procgen_road_reference.cpp:1)

- 当前规模：331 行；6 literal注册。
- 实际任务：Road authoring document/编辑command/terrain provider→实际road projection/history/writeback；具体字段与故障阶段见下列case及API。。
- `本文件任务`（当前起始 1 行，6 注册）：
  - 参与接口正文证据：editing、procgen、procgen_editing；实际调用 `edges`, `height`, `snapshotValue`, `storage`, `loadSnapshot`, `copyTile`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`procgen.road.authoring.referenceWorldUpFramePreservesHorizontalSectionsAndMigrates`；`procgen.road.authoring.referenceCubicWidthIsExplicitAndLegacySnapshotsRetainLinear`；`procgen.road.network.nodeMoveRejectsDegenerateIncidentEdgeAtomically`。
  - 含否定谓词的观察所在场景：`procgen.road.authoring.referenceWorldUpFramePreservesHorizontalSectionsAndMigrates`；`procgen.road.authoring.referenceCubicWidthIsExplicitAndLegacySnapshotsRetainLinear`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [procgen_road_roll_pivot.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/procgen_road_roll_pivot.cpp:1)

- 当前规模：92 行；1 literal注册。
- 实际任务：Road authoring document/编辑command/terrain provider→实际road projection/history/writeback；具体字段与故障阶段见下列case及API。。
- `本文件任务`（当前起始 1 行，1 注册）：
  - 参与接口正文证据：procgen；实际调用 `edges`, `setEdgeControlPoints`, `network`, `reverseEdge`, `beginEdit`, `prepare`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`procgen.road.authoring.rollPivotRecipeSnapshotReverseSplitAndUndo`。
  - 含否定谓词的观察所在场景：`procgen.road.authoring.rollPivotRecipeSnapshotReverseSplitAndUndo`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [procgen_road_sampling.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/procgen_road_sampling.cpp:1)

- 当前规模：106 行；1 literal注册。
- 实际任务：Road authoring document/编辑command/terrain provider→实际road projection/history/writeback；具体字段与故障阶段见下列case及API。。
- `本文件任务`（当前起始 1 行，1 注册）：
  - 参与接口正文证据：procgen；实际调用 `edges`, `setEdgeStyle`, `network`, `addNode`, `commit`, `addEdge`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`procgen.road.authoring.samplingSettingsSaveUndoMigrateAndRejectBudgets`。
  - 含否定谓词的观察所在场景：`procgen.road.authoring.samplingSettingsSaveUndoMigrateAndRejectBudgets`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [procgen_road_sampling_defaults.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/procgen_road_sampling_defaults.cpp:1)

- 当前规模：75 行；2 literal注册。
- 实际任务：Road authoring document/编辑command/terrain provider→实际road projection/history/writeback；具体字段与故障阶段见下列case及API。。
- `本文件任务`（当前起始 1 行，2 注册）：
  - 参与接口正文证据：procgen；实际调用 `network`, `edges`, `commit`, `beginEdit`, `capture`, `prepareRestore`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`procgen.road.authoring.presetV2MigratesEveryHistoryRootWithoutChangingChildInheritance`；`procgen.road.authoring.presetV2ExplicitSamplingOverridesRetainPresence`。
  - 含否定谓词的观察所在场景：`procgen.road.authoring.presetV2MigratesEveryHistoryRootWithoutChangingChildInheritance`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [procgen_road_side_placement.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/procgen_road_side_placement.cpp:1)

- 当前规模：119 行；2 literal注册。
- 实际任务：Road authoring document/编辑command/terrain provider→实际road projection/history/writeback；具体字段与故障阶段见下列case及API。。
- `本文件任务`（当前起始 1 行，2 注册）：
  - 参与接口正文证据：common、procgen；实际调用 `edges`, `points`, `addNode`, `commit`, `network`, `addEdge`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`procgen.road.authoring.sidePlacementPresetHistoryMigrationAndAtomicFailure`；`procgen.road.authoring.sidePlacementRotatesTerrainFootprintBeforeWorldUpOffset`。
  - 含否定谓词的观察所在场景：`procgen.road.authoring.sidePlacementPresetHistoryMigrationAndAtomicFailure`；`procgen.road.authoring.sidePlacementRotatesTerrainFootprintBeforeWorldUpOffset`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [procgen_road_side_terrain.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/procgen_road_side_terrain.cpp:1)

- 当前规模：418 行；6 literal注册。
- 实际任务：Road authoring document/编辑command/terrain provider→实际road projection/history/writeback；具体字段与故障阶段见下列case及API。。
- `本文件任务`（当前起始 1 行，6 注册）：
  - 参与接口正文证据：common、procgen、cap；实际调用 `edges`, `setEdgeStyle`, `getStringAttribute`, `points`, `network`, `getCount`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`procgen.road.authoring.sideAlignmentProducesOwnedEulerPoseAndRejectsInvalidNormals`；`procgen.road.authoring.sideTerrainBoundsModesConsumeProviderAndStandardRecipe`；`procgen.road.authoring.sideAlignmentVerticalFramesAndSharedProjectionBudget`。
  - 含否定谓词的观察所在场景：`procgen.road.authoring.sideAlignmentProducesOwnedEulerPoseAndRejectsInvalidNormals`；`procgen.road.authoring.sideTerrainBoundsModesConsumeProviderAndStandardRecipe`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [procgen_road_spacing_portals.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/procgen_road_spacing_portals.cpp:1)

- 当前规模：495 行；7 literal注册。
- 实际任务：Road authoring document/编辑command/terrain provider→实际road projection/history/writeback；具体字段与故障阶段见下列case及API。。
- `本文件任务`（当前起始 1 行，7 注册）：
  - 参与接口正文证据：editing、procgen；实际调用 `snapshotValue`, `edges`, `addNode`, `height`, `data`, `addEdge`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`procgen.road.authoring.rigidDistributionMatchesMeasuredReferenceAndBudget`；`procgen.road.authoring.rigidDistributionHonorsExclusionsAndEmptySpans`；`procgen.road.authoring.rigidDistributionPersistsPresetUndoAndLegacyMigration`。
  - 含否定谓词的观察所在场景：`procgen.road.authoring.rigidDistributionMatchesMeasuredReferenceAndBudget`；`procgen.road.authoring.rigidDistributionPersistsPresetUndoAndLegacyMigration`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [procgen_road_strength.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/procgen_road_strength.cpp:1)

- 当前规模：39 行；1 literal注册。
- 实际任务：Road authoring document/编辑command/terrain provider→实际road projection/history/writeback；具体字段与故障阶段见下列case及API。。
- `本文件任务`（当前起始 1 行，1 注册）：
  - 参与接口正文证据：procgen；实际调用 `network`, `edges`, `commit`, `beginEdit`, `prepare`, `undo`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`procgen.road.authoring.splineStrengthPresetOverrideSaveAndUndo`。

#### [procgen_road_surface_path.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/procgen_road_surface_path.cpp:1)

- 当前规模：81 行；1 literal注册。
- 实际任务：Road authoring document/编辑command/terrain provider→实际road projection/history/writeback；具体字段与故障阶段见下列case及API。。
- `本文件任务`（当前起始 1 行，1 注册）：
  - 参与接口正文证据：procgen；实际调用 `edges`, `status`, `addNode`, `commit`, `network`, `addEdge`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`procgen.road.surfacePath.retainsPresetHistoryAndMigratesMissingPolicy`。
  - 含否定谓词的观察所在场景：`procgen.road.surfacePath.retainsPresetHistoryAndMigratesMissingPolicy`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [procgen_road_terrain_author_policy.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/procgen_road_terrain_author_policy.cpp:1)

- 当前规模：303 行；5 literal注册。
- 实际任务：Road authoring document/编辑command/terrain provider→实际road projection/history/writeback；具体字段与故障阶段见下列case及API。。
- `本文件任务`（当前起始 1 行，5 注册）：
  - 参与接口正文证据：procgen；实际调用 `data`, `edges`, `height`, `undo`, `apply`, `copyTile`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`procgen.road.terrain.authorPolicyNetworkSaveConsumesOneAtomicPlan`；`procgen.road.terrain.authorPolicyPresetInheritanceHistoryAndValidation`；`procgen.road.terrain.authorPolicyDisabledPlanPreservesHistory`。
  - 含否定谓词的观察所在场景：`procgen.road.terrain.authorPolicyNetworkSaveConsumesOneAtomicPlan`；`procgen.road.terrain.authorPolicyPresetInheritanceHistoryAndValidation`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [rts_terrain_combat.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/rts_terrain_combat.cpp:1)

- 当前规模：39 行；1 literal注册。
- 实际任务：RTS fire line→真实terrain occlusion adapter；地形编辑重新开放射线。
- `本文件任务`（当前起始 1 行，1 注册）：
  - 参与接口正文证据：rts；实际调用 `motion`, `has_value`, `newFaction`, `newFactionUnit`, `setScriptTerrainElevation`, `stepScript`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`rts.terrainRidgeStopsFireAndTerrainEditsReopenFireLine`。

#### [scene_procgen_rotation.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/gameplay-composition/scene_procgen_rotation.cpp:1)

- 当前规模：215 行；3 literal注册。
- 实际任务：ProcgenSceneSink→Scene/SceneHost owning snapshot/delta/rotation，replace失败不泄漏hosts。
- `本文件任务`（当前起始 1 行，3 注册）：
  - 参与接口正文证据：common、scene、cap；实际调用 `batchRevision`, `findById`, `replaceBatchesCoordinated`, `findHost`, `format`, `replaceBatch`。
  - setup正文迹象：本section未直接启动VM/window/worker；仍可能经上文helper/production adapter间接进入。
  - 任务观察样本：`Scene.procgenSink.deltaSnapshotBuilderPreservesIdentityAndRejectsMalformedOrder`；`Scene.procgenSink.coordinatesReplacementWithoutLeakingCandidateHosts`；`Scene.procgenSink.preservesDegreeRotationAcrossSnapshotsAndDeltas`。
  - 含否定谓词的观察所在场景：`Scene.procgenSink.deltaSnapshotBuilderPreservesIdentityAndRejectsMalformedOrder`；`Scene.procgenSink.coordinatesReplacementWithoutLeakingCandidateHosts`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

### map-navigation

- 当前没有物理 CPP；不把其它目录/unit 的历史功能误计为本目录审阅。

### ocean-voyage

#### [ocean_boat.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/ocean-voyage/ocean_boat.cpp:1)

- 当前规模：222 行；6 literal注册。
- 实际任务：Ocean spectrum/water/wake→physics world queries/forces/真实boat bodies，fixed step及双owner销毁。
- `本文件任务`（当前起始 1 行，6 注册）：
  - 参与接口正文证据：common、fluids、physics；实际调用 `apply`, `build`, `newBody`, `frameBuilder`, `update`, `newBoxShape`。
  - setup正文迹象：body/world backend。
  - 任务观察样本：`fluids.ocean.coreWorldQueriesSurviveSplit`；`fluids.ocean.boatFloatsAndPropelsRealRigidBody`；`fluids.ocean.boatRejectsRepeatedStepAndStaleOwners`。
  - 含否定谓词的观察所在场景：`fluids.ocean.coreWorldQueriesSurviveSplit`；`fluids.ocean.boatFloatsAndPropelsRealRigidBody`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [ocean_gpu.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/ocean-voyage/ocean_gpu.cpp:1)

- 当前规模：215 行；4 literal注册。
- 实际任务：CPU spectrum/sea transition→真实GPGPU驻留资源/readback、代际token与provider缺席；128/256规模。
- `本文件任务`（当前起始 1 行，4 注册）：
  - 参与接口正文证据：fluids、gpgpu、graphics、window；实际调用 `build`, `grids`, `frameBuilder`, `submit`, `close`, `band`。
  - setup正文迹象：实际window/headless。
  - 任务观察样本：`fluids.ocean.gpuSpectrumMatchesCpuAndRejectsStaleFrames`；`fluids.ocean.gpuSpectrumReportsUnavailableProvider`；`fluids.ocean.gpuSpectrumLargeBandParity`。
  - 含否定谓词的观察所在场景：`fluids.ocean.gpuSpectrumMatchesCpuAndRejectsStaleFrames`；`fluids.ocean.gpuSpectrumReportsUnavailableProvider`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [ocean_render.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/ocean-voyage/ocean_render.cpp:1)

- 当前规模：340 行；3 literal注册。
- 实际任务：Ocean CPU/GPU frames+wake/floating origin→真实renderer像素输出，以及VM GC渲染builder ownership。
- `本文件任务`（当前起始 1 行，3 注册）：
  - 参与接口正文证据：fluids、gpgpu、graphics、window；实际调用 `build`, `publish`, `frameBuilder`, `hullMaskBuilder`, `draw`, `close`。
  - setup正文迹象：真实VM、实际window/headless。
  - 任务观察样本：`fluids.ocean.rendererPublishesSharedSurfaceAndFoam`；`fluids.ocean.residentRenderingMatchesCpuWithWakeAndFloatingOrigin`；`fluids.ocean.scriptRenderBuildersOwnInputsAcrossGc`。
  - 含否定谓词的观察所在场景：`fluids.ocean.rendererPublishesSharedSurfaceAndFoam`；`fluids.ocean.residentRenderingMatchesCpuWithWakeAndFloatingOrigin`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

#### [ocean_script.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/ocean-voyage/ocean_script.cpp:1)

- 当前规模：134 行；2 literal注册。
- 实际任务：实际Ocean bindings/VM GC→spectrum/frame及boat/wake/physics共同fixed clock。
- `本文件任务`（当前起始 1 行，2 注册）：
  - 参与接口正文证据：fluids、physics；实际调用 `run`, `compileSource`, `addTable`, `addFunc`, `to`, `time`。
  - setup正文迹象：真实VM、body/world backend。
  - 任务观察样本：`fluids.ocean.scriptOwnsSpectrumAndFrameBuilders`；`fluids.ocean.scriptBoatWakeUsesOneFixedClock`。

#### [ocean_spray_render.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/ocean-voyage/ocean_spray_render.cpp:1)

- 当前规模：126 行；3 literal注册。
- 实际任务：Ocean spray CPU mesh→真实渲染、脚本GC和provider retirement；不共享缺provider状态。
- `本文件任务`（当前起始 1 行，3 注册）：
  - 参与接口正文证据：common、fluids、graphics、window；实际调用 `build`, `publish`, `draw`, `particles`, `setWindowSettings`, `frameBuilder`。
  - setup正文迹象：真实VM、实际window/headless。
  - 任务观察样本：`fluids.ocean.sprayBatchRendersAndRejectsInvalidPublication`；`fluids.ocean.sprayScriptBuildersSurviveGc`；`fluids.ocean.sprayRendererObservesProviderRetirement`。
  - 含否定谓词的观察所在场景：`fluids.ocean.sprayBatchRendersAndRejectsInvalidPublication`；`fluids.ocean.sprayRendererObservesProviderRetirement`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

### weather-lightning

#### [weather_lightning_runtime.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/weather-lightning/weather_lightning_runtime.cpp:1)

- 当前规模：49 行；1 literal注册。
- 实际任务：Lightning准备→真实Graphics/Renderable，pulse/endpoint拒绝、ECS销毁后trigger失败与析构。
- `本文件任务`（当前起始 1 行，1 注册）：
  - 参与接口正文证据：graphics、weather、window；实际调用 `amplitude`, `trigger`, `setEndpoints`, `update`, `setWindowSettings`, `setColor`。
  - setup正文迹象：实际window/headless。
  - 任务观察样本：`weather.lightning weapon endpoints pulse and stale entity`。
  - 含否定谓词的观察所在场景：`weather.lightning weapon endpoints pulse and stale entity`。这些可能是预期失效状态而非主动失败注入；相同Rejected/Failed也不等于同一故障阶段。

### sensing

#### [environment_query_tests.cpp](C:/Users/xiaofans/.codex/worktrees/b069/EVEngine/test/integration/sensing/environment_query_tests.cpp:1)

- 当前规模：25 行；1 literal注册。
- 实际任务：真实Squirrel ModuleManager→Sensing canonical executor，有效/无效位置查询和debug trace。
- `本文件任务`（当前起始 1 行，1 注册）：
  - 参与接口正文证据：common、sensing；实际调用 `run`, `compileSource`。
  - setup正文迹象：真实VM。
  - 任务观察样本：`sensing.environment.scriptPositionsUseCanonicalExecutor`。

## 审阅深度与局限

- 77/77当前物理CPP均读入正文、枚举literal注册并扫描直接API/fixture/namespace。1758是静态literal数，既不是活跃平台执行数，也不是本轮运行通过数。大聚合的namespace任务索引不把未展开的所有个体case声称为逐条人工等价证明。
- 人工逐body对比重点覆盖了本报告R1–R6及“必须保留”中的force/snapshot/sweep/scene parent/action transaction/ocean/provider/粒子asset路径；其余namespace通过实际调用和代表case建立边界索引，尚未做每一行跨文件调用图、故障注入或运行验证。
- 没有C++ AST/模板实例化与完整callgraph，直接include/API列表仅供追溯；合并CPP跨section可复用include，RtsCompositionFixtures等helper也会带入真实跨域owner。报告明确避免把头文件少/无图形调用当成可迁或可删证据。
- map-navigation目录当前无CPP；map adapter语义仍在rts_crowd_registry及其它既有组合中。未重新审阅scope外所有unit/cmake profile，只把已存在代表用于解释边界。
- 没有改旧audit、test_levels、CPP、构建规则或执行测试。本文件是候选审阅，不是额外的测试policy catalogue；下一轮任何减量必须先补每条业务/失败观察的去向和验证。
