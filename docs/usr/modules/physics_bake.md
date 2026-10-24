# Physics Bake / 无解算器烘焙数据

`physics_bake` 位于物理宿主的 `physics/bake` 卫星目录，L3，负责拥有值数据的
轨道与资产发布，不创建物理世界，也不依赖图形或脚本提供者。

## 使用入口与完整流程

| 内容 | 录制入口 | 冻结与重放 | 表现消费 |
|---|---|---|---|
| 破坏碎块 | GeometryCollectionInstance → CollapseAnimation.capture | [CollapsePlayback](physics_bake_collapse.md) | 现有 GeometryCollectionRenderer / house-collapse 示例 |
| 2D/3D 刚体 | [captureRigidFrame](physics_bake_capture.md) / [脚本会话](physics_bake_recording_script.md) | RigidPoseTrack / RigidPlayback | [2D/3D 目标提交](physics_bake_graphics.md) |
| 布料、绳索 | [真实顶点捕获](physics_bake_deform_capture.md) / [脚本会话](physics_bake_recording_script.md) | DeformTrack / DeformPlayback | 三角网格或管段 |
| CPU 精灵粒子 | [事件记录](physics_bake_particle_capture.md) / [脚本归档](physics_bake_particle_script.md) | ParticleTrack / BakedParticlePlayback | 按 emitter 身份绑定材质的精灵批次 |

1. 在录制准备阶段固定配方：实际生产者/后端构建身份、全部输入依赖、参数、坐标单位、
   物理步长、采样间隔和命名随机流。运行时句柄不能充当持久对象身份。
2. 调用方按显式固定时间推进真实模拟，再捕获 owning 数据；录制预算或拓扑验证失败即处理诊断，
   不保存一个声称成功的截断片段。物理更新不由录制接口隐式调用。
3. 冻结、保存并检查 Result。保存不覆盖旧资产；只在 NotFound 时自动录制。
   身份冲突、损坏或 IO 失败必须显式处理，不能统一当成缓存缺失。
4. 用预期配方加载缓存，或用明确的普通 load 入口播放便携历史资产。普通 load 不能证明来源匹配。
   从冻结资产创建独立播放器；资产共享，时间、速率和实例变换不共享。
5. 退役录制源，重放只 advance/seek/sample 和提交表现。粒子源属于 ECS，丢弃脚本变量不等于删除实体；
   有脚本别名时不得从外部删除源再继续调用旧别名。完整合同见粒子脚本文档。
6. 图形资源由调用者管理，每次提交提供活绑定；先解除借用、按 Graphics 释放协议等待在途使用，
   再释放并替换资源。播放器不接管材质、纹理、mesh 或目标实体的生命周期。

可运行入口为 [physics-bake-replay](../../../examples/physics-bake-replay/README.md) 和
[house-collapse](../../../examples/house-collapse/README.md)。前者的 playbackOnly 模式验证第二进程
仅加载资产；首次录制与重放控制说明、资源目录及运行命令见示例 README。

### 精度与失败恢复

位置线性插值和四元数最短弧插值不会恢复遗漏的碰撞变化或整圈旋转。
选择采样频率时同时看最大误差、RMS 和离散状态，不仅看平均值。
[实测误差报告](../../dev/physics-bake-resampling-error-2026-10-21.md) 含真实落地、
布料/绳索和粒子对照；房屋资产降采样已有米级局部误差反例，不能直接采用。

Linux 保存失败若带 publication=published，目标可能已经完整可见，但目录耐久性未确认。
先检查目标；再次保存会返回冲突，不能删除该目标再盲目重试。publication=not_published 表示
对应屏障前没有发布。错误诊断并非所有失败都带这两个字段，应保留完整 status。
当前非 Linux 只承诺原子可见性，掉电耐久性与平台执行仍未完成。

固定变形拓扑不支持录制中撕裂/断绳；粒子通道不支持 GPU 驻留、ribbon 和视角相关排序。
烘焙重放只承担表现，不生成碰撞、伤害或玩法事件，也不自动转回实时物理。

## 刚体姿态轨道

原生 `RigidPoseTrack::prepare(space, frames)` 验证并冻结完整录制；复制 track 共享只读数据。
每帧由显式秒数和 `PersistentId` 配对姿态组成。首帧时间必须为零，后续时间严格递增；
输入顺序可不同，但每帧必须包含相同的非空、非零、无重复身份集合。上限为 16384 个对象、
2000000 个总样本。单位由 `RigidTrackSpace` 明确指定：

- `PlanarPixels` 保留生产者的 XY 像素坐标，不隐式翻转 Y；z/qx/qy 必须为零。
- `Metres3D` 使用米与 XYZW 四元数。

