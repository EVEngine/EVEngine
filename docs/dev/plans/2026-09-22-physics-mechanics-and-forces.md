# 物理机械约束、场效果、空气动力学与轮胎实施计划

状态：用户已批准实施；本清单记录尚未完成的工作，不以探索代码代表交付。
基线：开始实施前刷新 origin/dev。2D 后端为仓库固定版本 Box2D，3D 为 Box3D。

## 目标与边界

同时交付 2D/3D 的方向场、吸引/斥力场、浮力、介质阻尼、机翼升阻力、
摩擦和轮胎转动/接地模型，以及缺失的转轴、滑轮和齿轮机械约束。
默认游戏实时精度；不声称 CFD、真实翼型拟合、Pacejka 标定或齿面接触仿真。

已有 3D Revolute/Prismatic/Distance/Spherical/Wheel 关节及脚本、测试，直接复用。
2D Box2D 有 Revolute/Prismatic/Distance/Pulley/Gear/Wheel/Weld，但缺少引擎公共关节层。
3D Box3D 无专用齿轮/滑轮约束，需要在物理 owner 内设计耦合求解。

## 模块与权威状态

所有新增代码先放 physics 包内：mechanics（关节和耦合）、forces（场、气动和接触公式）。
不新增顶级模块或手写模块启动表；如形成可裁剪卫星，仅在 module_manifest 声明。
公开头不引入 graphics、vehicle 或 backend 实现头。高频路径直接使用类型化数据和已解析引用，
不逐步做 capability 字符串查找，不在锁内调用脚本。

- World/World3D 独占 backend 刚体与约束生命周期；脚本或 C++ owning wrapper 释放会移除约束。
- wrapper 不拥有 world/body。World 提供注册、失效和解析；跨帧身份采用带 generation 的 handle。
- Body 销毁前先移除相关约束；齿轮依赖的源关节销毁时先移除齿轮。world 先销毁时 wrapper 失效。
- 参数是调用者持有的强类型值；纯采样无隐藏状态；轮速仅由轮胎/车辆实例持有。
- 不引入 ECS System 或第二套 transform/velocity；Vehicle 通过现有 PhysicsLink 接入。

## 更新顺序与失败

固定 SimulationStep -> 读取 solver 状态和环境 -> 接触/浸没查询 ->
计算所有场、气动、轮胎力 -> 验证完整 force batch -> 一次提交 ->
solver 固定步推进 -> 提交轮速/观察结果。

纯采样和 batch 准备失败不改变任何对象。提交前检查 world/body 身份、动态类型和所有数值。
关节创建返回 Result 与 owning wrapper；类型错误、跨 world、锁定 world、无效轴、
非有限数及不支持的参数返回结构化失败，不能触发 backend assertion。
跨 solver 失败恢复需与既有 backend step 契约一起验证；不能重试时再次累计同一批外力。

## 单位与成本

计算使用 SI（m、s、kg、N、Nm、rad），2D 只在像素 API 适配层转换。
2D 浮力必须显式给出厚度。翼面使用气动中心点速度，包含 omega cross r；
轮胎输入使用接触平面相对速度和悬架提供的法向载荷。

便宜公式直接返回 owning value；浸没几何/场集合扫描与接触准备需单独准备阶段，
不隐藏在带默认参数的廉价采样 API 中。相同顺序的数值比较相对 1e-5、绝对 1e-6；
不承诺跨平台 bit-exact。时间由调用者提供，无 wall clock 或全局 RNG。

## 分批实施与完成证据

### A. 机械约束

- [x] 2D owning Joint2D/Result 工厂、统一读取和类型化配置。
- [x] 2D 转轴/滑轨/距离/滑轮/齿轮/车轮/焊接封装。
- [x] 齿轮源关节、body/world 两种销毁顺序和错误 world 测试。
- [ ] 3D 齿轮和滑轮：显式 Jacobian/有效质量，冲量施加包含角分量；测试传动比和绳长约束。
- [ ] 2D/3D 机械约束参数、拓扑和状态的版本化保存/恢复；未知字段拒绝，旧版迁移，失败不部分修改。
- [ ] 脚本 Result/所有权与 C++ 一致，示例证明转轴、齿轮和滑轮传动。

### B. 力与场

- [ ] typed force batch 与 2D/3D SI 适配、偏心力矩和失败原子性。
- [x] 方向场明确区分恒力/恒加速度。
- [x] 径向场支持符号、核心软化、衰减和加速度/力上限。
- [x] 全局、圆/球、矩形/盒范围、边缘衰减和对象过滤；固定顺序叠加。
- [x] 浮力支持调用者排水量/浮心；另提供规则几何浸没与浮心计算，2D 厚度显式。
- [ ] 重力平衡、单位比例、中心/边界连续性、多个场叠加和能量方向测试。

### C. 气动

- [x] 线性/平方介质阻力、角阻尼、介质速度；避免阻尼独自反向加速。
- [x] 单翼面迎角、线性升力、诱导阻力和连续失速曲线。
- [x] 点速度与偏心气动力矩，多个翼面/尾翼/控制面组合；舵面改变几何/有效迎角。
- [x] 风、静止、反向气流、侧向流、失速和速度平方关系测试。

### D. 摩擦和轮胎

- [ ] 静摩擦/动摩擦/滑动转换与接触切平面约定；明确不重复计算 solver 接触摩擦。
- [x] 轮转动惯量、驱动、制动、滚阻、锁止、倒车和离地。
- [ ] 纵向滑移、侧偏、联合滑移摩擦圆/椭圆及低速稳定化。
- [ ] 接入现有 Vehicle、悬架载荷/接触点；2D/3D 车身反作用力与轮速一致提交。
- [ ] 起步、制动、转弯、坡道、离地及固定步长收敛/耗散测试。

