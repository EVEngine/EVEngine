# 海洋海况与表面查询

海洋类型由 `fluids` 模块注册。谱定义与空间帧均由脚本对象独占包装，GC 释放；
空间帧拥有结果数组，frame builder 持有不可变系数的共享所有权。
创建后释放原 spectrum 不影响已创建的 builder 或空间帧。全部 VM 调用在 VM 所属线程执行。

| 调用 | 行为与成本 |
| --- | --- |
| `eve.OceanSpectrumBuilder()` | 创建轻量配置 builder |
| `builder.settings()` | Result，返回完整且独立的配置表 |
| `builder.configure(settings)` | Result，结构验证后原子替换配置；不生成谱 |
| `builder.build()` | Result，验证数值域并生成不可变 `OceanSpectrum`；每次海况构建一次，成本随各频段格数增长 |
| `spectrum.variance()` | 返回期望波高方差，常量时间 |
| `spectrum.frameBuilder(tick, rate)` | Result，创建拥有系数引用及原生 double 时间 tick/rate 的轻量 builder |
| `frameBuilder.build()` | Result，六场 CPU FFT，成本 O(sum N² log N)；每仿真步生成一次供多消费者共享 |
| `frame.time()` | 返回该帧仿真秒数，不读取墙上时钟 |
| `frame.sample(x,z)` | Result，返回世界高度 `height`、三元素 `normal`、`velocity`；不能反求的折叠水面显式失败 |

构建返回对象的类型不支持直接构造；只通过对应 builder 取得。
所有 Result 均须检查 `ok` 后访问 `value`。波高和坐标以米计，时间以秒计。
时间输入使用非负整数 tick 与整数频率 rate（30..1000 Hz），原生计算 tick/rate，
支持超过 32 位的 tick，拒绝超过十亿秒的时间；不累积脚本 float 时钟。
`frame.time()` 是便于显示的 VM 浮点投影，不可用它推进仿真。
当前查询坐标参数仍为单精度脚本数值；原生 double 大坐标渲染证据不代表
此查询接口能保留超大绝对坐标的亚米偏移。

```squirrel
function oceanChecked(result) {
    if (!result.ok) throw "Ocean operation failed";
    return result.hasValue ? result.value : null;
}
local builder = eve.OceanSpectrumBuilder();
local settings = oceanChecked(builder.settings());
settings.seed = "18446744073709551615";
settings.significantWaveHeight = 2.0;
oceanChecked(builder.configure(settings));
local sea = oceanChecked(builder.build());
local pending = oceanChecked(sea.frameBuilder(75, 60));
local frame = oceanChecked(pending.build());
local surface = oceanChecked(frame.sample(0.0, 0.0));
```

配置使用 `schema = "eve.ocean.spectrum"`、整数 `version = 1`。
必须保留 settings 返回的完整字段，包括最多四个频段；未知字段、缺失字段及未知版本均拒绝。
`seed` 必须是无符号 64 位范围内的规范十进制文本，不接受数值、前导零、符号或空白。
这样 JSON 与脚本 VM 往返不会丢失随机种子的高位。
版本 1 没有旧版本迁移；未来迁移必须显式实现。配置定义不是运行时尾浪或船体存档。

## 船体与局部尾浪

| 调用 | 行为与成本 |
| --- | --- |
| `eve.OceanWakeBuilder()` / `eve.OceanBoatBuilder()` | 轻量准备对象 |
| `settings()` / `configure(settings)` | Result；独立完整配置，结构错误拒绝且保留原配置 |
| `wakeBuilder.build()` | Result，分配 owning OceanWake，O(N²) 存储；构建一次后复用 |
| `boatBuilder.build(body)` | Result，绑定有效动态 Body3D，复制最多 64 个船体排水单元；仅保存 PhysicsLink |
| `boat.apply(frame, wake, rate, throttle, rudder)` | Result，返回 owning OceanBoatReceipt；同步提交浮力、阻力、推进和舵力，wake 可显式传 null |
| `receipt.summary()` | Result，独立 time/totalForce/submergedVolume/contacts 摘要；原始接触由 receipt 拥有 |
| `wake.injectReceipt(receipt, rate)` | Result，以同一步的接触历史注入尾浪/泡沫，O(N² × 接触数) |
| `wake.prepareStep(frame, endTick, rate)` | Result，返回独立拥有的 OceanWakeStep；不修改原状态，O(N² log N + N² × 水面查询成本) |
| `wake.commitStep(step)` | Result，常数时间交换候选状态；拒绝其他 owner、已提交或被后续修改失效的候选 |
| `wake.recenter(x,z)` | Result，按整数格移动局部窗口，保留重叠区域历史，O(N²) |
| `wake.sample(x,z)` | Result，局部扰动 height/velocity/gradient/foam；窗口外为零 |
| `wake.sampleSurface(frame,x,z)` | Result，同时间戳的完整波谱与尾浪组合水面 |