四元数在准备阶段归一化。`sample(seconds, id)` 对位置线性插值、旋转最短路径归一化插值，
时间超出区间夹取端点；不表示多圈角运动。未知身份和非有限时间返回 checked Result。
运行时 body handle 不可作为持久身份；真实刚体采集位于独立的
[physics_bake_capture](physics_bake_capture.md) 卫星；原生 2D/3D 表现提交位于
[physics_bake_graphics](physics_bake_graphics.md)，脚本录制和表现分别由对应 script 卫星提供。
完整示例见 `examples/physics-bake-replay`；纯值轨道本身不拥有实时生产者或渲染对象。

`RigidPoseTrack::save/load` 使用 EVERIGD1 v1/v2 资产格式：固定小端头保存空间/轨道数/帧数，
排序的 16 字节持久 ID 随后是时间与七个 float 姿态字段，尾部 32 字节内容校验覆盖前述全部数据。
加载先核对字节预算与校验，再校验精确长度、ID 顺序、时间与姿态，成功后才返回只读轨道。
未知版本与额外字段拒绝；v1 没有烘焙配方，仅用于显式资产播放。v2 内嵌完整配方，
只有通过 `loadForRecipe` 的精确比较才可准入缓存；格式细节见下文。

## 文件发布

`publishBakedAsset` 已由碎块和灰尘生产保存路径共同使用。不覆盖现有目标，失败通过 Result
及结构化 diagnostics 返回。Linux 使用匿名暂存、数据和目录刷盘，要求 O_TMPFILE 和
procfs 描述符链接；其他平台仍只有原子可见性保证。具体状态与恢复限制见
[破坏模块](physics_destruction.md)。

## module-interface

- 原生数据面：刚体、变形、粒子的 owning 帧/轨道/录制与播放器，配方、共用时钟/变换和发布函数。
- Capability 输入/输出：无；不查询或注册 provider。
- ECS 与 Link：无 ECS System；轨道持久 ID 是外部实体的逻辑对应关系，不解析或拥有活实体。
- 事件：无事件、回调或隐含玩法副作用。
- 脚本绑定：本模块不注册 VM 类；[physics_bake_script](physics_bake_script.md) 提供可裁剪的配方与播放器投影。
- 工具协议：无新增协议。

准备与写文件属于线性成本阶段；采样不执行 IO、模块名称查找或物理解算。世界或输入录制先销毁
不影响冻结轨道；轨道先销毁不影响世界。没有共享可变时钟。独立 profile 提供无 solver 的
生产对象链接/运行证据；完整模块的 Linux OBJECT/SHARED 裁剪另见下文 physics-bake-only 配置。跨平台链接仍需验收。

## 独立播放器与共用时钟

`RigidPlayback(track)` 以常数成本共享冻结轨道，并创建独立的时间状态。`advance` 接受显式
非负 dt，`seek` 夹取端点，`setRate` 支持零速和负速，`setWrap` 选择夹取或循环；
`sample(id)` 按当前时间返回姿态值。复制播放器共享样本，但不共享暂停、速率或时间。

`BakePlaybackClock` 是碎块与刚体播放器共用的时间组件。片段是时长的唯一所有者，调用时
将时长传给组件，不在组件中保存第二份资产状态。暂停时也检查非法 dt 和算术溢出；
循环前进使用半开区间，显式 seek 可停在末帧。无系统时钟、物理解算、IO 或回调。
刚体播放器支持实例变换与原生表现提交；脚本接入见 [physics_bake_graphics_script](physics_bake_graphics_script.md)。已有软件 Vulkan 窗口与 VM 验证，不能据此声称独显性能。

`RigidPlayback` 与碎块播放器组合相同的 `BakePlaybackClock` 和
`BakeRigidTransform`。每个实例独立持有时间、速率、暂停和变换，样本存储保持不可变共享。
实例变换按轨道单位先绕烘焙原点旋转再平移，不缩放；像素平面轨道拒绝 Z 平移及
X/Y 四元数分量。非法输入保留原状态，采样溢出返回诊断，不写回物理世界。

## 共用来源配方

`BakeRecipe` 现在由纯值 bake 模块拥有，碎块的 `CollapseBakeRecipe` 是兼容别名。
米制规范编码仍为 EVECREC1，字段、摘要和右手 Y-up 合同不变。来源依赖、参数、后端/生产者版本、固定步长、采样间隔及命名随机流只在
准备/准入时编码，播放阶段不执行这些操作。生产者必须填写实际使用的完整依赖及配置。

配方现在可通过 checked `setCoordinates` 显式选择 `PlanarPixels`，其编码为 EVEBREC1，
坐标字段为 planar-pixels-xy；原米制默认值仍逐字段使用 EVECREC1。未知坐标值拒绝并保留
原配方，解码拒绝 schema/坐标字段混配。碎块录制与加载只接受米制配方。
仅有配方键不能证明已有刚体文件匹配；加载时必须比较内嵌规范配方。