### E. 系统验收

- [ ] 模块六面 catalogue、Doxygen、脚本参数/所有权/错误文档与真实契约一致。
- [ ] make check、physics-core-only 编译/能力存在缺失 smoke、实际 Box2D/Box3D 集成。
- [ ] 全部新增脚本绑定在真实 Squirrel VM 运行，不能只依赖静态提取。
- [ ] physics-force-lab 与机械示例使用正式运行路径，至少 2 秒 smoke；截图使用引擎 MCP。
- [ ] 宿主构建/测试；环境缺少依赖时记录为未完成证据，不能替代成功。

## 持久化与裁剪

当前 world snapshot v2 对 2D 只有 body/fixture；增加关节不能静默丢失。
先显式拒绝无法表示的 snapshot 以防数据损坏，再在交付前完成版本升级/迁移和恢复。
新格式必须记录种类、稳定拓扑标识、参数与状态，不保存 backend 地址或运行时 handle。
恢复在 detached candidate 中验证，成功后才替换；旧 handle 失效并通过持久 ID 重建。

裁剪声明分别提供 L1 编译、L2 链接、L3 运行、L4 可观察证据；include 图只证明依赖方向。
现有 physics-core-only 是 object 编译边界，必须另有实际 solver 链接/运行测试。

## 原型审计发现

先前 force 原型只覆盖部分数值模型，尚缺场过滤、盒范围、几何浮力、角阻尼、批量事务、
完整车辆接入、机械约束与持久化。真实 Squirrel 测试发现 double 字段的第三方绑定不兼容；
须在边界使用受支持的 float 投影或统一 codec，不能据静态 binding 提取声称可用。

## 2026-09-22 首批验证记录

- 2D 七种关节、motor/limits、PhysicsJointLink、依赖销毁和锁定 world 拒绝已实现。
- 原型力模型已修复脚本 float 投影和 null 输入处理；更完整场/轮胎功能仍按 B-D 未勾选项推进。
- `test/profile/physics_mechanics` 从固定源码构建真实 solver/VM，25/25 CTest 通过：
  23 个新增物理用例、2 个上游 EveScript 用例。每例独立进程，并检查 ZeroErr warning/failure。
- `make check` 通过（包含 182 个仓库脚本测试）；无新增依赖反向边。
- `physics-core-only` 已重新 configure/build/smoke 通过，含新增纯计算和 Joint2D。
- 宿主 Vulkan/窗口开发包缺失；sudo 非交互安装返回需要认证。完整宿主编译、
  `physics-force-lab` 引擎 smoke 与画面证据未完成，不能用本夹具替代。
- 下一批首先处理 2D 关节拓扑与参数 snapshot（需拆分 1000 行以上的 WorldSnapshot.cpp），
  然后 3D 齿轮/滑轮耦合。当前 v2 带 planar joint 时显式拒绝，尚未完成持久化交付。

## 快照基础回归验证

- 将 WorldSnapshot.cpp 拆分为世界入口、内部编解码及受控访问文件，为后续关节存档扩展留出边界。
- 原有三个测试错误地期待 restore 后旧包装对象继续有效；按现有 detached replacement 契约，
  改为检查旧对象/句柄失效，并新增完整 payload 一致性检查，证明数据实际恢复。
- 新增闭合链条往返测试，先复现重复终点导致 restore 失败，再修复后端显式终点与
  Body 工厂隐式闭合的转换；不更改 schema。
- 无图形真实 solver/VM 夹具现有 35/35 CTest 通过，其中包括 10 项快照测试。
- 本批没有完成 2D 关节 snapshot，也没有完成 3D 齿轮/滑轮；上述阶段仍保持未勾选。

## 2D 关节存档与引用重建

- 2D schema 升为 v3：七种关节按持久编号保存端点、局部锚点、参考角/轴、电机、
  限位、弹簧、碰撞策略和滑轮参考绳长。v2 迁移为空关节列表，v1 返回 UnknownVersion。
- 齿轮保存源关节持久编号和创建参考姿态，在隔离候选世界重建原始传动相位；
  普通关节先于齿轮，并按编号排序，输入顺序不会改变依赖解析。
- World 负责恢复出来的关节、刚体和 fixture 包装对象；候选准备失败完整释放。
  发布时替换拓扑，分配新运行时 handle；原有 caller/VM owning wrapper 失效。
- C++ resolveJoint2D 与脚本 eve.resolveJoint2D 重建引用；脚本返回只观察的
  Joint2DReference，携带弱 world 生命周期和 PhysicsJointLink，不持有跨帧 backend 指针。
  回收引用不删除关节，显式 destroy 才删除；旧引用在读档/销毁后返回 StaleHandle。
- 已复现并修复 SimpleSquirrel 指针转换不校验类型造成的越界读取；力模型与机械工厂
  增加显式 typetag 检查，成员调用也校验 receiver。错误分支逐个检查 Result，避免
  提前返回触发未观察 Result 的断言。
- Physics 两个 world 工厂从绑定文件纯移动到独立 TU；无图形 ASan 夹具链接实际工厂和
  sensing 坐标实现，没有关闭 ASan globals 或增加物理替身。
- 完整 A 仍未完成：fixture 材质/过滤、额外 body 参数与 3D 机械状态仍待补齐；
  求解器 warm-start 缓存未保存，只能承诺有容差的恢复，不能承诺逐位重放。

