# EasyRoads3D 包内数据结构、道路约束与 EVEngine 核心差异审计

审计日期：2026-10-13。比较对象是当前工作树的 `procgen/road`、其编辑事务与产物投影；不把整个 EVEngine 其它模块缺少直接道路集成的能力称为“引擎完全不支持”。初版为只读审计；后续段落记录实现与实际执行，历史结论以最新复核范围为准。

## 当前验收状态（2026-10-15）

核心对齐未完成。最新完整SHARED道路及通用产物合并回归244项（道路198）、268325框架断言通过，逐项汇总无告警失败跳过。新增整网几何来源v1、生产artifact颜色保存/恢复与旧记录兼容；生产mesh身份升至geometry-sources-v19。作者预设策略持久化与按道路几何归属投影尚未实现，NetworkDerived仍未细分设施归属，分块网格不带来源。完整源门禁已运行，格式/分层/十配置通过、无Git命令错误，仍因既有七项架构发现失败；未增加豁免。重叠优先级、World全对象保存、完整拆卸/地址复用、渲染物理消费及最新Unity实帧、独立Heightmap receipt owner与B1–B8其余差异保持。

## 证据范围与版本边界

2026-10-15 批次脚本消费：新增World.conformRoadBatch(meshes, policies, maximumWork)，两数组长度匹配且1..4096，预算严格检查INTEGER/正数/size_t范围，不接收bool/float/string隐式转换。SSQ数组在当前同步调用内维持强根，每个mesh/policy经类型与已有owner/generation验证后转换为typed借用/值，全部验证后才调用已有原生批次，不触发表属性脚本回调或建立另一份作者状态。真实VM由两份networkSnapshot构建拥有型生产网格，验证不同纹理层及opacity、单帧撤销、修改options后redo仍恢复旧结果、错误数组/预算/后项失效的原子拒绝；大于32位的合法预算2147483649也通过。独立用例4框架断言和脚本内断言通过。脚本批次入口缺口据此关闭，作者预设保存/几何归属投影及重叠/实帧仍未完成。

本项遵循R-FACE-1/2更新E5方法与测试证据；R-MECH-1采用低频显式构建、单World owner原子事务；R-COST-1至5复用原生批次成本边界，预检与借用存活期在手册声明。Result共用投影，所有临时native借用在函数返回前结束，未增加provider/ECS/Link或持久schema字段，无架构例外。

2026-10-15 逐道路地形策略与批次：真实Unity三场景（全部/只第一条/只第二条）确认，不相交两条道路可分别写layer1/opacity0.4和layer2/opacity0.8；全部构建改变576纹理样本，中心为(0.6,0.4,0)/(0.2,0,0.8)，单选改变288，未选道路中心保持(1,0,0)，恢复全纹理误差0，退出0。证据为 `build/road-parity/unity-terrain-road-policy-evidence.json`。因此不能把一个全局write options保存后就称逐道路作者策略完成。

原生新增RoadTerrainWrite（当前调用内const mesh借用、按值options）与applyBatch/conformRoadBatch。全部道路/地形在一个私有完整通道候选上按输入顺序处理，一次发布/一帧撤销；单道路入口也委托到相同实现。共享预算覆盖全部项，各项预算继续生效；第二项缺层、独立预算不足、全局耗尽均不修改可见状态或redo。累计改变数以64位计算，超过公开int结果范围时发布前拒绝。已有schema-v2保存最终通道和历史，不序列化临时借用或作者策略输入。三项STATIC测试76断言通过；SHARED选定回归226项（道路192）/268088断言，逐项汇总无告警失败跳过。

本批适用R-FACE-1/2的typed数据/持久/World消费面；R-MECH-1的低频显式原子构建使用唯一owner候选，未增加provider/ECS/Link/模块查找。R-COST-1至5：批次借用与存活期、一次历史候选加累计栅格工作、显式共享预算均写入公共契约；单写委托同一路径，不增加更便宜的第二真源。保存格式无需迁移，结果复用schema-v2；无架构例外。原生夹具尺寸/分辨率与Unity不同，只证明策略和事务行为，**不是全栅格/实帧对齐**。作者预设分层保存与生产投影、批次脚本入口、重叠优先级继续未实现/未确认。

2026-10-15 脚本道路参数生命周期：上一批类型校验之后，新增对既有模块拥有MeshBuild/Heightmap代理的generation/ownerEpoch再解析。仅检查VM实例类型和非空地址不足以证明原生对象仍存活；现在先从原有NativeProxyReleases记录复制身份，再通过当前Procgen owner解析，缺席/过期/地址不符均返回StaleHandle，随后才形成当前调用内ResultRef。锁只保护身份查询，不持锁调用owner或脚本；不建立第二份registry或释放路径。真实VM释放道路网格后拒绝旧输入，新建网格仍能写回并撤销；生成Heightmap作为copyHoles输出成功后，释放它再调用安全失败，World历史不变。独立VM用例12框架断言及扩展脚本断言通过。普通原生借用值仍要求调用方owner在调用内存活；本探针未证明完整模块拆卸或分配器地址复用场景。

生命周期修改适用R-FACE-1/2的数据/脚本面、Result借用与唯一owner规则；R-MECH-1使用低频构建边界的原有typed registry，不新增模块查找或provider。R-COST规则沿用显式栅格构建，新增身份验证仅为同步调用内查询。模块内使用既有liveProcgen缓存及原有句柄解析，不改变持久格式、mesh算法修订或公共对象继承。无架构例外；作者配置保存与其余B1–B8缺口保持。

2026-10-15 脚本道路地形入口：TerrainWorldScript新增VM拥有的RoadTerrainWriteOptions，绑定全部通道字段、受检正整数工作预算，以及World.conformRoad/copyHoles。新增参数边界显式读取ssq对象、检查INSTANCE与仓库既有stableTypeHash，再取得当前调用内的ResultRef借用；null、原始值和其它类实例返回标准诊断，避免第三方指针转换在进入lambda前抛异常或接受错误类地址。copyHoles在完整校验后才替换输出，不保留临时地址。生产World事务仍为唯一owner，无新增持久schema/mesh版本。原先“缺少脚本道路写回入口”的子项据此更新，作者/编辑器参数持久保存、整个World跨进程保存及默认渲染闭环仍未完成。

本项架构适用性：R-FACE-1/2补齐E5脚本面，World模块契约声明新类、属性与方法；R-MECH-1沿用已知单owner、低频显式构建和原子事务，未增加模块查找/provider/ECS/Link；R-COST-1至5保留create→conformRoad的显式成本边界，在手册说明栅格/历史与洞遮罩复制成本，不增加隐式getter工作或第二入口。Result/nodiscard投影由common统一消费；VM对象根引用仅维持当前调用，typed借用不逃逸。未申请例外。

脚本入口验证证据：真实SHARED全选定回归223项（道路189）、268010框架断言，逐项汇总无告警失败跳过。随后仅扩展测试，增加newParams→networkSnapshot→buildMesh("mesh.roadNetwork")拥有型产物→World写回的脚本生产链；最终VM用例12框架断言及脚本内断言通过，不能称该扩展已包含在前一次完整日志。最终完整make check为已知七项架构发现，格式/分层/十配置通过，无Git命令错误。首次门禁遇到索引锁的日志仅保留为历史。汇总见 `build/road-parity/terrain-road-vm-evidence.json`。新增边界检查不代表已审计全部旧地形绑定或完成作者/编辑器持久化。

2026-10-15 原生纹理opacity：RoadTerrainWriteOptions追加有限[0,1] splatOpacity，默认1保留既有调用；统一apply将覆盖权重乘opacity作为目标值，仅当高于所选层当前权重时提高该层；其它层按原比例重分配剩余权重。opacity0仍执行所选高度/植被；非法输入在私有候选发布前拒绝，不破坏undo/redo。native TerrainWorldRoad通过原有事务消费相同参数。结果权重沿用schema-v2全通道历史保存，构建参数本身没有新增持久字段或mesh修订。三项STATIC测试106强断言通过（包含弱绘制不降低已有权重）；真实SHARED选定回归222项（道路188）、268000断言，逐项case/断言汇总无告警失败跳过。覆盖混合三层、zero/full、独立植被、JSON/restore/undo/redo、非有限/越界原子拒绝及World消费。该更新覆盖上段历史缺口中的C++透明度路径，**作者文档/编辑器设置/脚本道路写回入口、texel expand/smooth、全形状及渲染对齐仍未完成**。不模拟Unity8位量化，固定参考opacity0.5中心0.501960814与原生0.5的差异明确保留。

补充参考确认：九组最终探针包含初始目标权重0.3/0.8及三层0.2/0.3/0.5。opacity0.5将约0.302提高到约0.502，保留0.8不变；三层中心为0.1411765/0.5019608/0.3568628，对应原生未量化0.1428571/0.5/0.3571429。普通lerp朝1混合会错误得到约0.651，已通过该负向区分修正实现和测试。最终完整写回检查与严格恢复检查分开记录；严格树恢复仍失败，不能将整个参考探针称为通过。

本项架构适用性：R-FACE-1/2沿用typed数据面与既有Result接口，更新procgen模块契约和持久测试证据；R-MECH-1是低频编辑构建、已知单owner、全通道原子性，沿用私有事务，无新provider/ECS/Link/事件；R-MECH-2/3无每帧查找或便利模块查找；R-COST-1/2/3/4/5沿用显式initialize→apply和create→conformRoad、既有有界栅格成本，不引入替代入口或成本默认档。保存结果继续schema-v2，无迁移新要求。未申请架构例外；Squirrel/编辑器作者入口缺口未以C++测试替代。

最终证据汇总为 `build/road-parity/terrain-opacity-evidence.json`。修正后完整make check重跑，格式/分层/十配置矩阵通过，仍为IStateProvider三项、RenderSystem两项、Sensing一项、Vehicle一项，共七项架构发现，无Git锁或命令错误。未新增豁免/基线，核心目标仍未完成。

2026-10-15 Unity多通道实测：`UnityTerrainChannelsProbe.cs`执行六个独立场景（全关、纹理/树/细节各自开启、全开、纹理opacity0.5）。纹理开启修改288样本，中心权重1，opacity0.5时为0.501960814；树2→1，细节密度总量16384→13888，关闭对应通道时保持原值。纹理与细节逐样本恢复误差为0。但严格树实例恢复校验失败：清树后restore将被移除树的normalized Y由0.5恢复为0.6000122，顺序也改变；不能仅凭数量恢复2就称完整恢复。最终诊断日志退出1，完整前后字段保留于 `build/road-parity/unity-terrain-channels-diagnostic.json`，作为参考实测差异，不计为通过门禁。原生完整快照恢复契约继续保留，不复制此参考行为。原生RoadTerrainWriteOptions只有覆盖权重，没有独立splat opacity和texel expand/smooth，继续列为明确未实现项。合成树材质有shader告警，本探针不提供树木视觉正确性证据。

2026-10-15 Unity地形高度基准实测：新增公开API探针 `UnityTerrainOriginProbe.cs`，分别在Y0/Y100创建65×65、64×10×64m地形，初始归一化高度0.5，道路绝对高度为基准+6m。真实BuildRoadNetwork(false,false,false)后，两端中心均为0.6000121832、各改变1121个样本；RestoreRoadNetwork后全图精确回到0.5。平移前后全图最大归一化差为0.000030517578125（低于固定0.0001门槛，不能称逐位一致）。最终日志包含完成标记与Unity退出码0，证据见 `build/road-parity/unity-terrain-origin-evidence.json`。这支持原生originY映射的目标行为，但原生现有fixture范围/分辨率不同，尚不证明完整变形形状、过渡算法、多通道或渲染对齐；前述Unity真实导入/实帧缺口仍保留。

- 实际读取文件：`C:\baidunetdiskdownload\Unity3D_EasyRoads3D Pro v3_Vv3.2.4f5_道路设施地形创建工具\EasyRoads3D Pro v3 v3.2.4f5.unitypackage`。
- 文件大小 100,046,933 字节；SHA-256：`2548695600d8cadd9eeee5eaa4057cf2c3b5b3d210eacea2195cb2c64ddab905`。
- 外层 252 个 pathname 条目：26 个 HTML、9 个 Prefab、3 个场景、5 个 asset、3 个 DLL、2 个 C# 文件、20 个嵌套 unitypackage 等。目录条目也计入 252。
- 外层含 `EasyRoads3Dv3.dll`、`EasyRoads3Dv3Editor.dll` 和 `DelaunayER.dll`。只读解析运行 DLL 的 ECMA-335 类型、字段和方法元数据，没有加载执行 Unity 程序，也没有将反编译算法当成已验证契约。
- 20 个直接子包逐一列出资产：12 个 SRP 支持包，8 个 Beta Release 包；Beta 主包另有两个多车道 shader 子包。Beta 主包包含 `ERTrafficManager.cs`、`ERVehicle.cs`，不能与外层混为一版。
- 外层文件名声称 v3.2.4f5，外层 `release_notes.html` 写 v3.2.4f4；内部另带 v3.3b21。外层与 Beta 运行 DLL 哈希不同，两者各有 185 个 TypeDef 条目（包括嵌套类型及编译器生成类型）。不能以类型存在推断所有功能已经公开或可靠实现。
- 已验证的重要版本差异：Beta `ERRoad` 有 `GetLaneCount/GetLeftLaneCount/GetRightLaneCount/GetLaneData/GetLanePoints/GetLaneCenterPosition/GetSpeedLimit` 命名公开方法，外层 DLL 没有这些命名公开方法；外层已经有 `ERLaneData/ERLaneConnector` 字段结构。**交通数据存在与公共查询 API 可用是两项不同证据。**
- 下面“缺口”均指道路核心契约或道路构建闭环没有表达；“部分”指存在可复用通用机制但未等价集成；“不同”包括我们刻意采用的更严格边界。DLL 混淆字段、算法数值阈值及未运行的 Unity 行为另列，不作肯定推断。

2026-10-14 后续：B7 的道路算法修订缓存身份已落地，标准 BuildKey 与 artifact 使用同一生产者修订，历史键保存恢复但不命中新键。详见 [算法修订与缓存身份](2026-10-14-road-algorithm-cache.md)。这是显式修订，需要生成语义变化时维护，未宣称自动源代码哈希、完整依赖追踪或增量烘焙完成。

2026-10-14 后续：用于对照的 SDK 在 G-buffer 默认路径下未正确表现单面 unlit 材质，最小运行状态/关闭路径实验已取证；当前完整 graphics 构建的复现和修复继续列入 B7/B8，不用 forward 校准通过代替默认消费路径修复。详见完整横坡文档。

2026-10-14 后续：B2 完整一圈横坡已贯通作者、保存/撤销、split/reverse、标准 recipe 和 artifact/LOD，采用未取模 [-360,360]。参考地形开关开启时 API 仍接受超30°，不能将手册中的编辑手柄约束套到 API；独立地形变形作者开关和编辑模式约束仍未完成。详见 [完整横坡与地形编辑约束](2026-10-14-road-full-roll.md)。

2026-10-14 最新实测：完整一圈横坡和扫掠物理对角线修正后，32场景/64对图像为31 passed、1 different、0 blocked（固定 .98，统一单面 forward unlit 驾驶面轮廓）；仅镜像 Circular 透视 .972161未通过。完整作者道路58个、实际artifact/全部LOD 1个、Spline 16个契约测试通过。地形编辑约束、默认G-buffer消费及其余B1–B8剩余项未因此完成。

2026-10-14 最新圆弧进展：78个真实数据探针确认邻段虚拟向量尺度等于圆弧marker的3D弦长，已修正原生尺度、失败原子性与生产缓存v4。默认和独立零起伏参考同时保留，34场景/68对图像32通过、2差异、0阻塞：两份镜像Circular透视仍低于 .98；方向离散规则及具名起伏模型继续未完成。详见 [圆弧约束与后续实测](2026-10-14-road-circular-controller.md)，其余B1–B8剩余项没有缩减。

2026-10-14 最新默认对齐：直接创建参考道路的读回为2m/45°/1段，已作为原生新默认，旧网络保持0/4/MaximumIntervals，旧preset v1/v2全历史迁移为显式旧根默认，当前保存v3。新增不调用采样setter的实际场景两视图 .994634/.995802通过；当前35场景/70对图像33通过、2差异、0阻塞，仍为两份镜像Circular透视。默认值/历史迁移子项已落地，端部离散过滤和其余采样语义继续未完成。

## 初始 75 项审计（2026-10-13 历史快照）

以下编号保留最初发现，不代表当前实现状态；当前结论以文末“当前实现复审”及各实现契约为准。

## 1. 核心对象与权威状态

1. **路网容器。** EasyRoads 的 `ERRoadNetwork` 操作场景 `ERModularBase`，runtime 示例明确一个场景只能有一个 Road Network；我们是可独立创建的 CPU `RoadNetwork` 值容器，没有场景单例限制。〔E1/E2；V1〕
2. **作者对象分层。** EasyRoads 分开管理 Road Objects、Connection Objects、道路子级 Side Objects；我们由同一个网络拥有 node、edge、active/blocked lane links，mesh、overlay、placements 都是派生值。不能把其层级简单一一复制成 ECS 实体。〔E1；V1/V2〕
3. **公共门面与组件不同。** DLL 中 `ERRoad` 仅 5 个字段、159 个方法，而 `ERModularRoad` 有 305 个字段；作者状态、采样数组、mesh、材质、缓存、UI 状态出现在同一组件结构中。我们 `RoadEdge` 仅持有身份、两端节点、控制点、两向车道数和 style，派生缓存不持久驻留其中。字段多不意味着独立权威状态多。〔E3；V1〕
4. **道路类型。** `ERRoadType`/项目 road preset 有 ID、截面、UV、材质、物理材质、side-object 默认项、terrainDeformation 等；我们每条边按值持有 `RoadStyle`，没有道路类型身份、共享定义引用或类型更新传播。缺口。〔E3/E4；V1〕
5. **类型/实例/marker 覆盖。** EasyRoads 有项目预设、场景预设、road instance、marker、side-object instance 多层默认及覆盖；我们主要是整边 style 和 bake options，没有这套覆盖优先级与定义同步语义。缺口。〔E4/E5/E7；V1/V2〕
6. **身份与查找。** EasyRoads API 通过名称、GameObject 和连接索引访问，序列化还保留 id/GUID/timestamp；我们 node/edge 使用网络内稳定 uint32 ID、索引映射及 revision，lane-link 用组合值身份。两者都不应被误称为完全无稳定身份。〔E2/E3；V1/V5〕
7. **名称和作者锁定。** EasyRoads 有 roadName、locked、layer/tag/static、castShadow；我们的道路核心没有这些字段。名称/锁定属于作者体验，渲染与场景分类属于消费层，不能全部塞进几何核心。〔E3/E5；V1〕

## 2. Marker、路径与截面表达

8. **逐 marker 数据。** `ERMarkerExt` 有 86 个字段，包含左右 indent/surrounding、controlType、splineStrength、rotationCenter、roadMarkerWidth、roadShape、bridge 起终过渡与 side-object 数据；我们 `RoadControlPoint` 只有 xyz。这是最主要的表达力缺口。〔E3/E5/E6；V1〕
9. **混合路径控制器。** EasyRoads 支持逐段 Spline、StraightXZ、StraightXZY、Circular：XZ 直线保留平滑高度，XYZ 完全直线，Circular 按前段方向构造圆弧；我们道路 adapter 固定 open Catmull-Rom，未暴露逐段控制器。通用 `SplinePath` 虽支持其它插值，不等于道路已支持。〔E3/E5/E6；V3〕
10. **曲线强度。** EasyRoads 每 marker 可设 splineStrength；我们道路控制点没有 handle/strength，只有 Catmull-Rom 控制位置。缺口。〔E5/E6；V1/V3〕
11. **横坡与旋转轴。** EasyRoads 显式 tilting 与横向 `0..1` rotationCenter；地形变形开启时 marker 倾斜限制为最大 30 度，其它对象可全 360 度。我们虽然使用 transported frame，RoadControlPoint 没有作者 roll 或横向轴心；通用 SplinePoint 的 roll/scale 没接入道路。缺口。〔E5/E6；V1/V3〕
12. **宽度沿线变化。** EasyRoads 有 `SetWidth(...markerIndex)` 和 `ERMarkerExt.roadMarkerWidth`；我们 asphalt 宽度为整边 `laneWidth*(lanesForward+lanesBackward)`，不能一条边内连续变宽。缺口。〔E3/E6；V1/V4〕
13. **左右非对称。** EasyRoads 有 left/right lanes、shoulder width、独立 sidewalk preset/active 状态；我们的双向车道数量可不相等，但总截面仍左右镜像、所有车道共用 laneWidth、人行道与 barrier 共用宽高。不能把不同车道数称为完整非对称截面。〔E3/E10；V1/V4〕
14. **任意截面。** `ERRoadShape` 表达 nodes/hardEdge、shape UV、左右优先节点与 lane marking 节点索引；自定义连接可提取原始 mesh 的端口截面。我们 `RoadProfile` 是固定十点开放 Jersey-barrier 截面，公共 road authoring 不接受任意 profile、材料分段和 hard-edge 指定。缺口。〔E3/E4/E6；V4〕
15. **逐段截面替换。** EasyRoads `SetRoadShapeNodes` 可作用于 marker 集合；我们 profile 由整边 style 和车道数重建，没有 marker 间的截面插值与相容性规则。缺口。〔E6；V1/V4〕
16. **闭合轨道。** EasyRoads `ClosedTrack` 直接缝合首尾；我们禁止单 edge 的 from==to 且 spline 固定 open，闭环只能由多个不同 node/edge 组成。图闭环与连续单路段 seam 并不等价。〔E5/E6；V1/V3〕
17. **地形决定路径。** EasyRoads `FollowTerrainContours` 含 road/marker 开关和 contour threshold；另有 `SnapToTerrain` 直接吸附路面顶点并停止该道路地形变形。我们 bake 不读 terrain，只能事后让 Heightmap 贴路；通用 TerrainSampler 存在但未接入道路。缺口。〔E5/E6；V2/V3/V6〕
18. **道路起伏。** EasyRoads road/marker 可设随机 Y 偏移、tilting 及变化距离；我们没有道路曲面随机扰动契约。placement 的 seed 和通用对象随机变换不等价。〔E3/E5/E6；V1/V3〕
19. **采样策略。** EasyRoads resolution 是米制间距，angleThreshold 减少直段细分，另有横向 subSegments；我们用每 edge 固定采样数量、turnSamples，只有路口圆角使用 chord-error 自适应。长短道路不会按同一米制目标自动分段。部分。〔E4/E5；V2/V3〕
20. **纵坡作者约束。** EasyRoads marker UI 显示 grade/angle，并有 Force Y Position 的“每米高度变化”作者功能；我们 node/point 接受有限 xyz，并无整路最大纵坡、最小转弯半径或设计速度约束。包的 slopeAngle/grade 字段不能证明它有通用道路工程规范求解器。〔E3/E5/E8；V1〕

