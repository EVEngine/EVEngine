# Physics Bake Script

可选 L4 VM 卫星将 `physics_bake` 的原生配方与播放器投影为 checked Result；
纯数据模块不依赖本模块、Squirrel 或物理解算器。

## module-interface

- 原生面：PhysicsBake 模块注册；没有第二套文件格式或播放时钟。
- Capability：无查询或 provider。
- ECS/Link：不保存实体、世界或 body 指针。
- 事件：无回调、锁或玩法事件派发。
- 脚本：`eve.BakeRecipe`、`eve.PhysicsBake`、`eve.BakedRigidPlayback`、`eve.BakedDeformPlayback`、`eve.BakedDeformSample`、`eve.BakedParticlePlayback`、`eve.BakedParticleSample`，模块槽 `eve.physicsBake`。
- 工具协议：无。Linux 的 physics-bake-script-only 完整 manifest 配置已通过 OBJECT/SHARED
  编译、链接及真实 VM 加载/源销毁/独立播放 probe；其他平台仍需执行验证。

所有调用在 VM owner thread。VM 拥有配方包装器和播放器；包装器只持有一个原生
BakeRecipe，校验与编码全部委托该值。它与旧 CollapseBakeRecipe 原生别名使用不同
VM 类型，因此两种注册顺序均不得覆盖彼此；旧配方不会被通用加载器隐式接纳。

`BakeRecipe` 提供 setCoordinates、setProducer、setBackend、setParameters、setSource、
setTiming、setRandomStream、key。坐标字符串只能是 `metres3d` 或 `pixels2d`。
setTiming 的步长为 float 秒、stride 为正整数；setRandomStream 的 seed 是无符号
64 位十进制字符串，避免 Squirrel 整数宽度和浮点精度损失。所有修改失败保留原值。
生产者必须明确提供真实构建/后端版本、全部依赖、参数和 RNG；本接口不猜测来源。

`eve.physicsBake.loadRigid(path, recipe)` 同步执行有界文件读取和完整身份校验，
返回 `.ok/.hasValue/.value` 中的 VM-owned BakedRigidPlayback；失败不改变任何已有播放器。
成本线性于资产大小，必须在加载阶段调用，不在帧循环反复调用。不会隐式重新烘焙。
播放器共享不可变数据，可在文件和源仿真销毁后继续使用。

播放器提供 copy、advance、seek、setRate、setLoop、setPaused、paused、time、rate、
duration、setPlacement、trackCount、trackId、sample。copy 共享片段、复制独立时钟和变换；
trackId 接受从零开始的索引，返回稳定 UUID 字符串。sample 接受该 UUID，返回
{x,y,z,qx,qy,qz,qw} 值；位置单位取决于资产。setPlacement 使用平移 xyz 和 XYZW 四元数。
sample 的脚本投影分配小表，适合脚本控制/查询；大量对象的逐帧消费应使用原生批量表现接口。

`eve.physicsBake.loadDeform(path, recipe)` 以同样的 checked 身份校验加载布料/绳索通用变形轨迹，
返回 VM-owned BakedDeformPlayback；加载成本线性于资产大小并准备顶点 scratch，放在加载阶段。
玩家提供 advance、seek、setRate、setLoop、setPaused、paused、time、rate、duration 和 setPlacement，
语义同原生 DeformPlayback，像素平面轨迹拒绝离开 XY 平面的 placement。

`prepareInstance()` 为同一不可变片段准备另一个播放器及独立 scratch，成本线性于顶点数，
应在实例创建时调用。新实例从 time=0、rate=1、未暂停、默认变换开始，不复制当前时间状态。
`captureSample()` 在当前显式时间采样并应用实例变换，返回拥有顶点和共享只读拓扑的
BakedDeformSample；成本为线性采样及顶点缓冲分配，每次调用产生独立快照，不能误当作廉价查询。
失败不发布部分帧、不推进时钟。批量逐帧渲染应使用原生 DeformMeshPresentation 的可复用缓冲；
脚本快照适合检查、工具和少量数据消费，不承担实时物理解算。