本批最终验证：42/42 普通 CTest、10/10 ASan+泄漏检查用例通过；
`physics-core-only` configure/build/smoke 通过；`make check` 通过（182 个脚本测试），
`git diff --check` 通过。验证范围为无图形真实 solver/VM 与裁剪编译，
不是完整 Vulkan 宿主运行或画面证明。Result/代际句柄/隔离恢复/模块边界规则均适用于本批，
未新增架构例外。

## 2D 材质与刚体运行参数

- 开发中的 v3 已补齐 fixture 密度/摩擦/恢复系数、sensor、category/mask/group、tag，
  以及刚体线性/角阻尼、重力倍率、刚体/world 休眠设置；质量由形状和密度重建。
- v2 迁移显式补入原恢复路径的默认参数，严格拒绝旧版中不应出现的新字段。
- 过滤位宽、材料符号、阻尼及浮点质量/惯量范围在变更候选 fixture 前校验；
  避免密度与几何组合溢出进入 Box2D 的断言路径。失败候选由既有 RAII 所有权释放。
- 新增真实求解器轨迹比较：存档前后质量一致，60 步位置、速度、转角绝对误差 < 1e-5；
  11 种非法材质/参数输入验证当前 payload 和 body handle 保持不变。
- 最终 44/44 CTest、9/9 针对性 ASan+泄漏检查、physics-core-only configure/build/smoke、
  make check（182 个脚本测试）和 git diff --check 均通过。
- 本批继续遵循 Result、单一后端状态与隔离恢复规则，无架构例外。底层直接质量覆盖、
  待积分外力与求解缓存不属于本次存档格式；3D 机械状态、耦合约束及 B-D 功能仍未完成。

## 3D 传动约束第一批验证

- 新增 Coupling3D 的齿轮、齿条和滑轮约束；相位采用多圈角度展开，滑轮支持松绳与偏心连接。
- 使用包含世界惯量和力臂的雅可比，在原生求解前统一投影速度；共享端点先合并雅可比。
  位置误差通过有界修正速度消除，约束力有上限。该模型目前是分步耦合，尚未证明电机、
  接触和多约束闭环同时作用时的精度，不能声称等价于原生求解器内部约束。
- 投影先写入临时速度缓冲；后端拒绝本步时回滚速度和唤醒标志，只有成功时提交相位与冲量。
  注入失败的 provider 测试确认后端实际看到了投影后的速度，拒绝后原速度和观测步数保持不变，
  随后可用同一 tick 成功推进。
- 7 项新增测试覆盖多圈相位、齿条、绳长、松绳、偏心力矩、力上限、禁用和销毁顺序；
  当前独立真实求解器/VM 测试集 51/51 通过。make check 通过（182 个脚本测试）。
- 生命周期继续使用世界观察者注册和代际源句柄，源销毁使包装器失效；未引入图形依赖。
- 本批不是机械阶段完成：脚本接口、3D 状态保存、复杂约束交互和更多失败边界仍待补齐。
  含 3D coupling 的存档目前显式返回 Unsupported，避免静默丢失约束。
- 本批新增 7/7 ASan 用例通过（detect_leaks=1），git diff --check 通过。
  完整 Vulkan 宿主与画面验证仍未进行。

## 3D 传动脚本接口

- 新增 createGearCoupling3D/createPulleyCoupling3D 与 Coupling3D 的 getId/isValid/setEnabled/sample/destroy。
  结果统一投影为 owning Result，VM 释放只移除传动，源关节和刚体保持原所有权。
- 工厂和方法接收者显式核对 native typetag；错误类型、世界已销毁和失效传动返回诊断。
- 设置结构均提供脚本字段；采样结果用 Value 投影，避免 double/native Squirrel 浮点配置差异。
- 真实 VM 验证创建、启停、采样、重复销毁、错误接收者、VM 先释放和世界先销毁。
  全部 53/53 独立 CTest 与 9/9 传动 ASan+泄漏检查通过。
- 已更新用户文档和 module-interface 目录；存档恢复仍是下一阶段，不能将当前脚本可用
  解释为 3D 机械功能全部完成。3D 现有存档还需补齐源关节运行参数，才能证明恢复后行为一致。
- 最终 make check（182 个脚本测试）、physics-core-only 构建与 smoke、git diff --check 通过。
  本批沿用 Result、单一所有者、线程约束与模块接口目录，无架构例外；尚无完整图形宿主证据。

## 3D 恢复候选所有权

- 候选世界通过 unique_ptr 持有恢复创建的 Body3D/Shape3D/Joint3D，失败自动释放；成功提交
  同时交换注册集合与 owning 容器。原调用者对象保持原所有权，成功读档后失效。
- 世界销毁先使注册对象失效，再释放世界持有的包装器，避免析构访问已销毁后端。
  已补充 lookup 借用和跨 restore 的生命周期文档。
- 句柄容量预检按输入 body/shape/joint 数量计算，不再误用读档前世界数量。
- 新测试在候选已创建完整 body/shape/首个 joint 后注入第二个非法 joint，连续失败三次验证
  旧世界对象与句柄保持有效；连续成功恢复三次验证替换和释放。针对性 ASan 泄漏检查通过。
- 当前独立 CTest 54/54 通过。此批处理恢复的生命周期基础，不代表 3D 参数/耦合存档已完成。
- make check（182 个脚本测试）、physics-core-only build/smoke、git diff --check 通过。
  保持隔离恢复、单一所有者和模块边界，无架构例外；未运行完整 Vulkan 宿主。

## 3D 关节连接坐标恢复

- 回归测试证实旧恢复路径以世界轴重建关节时丢失局部旋转相位与第二端独立连接点。
  现恢复构造后显式写入两端完整 local frame，不改变现有 schema 字段。
- 后端调用前验证四元数单位长度；关节身份范围、同体引用和 int 转换在创建前校验。
  各解析 Result 顺序读取和检查，避免短路退出留下未观察的错误结果。