## 3. 路口、连接口与几何约束

21. **连接端口是显式数据。** `ERConnectionData` 有 road、marker、connectionIndex、endConnectionIndex、position、roadType、priority；其路口是具有 local/world port position 的变换对象。我们节点只保存位置、radius、control，socket 从 incident edge、宽度和夹角推导，不保存端口身份或占用槽。不同。〔E3/E9；V1/V3〕
22. **端点位置来源。** EasyRoads endpoint 对齐具体 prefab 连接口，路口旋转也会重定位道路；我们 node 是 endpoint 唯一真源，插入/恢复/移动时规范化首尾控制点，RoadNode 没有旋转。缺少端口局部坐标/变换语义。〔E9/E10；V1〕
23. **可接入范围。** EasyRoads 普通 road 用首尾 marker 接路口；中间插口会分裂道路。我们也支持 control-point/位置/spline 参数 split，但单独 split 不等价于原子“插入 prefab、两端绑定指定 port”操作。部分。〔E6/E9；V1/V5〕
24. **连接兼容性。** EasyRoads 文档允许不同 roadType，只要截面匹配；自定义 prefab 的一个兼容检查仅比较截面 vertex count，并额外受 sidewalk 状态约束。我们 lane-link 校验方向/共享 node/lane range，bake 校验路口几何，没有截面签名、材质接口或 prefab 端口兼容性校验。缺口。〔E10；V1/V3〕
25. **动态 X/T 的限定。** 包内手册明确动态 X/T 只接受两节点 road shape，暂不支持 road subSegments；复杂截面经 IConnector 过渡或 custom prefab。我们没有此两节点限制，但通用 profile 也不可作者替换，不能因此声称复杂截面能力更强。〔E4/E5；V4〕
26. **路口类型。** EasyRoads 分 dynamic X/T、roundabout、custom mesh prefab、IConnector，元数据还有 flex/Y/exit/snap 类型；我们按图的臂方向生成通用几何及 T/Y/X/multi 资产锚点，没有模板类型及模板约束。元数据类型存在不证明所有类型完整可用。〔E3/E4/E10；V3〕
27. **路口旋转、镜像和对齐主臂。** EasyRoads 有 rotationPriorityElement、SwapTurn、自动对齐主连接、T straight bending；我们用边的作者方向和最小二乘平面，没有独立路口旋转、T 模板镜像和对齐优先口。缺口。〔E3/E9/E10；V3〕
28. **几何优先级与交通路权必须分开。** EasyRoads 文档的 Road Priority/priorityRoads 可决定哪条主路在交叉处保留原始形状；我们 `trafficPriority` 只作用于路权、overlay 和 Yield 锚点，路口平面拟合按 asphalt 宽度加权。同名概念不是同一能力，几何主路约束缺失。〔E3/E8；V1/V3/V7〕
29. **逐角圆角。** EasyRoads 每 corner 有 radius、segments、innerSegmentDistance、独立 preset；我们只有 node junctionRadius 和全局 chord error，圆角依据相邻臂派生，没有四角独立作者覆盖。部分。〔E3/E4；V1/V3〕
30. **路口人行道。** EasyRoads 每 corner 的铺面开关与两侧连接开关分开，会传播到 attached road 及另一端 crossing；我们 style 按整边对称，路口人行道自动生成，没有逐角/逐侧连通开关或传播契约。缺口。〔E4/E10；V4/V3〕
31. **路缘截面。** EasyRoads sidewalk preset 可设 bevel height/depth、outer curb、自定义 shape 与 UV；我们 curb 实际是高 barrier U-channel 结构，不是等价的可组合城市路缘、人行道截面。缺口。〔E3/E4；V4〕
32. **宽度/材质过渡段。** IConnector 有两侧不同 roadType/shape/material、connector distance、Linear/Exponential/Smooth stretch、subdivision，以及 BlendTextures/TextureTransition 和作用侧。我们允许不同宽度边共享 node，但无显式有长度的截面 morph/材质过渡契约。路口铺面连接不等价。〔E3/E5/E9；V1/V3〕
33. **自定义路口资产的有效接口。** EasyRoads 从源 mesh 选择接入顶点、UV 和硬边，建立真正 road port；我们的 `junction.*` placement 仅能放资产，不能让该资产边界接管道路 seam/裁切/碰撞/traffic ports。缺口。〔E4；V3/V8〕
34. **环岛模型。** EasyRoads 显式 roundabout radius/width、入口位置索引、入口左右圆角、连接长度、内侧人行道和材质；我们环岛是普通 graph，资产识别只接受特定三/四节点小曲线闭环。没有环岛参数编辑和入口区域模型。部分。〔E3/E4；V1/V3〕
35. **环岛入口可放置约束。** 包手册拒绝入口重叠、跨 0/末尾索引、半径空间不足；我们一般拓扑验证及通用路口几何检查没有环岛入口占用弧段与可用空间校验。缺口；不能由普通近距锚点过滤替代。〔E4/E11；V1/V3〕
36. **两边约束策略不同。** 我们三臂以上拒绝几乎同向臂（dot>0.9999905），四臂以上校验 socket 与有界 plane 的高差，plane 坡度幅值限 0.25，容差 max(0.35m,asphaltHalfWidth*0.15)；≥6 总车道短边裁切后不足约 1m 拒绝。包手册多为 prefab 相容性与警告/不重建，不能证明与这些阈值相同。〔E10/E11/E12；V3〕

## 4. 车道、交通和标线

37. **左右侧与行驶方向。** EasyRoads `ERLaneDirection` 是 Left/Right，另有全局 `ERTrafficDirection` LHT/RHT；我们 Forward/Backward 相对 edge from/to，独立于左右行交通规则。没有道路级 LHT/RHT 契约。〔E3/E13；V1〕
38. **车道记录。** `ERLaneData` 有 points、connectors、laneType、laneIndex、direction；`ERLane` 有 position、direction、turnDirection、TurnOptions、laneIndex。我们按 lane 数和统一宽度派生 polyline，没有 authored lane type、独立横向位置或 lane definition。缺口。〔E3；V1/V3〕
39. **转向许可表达。** EasyRoads 枚举有 Straight/Left/Right/组合/AllDirections；我们用精确 lane-to-lane active/blocked route 表达许可。表达能力有重叠，但缺语义化转向作者选项及按夹角分类策略。部分。〔E3；V1〕
40. **异车道数自动连接。** 包组件有 aiMatchingLanesOnly/aiconnectNonMatchinglaneCounts，具体算法未验证；我们按归一化横向排名连接、作者同路线覆盖自动值、阻断值抑制自动恢复且操作幂等。明确策略不同，不能假设包也用同样映射。〔E3；V1〕
41. **转向曲线独立编辑。** `ERLaneConnector` 有 start/endOffset、strength、localPoints/points；我们 RoadLaneConnection 只有 route 身份，turn 几何按 socket 和 radius 自动生成，不能给单 turn 编辑 curve/handle。缺口。〔E3；V1/V3〕
42. **单 turn 速度与停车。** `ERLaneConnector` 有 stop、speedLimit、minSpeed/maxSpeed、rtSpeedLimit/customSpeedLimit；我们限速按 edge，turn 取入出边最低值，control 按 node。没有每 turn 的速度覆盖/停车契约。缺口；包字段存在不证明停车仿真完整。〔E3；V1/V3/V7〕
43. **交通控制粒度。** 我们明确 Uncontrolled/Yield/Stop/Signal，并为合法 incoming approach 派生锚点；EasyRoads 元数据含 traffic posts/lane stop，但本审计没有证据证明其有与我们相同的 node control 枚举、相位模型或冲突矩阵。不同/未证实；我们 Signal 也只是策略与资产锚点，并非完整红绿灯仿真。〔E3；V1/V7〕
44. **车辆运行层。** Beta 附带 ERTrafficManager/ERVehicle 演示道路→lane connector→下一道路，随机选 route、按速度目标移动；它明确有无 attached road connector 会挂住的限制，并非完整 traffic simulator。我们道路核心只产出导航数据，车辆行为应由其它领域消费。不同。〔E13；V2/V8〕
45. **标线选择。** DLL 有 Solid/Broken/Double/Mixed/None、median 另有 MixedReversed；我们根据车流固定白色路缘实线、同向白虚线、双向双黄线及斑马线，只有宽度/dash/gap 和全局开关，没有逐分界线 style authoring。缺口。〔E3；V1/V3〕
46. **方向标志与斑马线语义。** 包有 road spawnDirectionMarkings、ERCrosswalk/CrosswalkType 与 decal/traffic post 数据；我们箭头/斑马线按 bake 派生，没有逐口 zebra 选择、对角斑马线或逐 lane 箭头样式覆盖。元数据中的 diagonalCrosswalks 不等于已经运行验证。〔E3；V3〕

## 5. 地形、桥隧、植被

47. **Indent 与 Surrounding 是两段。** EasyRoads 左右各有从路缘向外的等高 Indent，然后才向原地形过渡的 Surrounding，且可逐 marker 设置；我们由 asphalt 覆盖样本直接向外 blend，只有调用级统一 blendDistance，没有独立等高路肩和左右/沿线参数。上一轮米制修复没有补齐这个结构缺口。〔E4/E5/E6；V6〕
48. **网格相关最小 Indent。** 包文档要求不低于 scene minIndent，最小值与 terrain heightmapscale 相关；样例 network 序列化 terrainCellSize≈2.4414062、minIndent≈3.662109，不能从一个样例推导所有版本固定 1.5 倍公式。我们 radius 最小可为 0，不自动设置等高保底区。〔E4/E8/E14；V6〕
49. **Indent 的高度基准。** EasyRoads 可选 Road/Terrain/Surrounding，左右独立；我们只用 asphalt triangle 的 roadY，无法以路肩/过渡外缘原始地形为另一种高度基准。缺口。〔E3/E5/E6；V6〕
50. **保形、平滑与关键高度。** EasyRoads 有 Preserve Terrain Contours、Smoothen Indent/Surrounding、Reapply Critical Road Heights；我们线性 blend 到最近合法 covered road sample，没有专用保形系数、内外边界 smoothing 或 critical 样本约束。通用地形算法存在不等于道路闭环已整合。〔E7；V6〕
51. **桥隧是显式区间。** EasyRoads 每 marker 可关闭 deformation，bridgeStart/EndLevelDistance 控制引道过渡；我们按 maxVerticalDelta 的绝对高差跳过桥下/隧道顶，统计 receipt。没有 authored bridge/tunnel interval，低桥/浅隧道仍可能落在阈值内被贴合。缺口。〔E3/E5/E6；V6〕
52. **隧道入口与开洞。** EasyRoads tunnel side object 有 cutHoles、入口 offset、terrain snap weights，手册标 Preview 且依赖 terrain 分辨率；我们 Heightmap 没有 road hole mask 或 tunnel portal 生成。保存隧道覆土不等于生成可通行洞口。缺口。〔E3/E15；V6〕
53. **桥墩与挡墙。** EasyRoads side-object 自动规则检测地形高差/坡角/重叠，分类 bridge/tunnel/retaining wall，并调整 deformation；我们桥面/桥墩按 style 和世界高度条件生成，baker 不采 terrain，没有真实地形接触、地基高度及挡墙区间求解。缺口。〔E3/E15；V1/V3〕
54. **多地形与选择构建。** EasyRoads 可选 terrain 集合或 selected roads，新增 terrain 要 UpdateTerrainInfo；我们每次 conform 显式传一个 Heightmap 与 mesh，支持自行分批，但无路网级多 tile 映射、跨缝一致性及选择 road 的协调调用。部分。〔E2/E7；V6〕
55. **高度范围与偏移。** Unity terrain 高度规范为 0..1、不能负值，文档提示低点 critical 与 raise offset；我们 Heightmap 是 float 容器，worldY=height*heightScale，没有强制 [0,1]、没有 originY 参数。core node 可负高。不同；不能把“normalized”注释当实际 clamp。〔E4/E7；V6〕
56. **植被类别。** EasyRoads 分 tree/detail、Vegetation Studio grass/plant/tree/object/large-object perimeter 与 biome mask；我们通用 topology Grid2D+excludePointsByGridMask 可保留 point ID/属性并按 clearance 排除，但道路不自带类别策略、terrain tree/detail backup 或 biome 组件集成。部分。〔E3/E4/E7/E16；V8/V9〕
57. **材质权重与真实 splatmap 写回。** EasyRoads 指定 terrain texture index、expand/smooth（部分单位是 texel 数）、opacity，构建时改 terrain splatmap；我们 terrain/surface-weight 是 0..255 派生 grid，没有 terrain 材质层索引或 splatmap 原子写回。部分；不是等价集成。旧 creating_roads 页的“最多 8 texture”是包内历史说明，不能作为 v3.3 的可靠全局上限。〔E5/E6/E7/E8；V8〕
58. **地形恢复。** EasyRoads ERTerrain 持有 heights/trees/detail/splat/holes 备份；BuildRoadNetwork 文档只恢复最后一次 build，重复 build 前要求 restore，runtime 注明退出 Play 的恢复风险。我们高度 receipt 记录 before/after，仅恢复未 stale 的精确样本，失败保持不变；没有其它地形通道的综合 receipt。不同/部分。〔E2/E3/E7/E11；V6〕
59. **恢复身份边界。** 包手册禁止 Build Mode 下改变 terrain 分辨率/尺寸，否则原 cell index 失效；我们 receipt 检查 width/height、indices 递增/范围、有限值和当前 after 值，但没有 Heightmap owner ID、世界映射、heightScale 或 generation。相同尺寸/数值的另一个 terrain 不会仅凭 receipt 被区分。明确核心约束缺口。〔E7；V6〕

## 6. Side Object、贴花与资产约束

60. **三种生成模式。** EasyRoads Mesh 实例、Procedural prefab 沿曲线重建、Shape 二维截面扫掠是不同模式；我们 placements+ObjectBuildLayer 覆盖实例化，通用 spline/mesh 能力可复用，但道路没有沿路变形 prefab 或 side-object 任意截面闭环。部分。〔E15；V2/V9〕
61. **连续设施与端件。** EasyRoads 支持 start/middle/end、只重复 middle、端口焊接/平滑、端盖、跨 connection 继续生成，以及 _start/_middle/_end 命名约定；我们离散锚点不表达连续护栏的区间 mesh、端件或跨路口衔接。缺口。〔E15；V3/V9〕
62. **区间作者开关。** EasyRoads road type→road→marker 控制特定 side-object active、start/end offset、xPosition、rotationAngle/distance/center；我们只在整 edge 控制左右开关与起终 offset，spacing/offset 是全局 bake 选项，没有按对象种类和 marker 区间的覆盖。缺口。〔E3/E5/E6/E15；V1/V2〕
63. **规则与互斥。** EasyRoads 以弯角、高差、坡角分类自动区间；bridge/retaining wall 可抑制 barrier，linked objects 可共享生成区间，instance 可 Keep Selected；我们仅用裁切范围、其它道路三维 clearance 和已接受锚点间距过滤，没有环境规则、区间竞争及作者固定选择。缺口。〔E15；V3〕
64. **放置参照和贴地。** EasyRoads 可 relative to center/left/right、双侧 mirror/180° rotation、terrain snap，以及包围盒覆盖地形的最低/最高/平均高度。现有 side.left/right 锚点新增 Center/Lowest/Highest/Average，显式局部矩形足迹通过公共世界查询消费，贯通快照、类型覆盖和撤销；标准 recipe 必须声明查询世界版本用于缓存失效。已新增 TerrainNormal/PathForward/PathNormal 作者规则与完整 Euler 输出，原生和 DLL 约束测试通过；已从实际导入 prefab 层级生成包围盒，通过强类型版本目录供标准 recipe 自动取样并贯通保存/撤销；仍未证明完整倾斜覆盖、弦向旋转、根姿态和真实设施渲染一致。正式场景发布链、增量恢复及持久化等 14 项定向用例通过。部分，见 `2026-10-14-road-side-terrain.md`。〔E15；V3/V9〕
65. **阶梯、隧道入口形变与碰撞。** EasyRoads _stepUp/_stepDown 固定高度段、_terrain/vertex color 控制入口顶点吸附，side-object 可 mesh/box collider 并调整 box scale；我们没有这些资产预处理/形变约束，road collider 只从道路 mesh 派生。缺口。〔E3/E15；V8/V9〕
66. **起终贴花。** EasyRoads ERDecal 有 roadType pair、prefab、priority、baseWidth/scale、heightOffset、start/end eligibility、endRotation 与 UV 区域；我们稳定 transition.start/end 锚点+通用加权对象选择可复用，但没有道路类型兼容筛选、按 asphalt 宽度自动缩放与贴花投影闭环。部分。〔E3/E4；V3/V9〕
67. **资产接口几何约束。** 包要求 procedural 切分边在一致 Z 位置、源长度接近 road resolution；custom connection 向外、pivot 对应包围盒中心、端口 UV 为 0/1（允许约 0.01 误差），不要封闭将接路的隐蔽端面，按顺时针编号；我们没有同类资产端口导入契约。它们是资产构建协议，不能当道路图几何限制直接移植。〔E4/E15；V1/V9〕

## 7. 输出、渲染、持久化和编辑约束

68. **UV/顶点色。** EasyRoads 有 UV clamp/phase、UV flip、planar、terrain-relative UV、UV4 detail、逐 marker 色/淡入淡出及连接 texture blend；我们主 UV 按实际距离/uvMeters，junction 与材料 group 派生色，不具备独立作者多 UV 契约。MeshBuild 有 color 能力不意味着 road authoring 已可覆盖。〔E3/E4/E5/E6/E17；V1/V3〕
69. **材质/物理材质归属。** EasyRoads roadType/road/connection/side-object 引用 Unity Material 与 PhysicMaterial；我们 CPU 核心用 RoadMaterial/group 和 collider data，由通用渲染、物理消费层绑定资产。这是保持依赖边界的设计差异，但目前缺少 road style 到多资产绑定的完整作者协议。〔E3/E17；V1/V8〕
70. **LOD/空间切块。** EasyRoads Build Mode 创建 LODGroup，level 按 mesh 屏占 bounds 阈值切换，resolution 为原 mesh 百分比；我们 artifact v2 有世界 chunk/LOD mesh 和内容 BuildKey，LOD 通过同一 baker 降低采样，不等价于已有屏占 LOD selector、流式加载或 CPU 增量 bake。部分。〔E4/E7；V8〕
71. **更新生命周期。** EasyRoads 有 Edit/Build/Restore/Finalize、显式 Refresh、若干 setter 的 refresh 控制、静态更新 callbacks；我们网络 mutation/revision、显式 bake、通用 editing transaction，核心没有 Unity build mode，Finalize 去脚本的破坏式动作也无直接对应。不同。〔E2/E6/E7；V1/V5〕
72. **撤销与原子性。** 包 known_issues 列了 Join/Detach/DeleteConnection 和深 undo stack 的失步、Build Mode 不建议 Undo；我们候选网络 validate 后 commit、inverse operation、stale revision 和 receipt conflict 保持明确失败原子性。不能将包里已知缺陷当必须复刻的行为，也不能从本审计宣称 Unity 所有操作都不原子。〔E12；V5/V6〕
73. **持久化结构。** EasyRoads 通过 Unity scene/prefab/GUID、项目日志和资产备份持久化；我们 roadNetwork schema v1 保存 nodes/edges/style/links/blocked links，忽略未知 object 字段，未知 schema/version 拒绝，候选完整恢复再发布；产物 schema v2 与路网快照版本不同。不同。〔E3/E4/E7；V5/V8〕
74. **导入/导出。** EasyRoads OSM/KML 可映射 road type、经纬度到 terrain bounds，支持 OBJ/Unity FBX exporter；DLL ERWay/ERModularRoad 有 OSM id/name/oneWay/lanes/surface/bridge/speed。我们无道路专用地理坐标导入、道路类型映射或 provenance；通用 asset export 不等于这条工作流。缺口。〔E2/E3/E4/E7；V1/V8〕
75. **输出预算与错误面。** 我们 lane 数每方向 [0,8] 且总数≥1，speedLimitMps (0,200]、trafficPriority [0,255]，finite 参数、采样 [2,4096]、mesh/placement/work/snapshot 硬预算及 Result 诊断；包 API 多为 void/bool/null/-1 与 console warning，未见公开等价预算协议。只能说公开证据不同，不能断言 DLL 内部没有任何保护。〔E2/E6/E9；V1/V2/V4/V5/V6〕