wake builder 的 settings 含 resolution、length、depth、gravity、damping、absorptionWidth、
foamDecay 和二维 center。boat builder 的 settings 含 cells、waterDensity、gravity、drag、
propeller、thrust、rudderCoefficient；每个 cell 为 `{center=[x,y,z], radius=..., volume=...}`。
默认 cells 为空，必须按真实船体设置，单位为米与立方米。上述参数表是临时构造参数，
不作为持久检查点或旧版本兼容格式。builder 在 build 时由原生算法统一验证物理数值域。

船体局部 +Z 向前、+X 向右、+Y 向上；throttle 和 rudder 为 [-1,1]。
World3D 始终拥有 Body3D，GC 释放适配器不会删除船体。先销毁 body/world 后再 apply 返回
StaleHandle；恢复世界或热重载后必须以新 body 重建适配器，不能保存进程内 Link。
谱帧、wake 和 receipt 参数只在本次调用中借用；没有持久保存 OceanWaterView 的脚本对象。

同一个固定步必须按以下顺序执行，所有船共享同一 rate、tick 和水面：

1. 用 `sea.frameBuilder(tick, rate).value.build()` 生成并检查当前谱帧。
2. 对所有船执行 apply，收集 receipt；每条船每个时间戳只能 apply 一次。
3. 收集完所有船之后，逐一 injectReceipt；避免后执行的船提前读取本步新扰动。
4. 先 prepareStep(frame,tick+1,rate) 并检查；再 World3D.update(1.0/rate)，最后 commitStep(step)。
5. 渲染下一步前创建匹配 tick+1 的谱帧，保持尾浪和海面的时间一致。

泡沫 coverage 使用与浮力一致的谱帧水平粒子速度（含海流）做中点回溯，
以有界双线性采样输运，然后施加配置衰减和边缘吸收。窗口边界开放，不周期卷回。
这是有数值耗散的覆盖率输运，不声称守恒流体质量；局部尾浪当前只贡献垂直速度。
谱帧过期或任一水面反求失败时，整步高度、速度、泡沫和时间均保持不变。

步进错误不会被吞掉，也不会自动跳过或偷偷更换后端。渲染脚本接口见下文。

OceanWakeStep 拥有候选数组，不借用源谱帧或原 wake，可安全晚于两者销毁。
同一个 owner 的任意成功注入、滚动或提交都会使旧候选失效。失败的提交不消费候选；
成功后不可再次提交。旧数组在已消费候选释放时回收，提交阶段不分配或执行查询。

## GPU 海面绘制

| 调用 | 行为与成本 |
| --- | --- |
| `eve.OceanGpuSpectrumBuilder().build(sea,gfx,gpgpu)` | Result，准备后显式创建同设备频谱投影，O(sum N²) 设备分配和 shader/pipeline 创建 |
| `gpu.frameBuilder(tick,rate)` | Result，轻量提交准备对象，持有原生频谱投影的共享所有权 |
| `gpuFrameBuilder.submit()` | Result，GPU FFT 并返回 owning OceanGpuFrameToken，O(sum N² log N)；每渲染时间点一次 |
| `token.time()` | 显示用时间投影；token 不延长设备寿命 |
| `eve.OceanRendererBuilder()` | 默认 gpu，轻量质量准备对象 |
| `renderBuilder.settings()` / `configure(settings)` | Result；完整 segments/rings/firstRadius/outerRadius/backend 参数表，backend 只接受 gpu 或 cpu |
| `renderBuilder.build(gfx)` | Result，创建拥有型 OceanRenderer，分配固定拓扑和 shader；复用到质量改变 |
| `renderer.hullMaskBuilder(config)` | Result，复制严格遮水配置并持有 renderer 原生所有权；不保留脚本输入引用 |
| `hullMaskPublication.publish()` | Result，构建半空间并同步原子上传；配置释放或修改不影响已准备发布 |
| `renderer.gpuFrameBuilder(gpu,token,wake,placement)` | Result，轻量发布准备；持有投影/renderer 的原生所有权、token 值及 wake 的 GC 引用，wake 可为 null |
| `renderer.frameBuilder(frame,wake,placement)` | Result，CPU 参考发布准备；持有 frame/wake 的 GC 引用，wake 可为 null |
| `renderFrameBuilder.publish()` | Result；GPU 路径同设备复制频谱 atlas 并上传局部尾浪，不读回谱；CPU 参考路径生成并上传顶点 |
| `renderer.draw()` | Result，在已建立相机的当前 3D pass 中绘制已发布帧 |