- 回归测试比较全部 14 个存档分量，覆盖不重合连接点和不同两端旋转；额外 6 类非法输入
  确认失败保持旧句柄与关节有效。完整独立测试集 55/55 通过。
- 此项保留几何与相位基准；电机、弹簧、限位及 Coupling3D 参数的版本化保存仍待继续实现。
- 最终针对性 ASan 2/2、physics-core-only build/smoke、make check（182 个脚本测试）和
  git diff --check 通过。沿用 Result、隔离恢复及单一所有者规则，无架构例外；无完整宿主运行声明。

## 3D 原生关节参数 schema v3

- 新增独立 Joint3DParameters 编解码，覆盖 distance/revolute/prismatic/spherical/wheel 的电机、
  弹簧、限位和目标参数；同时保存碰撞连接、约束调节、力/力矩阈值，车轮包含转向设置。
- 数值字段固定长度数组，开关独立 bool；先校验所有字段再调用 setters。
  未知/缺失键、类型、尺寸、数值范围、区间顺序及单位四元数严格校验。
  共享 float-array 解码也拒绝非有限值和超 float 范围值，防止窄化溢出进入后端。
- World3D 当前输出 v3；v2 缺失参数按原构造默认值迁移；v1 保留既有当前拓扑依赖迁移，
  运行时关节参数不反向注入旧版格式。未知版本仍拒绝。
- 测试覆盖五种关节的非默认原生参数完整往返、v2 迁移、每种关节四类非法参数回滚；
  球形限位开关与数值重名在测试中暴露并修正为独立 Enabled 字段。
- 电机初态存档后比较两次 60 步轨迹，旋转与角速度误差 < 1e-5；当前独立 CTest 57/57 通过。
- 仍不保存 solver 热启动缓存。Coupling3D 参数/多圈相位和 3D 材质/阻尼状态仍待实现；
  本批不代表机械阶段全部完成，也不改变后续力场、气动及轮胎范围。
- 最终 ASan 4/4（含泄漏检查）、make check（182 个脚本测试）、physics-core-only build/smoke
  与 git diff --check 通过。裁剪构建检查发现新增源路径缺少前缀，修复后重新构建通过。
  本批保持 Result/隔离恢复/单一后端状态和模块边界，无架构例外；尚无完整 Vulkan 宿主证据。

## 3D 耦合恢复前的引用提交边界

- 将 body/shape/joint 的新句柄分配与索引构建移入候选世界；成功提交时交换计数器和索引，
  不再在替换当前世界后分配索引。这使后续耦合源引用可以在发布之前解析并验证。
- 候选准备异常返回 Result；后端拒绝 observation 恢复时，候选整体销毁，当前世界句柄序列不消耗。
- World3D.step 期间 snapshot/restore 显式拒绝，防止 provider 回调重入替换正在使用的世界。
- 注入 provider 验证步进内重入被拒绝但外层成功推进；随后在候选链接准备后拒绝 observation
  恢复，原对象/步数保持不变，下一次创建仍分配连续句柄。完整独立 CTest 58/58 通过。
- 本批是耦合拓扑恢复的事务基础；Coupling3D 序列化和解析仍待接入，整体范围保持不变。

## 3D 耦合拓扑与相位存档

- 开发中的 v3 新增 couplings 数组：稳定编号、源编号、启停、限制参数、上次成功冲量与饱和状态；
  gear 保存 ratio/reference/多圈 coordinate，pulley 保存局部连接点、世界导向点、长度和松绳设置。
- 候选世界先完成 body/shape/native joint 及新运行时句柄，再重建 couplings；源以持久编号解析，
  运行时连接只引用新句柄。候选按编号排序创建，防止稀疏编号与工厂临时编号碰撞。
- 世界 owning 容器接管恢复的耦合；交换时重绑 world，原调用者持有的旧包装器失效。
  C++ World3D::resolveCoupling3D(id) 返回有明确有效期的 ResultRef 借用；脚本恢复后查询仍待补齐。
- 齿轮经多圈旋转后保存；恢复后的相位/参数再次保存一致，后续 60 步坐标/轨迹误差 < 1e-4。
  滑轮执行相同验证；每类约束四种无效记录验证失败不修改旧对象或句柄，源销毁使恢复耦合失效。
- 当前完整独立 CTest 59/59 通过；3D 材质、阻尼和更广泛接触组合证据仍未完成。
- 额外验证编号 1、3 的稀疏耦合集合（其中一条禁用）恢复；第二条记录源引用错误时，
  第一条已创建候选耦合也正确回滚。最终 59/59 CTest、15/15 ASan+泄漏检查、
  make check（182 个脚本测试）、physics-core-only build/smoke 和 git diff --check 通过。
  本批遵循隔离恢复、Result、单一所有者和线程边界，无架构例外；未验证完整 Vulkan 宿主。

## 恢复耦合的脚本观察引用

- 新增 eve.resolveCoupling3D(world,id)，返回 VM 持有的 Coupling3DReference 观察对象；
  其 getId/isValid/sample/setEnabled/destroy 与 owning 包装器共用类型安全投影。
- 观察者只保存世界借用与两枚弱生命周期标识（世界、约束实例）；约束 release 时标识失效，
  原编号重新出现也不会使旧观察者重新有效。Token 不延长世界或求解器生命周期。
- 连续两次恢复覆盖外部 owning 包装器失效和 world-owned 包装器实际释放；VM 先销毁不删除
  约束，世界先销毁不触碰悬垂世界，显式 destroy 移除约束；错误类型接收者安全拒绝。