## 关键数据结构对照索引

实现进度：本报告保留初始审计时的差距。随后已经加入逐控制点 widthScale 与中心线
rollDegrees，并贯通派生输出、编辑、快照迁移和撤销；详见
[控制点剖面契约](2026-10-13-road-point-profiles.md)。这只覆盖宽度与单枢轴横坡子集，
不将包含多个子功能的整项差距标为完成，也不表示已取得 Unity 渲染验证。

后续已经加入开放/闭合任意截面、显式桥隧区间、连续 shape 设施及真正参与拼接的自定义
connection 网格/端口。完整范围和保留限制见
[截面与连接实现契约](2026-10-13-road-sections-structures-connections.md)；
2026-10-14 后续又加入 prefab 变形、start/middle/end、显式重复与兼容焊接、多臂设施配对，
以及高度/splat/detail/tree/hole 的原子写回、完整道路地形历史保存和撤销；接入现有 TerrainWorldWorkspace。
契约、测试与边界见 [道路扩展说明](2026-10-14-road-prefabs-facility-routes-terrain.md)。
同日后续已补 prefab 自动间距（平均到末端及精确段长余段裁切）、三角面平滑组、自动首尾 portal-only 开孔，
并贯通快照迁移、拆分/反转/事务撤销和 native terrain 写回。见
[间距、平滑组与入口洞口契约](2026-10-14-road-spacing-smoothing-portals.md)。Unity 6000.0.79f1 已恢复；8 类驾驶面/宽度/固定横坡场景的 16 对实帧通过固定 0.98 阈值，
并据顶点数据补 cubic 宽度策略和 T 内侧圆角。详见 [Unity 实测对齐](2026-10-14-road-unity-alignment.md)。
任意截面、prefab、完整桥隧、地形通道和材质/光照仍待双端验收。

当前剩余差异（仅更新相关子功能，不将初始编号中的整组能力标为完成）：

- 设施规则：按弯角/地形阈值自动分类桥隧、挡墙、护栏及实例级规则覆盖；随机间距/偏移、地形吸附与顶点色权重。
- 设施网格：Unity prefab/GUID 导入与命名预处理、台阶模式、自动移除重复内部端面、跨独立路口烘焙批次的全局平滑、碰撞体类型/缩放和完整 UV 编辑器。
- 洞口：当前为显式隧道区间首尾的米制深度；尚无 EasyRoads 独立入口 offset、terrain vertex snap/blend、按地形交界自适应入口形状与各分辨率 Unity 实帧验收。
- 路口与车道：逐角人行道、geometry priority、道路类型共享预设、长度明确的截面 morph、环岛入口占用约束、逐 lane/turn 作者参数及左右行规则。
- 地形：保形/critical heights、完整 Indent/Surrounding 参数、专用纹理 texel expand/smooth、外部植被 provider 与整个 native World 对象/探针跨进程保存。
- 视觉：EVEngine 场景继续可运行；Unity 已可运行；基础路面、逐点宽度和固定横坡已验收，继续推进高级设施、桥隧、地形和材质的双端验证。

- `ERRoadNetwork/ERModularBase` → `RoadNetwork` 加通用编辑/资产消费层；不能一一映射。
- `ERRoad/ERModularRoad` → `RoadEdge` 加 bake snapshot；作者数据与 mesh/cache 要分开。
- `ERMarker/ERMarkerExt` → 当前 `RoadControlPoint.xyz/widthScale/rollDegrees`，加 style 宽度插值和框架策略；其它沿线参数与混合控制器仍缺。
- `ERRoadType/ERRoadShape` → 当前 owning `RoadStyle.crossSection` 支持任意开放/闭合截面；共享类型、逐 marker 截面替换与覆盖优先级仍缺。
- `ERConnection/ERCrossingPrefabs/ERConnectionData` → 当前 `RoadNode.connection` 有 owning mesh、yaw、显式端口及 seam 校验；模板、主臂优先与插入 prefab 原子流程仍缺。
- `ERIConnector` → 当前不同 style 的两条 edge/shared node；不能表达有长度的截面/材质 morph。
- `ERRoundabouts/ERRoundaboutElement` → 当前普通 graph 环与派生 roundabout placement；无环岛作者约束模型。
- `ERLane/ERLaneData/ERLaneConnector` → 当前 directional lane counts、RoadLaneConnection、RoadOverlay；逐 lane/turn 属性和曲线覆盖缺失。
- `CrossingCornerClass/SidewalkPresetClass` → 当前 node radius 与整边 style；无独立角/左右路缘预设。
- `SideObject/ERSORoad/ERSOMarkerExt/ERSideObjectInstance` → 当前 placements、连续 shape、变形 prefab、端件/间距/平滑组与多臂 facilityRoutes 已有；环境自动分类、覆盖层及导入预处理仍缺。
- `ERDecal` → 当前 transition anchors+资产选择；无完整道路兼容、投影和比例约束。
- `ERTerrain/ERTerrainData` → 当前 RoadTerrainWorkspace 与 native TerrainWorldWorkspace 支持 height/splat/detail/tree/hole 原子写回和历史；完整 Indent/Surrounding、外部植被集成及整个 World 保存仍缺。

## 未确认项与不能照搬的限制

- 未反编译/运行 DelaunayER 或生成器：三角化具体策略、退化容差、内部重建/dirty graph、复杂度及随机源不可据字段名确认。
- 混淆字段和历史 cache/old/tmp 数组不能直接定义为新的可编辑事实；元数据仅证明结构存在。
- 未在 Unity 中执行相容性、环岛空间、side-object 规则及 traffic 示例，因此手册约束与序列化样例不等价于运行验收。
- 包内历史手册有未启用/待实现功能：custom bending、Stream OSM、部分 texture offsets/Blend Presets 等，不纳入已完成能力；Beta 测试层也不作为外层生产能力。
- 本报告不能声称列全封闭 DLL 的一切内部约束；已列出与当前道路核心相关、可以从文档/元数据/序列化/源码验证的差异。

## 优先改进建议

1. 先建立逐路径位置的作者参数，支持截面宽度/roll、左右 Indent/Surrounding、显式 deformation/bridge/tunnel 区间；复用 SplinePath，而非新建道路 manager。先明确插值、反转、split、merge、快照与 undo 规则。
2. 再建立可复用截面与接入口相容性约束，补 geometry priority、逐角人行道和有长度的宽度过渡。资产端口要真正参与 seam/碰撞/traffic 构造，不能停留在 placement。
3. 将 Side Object 提升为可派生的连续区间输出：实例、扫掠、变形 prefab 分成本层；与 terrain/provider 交互仍走已有通用接口。
4. 独立补 lane/turn authoring：LHT/RHT、转向语义、单 turn 曲线/速度/stop；traffic simulation 留给交通/车辆领域。
5. 地形多通道写回与恢复使用 owner-qualified 身份和 mapping 契约；保持原子发布、失败注入与 provider present/absent 验证。

以上是建议，不是本轮已实施功能。本轮只增加比较文档，沿用当前单一网络权威、Result、module boundary 与显式成本规则，无架构例外。

## 证据索引

E 编号均为上述 unitypackage 内的原始路径，非互联网内容：

- E1：`Assets/EasyRoads3D/html/hierarchy.html`；`Scripts/runtimeScript.cs`。
- E2：`Assets/EasyRoads3D/html/ERRoadNetwork.html`；`Scripts/runtimeScript.cs`。
- E3：外层 `Assets/EasyRoads3D/lib/EasyRoads3Dv3.dll` 只读元数据；Beta 主包相同路径 DLL 用于版本差异复核。
- E4：`Assets/EasyRoads3D/html/general_settings.html`。
- E5：`Assets/EasyRoads3D/html/scene.html`。
- E6：`Assets/EasyRoads3D/html/ERRoad.html`；`ERRoadType.html`。
- E7：`Assets/EasyRoads3D/html/terrain_control.html`。
- E8：`Assets/EasyRoads3D/html/creating_roads.html`，包含历史说明，需结合较新 API 与元数据。
- E9：`Assets/EasyRoads3D/html/ERConnection.html`；`ERConnectionData.html`。
- E10：`Assets/EasyRoads3D/html/intersections.html`。
- E11：`Assets/EasyRoads3D/html/release_notes.html`；`Scripts/runtimeScript.cs`。
- E12：`Assets/EasyRoads3D/html/known_issues.html`，已知问题可能是历史债务，未运行复现。
- E13：内部 `Beta Release/EasyRoads3D.v3.3b21.unitypackage` 的 `Scripts/Traffic AI/ERTrafficManager.cs`、`ERVehicle.cs`；其中注释注明最低 v3.2，不证明外层 DLL 已公开相同命名 API。
- E14：`Assets/EasyRoads3D/Resources/ERRoadNetwork.prefab`、`ERProRoad.prefab`、`dynamic prefabs/Default X Crossing.prefab`、`Default T Crossing.prefab`、`Roundabout 4 connections.prefab`；`Assets/EasyRoads3D scenes/scene main.unity`。
- E15：`Assets/EasyRoads3D/html/side_objects.html`。
- E16：`Assets/EasyRoads3D/Scripts/ERVegetationStudio.cs`。
- E17：`Assets/EasyRoads3D/html/materials.html`。

V 编号为当前工作树源码：

- V1：[RoadTypes.h](../../src/modules/procgen/road/RoadTypes.h)、[RoadNetwork.h](../../src/modules/procgen/road/RoadNetwork.h)、[RoadNetwork.cpp](../../src/modules/procgen/road/RoadNetwork.cpp)。
- V2：[RoadBake.h](../../src/modules/procgen/road/RoadBake.h)。
- V3：[RoadBake.cpp](../../src/modules/procgen/road/RoadBake.cpp)、[SplinePath.h](../../src/modules/procgen/spline/SplinePath.h)。
- V4：[RoadProfile.cpp](../../src/modules/procgen/road/RoadProfile.cpp)。
- V5：[RoadNetworkEditTarget.cpp](../../src/modules/procgen/editing/RoadNetworkEditTarget.cpp)、同目录 EditApply/EditOperations/EditingCommands。
- V6：[RoadTerrain.cpp](../../src/modules/procgen/road/RoadTerrain.cpp)、[Heightmap.cpp](../../src/modules/procgen/heightmap/Heightmap.cpp)。
- V7：[RoadTraffic.cpp](../../src/modules/procgen/road/RoadTraffic.cpp)。
- V8：[RoadArtifact.cpp](../../src/modules/procgen/road/RoadArtifact.cpp)、[RoadRecipes.cpp](../../src/modules/procgen/road/RoadRecipes.cpp)。
- V9：[ObjectBuildLayer.h](../../src/modules/procgen/ObjectBuildLayer.h)、通用 PointSet/Grid mask 算法，以及道路设计文档中相应已完成项。

本地只读提取物与元数据记录位于 git-ignored `build/easyroads-audit/`，不将 Unity DLL、源码、原始场景或资产提交到仓库。

第二批真实双端轮廓测试已扩展到 16 类、32 对图像；14 类通过原定 0.98 阈值。新增剩余差异：弯道上坡的横截面框架（透视 IoU 0.850801）和变化横坡的属性插值（0.972013）。完整输入、参考顶点与失败帧见 2026-10-14-road-unity-alignment.md，未降低阈值。

第三轮已修正弯道上坡的截面框架，真实透视 IoU 0.850801 -> 0.991147，16 类中现有 15 类通过。变化横坡仍未通过，保留实际顶点用于进一步确认插值规则。新增 WorldUp 策略保存/迁移/撤销已贯通。


## 当前实现复审（2026-10-14，覆盖原 1–75 项）

本次重新读取作者类型、烘焙路径、编辑 codec/命令、地形 workspace/native adapter、recipe/artifact、相关合约测试与 comparison.json。
本轮仅修正文档，不修改 API 或实现；未重新运行编译和测试。测试覆盖依据为仓库用例及既有结果，不能替代新的运行验收。
不按原 75 个混合条目计算完成百分比：一项常同时包含已实现、尚缺及设计差异。

### A. 已实现，不能继续列为“没有”的子功能

- 8/11/12：逐点 widthScale/rollDegrees；新路 cubic 宽度、WorldUp 框架和 span-parameter smoothstep 横坡，旧快照保留原政策。横坡轴心目前固定中心。
- 13/14：任意开放/闭合截面、分段材料和凹端盖；可表达非对称几何，但没有独立左右 lane/sidewalk 预设与逐节点 hardEdge/UV 作者参数。
- 21/22/24/33/67：自定义 connection 的源 mesh、显式 local ports、yaw、边绑定及世界 seam/截面匹配参与实际烘焙，不再仅 placement。尚不等于 Unity prefab 导入协议。
- 51/52：显式桥隧区间、隧道壳体、首尾 portal-only 地形开孔、拆分/反向入口保持；尚无完整入口 terrain snap 与 bridge 引道参数。
- 54/56/57/58/59：多 tile 多通道原子写回、纹理层归一化、detail/tree 清除、稳定 tile/resource 身份、全通道历史保存和撤销；native world adapter 已接入。独立 Heightmap receipt 的 owner 边界问题没有被 workspace 自动消除。
- 60/61/62：连续 section sweep、源 prefab 变形、start/middle/end、平均/精确自动间距、余段裁切、兼容焊接、平滑组与显式多臂一对一衔接；具备每设施区间与偏移。
- 70/72/73：chunk/LOD 产物、原子编辑、反转/拆分补偿、版本化网络/道路地形快照已有；不能将缺少 Unity build mode 算成没有保存或撤销。

证据：RoadTypes.h、RoadBakeEdges/Profiles/Structures/FacilityLinks.cpp、RoadFacilityPrefab.cpp、RoadConnection.cpp、
RoadTerrainWorkspace*.cpp、heightmap/TerrainWorldRoad.cpp、editing/RoadNetwork*Codec.cpp，
及 test/procgen_road_authoring.cpp、procgen_road_extensions.cpp、procgen_road_spacing_portals.cpp、procgen_road_reference.cpp。

### B. 确实尚未实现或仅有部分底座

1. **类型与作者层（4/5/7/8/62）**：同文档的共享类型、父链传播、道路/marker/设施字段覆盖及名称/锁定已在第一组落地，见文末进展。仍缺跨文档/项目预设资产同步、layer/tag/static/castShadow 消费投影和专用作者面板；已有通用编辑命令与 gizmo。
2. **路径与截面（9/10/11/13/15/16/18/19/20）**：逐段 Circular 验收（已实现数据链路，镜像透视尚未通过，见第六组）；marker 作者操作手柄（逐 marker strength 已在第四组落地，自由 Bezier 入/出 handle 尚无参考确认）；地形模式横坡角度约束（可移动横坡轴心已落地，见第七组）；独立左右 lane/shoulder/sidewalk 参数；逐 marker 截面替换及 morph；单 road 闭合 seam；具名随机起伏/横坡；米制、角误差、横向细分采样的参考离散/默认策略验收（已有原生数据与消费链路，见第八组）；纵坡作者工具。工程最大坡度/设计速度求解器不算已证实的参考能力。
3. **贴地与地形过渡（17/47/48/49/50/53/54/55/56/57/59）**：FollowTerrainContours/SnapToTerrain；左右逐点 Indent/Surrounding、网格相关 minIndent、Road/Terrain/Surrounding 高度基准、保形/平滑/critical heights；按真实地形求桥墩落地和挡墙区间；跨 tile 缝的专用一致性/选择道路协调；originY/Unity normalized-height adapter；texel expand/smooth；外部 Vegetation Studio/biome provider；独立 Heightmap receipt 的 owner/mapping 身份加强。
4. **路口作者约束（23/25/26/27/28/29/30/31/32/34/35）**：原子插入 prefab 并拆边绑定指定端口；专用 dynamic T/X/Y/flex/IConnector 等模板协议；主臂对齐、镜像与 T straight bending；几何 priority；逐角 radius/segments/preset 和人行道连通传播；可复用城市路缘/sidewalk preset；有长度的截面/材质过渡；参数化环岛入口、占用弧和空间约束。已有通用自定义网格/图环不是这些作者模型。
5. **车道/转向/标线（37/38/39/41/42/45/46）**：LHT/RHT；逐 lane 类型/宽度/横向位置；语义化转向许可；逐 turn curve/handle、速度和 stop；逐分界线实/虚/双/混合样式；逐入口/车道箭头和 zebra/对角选择。40 的异车道连接算法不同，尚缺参考行为验证。
6. **设施规则与资产预处理（60/61/62/63/64/65/66/67）**：按曲率/高差/坡角自动分类与互斥、linked-object 区间和 Keep Selected；随机间距/偏移/旋转、中心/左右参照与资产 bounds 贴地；命名端件/GUID prefab 导入、阶梯模式、重复内部端面移除；跨独立烘焙批次平滑；入口 offset/vertex-color snap/blend；mesh/box collider 类型与 scale；贴花兼容筛选、比例与投影。源几何值导入、保留 UV 和局部平滑不等于完整资产工具链。
7. **渲染与运行产物（68/69/70）**：道路多 UV/phase/flip/planar/terrain-relative、逐 marker 顶点色/淡入、连接纹理混合；style 到渲染/物理材质资产的作者绑定；道路专用屏占 LOD 选择、streaming 和增量 bake 发布。已有材料组、碰撞几何与 chunk/LOD 网格，不能重新列成完全没有。
8. **数据工作流及生产消费入口（71/73/74，加本次发现）**：道路 OSM/KML 地理坐标/类型映射/provenance 与专用导出；整个 native World 对象/探针跨进程保存。完整作者网络的标准 recipe/artifact 快照协议已实现并通过实际 CPU composite 生成、LOD、发布及 JSON 保存恢复测试。跨模块 GPU/physics provider 消费与专用脚本/UI 入口仍需补齐；不能把 renderer 直接上传当前 mesh 的验证当作这些消费入口已贯通。

上述为道路闭环缺口，不断言 EVEngine 通用模块不存在对应底座。

### C. 已实现但未通过，及尚未做双端验收

当前真实 comparison.json：20 场景、40 对透视/俯视图，20 场景达到固定 IoU>=0.98，0 different/blocked。
此前失败的 variable-bank 已以真实参考确认的 smoothstep 政策修正，当前透视 0.997533909、俯视 0.999887151。详见第三组进展；这仍只证明驾驶面轮廓。
宽度、基础曲线、坡顶/凹谷、弯道上坡与 T/X 的通过只证明 unlit 驾驶面轮廓，不证明 UV、光照、碰撞或交通语义相同。
任意截面、完整桥隧壳体、prefab 变形/间距/平滑、多臂连续设施、自定义接入口、多通道地形及入口开孔已有 CPU 合约，
但当前 UnityCapture.cs 仍是 surface-only 适配，没有这些高级功能的真实双端验收。

### D. 不应作为缺失功能补齐的差异

1/2/3/6 的多网络值容器、作者/派生分离和稳定身份；36 的明确几何拒绝阈值；
43/44 的交通策略/仿真领域边界；71 的显式 bake；72 的更强原子性；73 的非 Unity 持久化；75 的 Result 和预算，
属于架构/策略差异或参考未确认行为。无需复制 Unity 单例、混淆缓存、已知 undo 缺陷或 void/null 错误面。
Beta 独有 API 与手册未启用功能仍不混入外层生产验收目标。

### 建议接续顺序

先确认并修正变化横坡，再贯通 authored-network→标准 artifact/recipe 消费入口；
然后补左右 Indent/Surrounding 与地形驱动设施、逐段路径控制/截面过渡、路口作者约束；
高级双端夹具与实现同时推进，之后补 lane/turn/marking、材质 UV、资产预处理和地理数据工作流。
本次没有实现变更、公共边界或持久化改动，无架构例外；不以文档审阅宣称编译/门禁通过。


## 第一组实施进展：共享类型与作者覆盖