## 刚体来源准入（EVERIGD1 v2）

`prepare(space, frames, recipe)` 验证坐标一致、完整来源与固定采样时间，冻结独立配方副本。
带配方的轨道保存为 v2：在原 24 字节头后增加小端配方长度及规范字节，后续身份与帧布局不变，
完整校验覆盖配方。恢复先验证整个文件，再验证配方、空间和时间，成功才返回轨道。
`loadForRecipe(path, expected)` 逐字节比较完整规范配方，旧 v1 无来源及不同配方均拒绝准入；
普通 `load` 仍允许旧资产显式播放。不会自动重烘焙、覆盖文件或把旧 v1 补签为可信来源。

## 固定变形拓扑

`DeformTopology::prepare` 冻结三角形或线段的有序索引、稳定顶点数量和可选 UV。
每个基本图元内的重复索引、越界索引、未知类型、不完整索引组及非有限 UV 会被拒绝。
身份覆盖顶点数、连通性、绕序和 UV；同点数撕裂/断绳会得到不同身份。UV 的正负零规范化。
拷贝共享不可变存储，span/key 借用仅在至少一份 owning topology 存活期间有效。
拓扑由下述 DeformTrack 持有，真实布料/绳索采集见 [physics_bake_deform_capture](physics_bake_deform_capture.md)。

`DeformTrack::prepare(frames, recipe)` 已实现固定拓扑帧冻结。每帧携带实际拓扑与稳定索引位置；
准入逐项比较连通性/UV，检查完整配方的坐标和固定采样时间，以及总顶点样本预算。
`sample(seconds, output)` 在精确大小的调用方缓冲区内插值，时间夹取端点，无分配和解算；
非法时间或输出范围在写入前拒绝。帧与配方均拥有不可变值，源输入销毁后仍可采样。
持久化和播放器见下文；真实网格表现通过 [physics_bake_graphics](physics_bake_graphics.md) 接入。

## 变形文件（EVEDEFM1 v1）

`DeformTrack::save/load/loadForRecipe` 已接入公共不覆盖发布器。格式按小端保存版本、图元类型、
顶点/索引/UV/帧数和配方长度，随后是规范配方、固定拓扑与 UV、逐帧秒数和 XYZ，尾部整体校验。
读取在总字节与元素预算内进行，先校验内容，再核对精确长度，最后经拓扑/帧准备阶段验证所有
语义后才发布。未知版本、尾部、非法数量/索引/数值及配方不匹配均拒绝；首版无更早迁移。
播放只读取冻结样本，不访问文件或源世界；变形网格上传与绘制由独立 graphics 卫星消费。

## 变形播放器

`DeformPlayback::prepare(track)` 为每个实例准备独立临时顶点缓冲，共享只读片段；播放器
只能移动，创建另一实例需显式准备。时钟与刚体/碎块共用，支持跳转、暂停、变速、倒放和循环。
采样先在私有缓冲完成插值和实例旋转/平移，全部成功后才提交到调用方缓冲；即使后面的顶点
变换溢出，调用方整帧数据也保持不变。成功采样无分配、IO 或 solver，实例间无可变共享状态。
平面轨道拒绝离开 XY 的变换。采样需独占该实例；不同实例可分别使用独立输出缓冲。

### 粒子轨迹核心

`ParticleTrack` 存储平面像素粒子的独立出生轨迹，身份为发射器会话 PersistentId 与非零
birthSerial；同一身份不得重复。每条轨迹至少两个有序关键帧，首尾分别表示包含的出生时间
和不包含的死亡时间，允许事件落在配方采样 tick 之间。生成者必须提供实际事件边界；
仅从稀疏快照猜测事件时间不能声称精确录制。片段显式时长可覆盖无粒子的静默区间。

采样不运行模拟、不分配内存。位置、尺寸、未折返角度及 RGBA 线性插值，UV 保持前一关键帧；
不同身份之间绝不插值。调用者准备 capacity() 大小的输出，采样返回实际可见数量。
非法时间或缓冲不足时输出保持原样。输入与资源引用不被保留，片段复制共享只读数据。
持久化使用下述公共资产协议；CPU 事件捕获由 physics_bake_particle_capture 适配，表现消费由 physics_bake_graphics 提供。

