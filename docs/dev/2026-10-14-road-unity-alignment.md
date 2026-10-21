# Unity / EasyRoads 道路实测对齐

日期：2026-10-14；Unity 6000.0.79f1 许可证已实测恢复。参考为已审计的外层 EasyRoads 包，未安装 Beta/SRP 覆盖。

## 已执行范围

八个真实双端场景：直路、曲线、坡道、高架驾驶面、T/X 路口、逐点宽度变化与固定 15 度中心横坡。
每个场景使用透视和俯视两种相机，1280×720、42 度垂直视角；共 16 对引擎自身 readback 图像。
这是驾驶面轮廓校准，尚不能代表桥梁壳体、连续 prefab、地形通道、孔口、材质和光照的双端功能验收。
既有这些功能的 CPU 合约测试继续运行，不把原来的候选侧图像提升为 Unity 通过证据。

## 实测发现与修复

- 相机/坐标手性：EVEngine 使用 lookAtRH；Unity Transform 使用左手坐标。参考输入和相机统一反射 Z，俯视 up 同步反射。首轮曲线镜像来自适配，未修改核心道路几何来掩盖它。
- 编辑模式：删除仅用于碰撞开关的 SetMeshCollider(false) 调用，避免外层 DLL 在编辑模式使用 Destroy 导致报错。物理碰撞不属于该轮廓实验。
- T prefab 朝向：读取 GetConnectionWorldPositions 并用 SetRotation 校准四种直角朝向。源参考 T 需要 90 度旋转；失配朝向曾令首轮连接停滞。
- 参考非路面网格：surfaceMaterial 的地形/路口 surround 填充包含地下裙边，不能染成沥青计算轮廓；只排除这个实测材质，保留真实 crossing material 和实际生成 road mesh。
- 陈旧候选：首次比较使用早期 mesh fixture，其端点已经与当前代码不同；改为当前 native generator 数据，严格核验 fixture、投影、adapter 和图片 SHA-256。
- 路口半径：测得 Default T/X prefab 端口距中心 5.5 米。校准夹具显式设置 RoadNode.junctionRadius=5.5，不修改引擎默认半径来迁就某个 prefab。
- T 内侧圆角：核心把向外 socket 射线的正向交点当作内侧圆角，导致真实内侧交点被拒绝、填出直线斜边。修正为双方射线后方的有限交点；新增点覆盖回归证明斜边多余区域不再可行驶。
- 宽度插值：参考导出的实际顶点与 Catmull–Rom 属性公式一致（抽样误差约 1e-4 米），与线性属性插值不同。新增明确策略，宽度轮廓从约 96% 提高到约 99.98%。新道路默认 cubic，旧快照迁移保留 Linear；通用样条默认仍 Linear。

## 作者与持久化契约

RoadStyle.widthInterpolation 为唯一道路策略，Linear=0、CatmullRom=1。SplinePoint.scaleXInterpolation
是样条自有值；道路只在构造临时样条时投影策略，导航、标线、设施、端口和 spline 参数拆分复用同一采样。
横坡仍线性插值。Cubic 正常允许 overshoot，极端输入保持正值和有限 float；插入节点仍受原作者范围检查。
通用 spline 的 outgoing span 在 catmullRom 模式下才启用该属性算法，其他中心线种类仍使用原线性属性规则。
路口宽度预算用 max + 0.125*(max-min) 保守包含 cubic overshoot；恒定宽度不扩大裁切。
旧 eve.procgen.roadNetwork schemaVersion=1 缺失 widthInterpolation 时恢复 Linear；未知枚举值原子拒绝，
capture 保存策略，reverse/split 的 owning style 继承策略，事务补偿使用原 style。
分段后 Catmull–Rom 邻点会改变，整条剖面不承诺与拆分前位级相同；拆分点采样值和撤销原始作者数据有明确测试。