快照提供 vertexCount、indexCount、primitive（triangles/segments）、topologyKey、hasUV；
point(index) 返回 checked {x,y,z}，index(index) 返回 checked 顶点索引，uv(index) 返回 checked {u,v}。
索引从零开始，越界或无 UV 返回诊断。这些读取是常数成本（返回小值表会分配），
不会重新采样整个轨迹。原始文件、播放器或其他快照销毁，以及后续 seek/placement 不会改变已有快照。

`eve.physicsBake.loadParticles(path, recipe)` 经原生 ParticleTrack::loadForRecipe 校验，返回
BakedParticlePlayback；目前粒子轨迹是像素平面 sprite 数据。加载成本线性于资产大小并准备
按总出生数计的 scratch，不能在帧循环重复加载。其时间/变换控制和 prepareInstance 语义与变形播放器相同；
prepareInstance 成本线性于总出生数，新实例从默认状态开始。

粒子 `captureSample()` 分配总出生数容量并采样当前可见粒子，返回 BakedParticleSample；
采样成本线性于总出生数，并对可见轨迹查找时间键。快照的 count 返回可见数量，particle(index)
以常数成本返回 checked 数值表：emitter（UUID）、birthSerial（完整 uint64 十进制字符串）、
x/y/w/h（像素）、rotation（未折叠角度）、r/g/b/a、u0/v0/u1/v1。禁止将 birthSerial 转成浮点数充当身份。
索引为快照的可见顺序，不是跨帧身份；身份由 emitter 与 birthSerial 共同定义。出生包含、死亡排除，
末尾及无粒子区间得到合法空快照；UV 保持前一关键帧，不在图集帧之间插值。
帧拥有独立数据，文件和播放器销毁后仍有效；纹理、shader 等资源没有写入数值快照，仍由表现端显式绑定。
逐帧批量绘制应使用原生 ParticleBakePresentation 的可复用缓冲，不能把昂贵脚本快照当作廉价查询。

当前接口覆盖配方、刚体、固定拓扑变形和粒子资产重放与数值采样。2D/3D 刚体及 Cloth3D/Rope3D 录制由独立的
physics_bake_recording_script 提供；粒子日志与多源归档由 physics_bake_particle_script 提供；脚本到原生批量表现由 physics_bake_graphics_script 提供，
不能用本模块存在来声明所有通道的脚本闭环已完成。

## 生产构建身份

BakeRecipe.producerBuildRevision() 和 box3DBackendRevision() 提供当前构建生成的身份字符串，
与 CollapseBakeRecipe 使用同一份生成机制；通用脚本模块不要求链接 destruction 模块。
生产指纹包含 physics、particles、引擎/脚本、相关资产/schema 源码、依赖补丁及编译配置。
调用者仍须填写实际参数、源内容、固定步长/采样间隔与所有命名 RNG 种子，不能仅凭构建指纹
宣称完整缓存身份。配置查询不读磁盘、不启动 solver；实例重放不要求当前 producer 与资产相同，
loadForRecipe 风格的准入才显式比较预期配方。


## 无实时物理的 VM 裁剪验收

根 profile `physics-bake-script-only` 选择 common、physics_bake、physics_bake_collapse 与
physics_bake_script，链接完整模块实现，禁止额外模块进入闭包。真实 VM 探针验证配方准入、
刚体/变形/粒子加载、独立实例和 GC 后的 owning 快照，运行中没有物理世界或渲染器。
命令见 [profile 说明](../../../test/profile/physics_bake/README.md#whole-manifest-script-playback-profile)。
Linux 两种链接方式的终态证据以实施记录为准；该配置不包含旧碎块脚本注册、录制和图形绑定，
不能用它代替这些组合或其他平台的验收。