粒子轨迹可使用 `save/load/loadForRecipe` 保存和重载。`EVEPART1` v1 为小端：
8 字节 magic、uint32 版本、float 时长、uint32 出生数、uint32 配方字节数、规范配方；
每条轨迹包含 16 字节 emitter UUID、uint64 birthSerial、uint32 key 数，以及每 key 的时间和
13 个外观 float，最后附 32 字节整文件 contentKey。校验检测意外损坏，不是安全认证。
未知版本和额外字节拒绝；没有历史版本需要迁移。恢复完成全部校验后才返回共享片段。
保存使用公共不覆盖发布机制，其耐久性以该机制的平台合同为准。

`BakedParticlePlayback::prepare` 为每个实例准备独立时钟和 scratch 缓冲，片段数据保持共享只读。
暂停、速率、循环和 seek 复用统一 BakePlaybackClock；显式 elapsedSeconds 是唯一推进来源。
实例 placement 只接受 XY 平面刚体变换，围绕烘焙原点变换 sprite 中心并累加角度。
采样不推进时间，不分配成功路径缓冲；非法输出或变换溢出保持整个调用者缓冲不变。
粒子绘制资源由 physics_bake_graphics 显式按 emitter 身份绑定；physics_bake_script 提供独立 VM 播放器和拥有数据的快照。完整脚本录制与批量表现见 [physics_bake_particle_script](physics_bake_particle_script.md)、[physics_bake_graphics_script](physics_bake_graphics_script.md) 和 examples/physics-bake-replay。

## 有界变形录制会话

`DeformRecording::prepare(recipe, sampleBudget)` 冻结完整配方并创建空会话，显式总顶点样本预算
为 1 到 2000000；该阶段成本为有界元数据复制。会话是 move-only、owner-thread 独占的值帧 builder，
没有世界、布料、绳索、VM 或临时输入指针，不进行隐式物理更新。

`append(frame)` 消费一个拥有数据的候选，和 DeformTrack 使用同一逐帧 admission：固定拓扑的
精确连接/UV 比较、有限坐标、平面约束、严格递增且符合配方的固定采样时间、顶点数和总预算。
任何拒绝保持计数与已录帧不变；成功后会话只保留自己的帧和共享不可变拓扑。每次检查成本线性于
候选顶点/连接，帧表增长摊销，不重新验证或复制全部历史帧。

`freeze()` 线性复制并校验所有已录帧，生成独立不可变 DeformTrack；空会话失败，失败不改变会话。
此操作放在资产发布阶段，不能作为逐帧查询。会话仍可继续追加，旧片段不改变；之后使用原有 save
和公共原子发布机制持久化，未新增文件格式。frameCount/sampleCount 是常数成本统计。
捕获模块仍负责在模拟 owner thread 把实际布料/绳索数据复制成 DeformFrame，再交给该会话。

## 有界刚体录制会话

`RigidRecording::prepare(recipe, sampleBudget)` 从完整配方确定像素平面或米制三维坐标，
冻结配方并设置 1..2000000 总姿态样本预算。会话没有 body/world/VM 指针或运行时 handle。
第一帧建立最多 16384 个稳定 PersistentId 的固定 roster；后续 append 可接受不同输入顺序，
但缺失、增加、重复和 nil 身份均拒绝。候选拥有自己的值，排序和四元数归一化不会修改调用者或已录帧。

append 使用与 RigidPoseTrack 相同的逐帧身份/有限数/坐标/旋转校验，另验证下一个配方时间和剩余预算。
失败不追加半帧、不改变 frameCount/sampleCount。每帧成本为线性姿态验证加身份排序，
没有历史帧的重复复制；输入构造与实际世界捕获由录制调用者显式完成。

freeze 复制并冻结当前记录，成本线性于样本加每帧排序，应在发布阶段调用。会话可继续录制，
已有轨迹和已启动播放器不受影响。空会话拒绝冻结；保存仍使用原有轨迹格式和公共发布合同。

## 完整模块裁剪验收

根构建的 `physics-bake-only` 配置选择 `common`、`physics_bake` 与 `physics_bake_collapse`；foundation 仍有其
声明的 Squirrel 依赖，但不创建 VM，不链接实时物理、粒子、图形或录制/表现卫星。
配置禁止显式启用这些额外模块。构建与 smoke 命令见
[profile 说明](../../../test/profile/physics_bake/README.md#whole-manifest-module-profile)。

Linux OBJECT 与 SHARED 路径已验证完整模块源码编译（L1）、对象/分组库链接（L2，OBJECT 无 section GC）、
破坏、刚体、变形和粒子通道持久化/采样运行（L3）和预期采样值（L4）。真实房屋资产的
507 个碎块、961 帧也在两种链接方式下完成双实例正反向采样；前轮已加载真实刚体、布料、绳索、粒子文件。
破坏纯播放实现归属 `physics_bake_collapse`，无需实时 `physics_destruction`。
该范围不包含脚本/渲染裁剪组合或其他平台。