2026-10-14 已实现 RoadPresetDocument：稳定共享 ID、父预设链、RoadStyle 所有字段的 owning optional 覆盖、道路级和逐 marker 宽度/横坡覆盖、逐设施 ID 的实例字段覆盖，以及名称/直接编辑锁定。
网络、定义与覆盖由一个作者文档拥有；解析后的只读图直接供 native bake，prepare/commit 原子发布并保留完整作者 undo/redo 历史。
支持 canonical Value/JSON 保存、历史恢复、旧可选字段迁移、循环/悬空身份拒绝、foreign/stale token 拒绝、锁定与显式解锁。
详见 [共享道路类型契约](2026-10-14-road-shared-presets.md)。
复审 B1 中共享类型/覆盖与名称锁定已不再是纯缺口；专用作者面板及渲染分类投影仍缺。本组当时保留的变化横坡失败已在第三组修正；B2–B8 的其他事项按后续进展更新。
本组当时仅有直接 native bake 消费；标准 recipe/artifact 完整作者快照入口已由第二组贯通。

## 第二组实施进展：完整作者网络的生产 recipe 输入

2026-10-14：已通过 RoadNetworkCodec 的既有 schema-v1 owning Value/JSON 快照与 setRoadRecipeNetwork/resolveRoadRecipeNetwork 接入标准 bakeRoadNetworkRecipe。mesh/overlay/artifact 及其 LOD 的 recipe 调用现在消费完整作者快照，错误显式输入拒绝而不回退 makeScene。全部 11 个高级功能场景改走此入口，并逐个确认与直接 bake positions/indices 相同。见 [作者 recipe 契约](2026-10-14-road-author-recipe.md)。

B8 中“只调用 makeScene、无标准作者网络 recipe 协议”的缺口已落地；原生完整 artifact 生成/发布、JSON 保存恢复和重复发布原子性已通过独立生产代码测试；跨模块 GPU/physics provider 消费与专用脚本/UI 入口仍未完成，OSM/KML/provenance/导出及 native World 对象探针保存继续保留。其余 B1–B7 和 variable-bank 双端失败仍待完成；不宣称整体核心对齐完成。

## 第三组实施进展：变化横坡参考对齐

2026-10-14：真实 EasyRoads 13 场景 API/网格探针确认每段参数 u 的 smoothstep 横坡，已添加 RoadRollInterpolation 并贯通共同路径、style/preset 覆盖、native/editor JSON 和 preset-v1 根默认迁移。roadPresets 新保存版本为 2；旧道路快照缺政策时仍为 Linear。详见 [真实横坡契约](2026-10-14-road-roll-reference-contract.md)。

当前真实 comparison.json 为 16 passed / 0 different / 0 blocked（32 对图像，阈值未变 IoU>=0.98）。variable-bank 透视 0.997533909、俯视 0.999887151；curve-width-bank 透视 0.994755298、俯视 0.995364385。上面的复审 C 的 15/16 和 variable-bank 失败属于此前快照，已由本组验收替代。B2 的逐段控制、strength、轴心等仍缺；其他高级功能的 Unity 双端验收及其余各组未完成，目标继续保持。

## 第四组实施进展：出段 Curve Strength

2026-10-14：逐 marker splineStrength 已进入共同 native 路径、共享类型/道路/marker 覆盖、反向出段映射、拆分采样、保存/撤销和标准 recipe/artifact。缺失字段保持 .5。新 strength-quarter/full 真实双端场景通过，当前 18/18 场景（36 对图像）通过固定 .98 阈值。见 [出段强度契约](2026-10-14-road-spline-strength.md)。B2 的 strength 不再是缺口；StraightXZ/XYZ/Circular、handles、轴心及其他分组仍未完成，目标继续。

## 第五组实施进展：StraightXZ / StraightXYZ 与邻段约束

2026-10-14：两种出段控制及真实参考确认的后继 Spline 切向约束已进入共同路径。反向按出段重排控制与起/末端约束，保存/恢复和预设覆盖/undo/redo 均贯通。相邻约束投影当前弦线 XZ 并置零约束端 Y 导数，已用不等长和高差真实探针验证；未只停在初步 .98 通过结果。见 [直线控制契约](2026-10-14-road-straight-controllers.md)。当前真实 20 场景 / 40 对图像全部通过固定 .98，仍只证明驾驶面轮廓。Circular、轴心、handles、完整地形/路口/设施/渲染等其余缺口继续保留，整体目标未完成。

## 第六组：Circular 当前实现与未通过项

2026-10-14：圆弧 XZ、继承高度、反向方向参考、共同路径消费、预设覆盖、保存/恢复和 undo/redo 已贯通。非法圆弧编辑在发布前拒绝，反向 193 个全路采样保持几何，双反向恢复作者快照。参见 [Circular 契约与证据](2026-10-14-road-circular-controller.md)。实际双端 22 场景 / 44 对图像中 21 场景通过固定 .98；正向 Circular 两视图通过，镜像透视 .974120 未通过，镜像俯视 .988611 通过。邻接高度与出口离散规则仍待更多参考确认，B2 Circular 不标为完整对齐。整体核心目标继续，其他八组缺口保留。

## 第七组：横坡轴心

2026-10-14：逐 marker 有符号 rollCenterOffset 已进入唯一作者数据、实际截面帧、标准 recipe/bake 消费、预设覆盖、保存/恢复、反向、拆分和 undo/redo。新增左右边缘轴心实际 EasyRoads / EVEngine 帧全部通过固定 .98，包括逐 marker 变化轴心的第三个场景也通过；当前 25 场景中 24 passed、1 different、0 blocked；镜像 Circular 未通过项保留。见 [横坡轴心契约](2026-10-14-road-roll-pivot.md)。B2 的可移动横坡轴心不再是未实现项，但地形模式角度约束、handles、非对称 lane/截面、闭合 seam 及其他分组继续补齐。

## 参考能力边界复核：handle

2026-10-14：重新检查 outer-package scene 手册与 ERMarkerExt 的公开 86-field 元数据。手册的 marker position handle、slider handle、rotation gizmo 是作者工具；Curve Strength 为已确认的曲线参数。当前 API/字段证据没有证明道路支持自由 Bezier 入/出 tangent handles，不能从 generic SplinePath 的能力反推 EasyRoads 缺口。该自由 tangent 能力归入参考未确认项，marker/连接口作者手柄操作仍保留在作者 UI 缺口，不把后者标为完成。

## 第八组：米制、转角和横向采样的当前链路

2026-10-14：新增 owning samplingSpacingMeters / samplingAngleDegrees / crossSectionSubdivisions、共享 preset 字段、标准 recipe 消费与 metric LOD 密度；保存、undo/redo、旧字段缺失迁移和输出预算贯通。通用弧长跨 marker 跳变已修复，metric 采样保留精确 marker。原生全部作者及通用 Spline 契约通过，实际重拍 30 场景 / 60 对图像为 28 passed、2 different、0 blocked。新增四类图像通过，20° 粗转角俯视 .974611 与镜像 Circular 透视 .972002 未通过。

详见 [米制采样实现与剩余语义差异](2026-10-14-road-metric-sampling.md)。B2 第19项保持部分：native 最大间距策略与参考参数网格/距离触发策略尚不等价；默认值、mixed controller 接缝及参考 Sub Segments 实际消费仍待完成。B7 的生产者算法修订缓存身份已随后落地，见算法缓存文档；自动源代码变化识别、完整依赖跟踪与增量烘焙仍未完成，不能用 recipe 参数哈希替代算法修订。整体目标保持进行中。

## 第九组：参数扫描与统一帧入口

2026-10-14：显式 ParametricDistanceTurn 策略、共享 preset/codec/save/undo、跨 edge probe 预算及生产 artifact/LOD 已贯通。普通与显式参数采样复用单一帧姿态实现，保留真实弧长 UV 相位。20° 粗转角中心点回归与两端轮廓通过固定 .98；当前 30 场景/60 对图像为 29 passed、1 different、0 blocked。镜像 Circular 的透视仍未通过。见 [参数扫描证据与剩余范围](2026-10-14-road-parameter-scan.md)。

B2 第19项继续保留细角度端部过滤、新建默认/旧 preset 默认迁移、更多 controller/结构组合参考和 Sub Segments 自定义 shape 实际消费；不能以这五个驾驶面场景通过标为完整采样或整体核心完成。旧最大间距政策通过可选枚举保留，未把两种语义混入同一无版本字段。

2026-10-15 renderer 消费进展：StaticPrefabRenderInstance 已消费拥有的 prefab preparation，创建实际 ECS Renderable3D，保留完整 world*local 矩阵、材质和阴影标志，使用代际句柄处理外部删除及槽位重用。正式 SHARED 编译和 17 项定向组合测试通过；真实导入→cook→evpack→GPU 上传→ECS→forward readback 用例检查实例出现及释放后消失，见 2026-10-14-road-side-terrain.md。它补上单实例绘制链路，仍不等于标准道路批次自动实例化、Scene 原子绑定/恢复或 EasyRoads 设施双端对齐。此次共享库重编译也使先前 35 场景的 v2 exe/DLL 哈希记录成为历史运行证据，重新比较当前运行时之前应刷新道路帧，不能继续称它们为当前 DLL 的证明。

2026-10-15 批次事务补充：Scene 多批次替换已增加 checked 的 prepared-owner 协调提交入口，覆盖失败/异常回滚候选 host、重入保护及提交后通知异常的 Applied 诊断；缺少该可选能力的 provider 明确 Unsupported。它提供 renderer 资源发布与 Scene 一致提交的生产边界，但标准道路 PointSet 到版本资产目录/renderer 批次的自动消费、删除及保存恢复仍需接线，不能标为设施批次功能完成。

2026-10-15 标准设施消费补充：已新增 native ProcgenPrefabSceneSink decorator，在显式 admitted prefab 目录及 composition 安装后，标准 PointSet 发布、快照、共享增量和协调删除可创建实际 GPU renderer。过期/缺失资产及外部提交失败保留旧画面；cell 缓存恢复内容重新发布后可重建同样画面。此结果补上 native 生产消费段，不等于默认启动/脚本 UI 自动安装、Scene JSON 自动 renderer 恢复、热重导入、跨 variant/device、Scene Link 或 EasyRoads 设施外观对齐，详见 2026-10-14-road-side-terrain.md。

2026-10-15 多版本补充：native GPU/CPU 目录现可同时接纳同一 AssetRef 的不同 package build，目录 resource pin 保证历史版本在 snapshot/undo 内容恢复时仍可重建。真实重导入用例验证旧红色保持、新绿色/加宽 geometry 发布、旧 build 精确恢复，以及 GPU 释放后 CPU bounds 可查。仅覆盖显式 native 版本 admission 与重新发布，默认 watcher/编辑器命令、直接 Scene JSON 恢复和 EasyRoads 设施帧仍未完成。

2026-10-15 设施参考探针补充：`scripts/road_parity/UnitySideObjectContractProbe.cs` 已通过 Unity 6000.0.79f1 实际执行（日志完成并退出码 0）。新建 network 的设施库为空；对唯一公开 `List<SideObject>` 字段临时加入 sentinel 后，`ERRoadNetwork.GetSideObjects()` 返回同一对象引用，移除后清理，确认可通过公开作者数据建立受控设施模板。新增 ERSORoadExt 的公开签名/字段记录供后续道路激活探针使用。证据为 `build/road-parity/unity-side-object-contracts.json` 的 `collectionObservations` 与同名 log；未读取 IL 或私有方法。该结果只确认模板数据入口，尚未证明实例生成、根姿态/包围盒语义或双端设施外观，不关闭 B6。首次受限启动因包管理器 IPC 失败终止，随后允许本机缓存写入的独立运行成功，失败运行不计为契约证据。

2026-10-15 设施生成入口复核：探针新增实际 prefab 资产（非零根 position/Euler、非均匀 scale）、GUID、公开模板注册和道路激活；修正 SideObject 必须以 ScriptableObject.CreateInstance 创建的 Unity 生命周期要求。最新 Unity 编译/执行退出 0，但 SideObjectSetActive=True、三个 marker 激活均 False、instances=0、points=0。因此仅“模板注册/道路级激活”得到确认，marker 关联数据尚未建立；不得从这个空结果推断 prefab 根变换语义或宣称设施对齐通过。完整观测保存在 JSON generationObservations。下一步应检查公开 ERSORoadExt/marker 关联结构与 SetSideObjects 消费条件，建立非空设施输出后再测变换；B6 保持未完成。

2026-10-15 设施关联进一步取证：公开反射补齐静态方法后，确认 ERSORoadExt.CreateInstance(SideObject)、ERSOMarkerExt.CreateInstance(SideObject,bool) 和 ERSORoad(SideObject) 构造器。探针使用这些入口建立基础/扩展道路记录及三个 marker 记录，记录 ID 均为 710032；同步 BuildRoadNetwork(false,false,false,roads) 执行完成仍无实例。重要修正：SideObjectSetActive 返回 False 时，GetSideObjectActiveState 实测为 True，因此不能把 setter False 单独解释为激活失败；上一条 marker False 也只能作为返回值观察，尚无证据证明其状态失败。当前确认数据记录齐备及道路设施 active 状态，生成入口仍待定位。根姿态语义保持未确认，不据空实例更改 native 变换规则。

2026-10-15 设施非空参考已建立：启用公开 buildSOinEditMode、rebuildSos 并调用 Refresh/UpdateSideObjectsInScene 后，实际 ERSideObjectInstance 下生成 7 个源网格 renderer（含子节点用例为 14 个），尽管 SDK GetSideObjectInstances/Points 仍为空。因此“空 getter 意味无实例”的前述判断被实际层级观察修正。道路级和三个 marker 状态查询均 True。

五组独立真实运行（identity/translated/rotated/scaled/child）均编译成功、执行完成、退出码 0。关闭随机 bump 后，根 position/rotation 两组的全部 renderer 观察值与 identity 完全相同；源根 scale(2,3,4) 保留。源根带平移和旋转时，子节点 localPosition(1,.5,2)、localRotation Y=35 与局部 scale 仍保留，首实例子节点 worldPosition=(2,1.52,8)，非均匀根缩放后的 boundsSize=(2.253093,2.25,5.242913)。范围仅为 Mesh 类型0、直线、默认 alignment；其他对齐/旋转随机规则未确认。

这确认一项实际差异：当前通用 native prefab renderer 完整 world*canonicalLocal 是通用资产契约；道路设施需要覆盖源根平移/旋转并保留根缩放及子层级的明确放置语义。尚未把该覆盖贯通道路作者字段、bounds provider、渲染、保存/撤销和测试，故该项继续未完成；不能直接全局修改通用 prefab 导入/渲染，否则破坏正常资产姿态。渲染和地形 footprint 必须消费同一根姿态规则，不能只调整画面。

新增 scripts/verify_road_side_pose.py 固化上述五组非空几何、状态、transform/bounds 约束及 report/log SHA256，生成 build/road-parity/side-pose-evidence.json；nativeParityVerified=false。真实五组验证通过，删除 child 几何的失败注入被拒绝，ruff 与 git diff --check 通过。另实测请求间距5时 Z 间距5.005、最后点30.03、固定高度.02，原因仍待独立参数扫描；未把它偷偷归一化成精确5。此阶段未改变 native 公共API/格式/模块边界，无架构规则例外。

2026-10-15 原生根姿态策略已接入：新增稳定枚举 PrefabRootPose，共同消费于道路绑定、bounds 查询、PointSet asset_root_pose、Scene 快照/标签和 renderer preparation。新建道路设施默认覆盖源根 position/rotation，通用资产默认 Authored；旧道路快照或 PointSet 缺失字段继续保留 Authored。模板解码器的 identity-only 容器不是源根，覆盖实际作用于其下的源根节点；根 signed scale 和子节点局部矩阵保留，加载时同时聚合两套不可变 bounds，查询缓存含策略，算法修订 v11。

正式 Windows strict SHARED 构建及 22 项定向组合测试全部通过（prefab-root-pose-tests-final.log）；实际导入/cook/evpack 用例确认非零源根平移被覆盖，带 90 度根旋转、反射 scale(-2,3,4) 和非零子位置的层级输出保留正确矩阵与 bounds；真实 Vulkan 验证改变实例位置并覆盖源根后，画面 mask 与原等价完整矩阵精确一致，非法模式不改变旧 revision/画面。道路 footprint 高度、预设保存后的 undo/redo、缺字段迁移和非法 enum 拒绝均通过。两个旧 Scene 用例初跑失败发生于尚未重编译的 graphics 消费者；重编译后两者通过，最终不使用旧二进制结果。无图形 procgen-core-only OBJECT 的 EVProcgenCoreCheck 编译通过，证据仅 L1，不代表完整 renderer OBJECT 链接。

本轮 HEAD 增量架构契约通过（101 条），git diff --check 通过；完整 make check 固定本轮核实的 origin/dev SHA 10f6faa7509e4a108ee5f2e6b6e3be357e99af26 执行，最终结果须以 prefab-root-pose-source-final.log 为准。证据清单 build/road-parity/prefab-root-pose-evidence.json 记录源码、日志、测试 exe 和 DLL 搜索目录哈希，明确不是 OS 已加载模块追踪。

此项已有 native 生产消费与作者历史链路，仍未完成：placement 模式 cell 内容恢复的专门断言、默认 host/编辑器接线、直接 Scene JSON 自动恢复 renderer、完整 renderer OBJECT 链接，以及同一设施几何的双端真实画面。五组 EasyRoads 参考只覆盖默认直线 Mesh 类型0，其他 alignment、随机旋转、曲线 bounds 对齐与间距规则仍独立保留，不以本轮通过标记 B6/B7 或整体核心完成。

2026-10-15 本轮完整源码门禁终态：prefab-root-pose-source-final.log 已结束，格式、依赖层次、脚本 API 覆盖、测试清单、质量元数据及 10-profile 矩阵均通过；最终在架构增量检查报告同样七项既有工作树/分支差异问题：IStateProvider.h:38/45/48 的 bool 操作、RenderSystem.h:42/43 的 pointer lifetime、Sensing.cpp:626 的能力目录范围、Vehicle.cpp:254 的 runtime 模块查找。没有修改基线、allowlist 或相关无关代码来隐藏问题。HEAD 增量目录检查通过，整体 make check 未通过；证据 JSON 已写入固定 base、七项原文与日志哈希。

2026-10-15 placement cell 专门验证已补齐：asset_import_unity_fbx.cpp 在 placement 模式完成 cell generation/serialize，移除原批次并确认真实 GPU 红色像素归零，再用新 RuntimeGeneration 反序列化并检查 asset_root_pose=placement、精确 build ID 和 PointId=1001。将恢复 PointSet 重新标准发布后，真实 GPU mask 与删除前精确相同；随后非法模式保持恢复后的 revision/画面。两个正式 asset 用例通过，源文件 928 行；变更行 clang-format 和 git diff --check 通过。恢复帧为 build/road-parity/host-profile/test/out/static-prefab-placement-restore-vulkan.png（engine-owned getPixel readback），证据为 prefab-placement-cell-evidence.json。与上轮记录逐项核对，本轮生产代码和 DLL 哈希均未变化，只有测试 exe/测试源码变化，因此旧 22-case 记录仍作为上轮记录保留，不冒称此次又完整重跑 22 项。上条 remaining 中 placement 模式 cell 专门断言已关闭；同一设施双端画面、默认 composition/编辑器/Scene JSON 自动恢复及完整 renderer OBJECT 链接仍保留。没有新增公共 API/格式/模块边界，本轮不产生规则例外；完整源码门禁仍引用上轮七项未通过的原文结果。

2026-10-15 同几何设施双端帧首次通过：新增 UnitySideObjectFrameCapture 与独立 native road_side_graphics_probe --road-side-parity 工具，七个实际 EasyRoads 单位立方体设施对照原生 FBX 导入→EVA→evpack→admission→标准 PointSet→ECS→GPU readback。源根 position=(2,3,4)、Euler=(10,20,30)、scale=(2,3,4)；原生消费显式 placement 根姿态策略。640x480 透视/俯视两张均 IoU=1.000000，RGB 最大/平均误差均0。该 native 夹具有意使用实测5.005m点位，隔离根姿态与 renderer，不验证5m请求的原生道路间距；后者保持差异。

颜色边界明确：Unity参考项目 m_ActiveColorSpace=0（Gamma），捕获器实际检查 QualitySettings.activeColorSpace 并记录Gamma；原生默认ACES会得到背景(115,115,115)、设施(232,0,232)，而参考为(32,32,32)/(255,0,255)。校准使用公开 setSceneToneMapping(None)、exposure=1，将显示域.125按标准sRGB解码到原生线性背景；没有拟合曝光、颜色或调整几何。校准后的全RGB一致只证明显式无光照显示契约，不证明默认ACES与Gamma或完整PBR材质相同；原默认图和日志保留。新增生产行为仅位于测试探针/脚本，未更改引擎默认显示策略。

scripts/compare_road_side_frames.py 将真实Unity帧与运行时适配器/程序集SHA绑定，并校验原生exe/源码/显式DLL搜索目录及帧SHA（不是OS已加载模块追踪）。side-render-comparison.json 为当前结果。4项验证器单测覆盖旧适配器、新DLL、篡改像素，以及mask完全一致但RGB差1仍报告different；UTF8契约10项通过，ruff、C++变更行/新inc格式和git diff --check通过。原生strict SHARED probe target编译并执行完成。此轮无生产公共API/持久格式/模块边界变更，无架构规则例外；完整make check仍引用此前七项未通过结果，没有把本轮工具检查称为完整门禁通过。未更新35类驾驶面历史帧的当前性，也未关闭B6/B7的曲线对齐、随机/变形/连续设施、完整材料、默认composition/编辑器及renderer OBJECT等其余项目。