placement 必须为完整 `{center=[x,z], origin=[x,y,z]}`。原生侧以 double 求相位/相对原点；
脚本传入的 float 已丢失的精度无法恢复，超大坐标应传整数基准加独立相对偏移的后续接口。
当前示例在小坐标真实船体仿真，广域海面网格独立覆盖 40 公里。

所有调用限 VM/render 所在线程。构建器通过拥有型引用保持输入存活，
不缓存跨帧 OceanWaterView 或设备 buffer 裸指针；publish 当次构造借用视图并重新核验时间和 token。
修改 wake 或再次 submit 之后旧准备对象可能失败；必须检查 Result，失败保留原发布画面。
共享投影不延长 Graphics provider 生命周期，provider retirement 仍使后续操作失效。
GPU 后端不支持时显式失败，调用方必须明确选用 CPU 参考路径，禁止静默 fallback。

可运行的完整调用顺序见 [Ocean Voyage](../../examples/ocean-voyage/README.md)。

## 三维飞沫数值状态（原生接口）

`OceanSprayFrame::create(settings)` 创建空状态。每个固定步通过
`OceanSprayStepBuilder(previous, water, sources, dt).build()` 构造下一帧，
检查 Result 后再替换应用持有的当前帧。昂贵计算全部发生在 build；失败不修改输入。

每个 source 包含非零稳定 ID 和本步 `OceanBoatReceipt` 的同步借用。最多 32 个源、
每源最多 64 个接触；ID 重复、时间不匹配或非有限输入均拒绝。源遍历顺序不改变结果。
`particles()` 是只读借用，随帧销毁、移动或替换失效；包含世界三维位置、速度、半径、
年龄、寿命和流内 ID。渲染只能投影这些值，不应再次推进轨迹。

settings 包含 seed、capacity、gravity、lifetime、radius 和 emissionPerVolumeSpeed。
`report()` 返回本步 emitted、dropped、expired、returnedToWater。容量不足会计数丢弃，
不积累待发射债务。落回水面采用步首高度近似，飞沫不向水体或船只反向提交动量。
脚本与绘制接口见下表；实际后端与场景画面需要分别验证。

## 飞沫脚本与批量绘制

| 接口 | 行为与成本 |
| --- | --- |
| `eve.OceanSprayBuilder()` | 廉价构造参数；settings/configure/build，build 返回 OceanSprayFrame |
| `spray.stepBuilder(frame,wake,rate)` | 廉价准备；拥有旧飞沫帧并 root 谱帧/尾浪；wake 可传 null |
| `step.addReceipt(id,receipt)` | 拷贝最多 64 接触；最多 32 个正整数源 ID，重复 ID 拒绝 |
| `step.build()` | 昂贵线性轨迹推进/水面查询，返回独立下一帧；不修改旧帧 |
| `spray.summary()` | owning time/alive/emitted/dropped/expired/returnedToWater 观测投影 |
| `eve.OceanSprayRendererBuilder().build(gfx)` | 创建 pipeline 和双缓冲网格，摊销到 renderer 生命周期 |
| `renderer.frameBuilder(spray,view)` | 廉价准备，拥有 renderer 与不可变喷溅帧的共享租约 |
| `publication.publish()` | 深度排序、生成四边形并上传备用网格，成功后切换；O(N log N) |
| `renderer.draw()` | 当前 3D pass 中一次透明 batch，深度测试、不写深度；在水与船之后调用 |