- 观察者重复 destroy 返回 StaleHandle；原工厂 owning 包装器的幂等 destroy 行为保持一致。
- 更新用户文档、模块接口目录和测试登记；完整独立 CTest 60/60 通过。
- 机械阶段仍需 3D 材质/阻尼状态和示例验证；后续力场、气动、轮胎及系统验收范围不变。
- 最终 16/16 ASan+泄漏检查、make check（182 个脚本测试）、physics-core-only build/smoke、
  git diff --check 通过。本批遵循单一所有者、失效检测、线程边界与 Result 规范，无架构例外。
  证据仍是无图形真实 solver/VM；完整 Vulkan 宿主与交互示例尚未验收。

## 3D 刚体动力学参数恢复

- v3 dynamics 保存线性/角阻尼、重力倍率、休眠阈值和刚体休眠开关；世界保存休眠开关。
  非有限/负阻尼/负阈值与未知字段拒绝；gravityScale 允许有限负数。
- v1/v2 明确迁移固定后端默认刚体参数，世界开关保持旧路径 false；拒绝旧版中的新字段。
- 将速度和休眠恢复推迟到形状及关节创建之后，避免质量更新与创建关节唤醒覆盖状态。
- 新测试比较 120 步位置、线速度与角速度，读档前后误差 < 1e-5；全 payload 往返一致。
  七类非法 dynamics 输入验证失败后 payload 和句柄不变；当前独立 CTest 61/61 通过。
- 3D 形状材质/过滤参数仍待补齐，不将此批误记为完整机械阶段或全计划完成。
- 最终 8/8 3D 存档 ASan+泄漏检查、make check（182 个脚本测试）、physics-core-only build/smoke
  与 git diff --check 通过。继续遵循隔离恢复与单一后端状态，无架构例外；未运行完整图形宿主。
- 下一批形状保存还需同时处理局部变换、sensor、碰撞过滤、表面/网格材质、事件开关和 one-way，
  否则密度/摩擦系数往返不足以证明接触行为一致。

## 3D 形状局部几何与事件身份

- v3 保存 localTransform 的平移和单位四元数；在候选中恢复形状后才恢复刚体速度。
  偏移旋转盒形往返验证完整 payload、质心和惯量保持一致。
- 严格检查形状字段集合及局部变换，旧版本按原有单位局部变换迁移；解析 Result 顺序检查，
  身份窄化和 next-id 溢出在调用工厂之前拒绝。
- 重建新句柄时同步重建 shapeRecords，避免稀疏持久编号被临时创建编号污染。
  实际接触事件测试覆盖编号 1、3 的形状读档后的碰撞开始事件。
- 当前独立 CTest 63/63 通过。材质、过滤、sensor、one-way 与网格材质仍待下一批；完整范围不变。
- 进一步拒绝刚体非单位四元数（含归一化可能溢出的巨大分量），回归验证失败不改变当前世界。
  最终 63/63 CTest、10/10 ASan+泄漏检查、make check（182 个脚本测试）、
  physics-core-only build/smoke 和 git diff --check 通过。无架构例外，尚无完整图形宿主验收。

### 3D 形状接触存档验证

- v3 保存密度、完整表面材质、64 位过滤掩码、分组/标签、传感器与事件开关、单向平面、爆炸响应和网格材质槽。
- 独立候选恢复先校验密度产生的质量与惯量，防止极值进入后端求逆；失败保留原世界及句柄。
- 修正后端普通形状也报告一个材质槽导致的非网格恢复拒绝；仅三角网格编码槽数组。
- 验证：66/66 独立测试；13/13 snapshot3d ASan/泄漏检查；make check（182 Python tests）；physics-core-only 构建及 provider present/absent smoke；git diff --check。
- 日志：/tmp/eve-contact-{tests,asan-tests,check,core-build,core-smoke}.log。
- 尚未覆盖世界级按 Body/Shape 配对的碰撞忽略规则；整体计划其余未勾选项继续保留。

### 批量 SI 施力 C++ 接入

- ForceLoad 使用 PhysicsLink 和 SI 数值；ForceBatch 准备/提交两个阶段验证全部目标并聚合偏心力矩与自由力矩。
- 提交前重新解析 generation；删除目标、静态/禁用目标、跨世界、非法维度、数值或合计溢出均在施力前拒绝。
- token 移动转移唯一提交权，成功后不可再次提交；不保存跨帧裸指针。提交按句柄顺序施加每个刚体的合力/力矩。
- 70/70 独立测试、7/7 施力/批次 ASan 和泄漏检查、make check（182 tests）、physics-core-only 构建/smoke、diff 检查通过。
- 日志：/tmp/eve-batch-{tests,asan-tests,check,core-build,core-smoke}.log。
- 尚需脚本与车辆 producer/consumer、solver 失败时的外力生命周期契约和统一累计边界；B 项不勾选完成。

### 恒力场与径向限幅

- DirectionalForceField 与 DirectionalField 分别表达 N 恒力和 m/s^2 加速度；C++ 与脚本入口均明确区分，不靠开关改变向量单位。
- RadialField 新增非负有限 maxAcceleration/maxForce，区域衰减后同时施加两个上限；中心/零强度/零上限返回零。对数幅值避免极小核心在限幅前溢出。
- 新增恒力区域淡出、轻重质量限幅、极小核心和 subnormal 距离、正负强度以及脚本参数/错误类型测试。
- 71/71 独立测试；18/18 forces ASan/泄漏检查；make check（182 Python tests）；physics-core-only 构建与 smoke；diff 检查通过。
- 日志：/tmp/eve-field-{tests,asan-tests,check,core-build,core-smoke}.log。

### 场的矩形与盒形支持