2026-10-15 最终独立工具复核：road_side_graphics_probe 已作为单独 EXCLUDE_FROM_ALL 桌面目标落地，仅在导入、procgen、Scene 和外层 graphics/scene-template 依赖齐备时提供；通用 UnityGraphicsProbe.cpp 恢复原样，没有新增导入器/procgen依赖。正式 host profile（strict ON、SHARED）完成1917步构建后，使用最终独立exe与新DLL重新捕获；两视图仍IoU=1、RGB最大误差0。新共享库下22项设施组合测试及14项脚本/UTF8测试均通过。side-render-run-evidence.json 将本次配置/构建/测试、比较器、参考与原生帧证据、间距报告和完整源码门禁关联；源码门禁本轮重新执行仍为同样七项问题，未把此前失败当作通过。由于共享库重编译，旧运行时哈希证据继续只作为历史；本次独立设施帧已有新的完整搜索目录哈希。

间距追加九组真实SDK测量：固定5m从0开始，严格排除路段末端（30m有0..25六个实例，31m有0..30七个实例）。Average Distance 样本符合 n=max(1,round-to-even(length/requestedSpacing))、step=length/n+0.005m，按n+1个实例放置；13m/5m使用3区间，12m/5m使用2区间，12.5m/5m半数取偶为2，2m短路也保留1区间；31m/7m和13m/2m额外量仍为每区间0.005m，并非请求间距的固定比例。公开GetLength/GetDistance及中心线末端均为精确作者长度，排除了“道路本身延长”的解释。verify_road_side_spacing.py 对九组报告、SDK程序集/适配器哈希与实际renderer位置进行约束校验，通过并输出 side-spacing-evidence.json。该数据仅直线/默认offset，曲线、侧向偏移、连接口和起末偏移尚未确认；原生现有半间距起点/分布政策尚未迁移，不标为已对齐。下一步需要把显式分布策略接入唯一作者数据、标准bake、缓存/保存/undo和约束测试，同时保持旧快照行为。


2026-10-15 原生刚性设施分布已进入生产链：RoadStyle 新增稳定枚举 LegacyCentered=0、Fixed=1、Average=2；新作者默认 Average，旧快照缺字段迁移为 LegacyCentered。公共验证拒绝未知枚举，preset override/codec、保存恢复及 undo/redo 共用唯一字段，生产算法身份更新为 road-core-2026-10-15-side-distribution-v12。Fixed 从零按请求间距放置并排除精确末端；Average 使用最近偶数区间数及每区间 .005m 参考偏移，自由末端沿端帧外推；作者排除区间和 junction socket 优先裁剪。新策略每 span 使用512个弧长探针，Legacy 保留32及旧累计浮点/半间距起点行为；公开 bake 成本文档已说明代价，没有修改通用 Spline API。

参考补充17.5m/5m奇数半数样本，验证器现有十组独立实际SDK数据。原生强断言覆盖十组纵向位置及 road_distance（.0001m容差）、标准 recipe 与直接 bake 一致、数量预算失败不修改输入、空区间、预设/保存/撤销及缺字段迁移。此前25项组合检查通过；六项 placements 契约已从大文件拆出并改用 REQUIRE，修正新默认数量，以及重复 marker 默认参数扫描必须拒绝、显式历史采样可分类的约束，最新六项全部通过。完整161项回归以 side-distribution-road-regression-final.log 终态为准。

本轮用重编译后的独立 road_side_graphics_probe 再次运行真实 GPU 捕获，side-render-comparison.json 两视角 IoU=1、RGB最大误差0；其 native runtime inventory 已刷新。此夹具仍使用实测点，不把它当作道路生产者间距证明。纵向直线政策已接入 native，不代表完整 XYZ 对齐：默认中心/侧向位置、.02m高度、曲线与 offset、连接口/随机/其他 alignment 的参考语义仍未关闭。完整 renderer OBJECT、默认 composition/UI、直接 Scene JSON 自动恢复以及 B1-B8 其余缺口继续保留。应用了强类型持久字段、缺字段迁移、单一事实源、Result、公共成本说明及六面架构契约；无规则例外。

2026-10-15 分布功能收尾终态：完整道路回归161/161通过；四项帧证据失败注入测试通过；独立设施帧两视图仍像素一致。split后完整make check已终止，格式/层次/绑定/测试清单及10-profile等检查通过，最终仍为前述IStateProvider、RenderSystem、Sensing和Vehicle七项架构问题，未豁免。side-distribution-evidence.json记录终态及日志SHA；procgen-core-only OBJECT仍仅L1证明。

2026-10-15 设施位置参考新增八组独立真实SDK实验：UnitySideObjectOffsetProbe 对宽6m平直路、单位cube、5m Average运行baseline/x2/y1/position1/position2/center-x2/yaw45/align1。每组七个非空renderer，全部执行完成且Unity退出0。默认position=0中心x=0，position=1为x=-3，position=2为x=3；xPosition=2得到x=2，relativeToCenter=true在该中心样本不改变结果；yPosition=1得到y=1.02，默认y=.02；yRotation=45得到实例yaw45。align1在平直路与默认相同，不能据此推断坡道/曲线方向语义。仅公开字段、构造和SDK执行观察，无IL读取。verify_road_side_offsets.py逐实例约束位置/旋转、适配器与程序集SHA及日志终态，输出side-offset-evidence.json，nativeParityVerified=false。

这确认B6仍缺一项明确的作者基准：当前原生side.left/right使用整个道路截面外侧加options.sideObjectOffset，尚未提供与参考position=中心/左右沥青边缘及独立x/y/yaw等价的唯一作者字段。不能以道路现有外侧锚点或默认height代替参考语义。下一步应在保留旧快照外侧行为的前提下，设计强类型基准及偏移策略，让实际点位、旋转、bounds footprint、保存/预设/undo与同一生产recipe共同消费，并增加曲线/坡道参考后验证alignment。原有间距与双端捕获适配器源码未改变，旧哈希可继续校验。

2026-10-15 原生设施放置基准落地：新增 owning RoadSidePlacement，稳定枚举 LegacyOutside=0、AsphaltEdges=1、Center=2，加有限有界 lateralOffset/verticalOffset/yawDegrees。它是 RoadStyle 的唯一作者真源，preset 可整体继承/覆盖，公共验证、共享codec、标准 recipe、保存、undo/redo 均消费同一字段；算法身份 v13。旧字段缺失及当前默认均保留零偏移 LegacyOutside，原生默认布局仍与参考中心基准不同；显式 AsphaltEdges/Center 忽略旧 bake sideObjectOffset，横向作者偏移在宽度缩放之后加到 path-right，高度在地形选高之后按 world-up 叠加。Center 在左右开关任一开启时仅生成一列，左role优先；数量预算使用相同规则。yaw 在 footprint 投影前加到解析姿态，避免画面与占地朝向不一致。

三项强断言测试全部通过：六组中心/左右沥青边缘/横向/高度/yaw参考位置与七个纵向实例、Center 双开关仍单列、紧预算及标准 recipe 一致；另覆盖保存、旧字段迁移、非法输入不改变作者、preset capture/restore/undo/redo；非对称矩形 footprint 在90度yaw后采样斜面最大高度，再加 world-up 偏移，共63次查询且62预算拒绝。新版 procgen-core-only OBJECT 编译通过（仅L1）。尚不宣称曲线/坡道 alignment、relativeToCenter 非中心情形、随机旋转、完整默认编辑器或 B6 完成。

同一轮双端设施探针已从手写实测点升级为实际生产者：真实导入/cook/evpack后的 prefab 进入bounds目录及sink，创建30m道路，明确选择Average/Center/.02m，标准recipe请求5m，经实际RoadNetwork作者快照、bake及带asset/build/rootPose的PointSet转发后创建七个ECS renderer。最终源码编译并运行成功，两视角仍IoU=1、RGB最大/平均误差0；scope限定这一个平直路刚性cube。比较器只有在该生产者完成标记和两帧阈值均通过时才设置nativeRoadSpacingVerified=true；默认显示pipeline仍false。五项验证器测试包含带生产标记但颜色差1时必须撤销verified。此前文档“该夹具只用实测点”的描述是历史阶段，现由本条更新，不把本条扩展为曲线或所有间距语义的证明。

2026-10-15 v13验证补充：完整道路164/164、另16项Scene/缓存/GPU组合及5项帧验证器测试通过；side-placement-evidence.json绑定源码、构建、测试与当前原生渲染运行时证据。相机/颜色契约不变，两端真实帧均保持IoU=1和RGB误差0。原生 yawDegrees 使用引擎world-Y约定，尚未以非零yaw双端帧证明Unity handedness转换；不把裸数值45的本地契约测试当作该坐标转换的证明。采用强类型单一owner、schema v1缺字段迁移/未知字段忽略、Result拒绝非法候选、同步能力查询与显式成本边界；没有新增模块边界或规则例外。源码完整门禁终态继续以side-placement-source-check.log为准。

2026-10-15 v13完整源码门禁已终止：格式、层次、绑定覆盖、测试清单、质量元数据和10-profile检查通过，最终仍报前述七项IStateProvider/RenderSystem/Sensing/Vehicle架构问题。未修改基线或豁免；整体make check失败。终态及原文已进入side-placement-evidence.json。

2026-10-15 非零yaw双端边界已增加真实证据：保持统一Z反射坐标契约，Unity设施yRotation=+45映射为原生RoadSidePlacement.yawDegrees=-45。RoadSideGraphicsProbe的新yaw45模式仍通过实际30m道路recipe、5mAverage/Center/.02m、资产bounds/PointSet/sink/ECS生成七个实例，没有手写实例位置或旋转覆盖。参考offset探针为scale(2,3,4)源cube重跑八组，并调用Camera.Render捕获yaw45两帧；实际Unity日志完成且退出0。两个校准视角均IoU=1、RGB最大/平均误差0。明确保持作者字段为原生world-Y约定，不把外部Unity角度直接复制进核心字段。

错误符号真实GPU负对照也固化：同一recipe故意使用native+45，透视IoU=.5538746307、俯视=.3831215193，RGB最大误差223，固定.98/0阈值拒绝。compare_road_side_yaw.py --run-native ... --run-negative-control可复现；side-yaw-comparison.json绑定正确结果及负对照SHA。九项验证器测试通过（原五项加yaw四项），覆盖未知模式、参考rotation被篡改及hash合法但RGB差1。零yaw基线已用最终exe重拍，仍像素一致；八组偏移及十组间距参考验证有效。生产道路源码及DLL本轮未变，前述164道路/16组合与core L1是上一轮有效记录，不冒称此次重跑；仅测试探针exe/工具更新。

该证据关闭“平直路45度yaw坐标转换未验证”这一具体子项，仍保留任意角度/反射非均匀层级组合、坡道与曲线alignment、meshBoundsAlignment、随机偏移与选择姿态、默认作者中心策略及B1-B8其余范围。此轮没有新增生产公共API、持久格式或模块边界；共享像素比较逻辑被抽为测试工具函数，失败测试通过，无架构规则例外。完整源码门禁以side-yaw-source-check.log终态为准。

2026-10-15 yaw轮源码门禁已终止：格式、层次、绑定、测试清单、质量元数据和10-profile检查通过，最终仍为IStateProvider/RenderSystem/Sensing/Vehicle同样七项既有问题，未豁免。side-yaw-evidence.json记录完整终态、源码/执行产物及正确/错误符号真实帧的SHA链；总体目标继续进行。

2026-10-15 坡道/曲线设施参考复核：独立 UnitySideObjectOrientationProbe 使用公开SDK生成grade(0,0,0)-(0,3,15)-(0,6,30)及curve(0,0,0)-(6,0,15)-(0,0,30)，每种align=0..3共八组。作者marker仍为原输入，三marker的GetSplineStrength均.5。grade有七个实例，curve有八个实例；实际SDK执行完成且退出0。grade align0/1保留水平，align2/3跟随坡度约11.31度；curve四值在此平面输入接近，不能由此推断terrain provider存在、银行横坡或alignment全部标签。

更关键的公开数据契约：GetSplinePointsCenter逐点等于GetSplinePointsLeftSide/RightSide的中点，最大误差1.20e-7m；中心折线累积长度与GetLength一致。按该折线以Average分布距离插值并在自由末端外推，可重建所有八组设施点位，最大误差2.53e-6m；除grade align0/1强制水平外，实例forward与所在中心折线段方向一致，最大向量误差3.70e-5。它不是解析曲线的连续切线。verify_road_side_orientation.py绑定适配器/程序集/日志并执行上述逐实例约束，产出side-orientation-reference-evidence.json；nativeParityVerified=false，参考契约与原生对齐结果分开记录。

生产者双端结果：road_side_graphics_probe 增加独立grade/curve模式，仍走实际作者graph/recipe/bounds/PointSet/sink/ECS/GPU readback，输出带SHA绑定的实际点位CSV。与Unity align2对照，grade使用native PathForward，两视角均IoU=1、RGB误差0，最大点位差5.85e-6m。curve两视角IoU=.9706140518/.9532555710、RGB最大误差223，严格返回different/退出1；最大点位差.075254753m。参考curve中心首点(-.0669527,0,.0343601)、首yaw28.2065；native仍从作者marker起、首yaw对应21.8014。该偏差涉及道路左右边界生成及设施所消费的路径，不能只加一个旋转offset，也不能把实测参考点塞回native冒称生产对齐。

compare_road_side_orientation.py --run-native ... 固化固定.98/0阈值、参考/原生来源哈希和实际点位差；grade passed、curve different保留在side-orientation-comparison.json。十三项验证器测试通过（既有九项加中心不等于边界中点、错误曲线方向、末端钳制等四项）；最终exe下零yaw、正确yaw和错误符号负对照重新捕获，原结果保持。引擎生产源本轮未改，164道路/16组合和core L1仍是此前对应生产源码的历史检查，不冒称重跑。此轮没有生产公共API/持久格式/模块边界变更，无架构规则例外。

下一项B2/B6联动修复必须围绕唯一道路表面边界/采样结果：进一步确认参考左右边界的端部及曲线离散生成规则，再让原生设施位置和orientation消费同一生成中心折线、长度及所在段，而不是继续从独立解析Spline travelFrame派生。当前闭合的是这组坡道align2实测，曲线及其边界算法未闭合，B6和完整核心目标继续进行。完整源码门禁终态以side-orientation-source-check.log为准。

2026-10-15 orientation轮门禁终态：完整make check结束，格式/层次/绑定/测试清单/质量元数据/10-profile检查通过，最终仍为既有IStateProvider、RenderSystem、Sensing、Vehicle七项架构发现，无基线修改或豁免。side-orientation-evidence.json绑定本轮参考、grade通过、curve不同、十三项工具测试和门禁终态。

2026-10-15 曲线边界端部进一步隔离：UnitySideObjectOrientationProbe.RunBoundary新增13组受控实验（width2/6/12 × resolution1/2/4，seed12345重复、seed17对照，源网格Z scale1/8对照）。每组都保存设施接线之前、Refresh之后/Build之前和Build之后的中心折线副本，记录公开tValues、marker方向及强度。13组均实际执行完成且退出0；源网格缩放对照和各生成阶段的中心数组相同，排除了本样本中设施尺寸或消费过程修改道路中心的解释。随机状态在每例入口显式设置并finally恢复；换seed在本无bump样本中也不改变中心线。

宽度2到12m变化时，中心点最大差1.91e-6m，左右边界距离与作者宽度最大差1.82e-6m；因此不能用宽度相关中心校正解释这组偏差。分辨率1/2/4m分别保留31/17/9行，首尾中心偏移随分辨率变化。对公开tValues用通用Catmull-Rom评估，中间保留记录吻合（验证明确排除两端各两行后最大差2.06e-6m）；1m分辨率的首尾邻近行也有约1.30cm/2.10cm偏差，不只是两个端点。2m样本的首端偏移.07525475m、末端.06428699m，4m首端.01821673m、末端.00051054m。当前只记录这些端部观测，不据它们引入“两行”“2m区域”或固定offset的生产经验常数。端部生成公式仍未确认。

两个独立Unity进程的13组几何指纹完全一致：c399cfed060290cbe8950395390406fe5ebc899757ce953ffae2717f98b6866d。verify_road_side_boundary.py --repeat-prefix side-boundary-repeat校验来源、终态、宽度/分辨率读回、实际seed/源scale、各阶段不变、边界中点、公开参数及中部曲线位置，输出side-boundary-reference-evidence.json；endpointRuleConfirmed与nativeParityVerified均false。17项验证器测试通过，新增未应用seed、阶段内中心改变及保持边界一致但中间几何被改的拒绝路径。

本轮没有修改native生产源或宣称曲线修复完成。原八组orientation/两组帧已用最终C#适配器重拍并重新校验，grade仍passed、curve仍different；旧报告哈希记录是历史，由本轮side-boundary证据关联新的报告。接下来的生产修复应保持通用Spline解析语义，新增由道路自身离散截面/边界准备结果派生的设施路径入口，并将位置、长度、段方向和footprint共同消费；明确作者策略、旧数据迁移与工作预算。道路端部边界算法必须继续核实，不能将参考实测点注入native结果。整体B2/B6及完整核心目标仍未闭合。

2026-10-15 boundary轮终态：13组参考在两个独立Unity进程重复通过，17项证据测试及10项UTF8测试通过。完整make check已终止，格式/层次/绑定/测试清单/质量元数据/10-profile通过，仍为IStateProvider、RenderSystem、Sensing、Vehicle同样七项既有架构发现，未豁免。side-boundary-evidence.json已记录终态；此轮为参考端部契约隔离，未改native生产源码或关闭曲线差异。

2026-10-15 SurfaceSegments 生产入口落地：RoadSidePlacement新增稳定枚举RoadSidePathSampling（Analytic=0、SurfaceSegments=1），缺字段迁移为Analytic，未知枚举拒绝；whole-rule preset继承、共享codec、保存/undo/redo与标准recipe共同消费，算法身份surface-facility-path-v14。新策略显式选择，当前新建默认仍为Analytic，不称默认行为全面对齐。通用SplinePath未改语义；没有注入参考点或拟合端部偏移。

一次network bake的RoadFrameCache唯一拥有每条edge的保留帧与共享samplingWork。道路几何使用同一准备入口；SurfaceSegments另保留实际裁剪/路口平面混合后的可见行，并用未绘制前后缀保持作者相对距离。设施位置沿这些中心行累积物理弧长，朝向取所在段方向，起末排除与socket用可见区间裁剪；整段未绘制时不产生设施。邻路排斥只查询对方可见表面段。路侧位置插值实际行的side向量及宽度，不用重建的单位法线替代真实沥青边缘；额外作者偏移、yaw、terrain footprint和height继续在同一最终pose链路应用。所有span只借用本次cache，未跨帧/回调保留，查找使用整数edge键；扫描工作不重复收费。公开bake文档补充总保留行内存、每edge最多两条裁剪行、每anchor对数查找与邻段线性排斥成本。

五项新契约在正式strict SHARED和独立无图形STATIC闭包均实际执行通过，STATIC统计143个断言、零告警；覆盖真实网格中心/段方向、真实沥青边缘插值、共享精确scan预算、输出预算/失败不修改作者、标准recipe、保存迁移/无效枚举、preset历史、路口实际裁剪截面及混合有效/省略道路。CPU核心新文件已补入road自有CoreSources.cmake，正式procgen-core-only OBJECT日志明确编译RoadSurfacePath.cpp（L1）；独立STATIC生产源闭包链接并执行同样五项（L2），不扩大为完整renderer OBJECT或跨平台证据。

测试证据更正：初版新夹具的6m截面误沿用7m总车道宽，辅助函数内REQUIRE没有正确上报进程失败，先前局部“通过”不可作为证明。现已显式laneWidth=3，辅助构造返回Result并由测试主体REQUIRE，最终SHARED详细日志及STATIC的143断言为有效证据；初版日志保留但不计。路口测试改为比较实际mesh裁剪截面而非名义radius（两者在既有采样中有毫米差），未放宽几何容差；全网空mesh仍保持原有拒绝契约，省略段用含另一有效道路的场景验证。

最新双端：SurfaceSegments的grade两视角仍IoU=1、RGB误差0；curve从解析策略.970614/.953256改善为.989005/.982464，但RGB最大误差仍223、最大点位偏差.07525475m，因此严格different/退出1保留。参考端部处理公式仍未确认，不因轮廓超过.98关闭曲线差异。最终DLL下零yaw、正确yaw及错误符号负对照、16项Scene/缓存/GPU组合已重跑；27项Python/UTF8测试通过。