新增 policy 没有脚本 binding；已有 script 点构造保持默认 Linear。没有 provider、事件、ECS 系统、时间或 RNG。
适用 Result/nodiscard、单一 owner、线程/重入、持久化迁移、六面模块声明与显式 bake 成本规则，无架构例外。
CPU 闭包 STATIC/SHARED 与正式 procgen-core-only OBJECT 验证 L1/L2，不宣称完整引擎 L3/L4 裁剪。

## 当前实帧结果与复现

最终 comparison.json 的八类场景均达到固定 silhouette IoU >=0.98，没有降低阈值：
直路/高架俯视 1.0；坡道俯视约 0.99998；横坡俯视约 0.99988；
宽度变化俯视约 0.99981；T 俯视 0.99418；X 俯视 0.98733；曲线俯视 0.99006。
仍可观察到采样分辨率、曲线和路口细节差异，阈值通过不等于完全相同。

运行产物均在 git-ignored build/road-parity/fixtures/<case>/：fixture.json、双端 PNG、difference PNG、
双端 evidence.json 和转换回 EVEngine 坐标的 unity-positions.json。Unity 还保存 Assets/RoadParityScenes 的真实参考场景。
Unity 图像由 Camera.Render/RenderTexture 捕获；EVEngine 为实际 presented-frame readback，使用已有 renderer 上传当前核心生成的网格。
最早未校准的证据保留于 first-calibration 与 unity-first-capture.log，不用于最终通过判断。

依次构建 native 夹具，运行 road_parity_fixture.exe build/road-parity/fixtures，执行 road_parity.py prepare、
render-unity、render-ev、compare。render-ev --case 可只重跑发生改变的场景。README 记录完整命令。
参考投影或 C# adapter 改变会使旧 Unity evidence 失效；新增测试覆盖此规则和手性转换。

下一阶段保留项：任意截面、真实桥隧壳体、prefab 自动间距/平滑组、连续设施的多臂衔接、
地形高度/splat/detail/tree/hole 的真实 Unity 写回与首尾洞口，及材质/光照、UV、碰撞和交通语义。


本轮 CPU 合约回归：STATIC/SHARED 各 30 项、6,102 断言，无失败或警告；新增 T 内侧
圆角覆盖、generic Linear/cubic 分离、非法策略、旧快照迁移与 spline 参数拆分用例。
正式 procgen-core-only/OBJECT 编译闭包通过。对照工具 10 项测试通过，包括 DLL、实际顶点、
投影、adapter 和图片变化导致陈旧证据拒绝。候选数据由当前 C++ 生成器产生；完整 renderer 未重编译。
默认 make check 的既有 IStateProvider.h 三项 bool API 和 RenderSystem.h 两项生命周期注释违规仍阻断；
本轮未修改这两个文件，没有增加 baseline/allowlist，完整原始结果位于 reference-quality-default.log。

增量 make check CI_BASE=HEAD 完整通过（266 项脚本测试，251.807 秒）；随后增加的两项
参考顶点/DLL 身份守卫另单独运行对照工具全部 10 项，并复核 Ruff、最终架构及改动行格式。
扩大 CPU 道路集合 55 项通过、4 项既有断言警告、0 项失败（8,103 项通过断言），不计作全套绿色。
最新完整功能夹具在 alignment-functional-scenes；宽度策略影响的 prefab-deformation 与
prefab-auto-spacing 已重新取得 EVEngine 实帧和输入哈希。这两类仍没有对应 Unity 功能验收。

## 第二批：新增八类场景（2026-10-14）

新增 S 弯、坡顶、凹谷、弯道上坡、变化横坡、负横坡、弯道变宽、弯道变宽叠加横坡。
每类均保存 Unity 真实场景、参考顶点和双端透视/俯视帧；合计 16 类、32 对图像。
沿用 0.98 阈值和全部输入/图像哈希守卫，新增八类中的六类通过。