- FieldRegionKind 显式提供 Global/Sphere/RectangleXY/Box；Sphere 负半径继续表示全局。
- 矩形使用 XY 正半尺寸、Z 半尺寸为零，忽略采样点 Z；盒形要求三个正半尺寸。当前轴对齐，不暗示任意旋转支持。
- 边缘按最近面内侧距离淡出，边界返回零；统一应用于方向加速度、恒力和径向场。
- 脚本 FieldRegion.kind/halfExtents 与 C++ 契约一致，非法尺寸、未知 kind、超过最小半尺寸的淡出宽度返回失败。
- 72/72 独立测试、19/19 forces ASan/泄漏检查、physics-core-only 构建/smoke 通过。
- 对象过滤和固定顺序场集合尚未实现，相关组合清单项继续保持未完成。
- make check（182 Python tests）与 git diff --check 通过；日志 /tmp/eve-region-{tests,asan-tests,check,core-build,core-smoke}.log。

### C++ 场集合与过滤

- 新增不可变 FieldSet，构建时复制并验证全部 FieldDefinition；每步按定义顺序采样方向加速度、方向恒力和径向场。
- FieldTarget 仅含 PhysicsLink 和调用方类别；64 位 includeAny/excludeAny 过滤独立于 backend 碰撞过滤，排除优先。
- 读取真实刚体 SI 质心/质量后准备同一个 ForceBatch；重复、静态/禁用、失效目标或数值错误整批拒绝。
- 批量零合力/零力矩不唤醒刚体，同时保留该目标在提交阶段的身份校验。
- 74/74 独立测试、21/21 forces ASan/泄漏检查、physics-core-only 构建/smoke 通过；git diff --check 通过。
- 脚本集合、正式示例与 solver 失败外力生命周期仍待实现；不将组合清单项视为已完成。
- make check（182 Python tests）通过；日志 /tmp/eve-fieldset-{tests,asan-tests,check,core-build,core-smoke}.log。

### 脚本场集合

- VM 拥有 FieldSetBuilder、FieldTargets、不可变 FieldSet 和一次性 ForceBatch；原生对象经统一 owning-instance release hook 转移。
- 三种场添加、build、2D/3D 目标添加、prepare 和 commit 均投影 Result；所有对象类型和掩码先验证。
- 类别使用无符号十进制字符串，覆盖 64 位高位并拒绝溢出/负值/错误类型。配置和目标只复制值/句柄，不保留世界/刚体裸指针。
- 真实 VM 验证三场叠加后的 2D/3D 速度、模型与 builder 修改隔离、错误维度、重复提交、目标与世界销毁、GC。
- 75/75 独立测试、22/22 forces ASan/泄漏检查、make check（182 Python tests）与 diff 检查通过。
- 日志：/tmp/eve-fieldscript-{tests,asan-tests,check}.log。绑定代码由真实链接测试编译；本轮无核心原生实现变更，不重复裁剪构建。
- 场范围/过滤/固定顺序叠加清单项已完成；正式示例、几何浮力、气动组合和轮胎车辆集成仍待后续完成。

### 规则几何浮力

- BuoyancyGeometry 提供水平液面下圆/矩形/球/轴对齐盒的解析排水体积、体积分数与浮心；2D 强制显式正厚度和 XY 中心。
- 球冠积分和圆弓形积分为纯常量规模计算；近干燥圆采用小角展开，接近全浸没计算较小补集。干燥体积为零且浮心返回原中心占位。
- C++ 与四个脚本采样接口返回相同拥有值；结果结合 BuoyancyModel 和 SI 浮心施力即可产生偏心力矩。
- 独立 20000 切片积分对照体积/一阶矩；真实 Box2D 两种 meter 与 Box3D 验证半浸没重力平衡、厚度换算和偏心力矩。
- 78/78 独立测试、25/25 forces ASan/泄漏检查、physics-core-only 构建与 smoke、diff 检查通过。
- 不声明旋转多面体、任意 collider 自动裁切或波浪液面；这些不是当前规则几何接口。
- make check（182 Python tests）通过；日志 /tmp/eve-buoyancy-{tests,asan-tests,check,core-build,core-smoke}.log。

### 转动惯量角阻尼

- AngularDampingModel 与平动阻尼分离，显式线性/平方角阻尼系数；输入是 body-minus-medium 相对角速度。
- 2D 返回 Z 轴标量扭矩，3D 在惯量主轴系分别使用三个正主惯量，返回同系扭矩。调用者负责世界系转换，不把非对角惯量矩阵的对角元素误作主惯量。
- 单步等效力矩采用 -c*omega/(1+c*dt/I)；不包含陀螺或其他外力矩。纯测试覆盖 dt=0.001/0.02/10、不反向、相对旋转能量耗散和旋转介质带动刚体。
- 原生扭矩经 ForceBatch 进入真实 Box2D/Box3D，结果与惯量预测一致；脚本参数和错误类型在真实 VM 验证。
- 80/80 独立测试、27/27 forces ASan/泄漏检查、physics-core-only 构建与 smoke、diff 检查通过。
- 多翼面/控制面、点速度采样与偏心气动力组合继续待实现。
- make check（182 Python tests）通过；日志 /tmp/eve-angular-{tests,asan-tests,check,core-build,core-smoke}.log。

### 多翼面与控制面接入