本轮完整道路详细回归另暴露验收缺口：169个CTest进程退出0，但13个用例实际输出断言告警或异常，不能称道路整体全绿。surface-path-road-warning-audit.json逐例记录；新增五项单独审核为零告警。13项涉及节点移动退化拒绝、位置拆分精度、三臂路口覆盖/侧壁、斜路口接缝、平滑拆分、环岛覆盖、虚线长度、退化三角形、非对称车道方向及编辑NoOp/撤销。尚未逐项做引入来源归因，不将它们直接归咎于本轮或一概称为已证明的既有问题。接下来的核心对齐必须把这些断言转为有效失败并修复实际行为；历史“164/169全通过”只代表当时CTest退出状态，不再作为全约束成立证明。

应用规则：唯一作者与一次bake派生owner、Result/借用生命周期、可选字段迁移与未知字段策略、预算/失败原子性、模块自有CPU清单与六面协议目录；无新ECS/Link/异步生命周期，也无规则例外。完整源码门禁终态以surface-path-source-check-complete.log为准。B1-B8及完整核心目标保持进行中。

2026-10-15 SurfaceSegments轮最终源码门禁已终止，仍为IStateProvider/RenderSystem/Sensing/Vehicle七项既有架构发现，未豁免；格式/层次/绑定/测试清单/质量元数据/10-profile通过。surface-path-evidence.json绑定最终源码、SHARED/STATIC五项143断言、明确包含新源文件的core OBJECT、16项详细组合和27项Python检查，以及169进程中13项告警审计和曲线不同结果。整体门禁/完整道路约束/完整核心均未通过，不以局部证据扩大结论。

2026-10-15 编辑NoOp闭环修复：makeReconnectEdgeEndpoint和makeConnectAllTurns不再把EditorStatus::NoOp送入失败构造器；它们返回成功NoOp及可幂等应用的规范操作载荷。命令adapter保留NoOp状态并输出零operations，避免把未改变状态的命令当作需执行的图修改。直接应用同端点且无恢复项的重连、或空连接批次，也返回NoOp且不增加dirtyRegion、不替换作者状态。

同端点的makeReconnectEdgeEndpointAndConnect仍会补充缺失的相关转向：有新增项则返回Applied，使用现有restore/remove-lane-links操作对而非重连逆操作，确保undo只删除本次新增项；无新增项为NoOp。普通端点改变仍保留原重连与旧路口连接恢复规则。未新增持久格式或操作opcode。公共返回语义、所有权/线程及成本说明和六面作者接口目录已更新。

两个原NoOp异常用例从大编辑测试文件移入test/procgen_road_noop.cpp并将CHECK升级为REQUIRE；另三项强契约覆盖成功状态/载荷、revision及完整作者快照不变、直接应用dirtyRegion不变、未知edge错误仍正确、同端点补转向undo/replay，以及注册命令的空计划。最终SHARED与独立STATIC相同五项各95断言，零告警/异常。先前91断言结果是增加直接应用状态/脏区检查之前的中间记录，以95断言最终日志为准。

完整道路详细复跑172个用例，两个NoOp告警已消除，剩11项；road-noop-warning-audit.json逐项保留，并继续标记cleanRegressionVerified=false。它们包括节点移动退化/拆分精度、路口/环岛/虚线/三角形、车道方向和编辑连接删除等，未通过项没有改基线或软跳过。后续仍要复核原生合同并修复实际行为，不能仅把CHECK改名或放松容差。此前设施帧已按本轮DLL刷新，零yaw/45度/grade保持通过，curve保持different。此轮改变的是作者操作状态及消费/逆操作，未修改道路bake生产算法身份v14。

适用规范为Result成功/失败状态、唯一作者与候选原子修改、owning操作载荷、同步命令边界及现有opcode/持久格式、六面接口目录与同合同SHARED/STATIC验证；无新增ECS/Link/optional provider，无规则例外。完整源码门禁最终结果以road-noop-source-check-final.log为准。

2026-10-15 NoOp轮最终补充：加入已有无关转向及禁止转向的三臂场景，组合undo后完整作者快照与原快照精确相同，redo保持正确；最终SHARED/STATIC同五项各102断言，零告警，95为此前未增加该快照断言的记录。完整172用例已在最终生产实现下复跑，仍有11项告警；新增的快照断言随后只定向重跑，生产源码未再变。完整make check最终仍同样七项IStateProvider/RenderSystem/Sensing/Vehicle架构发现，未豁免；格式/层次/绑定/测试清单/质量元数据/10-profile通过。road-noop-evidence.json记录最终源/日志/帧证据与这些范围限制。


## 2026-10-15 最近点拆分精度与接入撤销复核

`splitEdgeAtPosition` 原先把采样折线上的投影比例直接转换为样条参数；即使几何为直线，Catmull 参数速度也不均匀，因而出现毫米级拆分误差。现在保留有界采样定位区间，再在实际样条上执行最多40步距离细化；距离计算使用double，吸附半径也按实际曲线投影距离判定。端点拒绝、私有候选验证和原子提交路径保留。该局部细化不构成任意自交曲线的全局精确最近点证明，参考端部网格采样规则仍未确认。

四个独立强断言用例位于 `test/procgen_road_position_split.cpp`，SHARED与独立STATIC各66个断言、零告警：直线3D吸附与端点拒绝、曲线吸附半径、拆分撤销重做稳定ID、曲线接入撤销重做。曲线接入旧测试的筛选条件还匹配到原有接入起点，误将其作为插入节点；现明确排除该起点，生产撤销逻辑未因此修改。证据：`build/road-parity/road-position-shared-tests.log`、`road-position-static-tests.log`。

本次没有增加公共API、持久字段、模块边界或第二权威状态；沿用结构化Result和失败原子性，无架构例外。完整核心对齐、B1–B8及默认渲染路径仍未完成。此后DLL重链接后的历史图像证明不代表当前二进制已重新捕获。

最终道路域回归执行173项、退出0；详细断言审计仍有8项告警（`road-position-warning-audit.json`），相较上一批11项减少3项，不能据进程退出码称全绿。新增1项、迁移保留原名3项；最终四项契约各66断言，无告警。完整回归证据为 `road-position-full-road-final.log`。

本次完整 `make check CI_BASE=10f6faa7509e4a108ee5f2e6b6e3be357e99af26` 最终失败，仍为IStateProvider三项Result形状、RenderSystem两项指针生命周期、Sensing目录契约与Vehicle运行时模块查询，共七项；格式、依赖分层和十配置矩阵通过。未新增基线或豁免。摘要及日志/源码哈希见 `build/road-parity/road-position-evidence.json`。


## 2026-10-15 路口贯通补面与坡道接缝

复核确认两个独立问题：路口开口使用16次弧长采样而道路截面使用24次，现统一为24次；一般路口的贯通道路补面还额外沿平面法线抬高0.01m，导致同一开口处存在约一厘米高差，现保持原始开口截面坐标。仅统一采样后的强断言仍失败（0.00992537m），去除抬升后才通过，不能将精度不一致当作全部根因。算法身份提升为 `road-core-2026-10-15-junction-seam-metric-v15`，BuildKey与artifact消费者强制重编译。

两项测试迁入 `test/procgen_road_junction_seams.cpp` 并改用强断言；平滑拆分进一步检查所有asphalt顶点处于作者平面，而非仅统计恰好0.01m的顶点。最初独立STATIC两项28断言无告警；随后混合截面测试也迁入并更新为生产道路24次采样的位置查询，原1毫米容差不变。最终SHARED/STATIC三项各43断言无告警。该用例名称保留历史身份，不证明所有二臂节点都跳过路口生成：当前activeJunctionNodes仍纳入二臂节点。一般路口补面与基础三角剖分的重叠、轴对齐四路口另有的0.01m抬升、默认渲染及完整B1–B8继续待核验。

本次无新公共API或持久格式，保留单一作者状态、Result与失败原子性，无架构豁免。当前重链接二进制没有新的Unity画面对照证据。

混合截面首次完整回归在旧16次采样预期位置查不到顶点（`junction-seam-full-road.log`）；测试改为24次采样后强断言通过，并保持原1毫米位置容差。最终完整回归另存 `junction-seam-full-road-final.log`。四臂坡道探索输入还在bake阶段被拒绝，见 `junction-seam-cross-before.log`，其合法性与拒绝根因尚待审计，未当作已对齐证据，未新增跳过或豁免。

最终本批道路域回归173项退出0，详细审计仍有5项告警（退化节点移动、环岛覆盖、虚线世界距离、退化三角形、非对称车道约定），相较上一批8项减少3项。标准recipe/全部LOD契约在本次执行中通过。完整make check仍为同样七项架构发现失败，未修改基线或豁免。摘要与哈希见 `junction-seam-evidence.json`。四臂探索输入保存在 `junction-seam-cross-input.json`。


## 2026-10-15 四路口补面共面性与拒绝诊断

原四臂探索输入的拒绝原因已经实测确认为 `PreconditionViolation: junction arm sockets cannot share one bounded construction plane`。新增强断言保留该诊断、revision不变和作者网络有效性。此为当前原生的有界施工平面约束，EasyRoads是否等价拒绝尚未确认，不能把保留拒绝作为参考行为对齐完成。

另一个满足同一作者平面的四臂输入揭示轴对齐四路口的asphalt扇形补面沿法线抬高0.01m、封缝条抬高0.012m。二者现在位于共同施工平面，四臂约束判定的弧长采样也与道路开口统一为24次；平面拟合本身仍使用16次取样，不称所有采样精度已统一。算法身份提升为 `road-core-2026-10-15-cross-apron-plane-v16`，强制重编译BuildKey与artifact身份消费者。

共面测试只检查路口内部 |x|、|z|<=4m 的asphalt顶点，不把远端没有横坡的道路截面错误地要求位于整张斜平面；该区域误差门槛0.0001m，修复前误差0.0103045m。五项接缝/约束测试SHARED与独立STATIC各55个断言，零告警。日志 `cross-plane-before.log`、`cross-plane-shared-tests.log`、`cross-plane-static-tests.log`。该证据不证明无重叠、所有接缝空间拓扑一致或默认PBR渲染对齐。此轮没有新公共API/持久字段，遵守Result、单一作者状态和失败原子性；没有架构例外。

短四路口旧用例用“y恰好0.01m”辨认路口顶点，修复后该测试首次完整回归失败（`cross-plane-full-road.log`），不能称生产几何失败。现在该用例迁入独立接缝文件，以连接中心的实际三角形识别补面，保留5.5m最大半径约束，新增0.0001m共面约束；未放宽原半径门槛。最终六项SHARED/STATIC各62个强断言、零告警，见 `cross-plane-shared-tests-final.log`、`cross-plane-static-tests-final.log`。已从实际EVDomains.dll确认v16身份字符串；未重新捕获双端渲染帧。

本批最终道路域回归175项退出0，仍有5项断言告警；六项定向测试各62断言无告警。完整make check在格式修正重跑后仍为相同七项架构发现失败，格式、分层与十配置矩阵通过，未增加豁免。最终证据与源码/日志/运行DLL哈希：`build/road-parity/cross-plane-evidence.json`。


## 2026-10-15 环岛中央岛与车道坐标预期复审

本轮不改变生产网格或车道算法。原useOneGraphModel测试要求span48/单车道环岛在x=-2..2、z=9..13均被asphalt覆盖；工厂实际作者数据为中心线半径12m、单车道宽3.25m，z=9/10的探针处于中央岛内。覆盖契约迁入独立强断言 `procgen_road_roundabout.cpp`：中心及原先十个岛内探针必须留空，原先十五个车行环带探针必须覆盖，30断言在STATIC下通过。没有降低位置/面积容差或修改生产几何来填满中央岛。仍不证明任意半径/车道数和全部入口的拓扑覆盖。

非对称车道旧测试固定要求沿+X的Forward车道位于+Z侧，与当前样条水平侧轴world-up cross forward=(-Z)相反。现在相同作者数据检查Forward z=-3.5m、Backward z=3.5/0m，另增加四个水平朝向中所有导航采样点的侧向坐标协变性，1e-4m门槛不变。两项强断言在STATIC下285断言通过，迁入 `procgen_road_reference.cpp`。没有改变生产车道方向、lane索引或把当前坐标约定声称为参考LHT/RHT实现；左右行交通规则仍为差异。

本次没有新公共API、作者字段、持久格式、模块边界或第二权威状态，不改变算法缓存版本v16；测试辅助函数为纯值查询，测试构造失败由测试体强断言检查。无架构例外。

最终SHARED三项315断言无告警，STATIC两份日志合计315断言无告警。完整道路回归177项退出0，仍有3项告警（退化节点移动、虚线世界距离、退化三角形）；其余差异未据此缩减。完整make check仍为相同七项架构发现失败，格式、分层和十配置矩阵通过，未增加豁免。摘要与日志/测试源码哈希：`build/road-parity/road-ring-lane-evidence.json`。


## 2026-10-15 虚线按实际三维路面段长累计

原addLaneMarkings把每段样条缓存normalizedDistance乘总弧长作为绘制区间，但标线顶点实际沿相邻路面截面线性插值，两个度量不完全一致，导致原40m直线中心虚线面积4.84m2的契约偏差约0.006m2。现在保留首个裁剪截面的作者距离作为相位起点，后续按实际中心线折线的三维段长累计，虚线切分比例和绘制位置共享同一度量。没有修改虚线宽度、长度、间隔或放宽原0.001m2面积门槛。算法身份更新 `road-core-2026-10-15-marking-world-distance-v17`，BuildKey和artifact身份消费者强制重编译。

原测试迁入procgen_road_reference.cpp并改为强断言；构造helper改为Result返回，失败由测试体检查。测试覆盖旧samplingSpacingMeters=0与新2m米制采样；旧路径还明确检查粗细网格顶点数不同，避免“改变resolution但实际相同网格”的伪验证。水平(40,0,0)实际长40m，中心虚线六个3.5m完整段加1m尾段，乘0.22m宽得4.84m2；坡道(40,30,0)实际长50m，八个完整段得6.16m2。粗/细网格在两种路径下均满足原面积门槛。还验证极小虚线周期触发网格预算失败。STATIC最终22断言无告警，见marking-distance-static-tests-final.log。

此改动采用当前原生的可见中心线折线距离，不证明曲线标线与参考样条距离/相位完全等价；不同采样下曲线折线长度可能不同，裁剪起始相位仍来自作者缓存距离，所有跨边/跨路口相位及参考开端规则继续待核验。没有新增公共API或持久字段，保持Result和失败私有输出；没有架构例外。本轮没有新的双端渲染帧。

最终SHARED/STATIC各22断言通过，177项道路回归仍有2项断言告警；标准recipe/全部LOD契约已在本轮执行通过。完整make check最终失败，仍为IStateProvider三项Result形状、RenderSystem两项指针生命周期、Sensing契约目录与Vehicle运行时模块查询，共七项；格式修正后格式/分层/十配置矩阵通过，未新增豁免。日志、源码和实际DLL哈希见 `build/road-parity/marking-distance-evidence.json`。


## 2026-10-15 作者截面共线性与端盖伪面

对原commonConnectionsEmitNoDegenerateTriangles逐三角形捕获场景、材质、坐标，13项全部为curb端盖扇形填充中的共线三点：y/skew/sloped-t/curve-uphill/y-junction/roundabout，世界坐标浮点减法把本来共线的作者截面点算出微小非零面积。临时四边形记录器没有捕获对应问题，已完全移除；没有给appendStripQuad统一提高剔除阈值。日志road-sliver-before.log与road-sliver-quad-probe.log保留定位过程。

RoadBakeProfiles现在在宽度缩放后的作者(side,up)二维坐标用double计算扇形三角形面积，再决定是否提交世界坐标索引。原面积平方1e-12门槛保持，复用到完整端盖与肩部端盖；没有通过提高全球面积阈值或删除整个端盖来消除告警。显式闭合自定义截面的原二维剖分路径保留。算法身份提升为 `road-core-2026-10-15-section-cap-area-v18`，BuildKey与artifact身份消费者强制重编译，实际EVDomains.dll中已确认v18字符串。

原公共场景用例迁入procgen_road_reference.cpp，改为测试体聚合错误数后强断言，保留原1e-8面积平方要求，覆盖十类场景所有材质。另加平移(100,-12,75)与四个水平朝向的有效端盖检查：每个终端面三角形必须有有效面积、端盖非空且数量随旋转保持一致。SHARED/STATIC两项各101强断言无告警，road-sliver-shared-tests.log与road-sliver-static-tests-final.log。该证据不证明无限世界坐标精度、所有自定义截面或参考端盖形状完全等价；本轮没有双端渲染帧。

无新公共API、持久字段、模块边界、第二权威状态或架构豁免；纯几何判定为内部值查询，Result和私有烘焙失败边界保留。全部核心缺口仍按B1–B8审计推进。

本批最终178项道路回归退出0，仍有1项断言告警；两项定向测试SHARED/STATIC各101断言无告警，标准recipe/全部LOD契约通过。完整make check仍为相同七项架构发现失败，格式、分层、十配置矩阵通过，未新增豁免。证据、源码/日志及实际DLL哈希见 `build/road-parity/road-sliver-evidence.json`。


## 2026-10-15 节点移动塌缩、身份与规划预检

旧nodeMoveRejectsDegenerateIncidentEdgeAtomically使用makeStraight工厂，实际含(-10,0,0)、(0,0,0)、(10,0,0)三个控制点；把起点移到终点后得到10→0→10折返，并非整条道路缩为一点。当前原生hasUsableGeometry契约只要求存在非零段，因而该输入被接受。折返重叠是否应额外拒绝，以及参考是否等价限制，仍是B2待确认项；没有为得到全绿而新增“所有端点坐标重合均拒绝”的限制。

原测试身份保留，迁入procgen_road_reference.cpp，用明确两点道路验证完全塌缩拒绝，并检查controlPoints确实为2、revision及完整作者快照不变。发现生产makeMoveNode此前只检查存在性/有限数，未预检几何；现在先复制私有candidate执行原生setNodePosition，失败则返回结构化拒绝，不生成可提交的forward/inverse操作。公开成本文档标明每次规划对作者图线性预检，apply仍保留自身验证，不引入第二权威状态或新的操作码。

另一项强断言验证非退化方形路径的终点移动到起点坐标时，两个节点身份保持独立，并贯通作者保存/restore、undo/redo及完整快照一致性。该测试只证明作者/编辑消费闭环，不证明闭合端部的渲染接缝或完整Spline闭环语义。两项SHARED/STATIC各34断言无告警，road-node-geometry-shared-tests.log与road-node-geometry-static-tests.log。

本次无新作者字段或持久格式、公共签名、模块边界；生产生成算法未变，缓存身份保持section-cap-area-v18。适用Result检查、失败原子性、唯一作者所有权、保存事务和成本契约，无架构例外。完整核心差异及参考未确认项继续保留。

最终179项道路回归退出0，逐项case与断言汇总均无告警/失败/跳过，共267003断言；两项定向测试SHARED/STATIC各34断言通过。完整make check仍为相同七项架构发现失败，格式、分层、十配置矩阵通过，未增加豁免。此为道路回归证据，不涵盖完整功能清单或参考等价性验收。最终证据和源码/日志哈希：`build/road-parity/road-node-geometry-evidence.json`。


## 2026-10-15 道路地形世界高度基准与schema v2

RoadTerrainConformOptions和RoadTerrainTile新增有限originY（默认0、追加字段以保留旧位置聚合参数含义），世界高度定义为originY+sample*heightScale。conform计算道路/地形高差、写回样本反算以及workspace的纹理/detail/tree覆盖判定都使用同一映射；桥隧显式保护政策保留。新字段是typed terrain tile作者值的一部分，历史拓扑比较包含originY，禁止保存历史中的同一tile静默改坐标基准。

新capture写procgen.road.terrain v2，所有tile/历史帧要求显式originY；v1继续读取，缺字段的每一帧迁移0。未知对象字段继续忽略，未知版本、已知字段错型/非有限值、历史映射不一致在私有候选中拒绝。格式升级避免旧reader忽略非零基准并误解释坐标。原未知版本测试改为尚未支持的v3，不把现已支持的v2继续当作坏输入。模块持久化目录与组成快照已同步。

两项SHARED/STATIC强断言各55次：初始sample=.5、heightScale=10、originY=100对应worldY=105，roadY=106写回sample=.6；未覆盖样本不变；纹理目标层、detail清除和树移除也必须生效。验证JSON保存/restore、undo/redo、旧版多帧迁移、v2缺字段拒绝、错型与非有限originY、历史映射不一致原子拒绝，以及独立Heightmap receipt恢复原样本。修改布局后触碰直接/内部头消费者并实际重编译，含native TerrainWorldRoad适配。

这是独立RoadTerrainWorkspace和Heightmap conformance的非零基准支持。native TerrainWorldCreationSettings当前仍只有centerX/centerZ，其世界地形基准为0；本轮不宣称native World整体Y平移已实现，Unity normalized-height导入/真实双端地形帧也尚未验收。独立Heightmap receipt的owner-qualified身份问题未因新增映射消除。mesh bake算法未变，缓存身份仍为section-cap-area-v18。

适用typed单一作者状态、公开映射契约、schema版本/迁移/未知字段、全历史原子restore、Result检查和真实消费者重编译规则，无架构例外。完整B1–B8目标继续保留。