- s-curve：透视 0.989493，俯视 0.990693。
- crest：透视 0.995167，俯视 0.996110。
- valley：透视 0.994942，俯视 0.999881。
- negative-bank：透视 0.995803，俯视 0.999861。
- curve-width：透视 0.994318，俯视 0.996555。
- curve-width-bank：透视 0.986268，俯视 0.993381。
- curved-uphill：透视 0.850801，俯视 0.989366，未通过。
- variable-bank：透视 0.972013，俯视 0.996267，未通过。

弯道上坡参考相邻左右顶点的最大 Y 差约 0.000104 米，说明未指定横坡时参考截面保持水平；
当前样条采用平行传输框架，弯曲加坡度会产生横向倾斜。这是待修正的道路框架策略差异，
不能通过修改参考相机或放宽阈值消除。变化横坡暴露属性插值差异；当前 core 为线性，
参考实际顶点已保存，尚需逐点拟合后确定策略，不能仅凭轮廓认定其公式。

本批只扩展测试输入及场景注册，没有修改核心 API、持久化、ECS 或模块边界，无架构例外。
原生生成器构建通过，对照工具 10 项测试及 Ruff 通过。既有八类仍全部通过；总体 14/16。
完整结果位于 build/road-parity/comparison.json，失败图像保留在相应 fixture 子目录。

## 第三轮：弯道上坡框架修正

RoadStyle.frameOrientation 是道路唯一作者策略：ParallelTransport=0，WorldUp=1。
新道路默认 WorldUp；schemaVersion=1 缺失该字段时恢复 ParallelTransport，非法枚举原子拒绝。
现有 style 编辑、拆分、反向和补偿快照携带 owning 策略，无独立缓存/注册表。
临时样条的 outgoing SplinePoint.worldUpFrame 与 evaluated sample 只投影策略，通用样条默认仍平行传输。
道路 loft、设施、接入口校验、标线、导航和交通复用 edgeToSpline 路径，不仅修改截图中的驾驶面。
WorldUp 每个采样以世界 Y 重建未横坡侧轴，再应用作者 roll；近竖直切线采用确定性的世界 X。
既有昂贵采样流程和成本量级不变，不新增事件、provider、ECS、时间或 RNG。
适用 R-FACE-1/R-FACE-2 的六面目录、R-BOUND-3 的 L1/L2 编译链接证据与持久化原子迁移，无例外。

真实弯道上坡透视 IoU 从 0.850801 提高到 0.991147，俯视 0.993592，已通过 0.98。
16 类中现为 15 类通过，变化横坡仍为约 0.9718（透视），尚不能认定完全对齐。
参考顶点拟合显示简单线性、标准 Catmull-Rom（参数域或位置域）均有残差；
没有为通过图片阈值而替换为未经确认的近似公式，参考数据和失败帧继续保留。
新合约测试覆盖 WorldUp 全部 97 帧水平/正交、generic transport 保持原行为、
新快照往返、缺字段迁移、非法枚举原子拒绝以及真实编辑 commit/compensate 恢复作者快照。
STATIC 聚焦集合 31 项、6,318 断言通过，无警告/失败。

最终验证：STATIC/SHARED 聚焦集合各 31 项、6,318 断言通过；正式 procgen-core-only OBJECT 编译通过（L1），独立双链接闭包通过（L2）。对照工具 10 项通过。本轮 HEAD 增量架构审计通过；默认 make check 仍仅由 IStateProvider.h 三项 bool API 与 RenderSystem.h 两项生命周期文档违规阻断，完整日志 frame-quality-default.log。未修改基线或增加 allowlist。完整渲染器未重编译，实帧使用当前核心生成的数据。

## 变化横坡后续结果

通过真实 EasyRoads API/mesh 探针确认 span-parameter smoothstep 横坡并实现后，当前 16 场景 / 32 对图像全部通过固定 0.98 阈值。variable-bank 透视 0.997533909、俯视 0.999887151。此前 15/16 与该项失败是历史结果，详见 2026-10-14-road-roll-reference-contract.md。完整核心对齐和高级双端验收仍未完成。