- WingAssembly 复制局部翼面几何与极曲线；prepare 从实际 Body/Body3D 姿态转换气动中心和 chord/normal，读取包含 omega cross r 的点速度并减世界风速。
- 各翼面独立偏转角旋转局部几何，可组合机翼、尾翼和差动控制面；同一 ForceBatch 统一提交偏心力矩，无第二套刚体姿态。
- 脚本提供构建器、控制角容器与 2D/3D prepare，复用一次性 commit；类型、角数量、失效目标和任一翼面失败整体拒绝。
- 测试覆盖旋转 2D 刚体两个 meter 的点速度/力矩预测，3D 对称与反向舵面、随风同速、无效控制不唤醒、失速连续性/反向气流，以及真实 VM 改变舵面后升力方向变化。
- 85/85 独立测试、32/32 forces ASan/泄漏检查、physics-core-only 构建与 smoke、diff 检查通过。
- 气动模型与组合清单已完成；正式宿主示例/视觉验收仍归 E 阶段，不以本次独立测试代替。
- make check（182 Python tests）通过；日志 /tmp/eve-wing-{tests,asan-tests,check,core-build,core-smoke}.log。

### 轮胎隐式接触与低速稳定性

- 以冻结滑移刚度计算隐式纵向/侧向冲量，输入新增两个非负对角有效逆质量；轮速与车身力使用同一接触冲量。
- 驱动在当步进入接触响应；制动/滚阻使用有界单调锁止求解，返回实际 resistanceTorque。步末 slipRatio/slipAngle 对应预测轮速与车身速度。
- 修正旧原型滚阻晚一帧传给车身的行为；无滚阻自由滚动保持零滑移/零接触力，存在滚阻时当步产生车身阻力。
- 新增高刚度低速多步长耗散/不超调、广义冲量一致性、驱动反向对称、静止制动保持/运动锁止，以及对解析滑移衰减的步长收敛验证。
- 实际 Box2D/Box3D 车身连续 32 步接受轮胎力，再将实际速度反馈模型，验证能量不增长及接触冲量一致；轮速只在成功 step 后更新。
- 89/89 独立测试、36/36 forces ASan/泄漏检查、make check（182 Python tests）、physics-core-only 构建/smoke 和 diff 检查通过。
- 日志 /tmp/eve-tire-{tests,asan-tests,check,core-build,core-smoke}.log。
- Vehicle 悬架现仍为旧速度控制路径；尚未替换，也未完成世界 step 失败时轮速/外力一致提交。一般接触交叉有效质量仍需在车辆适配中处理，不能将本轮公式验证视为完整车辆交付。

### 外力提交与固定步完成回执

- ForceBatch commit 明确接收 SimulationStep，返回只读 ForceReceipt。世界独占 Pending/Applied/Cancelled 状态转换，调用方只共享回执存储，不持有世界/刚体指针。
- 世界内多个批次绑定同一个 tick/duration；错误时间在进入 backend 前拒绝。后端失败保留 pending 外力，重试不重新施力；成功步进才发布 Applied。
- 成功 restore / destroy 取消待处理回执，失败 restore 保持原状态；snapshot 拒绝遗漏待处理外力。禁止绕过世界 raw stepping / ClearForces，2D 自动清力关闭时明确拒绝。
- 2D 新增整个 backend 调用期间的重入保护；3D 复用现有 coupling step guard。3D topology adoption 从大文件移至独立 TU。
- 脚本 commit 一并迁移，tick/ns 使用十进制字符串；提供精确步进和回执查询，实际 VM 覆盖成功、错误时间、错误参数、世界销毁后 retained receipt。
- R-FACE-1/2：物理脚本面和契约测试同步更新；R-MECH-1/2：已知世界与批次的一对多高频同步提交，使用 typed 直接调用及 world-owned completion state，无 capability/string lookup、无新 ECS。
- 该步解决车辆发布预测轮速所需的完成信号；Vehicle 适配、一般接触交叉有效质量和跨批次累计外力限幅仍未完成。
- 验证：92/92 独立测试、39/39 forces ASan/泄漏检查、make check（182 Python tests）、physics-core-only 构建及 provider present/absent smoke、git diff --check 均通过。
- 日志：/tmp/eve-receipt-{tests,asan-tests,check,core-build,core-smoke}.log。源码门禁首次发现累计改动行格式不一致，已仅格式化改动行并重跑通过。
- 后续 Vehicle 接线必须区分 Pending/失败与 Unavailable：当前 wheelMove 仅 Applied 阻止 kinematic 回退，不能让尚待物理步的车辆落入回退移动。悬架运行时当前只有 prevCompression/grounded；轮速候选及接触状态须统一在回执 Applied 后发布。

### 2D Vehicle 轮胎接入

- 替换 attached 2D wheel 的速度/朝向 setter 路径，使用真实刚体点速度、质量与转动惯量，前后轮施加偏心接触力。ship 和 track 保持既有独立移动策略。
- 2D 明确为顶视 XY 接触平面，虚拟 supportAcceleration2D 提供法向载荷；不是侧视悬架。wheel z 必须为零，安装点与轮半径是 SI，车身和 maxSpeed/accel 在边界换算像素单位。
- 新增 submitWheelStep/synchronizeWheelState Result 接口；binding 仅拥有未完成候选，SuspensionState 拥有完成轮速。重复 pending 不重施力，失败不发布；失效 body 不发布候选。旧 mobility 只对 Unavailable 执行 kinematic 回退。
- 多轮车身响应采用 n*(对角有效逆质量 + 同接触交叉项绝对值) 的对角上界，显式作为保守隐式阻尼；不声称精确多轮同时求解。加入偏心高刚度、不同 meter 的总动能耗散测试。
- 定义 codec 加入轮惯量、刚度、摩擦、滚阻、制动力矩、转向角和虚拟承重参数；旧定义缺省值可读取。3D canonical submit 仍明确 Unsupported，原 3D 旧路径待下一步替换。
- ECS 范围为已附着 VehicleEntity；读取 Definition/Input/PhysicsBody/SuspensionState，提交不改完成态，synchronize 写 SuspensionState，legacy projection 写 Motion；无实体/组件结构修改，无新 ECS System/事件；phase 为物理步前提交、成功步后发布。
- R-FACE/R-MECH：更新物理 adapter 六面与 optional backend catalogue；native physics-present 符号为硬链接依赖，absent TU 单独选择，测试真实 absent adapter 的 L2/L4；无整个 Vehicle 模块裁剪主张。
- 轮胎运行态持久化、3D 悬架和用户示例仍未完成，不勾选整个 D/E 阶段。
- 验证：100/100 独立测试（含 7 个车辆轮胎/定义用例及 1 个真实 absent adapter 用例）、47/47 forces+vehicleTires ASan/泄漏检查、make check（182 Python tests）、physics-core-only 构建/provider present+absent smoke、diff 检查通过。
- 独立 fixture 链接真实 VehicleSystem、VehiclePhysics、VehicleDefinitionRuntime 与轮胎实现；验证真实 wheel mobility 在 Pending 时不进入 kinematic 回退。没有用测试桩替代车辆分发器。
- 日志 /tmp/eve-vehicle-{tests,asan-tests,check,core-build,core-smoke}.log。