本批最终181项道路回归退出0，逐项汇总无告警/失败/跳过，共267058断言；定向SHARED/STATIC各55断言通过，native零高度基准适配及标准recipe/LOD契约仍在本轮范围内通过。完整make check仍为相同七项架构发现失败，格式、分层、十配置矩阵通过，未新增豁免。证据、日志/源码/实际DLL哈希：`build/road-parity/road-terrain-origin-evidence.json`。


## 2026-10-15 native World高度基准贯通

TerrainWorldCreationSettings追加有限originY（默认0），由World.settings唯一拥有；每tile的Y基准是它的派生查询getTileOriginY(index)，不增加可独立编辑的tile基准副本。create校验非有限基准并原子替换世界，既有World内部history保持同一创建设置。setHeightWorldUnits对绝对世界高度减去originY再Clamp01归一化，默认0保留既有语义。

native TerrainWorldRoad把该值投影到上一批RoadTerrainTile.originY。树/对象/探针的同步调用与分阶段spawn共六条临时tile描述符入口都传递同一基准；TerrainTreeTile/TerrainObjectTile/TerrainProbeTile追加默认0的有限originY，仅叠加到采样地形高度。既有rule.heightScale继续明确控制缩放，SeaLevel/Custom以及不依赖terrain的probe模式保留绝对世界语义，不做隐式二次抬升。normalized heightmap、splat/detail等二维栅格本身不因整体Y平移而改变。

TerrainStampScript.cpp原超过2000行，World创建设置和World类绑定纯移动到TerrainWorldScript.cpp，保留注册时机和原有方法，再在新文件增加originY字段及getTileOriginY绑定。没有在大文件继续堆叠World API，也没有新模块、管理器或第二作者状态；实际SHARED构建日志确认新TU被编译并链接。手册与六面module-interface目录补充数据/脚本面及显式RNG说明。

三项原生用例STATIC共210断言，SHARED三项210断言；另一个真实Squirrel VM用例通过2个C++框架断言及脚本内创建/查询/高度设置/undo断言，SHARED共四项212框架断言。测试覆盖worldY=100+(.5*10)=105、树/对象Y105、light probe Y107.5、分阶段候选发布、roadY106写回sample .6、树清除及undo/redo；SeaLevel树保持Y200、Custom对象保持Y7、Reflection海平面探针保持Y201；非法create保留World基准和历史。类型布局消费者按头文件包含闭包强制触碰重编译，列表world-origin-header-consumers.txt。

这是native CPU World与既有Squirrel生产入口的高度基准支持，不证明每个渲染/物理adapter已采用新查询、World跨进程全对象保存、Unity地形导入或双端渲染等价。独立规则heightScale与World.tileHeight的配置协调仍由调用方明确负责；本批不把既有缩放策略改为隐藏覆盖。独立Heightmap receipt owner资格问题也仍保留。道路mesh算法身份不变（section-cap-area-v18）。

适用typed单一作者事实、Result/原子事务、短期借用tile描述符、六面契约、显式时间/RNG、公开API文档和改头消费者重编译，无架构例外。World创建设置尚无跨进程persistent格式，本次没有伪造其保存能力；RoadTerrainWorkspace仍使用已实施schema v2/v1迁移。

扩展回归发现两个原生地形用例告警：对象碰撞单地形导出期望8却输出0、预算失败断言未触发；该函数体与HEAD的去空白哈希相同，但没有执行HEAD二进制基线，不能据此断言已证明历史行为。该问题仍未解决，见world-origin-single-export-audit.json。树多地形预算子实验复用了原east点集，向它追加4棵后又期待原8棵移除；改用独立budgetEast副本、恢复原指针并升级测试体强断言，保留原8棵预期，不修改生产规则。最终23项兼容回归无告警。当前最终27项定向/兼容回归共613框架断言无告警；完整make check仍为相同七项架构发现失败，格式、分层与十配置矩阵通过。证据、源/日志/DLL哈希：build/road-parity/world-origin-evidence.json。没有新架构豁免。


## 2026-10-15 对象空占位与连续边界修复

在扩大地形回归时发现对象碰撞用例的两个告警。本轮区分测试夹具与实际实现缺陷：原夹具在2x2范围使用边缘候选、旋转和负子偏移，却硬编码期望8实例；移除空占位后该输入实际仍只有2个实例。现在夹具明确为4x4、起始偏移1、固定原型方向0，使四个有效父中心各生成两个子实例，保留原8实例预期及maxPoints=1失败原子性，不把0或2直接当成新的期望。

实际缺陷由独立最小用例验证：空候选在未生成任何子实例前就加入acceptedCenters，阻挡后续有效候选；多地形还在连续边界检查前执行nearest-even像素查询，使x=-.1落入像素0。旧实现的负对照实测singleX=1.9、multiX=-.1（object-empty-occupancy-before-tests.log）；新契约要求第一个真实有效父中心1加偏移-.1得到x=.9。修复单/多地形两条路径，只在emitted增加时登记碰撞中心；多地形先拒绝非有限/范围外的连续u/v，再保留原nearest-even像素规则。未改为floor、边界吸附或调碰撞阈值。

直接DLL消费该多地形公共API还暴露缺少EVENGINE_API_DOMAINS；树/探针对应函数已经有导出。已补齐对象函数导出，保持签名和STATIC宏语义；添加公开成本/边界说明和精确的模块接口目录，未进行跨模块机械扫改。测试迁入纯CPU文件terrain_object_collision.cpp，保留原用例名并新增双路径空占位契约。独立STATIC两项13个强断言通过，含预算失败后原PointSet的count/id保持。

此为原生对象放置的正确占位与坐标验证，不证明EasyRoads/Pcg私有碰撞算法、真实prefab碰撞体或渲染等价。无新持久字段或API签名，World/history生成数据只在明确重新执行放置时改变；相同构建/种子/输入仍确定，不声明跨算法版本生成结果一致。道路mesh身份保持v18。适用Result、唯一输出owner、私有全事务发布、DLL真实消费者、模块六面和有界显式RNG契约，无架构例外。

最终SHARED/STATIC两项各13断言无告警；219项扩展回归、185项道路范围全部无告警/失败/跳过。此前对象碰撞告警已由明确夹具和真实实现修复共同关闭，并保留旧算法负对照，未降低原8实例/预算期望。公共多地形函数的Windows DLL符号由实际跨DLL调用验证。初次源码门禁遇到临时Git锁，锁自行释放，没有删除锁或修改检查器；最终原样重跑无Git错误，格式/分层/十配置矩阵通过，完整make check仍因原有七项架构发现失败。新增模块接口为精确TerrainObjectPlacement.*范围的声明，不是豁免或基线。日志、源码、负对照及DLL哈希见build/road-parity/object-empty-occupancy-evidence.json。参考碰撞/渲染等价性仍未确认，无架构例外。


### 2026-10-15：整网几何来源与顶点颜色保存

生产道路烘焙写入 `eve.procgen.road.geometrySources` v1 元数据，算法身份升至 `road-core-2026-10-15-geometry-sources-v19`。完整三角形区间分为 Edge、Junction 和 NetworkDerived；边与节点 id 仅在本次作者网络内有意义，连续设施衔接和导航覆盖几何保留为 NetworkDerived/id=0。没有引入全局作者身份、每道路策略持久化或策略投影，不将这项基础归属标记等同于 B3 完成。分块网格没有完整来源元数据，查询明确返回 Unsupported。

`PreparedRoadGeometrySources::prepare` 在显式构建/加载边界校验网格流、非认证摘要及完整有序区间，成功后返回拥有区间数据的 token；不保留网格指针。来源元数据是唯一持久派生副本，RoadBakeResult 不重复保存另一份公开可变区间列表。更改网格后必须重新准备，旧摘要拒绝；旧网格需要重新生成。

真实 artifact store JSON 回归暴露并修复了顶点颜色丢失：旧通用网格快照仅保存位置、法线、UV 等流，遗漏 RGBA。网格编码和有效性检查已从大文件提取到 GeneratedArtifactMeshCodec.cpp；现在保存完整颜色，恢复时验证有限值和逐顶点 RGBA 布局。dense snapshot v2 的 colors 为新增可选已知字段：缺失继续恢复为空流，已有字段损坏则整笔恢复拒绝，保留原存储。旧快照已经丢失的颜色不能凭空恢复，也不能绕过来源摘要检查。

当前 STATIC 定向验证 5 用例、101 断言通过（无警告、失败或跳过），覆盖完整区间、错误 schema/区间、网格变更、生产 recipe、产物 JSON 恢复颜色逐值相等、旧记录兼容与坏数据恢复原子性。生产 SHARED 构建及232项选定回归（道路198项、268193断言）已通过；另12项通用产物回归132断言通过。通用存储用例曾把消费性takeValue放进CHECK_EQ，断言重复求值产生告警；现先取局部拥有值再比较，复测干净通过。最终完整make check已运行，格式、分层及十配置通过，无Git命令错误，仍为原有七项架构发现失败。最后以同一最终测试二进制执行244项合并回归全部通过（道路198项、268325断言，逐项无告警/失败/跳过）；本批没有新增双端渲染帧。


本批适用并落实 Result/nodiscard、唯一派生数据owner、持久schema与旧字段迁移、失败原子性、显式线性准备成本与便宜查询、模块六面契约登记。没有新增向上依赖或架构豁免；既有procgen-core-only矩阵仅证明既有L1范围，不宣称新L2–L4裁剪证据。源码、二进制及逐项测试证据见 `build/road-parity/geometry-sources-evidence.json`。


### 2026-10-15：作者地形策略与来源计划（验证中）

RoadStyle 新增 owning terrainPolicy，与 sideTerrain（设施放置地形查询规则）分离。enabled、blendDistance、maxVerticalDelta、splatLayer/Opacity、clearDetails/Trees、cutTunnelHoles、tunnelHolesAtPortalsOnly 进入网络v1与预设v3的共享编解码。策略不存在时使用明确原生默认值；存在时要求完整已知字段并验证有限距离、层>=-1、opacity[0,1]。未知字段忽略；预设缺失覆盖继承，显式整策略替换，false不丢失。所有作者历史帧私有恢复。执行预算仍只属于请求。

新增原生两步 RoadTerrainPlan：prepare 从传入的同一作者快照真实烘焙，验证v19来源元数据，再按来源压缩拥有网格与复制策略；不接收可被错误拼接到另一网络的旧网格/id组合。路段使用自己的策略，Junction/NetworkDerived 使用调用方明确给出的共享策略；不推测参考端路口获胜道路。输出受复制index/attribute元素预算及4096写入上限约束。apply/applyWorld 使用既有原子批次通道写回与一次撤销；禁用全部来源为NoOp。计划不保留网络、provider或临时mesh指针，作者改变不回写已准备计划，保存只存作者与既有结果历史。

STATIC 定向测试初版4项88断言通过；最终追加禁用false的JSON恢复和未创建World失败契约，正在完成STATIC及生产SHARED验证。覆盖两条独立道路策略、作者网络JSON、预设父子继承与保存后undo/redo、完整地形一次撤销与保存redo、后续道路缺失纹理层的原子拒绝、共享路口策略通过native World消费，以及准备/执行预算失败。当前没有新Unity实帧，重叠及路口归属参考语义、脚本/编辑器入口、按marker扩展策略、完整World跨进程保存与B1–B8其他项继续保持。


### 2026-10-15：材质实际预览边界

按用户要求补拍四个原生完整功能场景（顶冠、桥隧、多臂设施、地形写回），由当前重编译的road_authoring_fixture生成网格；通过标准EveScript生命周期与gfx.saveFramePng引擎帧回读捕获24帧后的1280x720图。使用用户包的roadsample_d.jpg与road_b.jpg作为道路颜色/法线贴图，保留原生UV；请求pbr模型、roughness=.88、metallic=0，固定方向光和环境光，未修改引擎默认渲染代码。材质/mesh/texture脚本引用显式保活。

这是EVEngine单端预览，不是之前35个纯道路表面轮廓场景的双端材质验收。画面可见整体偏亮、重复标线及纹理拼接；未声明法线纹理色彩空间、UV尺度、阴影、曝光或默认PBR/G-buffer路径已与Unity对齐。资产仅留在忽略的本地build产物，不提交第三方贴图。源码、几何、贴图、当前renderer/generator与帧哈希在build/road-parity/material-preview/evidence.json；可浏览build/road-parity/gallery-35/material-preview.html。

### 2026-10-15 当前材质实拍（仍未完全对齐）

已补拍同材质、光照、相机的 35 场景、双视角、双引擎，共 140 帧。
共享显式 forward 沥青材质校正色彩解码、曝光、图片 V 方向及背面剔除；
采样贴图内部去掉自带白边，几何标线保留；连续世界三向投影消除本材质接缝。
原始 UV 数据未修改，默认延迟管线、设施和地形材质仍待验证。
31/35 通过既定阈值；圆弧镜像两项仍有轮廓差异，完整翻转正负两项轮廓已通过但着色仍超阈值。
当前证据：build/road-parity/material-35/material-comparison.json，
展示：build/road-parity/gallery-35/material-35.html。
详见 scripts/road_parity/README.md 的复现及范围说明；不视为全部核心功能完成。

### 可选部件预览范围（2026-10-15）

新增独立预览生成脚本 scripts/road_component_preview.py，复用当前作者生成器，
涵盖 11 组已有完整场景，各有透视与俯视。除沥青外的部件使用统一粗糙介电
分类色材质；并非参考工程最终资源材质，也未开启阴影。当前只声明 EVEngine
部件预览，不声称 Unity 部件组合渲染一致，不改变此前 31/35 的路面分数。
生成器已有的保存/撤销路径仍执行，但截图不能替代其行为断言。

### 可选部件的一致性对照修正（2026-10-15）

此前 11 组单端预览不能作为一致性结论。当前 component-parity/comparison.json
记录 6 组真实双端参考及相同材质的隔离对照；6 组完整场景均超既定阈值。
连续护栏/共享预设的参考形状缓存准备已通过公开 ERMesh 构造器解决；隐藏
载体不参与渲染，生成护栏尺寸符合作者截面。另 5 组桥隧、接入口、多臂及
地形组合缺完整参考适配，不评分。这是验证适配缺口，不证明原生功能缺失。
prefab 重复数、端盖、平滑组和自动间距设置仍需参考校准，现有差异不能
全部归因于引擎。完整场景标线与材质分组也尚未完全投影；隔离对照用于
分开观察。components.html 已替换为真实对照，旧单端预览另存。
未宣称完成 11 组的一致性验证，未改变此前 35 路面的通过率。
4 项比较器测试及 10 项 UTF-8 测试通过。完整 make check 仍有原有 7 项
架构问题；格式、分层和配置矩阵通过；无新增基线或架构例外。

### Unity 路口参考已建（2026-10-15）

已新增 T 型、十字、四接入环岛、带护栏 T 型与四臂设施路口五组真实 SDK
参考；20 张双端帧及差异图见 gallery-35/junctions.html。五个 Unity 场景保存在
本地参考工程 Assets/RoadJunctionParity/，生成的网格和材质已保存为资源；
另启 Unity 进程重开后，三角形数量不变，透视重拍 PNG 逐字节一致。
端口与实际首末点在连接/构建后重新读取，接入误差均小于 .001m。
四臂路口不再是“无参考”；指定设施路由对仍未完成对应。
这批是统一材质、去原生几何标线的诊断，默认参考步道/路缘设置、环岛尺寸
与设施路由映射仍有缺口，五组均未通过双视角阈值；不能作为完全对齐结论。
自定义 prefab 接入口、任意臂数及桥隧/地形组合参考仍需后续覆盖。

路口参考最终源码门禁已完成，仍为原有 7 项架构问题；格式、模块分层和
10 个配置矩阵通过。未新增生产 API 或生产存储格式，无基线或例外。


### 2026-10-16 路口作者截面约束修复与第二轮实帧

轴对齐十字路口原来固定生成 0.35m 路缘及至少 0.4m 步道，即使显式两点沥青截面也会多出补面；不同接入口又共用第一条道路的高度。现圆弧分支仅用于统一截面，使用作者路缘/步道宽高并分别遵守宽度关闭；不同截面交给已有逐开口插值分支。此处关闭约束针对路口生成，不额外改变道路默认截面的竖直面语义。没有更改公共 API、持久字段或模块边界；算法身份提升为 `road-core-2026-10-16-junction-author-bands-v20`，BuildKey/artifact 两个身份消费者已强制重编译。单一作者状态、结构化 Result、已有版本化网络恢复与编辑事务继续适用，无架构例外。

新增 `test/procgen_road_junction_style.cpp` 五项强断言测试：纯沥青路口、窄带尺寸、路口路缘/步道独立关闭、不同臂的开口宽高，以及真实编辑事务提交后保存恢复和撤销的网格一致性。最初四项均失败；最终独立 STATIC 五项29断言、生产 SHARED 道路208项267758断言均零失败、零告警。最终独立STATIC道路全回归149项11285断言通过；artifact 三项207断言通过。

参考适配修正了四接入环岛原始端口处于对角方向的问题：旋转候选从90度步进扩充到45度，并移除环岛方向不匹配的特殊豁免。真实公开点列证明默认环岛中心半径8m、内外半径5.5/10.5m；给公开半径/宽度字段赋值后调用 BuildRoadNetwork 或 ResetData 均未改变生成点列，因此未将字段赋值冒充尺寸映射。人行道公开 SetSidewalk 返回成功且左右激活为true，但实例列表为空、最终网格仍仅路面；该配置未合入参考生成器，也未算通过。探索日志 `junction-geometry-probe.log`、`junction-geometry-probe2.log`、`junction-geometry-probe3.log` 保留这些失败的适配证据。

第二轮原始证据保存在 `build/road-parity/junction-alignment/`，旧 `junction-parity/` 保留。五组两视角双端共20张真实帧；五个Unity场景均在另一个进程重开，三角数一致且重拍PNG逐字节一致。页面 `gallery-35/junctions.html` 已更新到本轮。四臂设施透视/俯视IoU从 .96350/.95553 提升到 .97944/.97920；环岛修正参考朝向后从 .39731/.39598 提升到 .49675/.49581，这一改善属于参考场景配置校正。T/X及T型设施未发生可见变化。阈值保持 .98、内部RGB MAE2/255、P95 5/255，五组仍没有全部视角通过的场景，不宣称核心或渲染完整对齐。

剩余重点：T/X参考步道/路缘作者数据与生成实例映射、环岛半径/环宽实际重建、设施明确转向路由的参考对应，以及B1–B8未闭合项目。不能把参考适配缺口全部归为EVEngine几何错误，也不能用这五组诊断覆盖默认PBR/Gbuffer或原35场景所有材质路径。

本轮完整 `make check CI_BASE=10f6faa7509e4a108ee5f2e6b6e3be357e99af26` 已结束，仍被此前同样7项架构问题阻断：IStateProvider三项bool操作、RenderSystem两项生命周期文档、sensing接口目录和vehicle运行时模块查询。格式、分层、绑定覆盖、测试清单与10种裁剪配置检查通过；未添加豁免。日志 `build/road-parity/junction-alignment-source-check.log`。另行运行比较契约4项、UTF-8契约10项及相关脚本ruff通过。


### 2026-10-16 Unity 作者生成链路与显式设施转向参考

本轮修改参考生成器，不修改 EVEngine 生产公共 API、网络持久格式或模块边界；无架构例外。新增 `scripts/road_parity/UnityJunctionAuthoring.cs`，只调用所供 SDK 公开工厂、作者字段及生成函数；没有移植其几何算法。为定位空人行道，检查了公开生成入口的参数早退条件，确认未注册道路预设会因找不到 rt 而返回空结果。调用含混淆名称的公开生成入口，因此证据同时绑定 SDK DLL SHA、主适配器和辅助适配器 SHA；不能承诺替换 SDK 版本仍兼容。

人行道使用公开 Upgrade 工厂准备完整作者截面，再通过 ERSideWalkInstance 和 SDK 生成器生成实际左右网格。作者截面保持原生U形路缘轮廓及路缘/步道高度，并修正左右边界输入方向。注册预设后，还需公开 SetWidth(width) 设置整路宽度，否则只设置 marker 宽度会在刷新后回落到6m。SDK侧物体采样边界缓存与作者宽度可能不同，输入使用SDK采样中心线和作者半宽重建两条边界；网格由SDK生成。中段实际被三角形引用的顶点检查内外宽度与道路接缝，0.005m容差；此检查不覆盖接入口展开区域，也不证明路口转角人行道已连续。

环岛调用SDK真实生成入口后，按新的环形分段数重新设置四个接入口索引并刷新，再包装当前端口。中心半径12m、环宽3.25m、内外半径及最终连接端点均实测；最终所有连接、步道和设施构建后再次读取尺寸，防止构建覆盖。参考中间点列72段；原生仍为八段环形图的独立生成。道路接入口圆角、岛缘/内侧步道和着色仍有差异。

