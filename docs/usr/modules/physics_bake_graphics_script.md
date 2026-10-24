# Physics Bake Graphics Script

可选 L4 VM 表现桥；依赖 graphics、physics_bake、原生 graphics adapter 与 playback VM。
纯播放和原生表现模块不反向依赖该模块。

## module-interface

- 原生面：PhysicsBakeGraphics 注册，刚体委托 submitRigidPlayback，变形委托 DeformMeshPresentation，粒子委托 ParticleBakePresentation；复用原生播放器与时钟实现。
- Capability：无 provider 或查询。
- ECS/Link：只在提交调用期间借用玩家和已解析的 live Renderable，不建立持久链接。
- 事件：无回调、锁或隐式物理更新。
- 脚本：eve.PhysicsBakeGraphics / eve.physicsBakeGraphics，submitRigid2D/submitRigid3D、prepareDeform、prepareParticles 与两类表现对象。
- 工具协议：无。完整模块裁剪与窗口表现仍须独立验收。

`submitRigid2D(player, uuid, renderable2D)` / `submitRigid3D(player, uuid, renderable3D)`
接受原生 BakedRigidPlayback、稳定 PersistentId 字符串和对应的 live 渲染对象，返回 checked Result。
像素平面和米制三维不可混用；错误类型、空值、非法/未知身份或采样失败保留目标原状态。
操作不推进玩家时间，不进行模拟、不创建实体或 GPU 资源。费用为身份与时间键查找加单对象变换，
适用于逐帧调用；无需将姿态先投影为脚本表。玩家的原生时间/placement 仍是唯一状态来源。

在 VM/render owner thread 调用，排除其他目标 transform 写入者。调用者负责保证本次目标存活；
提交不延长对象寿命，也不跨帧保留裸指针。源对象或目标销毁后无需解绑；下一次提交须传入新解析的目标。
2D 保留 sprite/scale，3D 提交世界矩阵并保留网格尺度；随后调用目标 TRS setter 会替换矩阵。
刚体提交不分配资源；布料与绳索使用下面的显式准备和网格上传入口。

## 布料与绳索的批量表现

`prepareDeform(player, ropeRadius)` 从 BakedDeformPlayback 的不可变片段准备
BakedDeformPresentation，返回 checked Result。准备成本随输出顶点和索引线性增长，
一次分配可重复使用的 CPU 缓冲；三角形输出双面表面，线段输出带端盖的八边形管。
仅接受米制三维片段；绳索半径必须为有限正数。它共享片段，拥有独立的播放器，
初始时间为零、速率为一、未暂停、Clamp 和单位 placement，不复制输入播放器的当前状态。
输入播放器随后释放不影响表现对象。

表现对象直接提供 `advance/seek/setRate/setLoop/setPaused/paused/time/rate/duration/setPlacement`，
语义与 BakedDeformPlayback 相同；不暴露可能超出 owner 寿命的内部播放器指针。
`createMesh(graphics)` 是一次 GPU 分配及全量上传，返回 checked Result，value 是
**Graphics 所有**的借用 Mesh，ownership 为 `graphics-owned`。
`upload(graphics, mesh)` 在线性顶点/索引成本下复用 CPU 缓冲并上传，没有脚本逐顶点数组，
不隐式推进时间；空值、错误类型、foreign/released mesh 返回失败。
采样失败不写入 GPU；后端上传失败后必须跳过该网格的绘制，直到下一次上传成功。

表现对象不保留 Graphics/Mesh/源仿真指针。释放表现对象后，网格保留最后上传的几何；
丢弃脚本 Mesh 引用不会释放 Graphics 的 GPU 资源。图形宿主必须活到每次网格调用结束，
其销毁或原生 releaseMesh 后不得继续使用旧脚本网格句柄。成功 releaseMesh 转移 CPU facade
删除责任给原生调用者；本桥不添加第二套资源 owner 或隐式析构回调。
存盘/恢复仍通过片段资产，恢复后显式重新 prepare/createMesh。该接口不新增 ECS System、
持久 Link、事件或工具协议，采用已知一对一、逐帧、同步 Result 调用；每实例播放器是时间唯一 owner。

图形宿主销毁次序及各平台 GPU 生命周期验收独立跟踪。

## 粒子的批量表现与材质绑定

`prepareParticles(player, emitterIds)` 接受 BakedParticlePlayback 和按 UUID 升序排列、
唯一且非 nil 的发射会话 ID 数组（最多 65536 项），返回 VM 所有的 BakedParticlePresentation。
准备阶段复制身份值、分配按粒子出生数量和材质数量计的原生缓冲，摊销到后续帧；
输入播放器或 ID 数组随后释放、修改不影响表现。其播放器从零时刻和默认控制状态开始，
并提供与变形表现相同的时间/placement 控制方法。没有第二份时间状态或内部借用 player。

`draw(graphics, materialSprites)` 接受与准备 ID 一一对应的 live Renderable2D 数组，
返回 checked Result，其 value 是提交的可见粒子数量。sprite 只提供 texture、normal、
shader、canvas、camera、layer、blend 和 receiveLight；几何、颜色、UV、生灭与身份顺序
由资产决定，sprite 的 visible、变换、trim、quad 和 tint 不参与此次提交。
可将这些材质模板 sprite 设为不可见，避免普通场景收集额外画出模板。
缺省 camera 仍由普通 RenderSystem 选择。每次调用重新读取材质，支持逐帧切换资源和图层。

身份解析发生在准备阶段；draw 复用预留的模板/绑定缓冲，不生成逐粒子的脚本表。
成本为线性材质校验，加原生轨迹采样、可见粒子提交和普通渲染排序/上传。
数组长度、类型或可见 emitter 的材质缺失使整个调用在提交前失败，不允许部分粒子偷偷绘制。
没有可见粒子时返回零。draw 不推进时间、不 present、不访问实时发射器或物理解算器。

所有输入资源只在调用期间借用，临时绑定在返回时清除（包括失败路径）；不保存 VM 数组、
sprite 或 graphics 指针。调用者保证模板存活到本次 draw 返回，模板引用的纹理/着色器/
canvas/camera 必须存活到当前图形帧完成。恢复后重新 prepare，再以 live 材质重建绑定。
资源仍由既有 Graphics/ECS owner 管理，无新持久 Link、provider、事件或工具协议。
该通道继承当前 CPU sprite 录制支持边界；GPU 驻留、ribbon 和相机相关排序不是已验证能力。