view 必须包含 eye、forward、up、origin 四个三维数组，forward/up 不得平行。
先以 double 减去 render origin，再转换 float；相机与其他绘制必须采用相同原点。
原生保留 double 世界精度，脚本坐标受 VM float 精度限制。透明液滴仅在当前 batch 内排序，
不承诺与外部透明物体的全局排序或软粒子深度淡入。GLSL/WGSL 均有对应源码，
Vulkan 与 WebGPU 运行证据分别记录，不能以 WGSL 文件存在替代后端验证。

固定步中先构造并检查 nextSpray 和尾浪候选，再更新 World3D、提交尾浪、发布 nextSpray。
脚本 settings 是临时构造参数；seed 使用规范 uint64 十进制字符串，并非动态持久检查点。

## 船体内部遮水的原生接口

`OceanHullVolume` 描述随船移动的有限凸体：中心、单位四元数、三个正半尺寸，
以及最多十个局部斜切面 `dot(normal, localPoint) <= offset`。边界视为内部。
六个包围盒面保证体积有限，额外面可定义斜舷或船首；多体积取并集，最多 32 个。
互相矛盾的斜切面形成空体积，不会扩大遮水范围。它仅裁除海面片元，不改变浮力查询。

`OceanHullMaskBuilder(volumes)` 借用输入到 `build()` 返回，配置 `origin` 后构建拥有型帧。
先在双精度世界中减原点，再编码渲染半空间；中心距原点每轴最多 1e6 米。
分类采用 float，与材质一致，容差随原点距离增大；近船精度应通过跟随原点保持。
帧不保存船体句柄，也不自行推进姿态；每次船姿变化由权威刚体重新投影。
`renderer.hullMaskBuilder(frame).publish()` 同步上传候选并原子切换，空帧清除遮水。
绘制时遮水与海面的原点必须一致，否则返回 Conflict，防止原点切换时错误挖洞。
脚本配置严格包含 `origin:[x,y,z]` 和 `volumes`；每个体积严格包含 `center`、
`rotation:[x,y,z,w]`、`halfExtent`、`bevels`，每个 bevel 是 `[nx,ny,nz,offset]`。
未知字段和错误尺寸拒绝；几何语义在 publish 时验证。配置是临时调用参数，非持久格式。
`renderer.hullMaskBuilder(config)` 复制配置并持有 renderer lease，输入或 renderer 脚本引用
释放后仍可 publish；Graphics 生命周期不延长。原生大坐标采用 double，脚本受 SQFloat 精度限制。
航行示例从同一船壳三角形计算十个斜切面，向内收 1 厘米以保护外侧可见水面，
使用原生刚体四元数和位置逐 tick 投影；不独立积分姿态。GLSL 已有像素证据，WGSL 运行仍待验证。

## 海况过渡数值核心

`OceanSeaTransitionBuilder(from,to)` 立即持有两个不可变谱的 owning lease，
设置 `start` 和 `duration` 后 `build()` 验证并产生不可变过渡。
`transition.frameBuilder(time).build()` 返回普通 OceanSpectrumFrame，
可直接用于既有 OceanWaterView、浮力、尾浪与 CPU 参考渲染，不增加第二个时钟。
过渡外只计算对应端点，区间内用五次平滑权重，端点的一阶、二阶权重导数为零。
速度包含 `w'(toDisplacement-fromDisplacement)` 和平均海平面变化率，
不只插值两个速度。输入可以改变浪高、周期、风向、相位 seed、choppiness 和海平面；
当前要求频段分辨率/长度、重力和洋流一致，其他组合显式拒绝。
这是有界海况投影，不表示风场或能量守恒演化；独立相位端点的中途方差可能下降。

`OceanGpuSpectrumBuilder(transition, graphics, compute)` 接受同一不可变过渡计划，
拥有两个端点的 lease。每个频率先计算两端点位移/速度并加入权重导数项，
再共用一组六场 IFFT；只在 16 秒相位重基时上传系数，不逐帧读回或上传顶点。
返回的 GPU projection/token/atlas 与普通海况相同，海平面元数据随过渡更新。
静态谱仍只分配单份系数，过渡谱分配两份。脚本和航行示例过渡接入仍待完成。
非零洋流下仍沿用既有谱粒子速度契约；低分辨率空间插值与谱平流的导数有离散差异，
需要在整系统速度/采样精度验收中单独解决，不以过渡测试通过代替该项验收。