## 2026-10-15 dev 合并进行中

- 原基线 0182af046，已 fetch 并快进到 origin/dev 10f6faa7509e4a108ee5f2e6b6e3be357e99af26。
- 完整原始修改保存于 stash c88de477d9328ea17bb6bcde40db4611f7678f7e（physics-wip-before-dev-2026-10-15）；外部 third-party 链接未收入快照。集成完成前不得删除该快照。
- 上游已新增机械机构和同名 Joint2D，并将 3D 原生访问迁移到 Box3D/Jolt 后端抽象；不能通过整文件选择一侧解决。
- 已处理 Makefile、文档、示例目录、构建源列表、检查器和合并后的快照测试文件；保留上游删除 JSON catalogue 的新机制，将本地 forces/mechanics/vehicle 高频文件加入 HOT_PATH_GLOBS。
- Physics 的 backend 工厂迁入本地已拆分的 PhysicsWorldFactory.cpp；2D 步进同时保留本地事务保护和上游机构同步。
- 尚有 World3D.cpp、World3D.h、WorldSnapshot.cpp 的真实冲突。另需统一两套 Joint2D 的所有权/API，迁移本地 forces、coupling、snapshot、vehicle 中已删除的 3D raw() 使用；不能只消除冲突标记就声称集成完成。
- 检查器单元测试 11/11 通过。make check 在入口因缺少 ruff 退出，尚未执行后续检查；未获得合并后的编译或运行证据。
- 本节只记录合并进度，原 A-E 完整目标和之前未通过的 3D 坡道驻车测试仍未完成。

### 2026-10-15 合并收尾与验证边界

- 已清除全部合并冲突，当前基线为 origin/dev 10f6faa7509e4a108ee5f2e6b6e3be357e99af26。
  上节的三处待解决冲突、Joint2D 双注册表和 3D raw() 迁移均已处理；备份 stash 保留。
- 像素和 SI 关节工厂共用一个 owner/handle registry；恢复支持新增 Motor，
  3D v3 补齐 Weld/Motor/Parallel/Filter 参数。Jolt 保留 portable v2，通用外力走后端接口；
  Coupling3D 和射线轮胎对 Jolt 显式 Unsupported，未伪装为跨后端功能完整。
- 3D Box3D 轮胎已接入 ForceBatch/回执；多轮共享刚体使用 64 次有界预测迭代，
  驱动力矩每步只积分一次。坡道驻车、运动地面角速度、动态地面反作用力测试通过。
- 集成夹具启用实际编译出的测试发现，断言 WARN 也会使 CTest 失败，避免零退出码造成假通过。
  GCC Debug 集成构建和 Physics.cpp 完整绑定翻译单元编译检查通过。
  153 个独立 CTest 中 148 通过、5 失败。使用未修改的基线源码、相同固定依赖重新构建对照：
  38 个 physics.jolt 测试中 33 通过，同样 5 项失败：
  worldControlsAndBoundsParity、oneWayAndPairOverridesParity、solvedImpulseMomentumParity、
  solvedContactCoverageLifetimeParity、meshRollingMaterialAndHitParity。失败位于 Box3D 分支；
  不降低断言、不跳过测试，也不将此结果记为全绿。
- physics-core-only 已完成独立构建和 provider-present/provider-absent smoke。
  补齐 EVCommon 的 Squirrel 内部头路径及物理核心的 GLM 路径；该裁剪证据不等于完整宿主构建。
- ASan（Jolt 关闭）110 个物理单例、vehicle absent 和 evescript.types 通过。
  完整 113 项中 evescript.modules 因固定第三方 Squirrel 编译器泄漏 1368 字节失败。
  测试框架 zeroerr::UnitTest::parseArgs 的 96 字节泄漏使用仅限本次运行的精确 LSAN 符号排除；
  未关闭物理代码内存检测，未提交屏蔽文件。不得声称完整 ASan 套件通过。
- 本次适用并验证单一权威状态、Result、双方销毁顺序、事务恢复、模块分层和裁剪规则；
  未引入架构豁免，沿用最新 dev 的代码式架构门禁，不恢复已废弃 JSON catalogue。
- 原 A-E 全量目标仍有轮胎运行态存档、正式机械/场景示例、完整宿主及视觉验收等未完成项。
  本节完成的是最新 dev 合并与接口适配，不把上述基线失败或未验证项计为通过。

- 最终 make check 全部通过（含 313 项脚本测试）；git diff --check 通过，Git 未解决条目为 0。验证日志保存在 build/merge-validation/，原始修改未提交或推送。