显式设施路由对应关系已建立：T 型1条、四臂2条。普通 continueOnConnections 和扩展状态链接尝试仍未输出连接段，不能宣称包内自动衔接已复现。当前独立参考使用隐藏路面的 SDK 承载道路生成人工明确指定的转向设施；只激活中间区间，标记 active 和 splineActive 必须分别设置，避免生成承载道路前后段。SDK默认10m设施采样会把转弯压成线段，当前使用1m采样。两端使用相同接入切线方向，SDK直线相邻控制强度按弦长/切线投影转换，并用实际样条点测量验证；没有根据图片像素反复优化参数。该参考限定于本批平面作者路由，未证明任意N臂、竖曲线、非对称/变截面设施和prefab转向自动处理。

原始证据保存在 `build/road-parity/junction-authored/`，保留旧 `junction-parity/` 与 `junction-alignment/`。统一材质、光照、曝光和两种相机未改；20张真实双端帧重拍，五个Unity场景在另一进程重开，三角数一致且透视帧字节一致。新测试输入/生成证据版本2，拒绝旧版本，忽略未知附加字段；生产场景格式未变。发布校验增加实际道路宽度、非空左右步道计数、显式路由计数、最终环岛尺寸与辅助脚本SHA，新增5项契约测试通过；图像比较4项、UTF-8契约10项及相关ruff通过。

透视/俯视整图IoU：T型 .95386/.94713，十字 .96852/.96930，环岛 .89600/.90409，T型设施 .99239/.99084，四臂设施 .98583/.98120。最后两组通过现有整图IoU>=.98、内部RGB MAE<=2/255、P95<=5/255阈值。整图包含大量路面，不能将整图通过等同设施局部掩码、端口接缝或拓扑完全一致；图中仍保留SDK端口距离、设施采样与接缝差异。前3组不通过。与前轮比较，人行道和环岛参考配置修正解释了大量之前误计的差异；本轮没有宣称通过修改原生几何提高结果。

剩余优先项：T/X连接内部转角步道的SDK生成；环岛岛缘、入口展开与着色；设施专用局部指标、端口/端盖接缝，以及真正的包内多臂自动设施衔接。完整make check运行仍被同样7项已有架构问题阻断，日志 `junction-authored-source-check.log`；格式、分层和10种源码裁剪配置通过，未加基线或豁免。


### 2026-10-16 护栏外缘布局纠正（覆盖前轮错误场景验收结论）

用户指出此前护栏横穿路口。已确认是测试作者配对错误，不能归因于EasyRoads自动衔接。旧两组整图通过不能证明布局合理：新增逐三角形XZ足迹相交检查后，旧原生T型/四臂场景分别检出94/88对正面积交叠，最大单对面积约0.245657/0.243229平方米。

本轮只修改对照夹具/参考工具，不改生产API、持久格式或模块边界，无架构例外。反转右侧接入道路方向；四臂还反转南侧道路方向，使既有单侧护栏落到外侧边界。T型明确配对右侧入路到北支路；四臂配对右侧到北支路、西侧到南支路，消除穿越行车区域的旧转向。

SDK短转弯还暴露未激活终点时的零位置截面，三角形从有效截面连到原点附近，形成路中尖片。端盖关闭和平均间距均未消除，不能误记为端盖根因。最终使用0.05m短切线引导段、全程有效标记截面；连接段关闭内部端盖，普通直路保留原端盖。SDK参考仍属于显式作者路由承载道路，未升级为包内自动衔接的证明。

SDK实际渲染网格导出版本1足迹，按证据SHA绑定；原生读取同一份实际渲染夹具。`road_junction_safety.py` 使用三角形裁剪判断正面积相交，包括没有顶点落在另一三角形内的边穿越。单对交叠面积超过0.0001平方米则拒绝发布。当前T型、四臂的Unity与EVEngine全部最大交叠面积为0、相交对数0。页面已加入几何安全状态，保留整图指标但不能以它替代布局检查。

原始证据 `build/road-parity/junction-outer/`；`guardrail-correction.json` 记录修复前后及源码SHA。五组20帧在相同材质、光照、相机下重拍；五个Unity场景独立重开且透视PNG字节一致。4项几何检查契约、5项生成契约、4项图像比较、10项UTF-8契约和相关ruff通过；MSVC夹具构建通过。生产代码未变，不将上一轮生产领域测试计为本轮新执行。既有T/X转角步道、环岛和设施局部接缝差异继续保留。

### 2026-10-16 可选设施与坡度立交扩展

新增8组独立双端测试作者场景：中央隔离带、双侧路灯、花箱/树形装饰/长椅、三类设施组合，以及30度/4%、60度/8%、90度/12%、45度/8%纵坡加5度横坡的分层道路。测试数据使用现有生产RoadStyle、连续设施和源prefab变形路径，没有新增生产API、持久字段或模块边界；单一作者状态与Result规则继续适用，无架构例外。源数据保存在recipe_network.json/authoring.json，不将测试夹具当作新的生产格式。

原生场景由独立OptionalFixtures.cpp构造，经生产recipe网络快照和直接bake两条路径比较网格相等。参考UnityRoadOptionsCapture.cs从相同作者输入与源prefab独立调用真实EasyRoads SDK生成；没有导入原生烘焙网格。修正多设施适配器原先逐次用单元素列表覆盖前项的问题，现在完整扩展列表注册，并逐设施断言实际非空渲染器。闭合桥面显式启用SDK自定义道路形状，防止作者形状已赋值而仍只生成平面。该修正限于本轮参考适配器，不表示旧component参考已重拍。

证据根build/road-parity/options-parity/；图库gallery-35/options.html，共32张1280x720双端真实帧，材质、光照、曝光与相机一致。8个Unity场景在独立进程重开，三角数一致、透视PNG逐字节一致。生产网络schema仍为eve.procgen.roadNetwork v1；测试输入/参考/比较证据使用各自schema与version1，拒绝未知版本、忽略未知附加字段。发布绑定作者输入、适配器、SDK、实际网格快照、场景、重拍帧与运行时哈希，检查原生截图成功日志及所渲染网格与源快照相等。

中央隔离带宽1.2m，置于两个6m车道之间的2m空隙，Unity/EVEngine实际三角形足迹均0相交；双视角通过既有整图阈值。其余7组未通过，不能宣称新增设施已经一致。整图透视/俯视IoU分别为：路灯.8521/.9442，装饰.8214/.9009，组合.8708/.8762，30度立交.9207/.9514，60度.9215/.9486，90度.9236/.9450，45度横坡.8745/.9438。全部保持IoU>=.98、内部MAE<=2/255、P95<=5/255，不修改原35场景分数。

立交实际道路三角形采样验证纵坡4/8/12/8%，交叉中心实际净空约9.194至9.200m，两层无共用连接节点。该中心净空测量不是全桥最小净空证明。剩余差异包括模块重复相位/首末位置、护栏局部偏移、桥面端部与桥墩随坡变形；测试灯具没有夜景照明，装饰使用测试形体和分类色。当前为分层桥梁测试，无匝道，不等同完整互通、最终资源材质或刚体直立部件对齐。

MSVC场景夹具构建和23组导出通过；新增7项实际高度/坡度证据检查测试、9项路口生成/足迹契约、4项图像比较器、10项UTF-8测试及相关ruff通过。图像比较器首次因沙箱临时目录权限失败，主机正常权限重跑4项通过。本轮只扩展夹具和参考工具，未重新执行生产领域全回归或完整make check；前轮7项源码架构问题未在本轮修复。

### 2026-10-16 可选设施参考用法审计：暂停作为验收依据

用户质疑参考质量后重新对照随包html/side_objects.html：Mesh Object段明确将灯柱、树木、岩石等列为独立实例放置对象；Procedural Mesh Object段用于连续栅栏、墙、护栏和桥梁等源网格重构。当前UnityRoadOptionsCapture.cs将所有非空源prefab统一设为objectType=2并用align=2，灯具/装饰因而并未验证推荐的独立实例路径。桥墩也只是自制整体源网格随路变形，不能代表插件正常桥梁预设效果。全部测试资产由OptionalFixtures.cpp的box构造，并非插件自带美术资产；参考材质被替换为分类色与共同诊断shader，阴影关闭。

因此撤回“这8组已具备用法正确的插件质量/功能对齐验收基础”的隐含判断。真实SDK调用、保存重开、截图及几何数据仍有效，但只能证明这些人工配置下结果可复现；非空设施和整图阈值不足以证明正确用法或优劣。options.html已加显著审计提示，comparison.json增加referenceUseAudit；现有数值仅为测试夹具差异，不作插件质量排名。尚未完成独立Mesh Object放置、官方桥梁使用配置及原生美术效果的重建验证，需修正参考后重拍。本次未修改生产API/格式，无架构例外。

### 2026-10-16 转向随包原始示例基线

按用户要求，测试基准改为EasyRoads随包原始示例，先保留作者资源与配置，再建立原生对应场景；自制拼装件不再作为核心对齐验收基准。新增UnityBundledExamplesCapture.cs，只打开场景、读取组件/网格/依赖并在内存添加证据相机，不重建道路、不替换材质、不保存原场景。road_bundled_examples.py从用户unitypackage读取原始场景并验证包内、工程、生成证据三者SHA一致，验证所有场景依赖及帧哈希。发布页面gallery-35/bundled-examples.html，证据bundled-examples/manifest.json。

包内3个示例均已清点：scene main包含33个ERModularRoad、T/X连接、1个环岛和地形，实际可见网格32285三角形；scene runtime - existing road network编辑状态有1条道路、162三角形；scene runtime - new road network编辑状态无网格，需要其自身运行脚本创建，当前未完成play验证，不能判作缺功能。前两者各拍overview/detail共4帧，保留原有shader/材质/灯光，缺材质与不支持shader计数均0。这些是官方作者场景原始参考，尚未完成EVEngine对应场景，无一致性分数。当前包内未检出独立路灯/长椅/桥梁源模型prefab，不再用自制方盒冒充官方资源。没有生产API/格式或模块边界修改，无架构例外；Unity批处理退出0、发布来源/依赖检查与ruff通过。

### 2026-10-16 随包示例的原生对应与第一轮逐项修正

新增UnityBundledSceneExport.cs读取原包作者控制点、控制器、截面/UV资源、原网格法线/切线、材质纹理与采样/深度偏移、灯光、实际环境球谐、相机和完整地形高度/权重。用层级及同级索引作为道路标识，避免主示例同名道路覆盖。SDK GetWidth包含步道，原生采用实际截面界定行车道。新增UnityBundledRuntimeCapture.cs通过真正Play生命周期运行原包runtimeScript，运行前只克隆TerrainData并同步Collider，不编辑官方脚本，不修改源资产。三个原示例分四种状态：主33、已有路网编辑1、已有路网Play2、新建路网Play1，共37份道路作者数据（不是37个独立示例）。

原生BundledRoads.cpp仅调用生产restoreRoadNetwork/bakeRoadNetwork，逐条建立独立道路，再与共享的原SDK连接/环岛网格及同一地形资源组合。每份网络保存恢复后比较位置、法线、UV、索引和分组完全相等。必须明确：目前未建立等价的完整原生路口连接图；共享连接资源不构成原生路口生成通过证据。原网格同几何导入另列校准，不能与独立生成混算。

road_bundled_alignment.py将相同源纹理与材料参数绑定到GLB资源及原生显式材质移植路径；原网格切线经测试夹具COLOR_0运输，原Unity道路shader本身不使用该颜色通道。原生生成的UV由夹具适配原作者截面图集，目前不是生产UV合同扩展。地形保留原高程、两层纹理与512x512权重，并按原tileSize采样，不再预合成低分辨率草地。

本轮修复了深度偏移漏传导致的路面/地形Z冲突、mipmap与重复采样、切线/手性与纹理V方向、额外FXAA和原2x MSAA的配置差异。公开属性实测地形shader为Nature/Terrain/Diffuse，原生按Legacy Lambert处理，不再误加Standard粗糙高光。环境光分析中曾错误把0.033线性SH直接用于Gamma着色；该尝试已撤回。依据本机UnityCG ShadeSH9及UnityStandardUtils ShadeSHPerPixel，Gamma路径须把SH转回约0.2，最终代码及契约测试保留完整转换，不能把降环境亮度称为对齐修复。UnityBundledTerrainProbe.cs保留原材质公开属性证据。

证据根build/road-parity/bundled-alignment/，页面gallery-35/bundled-alignment.html。四状态两相机：Unity原材质8帧、原生同几何校准8帧、原生独立道路8帧，各有对应道路专用掩码，共48张真实帧。阈值仍为IoU .98、内部MAE2/255及P95 5/255；1像素带外计数仅诊断，不替代阈值。每次原生启动前记录GLB/脚本/纹理/SPV哈希，发布校验截图输入与输出未变；版本化测试schema均拒绝未知版本、忽略未知附加字段。原生生产网络schema仍为v1。40项源依赖（含全部三场景、TerrainData、材质、纹理、原脚本与SDK）逐字节匹配原unitypackage，运行后源地形仍相同。

最终主示例同几何总览/细节整图MAE为1.399/2.034，独立生成1.419/4.074；对应道路专用IoU为同几何.85976/.98232、独立生成.79008/.87910。远景极细路的采样覆盖差异明显，因此不能只看整图MAE很小就报通过。已有路网编辑态独立生成IoU .99601/.99646，但内部RGB MAE约5.555/5.599，仍失败。已有路网Play独立生成IoU .97819/.99294，新建路网Play .98096/.98252；两者内部颜色亦超阈值。四状态均未同时通过所有既定图像阈值，不改变原35场景的历史分数。

逐条道路表保留实际被索引顶点的包围盒差（不冒充Hausdorff距离）。主示例Default Road0045包围盒最大差13.84169m，0056为5.17993m，圆弧语义与连接端约束是原生下一优先项。UnityBundledCurveProbe.cs用0045相同作者数据通过SDK公开接口独立重建；原场景/隔离SDK中心线包围盒差约.01008m，但采样/端部点并非逐点相等。该证据表明大轮廓差需要继续审查原生Circular语义，不能继续笼统归咎参考场景配置；尚未据此臆造或复制SDK算法。

MSVC夹具构建、37份完整网格保存恢复、14项作者/截图输入/环境转换契约、4项图像比较器及10项UTF-8测试通过，相关ruff与git diff --check通过。只修改测试夹具、参考导出及显式shader移植，没有生产公共API、生产存储格式、模块边界或生命周期改动，无架构例外；未在本轮重跑生产领域全套或完整make check。剩余Shader BRDF/环境反射/阴影过滤、天空/地形LOD、原生连接图与Circular几何、生产截面UV合同均明确未完成。

### 2026-10-16 原包 Circular 直线引入与双端切向修正（v21）

使用原包主场景 Default Road 0045 的四个作者点，通过 SDK 公开 CreateRoad / SetMarkerControlType / SetSplineStrength / GetSplinePointsCenter 隔离验证，未读取或移植 DLL 内部算法。扩展探针保留三个变体：出弯改为向东、前一段长度加倍、strength 改为 .25。后两者圆弧主体边界几乎不变，改变出弯方向则改变主体形状。隔离中心线的中间段首先沿入射方向直行，之后的圆弧拟合半径约24.80765m、圆心Unity XZ约(262.92221,489.09888)，拟合径向残差小于.00002m；不是从第一标记立即开始的一整段圆弧。

生产 SplinePath 在有前后邻点时增加双端切向的非负直线引入拟合，再沿直线与圆弧的合计长度采样；道路反转使用同一派生系数的反向采样，保存仍仅持有原作者控制点。端点缺少邻点、方向退化或双端拟合需要负引入长度时，保留原单切向圆弧合同；这些组合的EasyRoads行为尚未补齐验证，不能声称全部Circular控制已对齐。内部系数为同步owning value，无回调、随机数、跨域状态或持久字段；新增计算返回受检查的Result，DLL导出显式声明，生产烘焙身份升至road-core-2026-10-16-circular-tangent-lead-v21，避免复用旧派生产物。

保留旧comparison.json、每路native-roads.json、原生帧及输入清单到build/road-parity/bundled-before-circular-v21/。当前对照页增加Unity / 修复前 / 修复后三列，检查源scene.json、灯光、相机输入与Unity帧哈希一致，不更改阈值。Default Road0045包围盒最大差由13.84169m降至.42349m；0056由5.17993m降至.05145m；它们是被索引顶点的AABB差，不是表面距离。主示例独立道路总览/细节IoU由.79008/.87910提升到.81564/.94945。细节内部RGB MAE仍约10.742/255，四状态均未通过全部图像阈值。当前最大剩余AABB差为0034的2.35211m，仍需核对端部与截面框架。

37份网络仍经生产保存恢复后网格逐项相等；新增官方点位回归覆盖直线引入、边界及反转恢复。STATIC/SHARED两种闭包各168例通过，artifact测试3例通过，实际EVEngine主程序编译链接通过。截图清单新增实际exe/DLL哈希，发布器拒绝无运行时记录或编译后旧帧；15项作者/截图合同、4项图像比较器及10项UTF-8合同通过。没有生产存储版本、模块边界、ECS或生命周期修改，没有架构例外；完整make check的最终结果另记本轮门禁日志circular-v21-make-check.log。

最终主程序已重拍32张原生彩图/掩码，40项原包资产校验通过，发布器检查所有帧的实际运行时与当前程序一致；Unity原始16张参考帧沿用未改动的基线。完整make check本轮已结束：格式、分层、脚本覆盖、测试布局及10种裁剪配置通过，仍被相同7项已有架构问题阻断（IStateProvider三个bool操作、RenderSystem两项生命周期说明、sensing接口目录、vehicle运行时模块查询）。没有新增基线或豁免，不能把本轮报告为全仓库门禁绿色。

### 2026-10-16 八类原包道路受控派生与负引入修正（v22）

新增UnityBundledVariants.cs，每例重新打开原包scene main，在原道路0045实例上通过公开UnConnectStart/End、SetMarkerPositions、SetMarkerTilting、Refresh编辑；不是重新拼装简化替代物。GetRoadType工厂并不自动携带该道路实例的全部作者截面，因此最终保留原实例并断开连接，而非接受默认截面。参考导出前逐项检查截面及实际材质对象相同，其他道路及连接上下文隐藏，原地形先克隆，源文件不保存。八例是出弯向东/北/南、X镜像、半尺寸、沿四点累计升高12m、累计降低12m、抬高3m并倾斜10度；不是新增八个官方示例。derivation.json记录原点位、变体点位、公开SDK中心线、场景和道路身份与生成脚本哈希，发布器同时与此前未修改的原包作者数据核对。

向南出弯使双切向圆弧的引入长度为负；真实SDK中心线在该控制区间只发出起点、弦中点和终点，直接连线。v21的单切向圆弧保留策略与此不符。v22改为经过既有有限性/非法反向几何验证后发出直线弦，并保留前邻普通曲线的入射方向；反转时对应修正后邻方向。无新增生产持久字段或公共领域接口，仍由同一SplinePath拥有纯派生系数，Result受检查，生成身份为road-core-2026-10-16-circular-unrealizable-chord-v22。原先精确掉头不可实现几何的拒绝与原子编辑合同保留，不能把该例推广成所有非法输入都自动转直线。

证据build/road-parity/bundled-variants/；修复前网格及失败例四张原生帧保留在bundled-variants-before-v22/。新页gallery-35/variants-v22/bundled-alignment.html按两视角展示Unity原插件生成、原生同几何校准、原生独立生成、差值和道路掩码；失败例另有v21/v22并排图，并核对材料/脚本输入哈希未变。共32张新Unity彩图/掩码与64张最终主程序EVEngine彩图/掩码，所有帧1280x720，40项原包资产逐字节相同；不覆盖原包三示例的历史页面。新增capture_bundled.ps1统一引擎自有截图与输入、exe/DLL清单，发布器拒绝陈旧运行时、改变的原始点位或变体点位、未知来源schema版本。

向南出弯细节IoU由.46692升至.98931，总览.98990；AABB最大差6.8833m降至.3596m。其他变体最终AABB差依次为向东.1646m、向北.1187m、镜像.0276m、半尺寸.0251m、上坡.0364m、下坡.0922m、倾斜.0195m。它们不是表面距离。16个独立生成视角中10个道路IoU达到.98，但上坡.96892/.96838、下坡.96510/.96461、倾斜.97813/.97926仍未通过。各例道路内部颜色MAE仍约7.09～11.31/255，八例均未通过全部图像阈值。坡度例显示包围盒接近不能证明中间段高度相同；按XZ匹配中心线的诊断还存在约.2m高度差，含采样相位影响，尚未建立精确插值合同，不据此硬调参数。

验证：STATIC与SHARED道路闭包各169例、artifact3例、作者/截图/来源合同18例、比较器4例、UTF-8合同10例均通过，实际eve主程序编译链接通过，每例仍检查保存恢复后的网格相等。完整make check已执行，格式、分层、绑定覆盖、测试清单及10种裁剪配置通过，仍是IStateProvider三项、RenderSystem两项、sensing及vehicle共7项已有架构问题；日志bundled-variants-make-check.log。未引入模块边界、ECS、跨域生命周期或生产存储格式变更，没有架构豁免。当前剩余重点：坡度高度插值与倾斜截面框架、原道路0034端部约束、Standard/Gamma反射/阴影与颜色差、原生连接图和生产截面UV合同；未改原35场景历史评分。
