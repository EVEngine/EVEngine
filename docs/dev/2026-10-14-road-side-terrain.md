# 道路设施地形高度取样

EasyRoads 外层包 `side_objects.html` 的 Mesh Bounds Alignment 说明提供最低、最高和平均高度选择。本次实现对应的高度模式，并明确采用作者显式配置的局部矩形足迹；尚未证明参考实现的采样网格、权重或自动资产包围盒算法。

`RoadStyle::sideTerrain` 是唯一作者状态。None 保留道路高度；Center 查询锚点；Lowest、Highest、Average 在朝向旋转后的足迹上均匀取样。每轴 2 至 17 个样本，默认 3；平均值是样本的等权平均。此入口仅调整 side.left/right 锚点高度，法线保持世界向上；不声明 prefab 顶点变形、地形法线对齐或完整 Mesh Bounds Alignment 已完成。

原生 bake 可同步借用 `IProcgenWorldQuery`，标准 recipe 使用同一公共 capability。查询服务只解析一次，所有锚点共享工作预算；缺失、未命中、非法高度、服务错误和预算耗尽均返回结构化失败，不发布部分结果，不修改作者网络。提供者必须在调用期间存活；回调不得修改作者网络、注销自身或重入 bake。

启用贴地的标准 recipe 必须显式提供 `placementWorldRevision`（非空、不含控制字符、最长 256 字节），该依赖随 canonical Params 进入 artifact key。调用方负责标识查询世界内容与选择规则，并在可能改变投影结果时更新版本；调用期间世界必须保持稳定。当前公共查询接口不能自动提供内容版本，因此这里不声明自动依赖跟踪已完成。直接原生 bake 不缓存，无此参数要求。

网络快照与共享类型覆盖使用同一 style codec。缺少新增字段的旧快照恢复为 None；已提供字段必须完整且有效。共享类型编辑、保存、撤销、恢复及重做保留该规则。算法缓存版本同步更新，避免沿用旧放置结果。

`test/integration/gameplay-composition/procgen_road_side_terrain.cpp` 覆盖四种模式、标准 recipe 消费、服务存在/缺失、查询失败/未命中/非法高度、共享预算和作者数据不变，以及快照、类型覆盖和撤销历史恢复。原生独立 harness 的全部三个 CTest 入口已通过。DLL 路线与正式模块编译仍需当前构建验证；真实 Unity 设施贴地渲染对照尚未完成。

剩余差异：自动导入资产包围盒、参考采样算法、旋转与法线对齐、刚性 prefab 实例及连续变形设施的贴地消费，以及真实双端设施对照。道路表面 35 类轮廓结果不作为这些项目的完成证据。

补充验证：正式 `procgen-core-only` 的 `EVProcgenCoreCheck` 已以严格警告编译通过，包含新查询实现、recipe 版本校验及 v7 producer key。14 项渲染证据校验测试通过。新增 artifact 贴地/保存恢复和异常快照检查尚待独立测试构建结束，不能沿用此前通过结果。完整 `make check` 已完成，仍报 `IStateProvider.h` 三项 bool API 和 `RenderSystem.h` 两项裸指针生命周期契约问题；未添加 baseline 或豁免。

当前源码独立验证：`build/road-parity/native-verified` 已完整编译，道路与样条入口通过；artifact 测试初次发现测试自身跨发布借用 PointSet 的生命周期错误，改为拥有副本后重编译，该入口通过（1 case / 172 assertions），证明贴地放置数据进入生产 artifact、保存恢复保持高度、查询世界版本变化改变 key。`shared-verified` DLL 与测试可执行文件已链接成功，运行结果待确认。两个旧构建仍存活，独立目录未覆盖它们的输出。

最终 DLL 运行结果：`ctest --test-dir build/road-parity/shared-verified --output-on-failure` 的道路和样条两个入口均通过，包含 provider 存在/缺失、注入及注册查询、保存撤销和查询预算用例。上述待确认状态已由此结果替代。没有因此宣称完整资产工具链或双端设施渲染已对齐。

朝向对齐的生产前提审查发现：PointSet 已拥有 pitch/roll，但程序化场景描述只传 yaw；Scene 的 NodeDesc/TransformSystem 实际消费弧度，而程序化点集使用度。已补齐拥有值描述的 pitch/roll，两个场景发布入口统一将三轴角度转换为弧度。Scene 的全量快照、原子多批次和 delta 都经过对应入口，不新增持久化格式；现有 SceneNode 保存路径保留其原生弧度语义。新增 `test/integration/gameplay-composition/scene_procgen_rotation.cpp` 检查全量、增量和过期拒绝，生产者测试检查 pitch/roll 传递。该跨模块改动尚未完成编译/运行验证，不属于上面的道路独立 harness 通过范围。Terrain Normal/Path Forward/Path Normal 作者规则本身仍待接入与真实双端测试。

进一步补入场景发布前的有限角度检查：兼容单批次与原子多批次均拒绝非有限旋转，测试检查拒绝后批次版本与已发布旋转不变。正式主机验证配置中，`3d` profile 因 spritestack_editing 在过滤后无源码而失败；改用仓库默认 full profile，未修改该 profile 的模块声明或为道路增加豁免。当前 full 配置仍在执行，不能视作编译或运行通过。旋转改动后的完整源码门禁仍报告相同五项既有架构问题。

full 主机配置已成功完成，确认新增 scene_procgen_rotation 源码归入 unit_test_scene。当前已启动 unit_test_scene 与 unit_test_procgen 的正式构建；尚未完成链接及对应测试运行。

主机构建首先确认 ECS.hpp 子模块缺失，已按仓库固定提交初始化 ECS.hpp、VKBuilder 和 ik.hpp，再配置成功并继续构建。程序化发布方也在调用 provider 前拒绝非有限 pitch/yaw/roll，测试检查原发布姿态保持不变；此检查不依赖具体 provider 的防御实现。源码审查确认 EVPCG_CELL v3 已序列化并校验三轴角度，既有 `persistsAttributedCellCachesAtomically` 用例含非零 pitch/roll；Scene JSON 则直接恢复原生弧度字段。因此本次发布边界修正无需更改已有保存格式，但上述既有用例和新增场景测试仍需当前二进制运行证据。

朝向作者规则已接入：`sideTerrain.alignment` 为 None/TerrainNormal/PathForward/PathNormal；缺少新增字段的快照恢复 None。None 保留原 yaw 输出；TerrainNormal 在锚点中心单独查询并规范化地形法线，将路径方向投影到切平面；方向与法线平行时选择确定性备用轴。PathForward 保持路径前向并去掉横滚，竖直前向沿用有效路径上向；PathNormal 保留路径上向及横滚。所有输出为拥有的 pitch/yaw/roll 度值，采用 Y-X-Z 组合；垂直前向以 roll=0 的等价角度表示。高度与朝向独立，即不贴高度也能选择地形法线。地形法线查询与高度取样共享预算，拒绝未命中及非法/零法线；标准 recipe 在任一查询启用时要求世界版本，缓存 producer 已升至 v8。

这些规则经过作者校验、共享 style codec、类型覆盖与撤销历史路径，新增测试检查倾斜平面法线与 Euler 上向一致、PathForward/PathNormal 横滚差异、无 provider 的路径模式、非法法线拒绝和保存后重做。当前新版 native 测试仍在编译，尚不能计为通过；真实 Unity 法线取样算法、足迹对齐与渲染尚未验证。上述历史“高度专用”范围只描述前一版，不能作为当前朝向功能的完成证据。

新版 native 验证已完成：三个 CTest 入口（road_profiles、road_author_artifact、spline_contracts）全部通过，总计约 13 秒。这替代上条 native 待验证状态；不覆盖正式 Scene 实例发布或真实 Unity 设施。当前 full 主机构建仍在编译，新版 shared 验证已启动。

进一步验证：新版 shared 道路/样条两入口通过。新增竖直姿态与高度/法线共享预算用例最初暴露测试中心线没有完全竖直，修正两端节点及全部中间作者点后，native 道路入口全部通过。正式 full 的 unit_test_scene 和 unit_test_procgen 已完成链接；当前正在重编译构建期间修改的测试，正式 Scene 运行证据仍待取得。

正式运行验证已完成：当前 full 引擎的 Scene.procgenSink、procgen.sceneSink、EVPCG_CELL v3 持久化和道路 side 系列定向 CTest 共 14 个用例通过，包括新三轴旋转全量/增量/过期拒绝测试。原生、DLL 与真实场景 provider 的组合证据现在均存在；这仍不代表 Unity 设施姿态或自动包围盒算法已对齐。

当前源码 eve 主机已链接成功并真实捕获默认采样两视角，Unity 轮廓 IoU 为 0.99463449/0.99580203；重新生成的 35 个 native fixture.json 与既有输入逐字节一致。该帧仅是道路表面基线。SHARED 证据 v2 增加可执行文件路径和捕获脚本实际使用的两个 DLL 搜索目录文件哈希，捕获前后变化被拒绝，比较时重新检查；这不是 OS 已加载模块清单。旧 v1 证据保留原来的有限证据范围，不能据此宣称其运行库版本已确认。15 项证据校验测试通过，包括 DLL 变化拒绝。

自动资产包围盒的下一步前提：当前 IProcgenSceneSink 将 asset 标识写入节点名称/pcg.asset 标签，但这条路径未加载资产网格；源码中未找到标签的实际资产解析消费者。因此 generic side 锚点发布不等于 prefab 资产已显示。连续设施已有源网格值变形，但不能替代刚性实例资产导入、实际网格加载、包围盒导出及相应失败事务。后续需贯通这条生产消费路径，再验证 Unity 设施。

证据版本验证补充：未知版本明确拒绝，16 项 Python 证据测试及 ruff 通过。当前比较仍保留两类镜像圆弧差异，未用默认采样场景通过替代它们。

自动包围盒实现开始于实际 evpack 消费端：LoadedGraphicsMesh 从 admitted canonical 顶点计算拥有的局部 min/max；EvpackStaticPrefab 将启用 renderer 的这些网格 AABB 八角点乘完整 prefab 层级变换并聚合，公开 Result localBounds。该值与 GPU lease 指针无关，release 后仍是有效的不可变 CPU 快照；没有 renderer 返回 NotFound，变换后不可表示的坐标使整个加载候选失败并释放已分配资源。这是保守的层级 AABB，不是动画或任意实例变换后的精确顶点包围盒。

真实 Unity prefab 导入/evpack 测试新增了带根平移的六轴包围盒及 release 后保留检查，当前正式 asset 领域构建正在执行。道路作者规则仍未自动消费该查询，也未完成 generic 实例资产加载；此新增资产消费 API 不替代完整设施资产接线和双端渲染验证。资产 persistent 格式未改变，新增数据均为派生 CPU 快照。

资产端验证已通过：正式 full SHARED 的 unit_test_asset 构建/链接成功；unityStaticPrefabResolvesAndReleasesGpuLeases（36 assertions）通过，使用真实 FBX/Unity prefab 导入、cook 和 evpack 读取，backend mock 只持有不透明 lease token。独立检查模板根平移为 (1,2,-3)，本 fixture 的 FBX 解码 X/Z 为 [-1,0]、Y 为 [0,1]，聚合范围为 (0,2,-4) 至 (1,3,-3)。测试还覆盖 mesh/image 加载失败不损坏既有实例和 release 后 CPU 范围保持不变。接口契约相对 HEAD 检查通过；完整 make check 仍报五项既有架构问题，无新增 baseline 或豁免。

道路自动消费已接入：作者的可选 RoadSideAssetBinding 使用强类型 AssetRef 与精确 package build ID；持久化使用规范 assetRef/assetBuildId 字符串，旧数据缺省仍为显式足迹。PrefabBoundsCatalog 从实际已接纳 prefab 复制 CPU 范围及构建身份，不持有 GPU、reader 或 prefab 指针；重复、缺失与过期构建明确失败。组合宿主负责注册/注销 IProcgenAssetBounds，bake 仅同步借用；每个不同 asset/build 在一次网络烘焙中解析一次。自动范围覆盖显式 XZ 足迹，输出保留本地六轴 bounds、asset 与 asset_build_id。遵循 canonical -Z 前向的资产，输出相应转换后的 yaw/完整姿态；无资产的旧输出不变。Producer cache 已升至 v9，构建身份进入作者快照和 recipe key。

验证：强类型版本的原生道路/artifact/样条三个入口通过；正式 full SHARED 的七项定向测试通过，包含真实 Unity prefab 导入→evpack→版本目录→标准 recipe 的组合、自动斜面平均高度、缺失 provider、非法范围、过期构建、快照以及共享类型保存/撤销/恢复后重做。强类型改造期间正式链接抓到旧 string_view 提供者对象，强制重编译提供者与传递消费者后消除，未保留兼容导出。

范围限制仍保留：自动取样使用 yaw 旋转的均匀 XZ 网格，不代表完整倾斜几何的地形覆盖算法；加载器保留模板根 TRS，Unity 刚性实例根姿态的替换/组合规则尚未对照。构建 ID 守卫覆盖 package 内容变化，但多 capability variant 的跨上下文依赖与缓存仍待核对。generic Scene 实例仍缺实际 prefab 几何挂接，完整资产渲染、精确 Mesh Bounds Alignment 的弦向旋转以及双端设施帧未完成。

完整姿态取样修正：先生成最终拥有的 pitch/yaw/roll，再以与场景相同的 Y-X-Z 组合投影地形取样点。手工足迹使用局部 Y=0 平面；倾斜资产将来源 AABB 的 minY/maxY 两层均匀网格投影到世界 XZ，因而源网格高度也参与覆盖。直立资产折叠重合的上下层，Center 仍只查询锚点；所有层和法线共享工作预算。法线改为候选姿态准备时查询，拒绝的候选也可能消耗查询预算。平均值定义为投影样本的等权均值，并非连续投影面积积分；不宣称参考的非线性地形取样已确认。

Producer 版本升至 v10。原生三个 CTest 入口通过，新增非对称高度范围的 90 度横滚资产测试，独立检查线性斜面上的投影质心平均高度和每实例 18 次查询。此结果替代上一版仅 yaw 取样的限制描述；真实 Unity 取样分布、弦向对齐、根姿态和 generic 资产实例渲染仍未完成。

正式 full SHARED 编译及七项资产/道路定向组合测试通过，包含此次完整姿态取样的新边界。没有新增作者字段或保存格式，变更只影响派生姿态与投影算法；既有绑定保存、撤销和恢复路径随组合测试通过。

实例发布版本守卫已接入：ProcgenInstanceDesc 新增拥有的可选 ProcgenInstanceAssetBinding（AssetRef + package build ID），旧 asset 文本仅作为兼容标签，提供强类型绑定时必须一致。发布方将 asset_build_id 解析为规范非 nil 身份，错误类型、非法版本和标签不一致在 provider 提交前拒绝。Scene 全量/多批次候选及增量保存该绑定，节点记录派生的 pcg.asset-build 标签供场景持久化与诊断；标签不是新的资源所有者。新增生产者及真实 Scene 用例检查绑定保留、非法身份不覆盖已有状态和标签持久化入口。最终编译和运行证据尚待完成，实际 prefab renderer 挂接仍未实现。

版本守卫验证完成：正式 full SHARED 的 scene/procgen 两个领域程序编译链接通过，9 项定向 CTest 用例通过，覆盖绑定输出、非法身份拒绝、真实场景标签、全量/增量及既有单元持久化、修复/清理组合。未单独新增 Scene JSON 标签往返用例，因此此结果不外推为新资产实例 renderer 的持久化证明。

Scene JSON 往返用例已补足并通过：将版本绑定的真实程序化 host 导出，再挂载到新 host，验证 asset/build 标签及更新后的 pitch/roll 仍一致。此结果替代上条缺少 JSON 往返的状态，但只证明源元数据与姿态，不证明 GPU renderer 恢复或批次 provider 的内部缓存自动重建。

渲染挂接的资源准备阶段已实现：EvpackStaticPrefab::prepareScene 返回拥有的 PreparedStaticPrefabScene，保留 shared prefab pin、完整 PbrSurface 材质及每个 renderer 的局部变换/网格/阴影标志。parts 中的 mesh/material 借用仅在租约存活且未 release 时有效；调用方必须先解除 renderer 链接。资产显式 release 在 pin 存活时返回 PreconditionViolation，不使已有资源失效；租约清理其 GPU 材质记录，再解除 pin。准备不创建 ECS/Scene 节点，失败不提交部分场景。最多 65536 parts；TVE 变形明确 Unsupported，仍需专用 adapter。GPU-driven 当前仍拒绝完整 PbrSurface，未借此放开不支持的功能。

正式 SHARED 构建和真实导入/evpack 用例通过，新增检查材质表面与根变换投影、提前资产释放被拒绝且 factory 释放计数不变、重复租约释放及已释放资产不可再次准备。此用例使用真实导入与具体 Graphics 后端对象，但 mesh/image 工厂仍为不透明 mock token，未发出 GPU draw，因此不作为实际 prefab 画面证明。ECS renderer 创建、Scene 原子挂接/解绑与 renderer 恢复仍待接入。

多实例租约验证已完成：同一已加载 prefab 的两次 prepareScene 共用 mesh、持有不同 Material；释放第一个租约后，第二个仍保留完整 PbrSurface，资产 release 继续拒绝，最后一个租约释放后才解除资源保护。空 owner 明确失败。正式 unit_test_asset 重新编译链接后该用例通过；随后 16 项资产、道路贴地、姿态、场景发布与持久化定向 CTest 全部通过，帧证据工具的 16 项 Python 测试通过。

场景接线复审：graphics/SceneLinks.cpp 的 pushRenderable3D 保留完整 linkedWorld，可承载父级剪切与镜像；scene/TransformSystem.cpp 的作者局部矩阵按 Y-X-Z 旋转组合。现有 scene/loader/SceneLoader.cpp::linkMeshNodes 为 Assimp slot 逐项创建 renderer，加载失败会继续其他项，并非版本绑定 prefab 批次的原子挂接入口。后续接线必须保留候选发布、旧批次回滚、renderer 先解绑销毁再释放 preparation lease 的顺序；不能直接以该兼容加载器的部分成功路径替代批次事务。此复审没有新增公共 API、持久化格式或架构豁免，实际 ECS 挂接仍未完成。

StaticPrefabRenderInstance 已加入实际 ECS 消费实现（最终构建及新测试仍待完成）：显式消费 preparation lease，每个启用 part 创建一个 Renderable3D，借用其网格及完整材质并投影阴影标志。调用方的 affine world 矩阵与 prefab-local 完整矩阵相乘，保留剪切与镜像；位置和尺度仅作为派生 renderer 元数据。无新模拟 System 或 View：在非 deferred 的 graphics/ECS 所有者阶段创建，写 Transform3D/MeshRenderer；无事件、时间、RNG 或逐帧资产字符串查询。更新先检查全部 generation handle 与矩阵可表示性，失败不修改任何存活 renderer。外部删除/槽位重用以 ECS EntityHandle 检测；release 清除借用并销毁存活实体后才清理 preparation，表、Graphics、工厂均必须存活到 adapter 释放之后。

新增用例覆盖真实导入/cook/evpack 到 ECS、反射及剪切矩阵组合、非法输入保留旧姿态、过期 handle、不误销毁重用实体、正常释放销毁实体；另有独立真实 GPU 用例通过现有 GraphicsMeshFactoryAdapter/GraphicsImageFactoryAdapter 上传，读取有设施/释放后无设施两阶段图像并写 PNG。这些是待运行的验收要求，不是已通过的声明。没有改变作者快照或持久格式；Scene 节点原子发布、动态批次消费、保存后的 renderer 重建及 Unity 设施双端帧仍未完成。

上述验收现已运行通过：full SHARED 严格构建完成，最终增量复核没有漏编 source；17 项资产/道路/Scene 定向组合测试全部通过。随后补充 deferred 创建拒绝用例，重新编译后 CPU 生命周期和真实 GPU 两项再次通过。GPU 用例使用实际 Vulkan、真实上传的网格/纹理及完整 PbrSurface，正向检查可见红色像素，释放 adapter 后再渲染四帧并检查红色像素为零。输出为 `build/road-parity/host-profile/test/out/static-prefab-owned-vulkan.png`，来自引擎 getPixel readback，已经目视确认不是空帧；未使用桌面截图。测试明确关闭 gbuffer/gpuDriven 等分支，仅证明现有 forward 路径，不证明默认图、GPU-driven 完整 PBR 或 EasyRoads 设施外观已对齐。

证据日志：`prefab-render-instance-configure.log`、`prefab-render-instance-build.log`、`prefab-render-instance-final-build.log`、`prefab-render-instance-tests.log`、`prefab-render-instance-composition-tests.log`，均位于 build/road-parity。增量架构目录检查（98 条）及 diff 空白检查通过；完整 make check 仍报告 IStateProvider.h 三项 bool 与 RenderSystem.h 两项生命周期文档违规，无新基线或豁免。适用 Result/nodiscard、唯一资源所有者、代际 Handle、先验证后发布、六面契约和成本/线程/生命周期规则；没有新增 ECS System、时间/RNG、脚本边界或 persistent schema。标准 PointSet/Scene 批次尚未自动创建本 adapter，Scene 保存恢复也尚不重建它，完整核心对齐继续保持未完成。

批次挂接的协调提交入口已补齐：IProcgenSceneSink::replaceBatchesCoordinated 在树、索引、revision 和统计全部准备后调用一个已准备好的资源 owner。回调必须同步、不得访问 Scene/脚本或重入，失败必须不改变自身 owner；Scene 不持有回调。新建的候选 host 用代际句柄清理，owner 拒绝或抛异常后不留下可查询残影。所有批次与统计提交后才通知观察者；通知异常返回 Applied + warning diagnostics，避免调用方误将已提交状态视为失败而撤回 renderer。删除事务同步补上 owner 异常和重入保护。

机制选择：一个已知资源 owner 对一次显式批次事务，发布频率为生成/编辑提交，要求与 Scene 原子可观察，因此使用已有 common 接口的 checked 同步协调提交，不新增向上 include 或事件真源。公开成本包含 target instances 与整个已提交 Scene cache 的 staging；不是逐帧查询。没有新 ECS System、随机数/时间或 persistent schema。旧 provider 不支持新可选操作时明确 Unsupported，并且不调用 owner，已覆盖该路径。

正式 SHARED 的 Scene/procgen 构建及 11 项定向测试通过，包含外部失败、异常、候选 host 回滚、重复 revision、重入、deferred 阶段拒绝，以及通知看到全部 batch 已提交且异常不撤销结果。此前 19 项资产/道路/Scene 回归通过；在通知语义最终变更后的资产/GPU 回归与默认 make check 另记日志，不能将此前结果冒充最终验证。此入口是 renderer 批次组合的前置条件；标准 PointSet 尚未自动查询资产并创建/更新 StaticPrefabRenderInstance，保存后恢复与 Unity 设施对照仍未完成。

最终补充验证：候选 Tree 现在使用与 TransformSystem 相同的唯一 local TRS 投影函数，在提交前计算所有世界矩阵并拒绝非有限结果；发布后先完成目标同步，再发送生命周期通知。新增事件用例同时检查 revision 和新旧 batch 的 world 坐标，避免“新版本、单位矩阵”的中间状态。正式严格 SHARED 构建通过；最终 22 项组合回归通过（含真实 GPU），既有 Scene 层级/重建/父子变换/多批次事务等 10 项回归通过，两组包含重叠用例，不合并声称 32 个独立测试。默认 make check 最终仍只有 IStateProvider.h 三项 bool 与 RenderSystem.h 两项生命周期文档违规，无新增基线或豁免。相关日志为 coordinated-replace-final-build.log、coordinated-replace-composition-tests.log、coordinated-replace-transform-tests.log、coordinated-replace-make-check.log。此前 19 项记录是本轮中间状态，以本条最终结果为准；资产目录到自动 renderer 批次及保存恢复仍待实现。

版本资产到标准发布入口已接通：asset_graphics::ProcgenPrefabSceneSink 是显式组合安装的 IProcgenSceneSink decorator。宿主先完成 evpack prefab 加载并 admit，再以它替换 capability；标准 Procgen::publishInstances、完整快照、共享增量及协调删除即可消费资产/build 强绑定，创建实际 ECS renderer。Scene 仍拥有作者元数据，deco 缓存只是带 revision 守卫的拥有型投影；外部 Scene writer 导致不一致时明确 Conflict。纯标签保持 metadata anchor，不从无版本文本猜测或隐式加载资源。

每次提交先生成整批隐藏候选，再由 Scene prepared-owner 协调入口切换可见性与预分配 map node；失败不覆盖旧批次。旧 renderer 及其 pin 进入不可见退役队列，宿主在 publication 之后显式 collectRetired；cleanup 失败保留资源以便重试，不能把已提交的 Scene 当作未提交。当前容量边界为 4096 admitted assets、64 active/retired transaction batches、65536 peak instances/renderer parts（含 active/retired/prepared），delta 每个集合也在 staging 前限制到 65536。Graphics、Scene provider、ECS table 与资源工厂均必须存活至 release 之后，capability 必须在销毁 decorator 前恢复；admitted GPU leases 属于同一 Graphics device/context。批次未建立 Scene renderer Link，直接编辑生成 Scene 节点并不会更新投影，仍应通过源 PointSet/recipe 重新发布。

共享 buildProcgenInstanceDeltaSnapshot 已移到 common/Foundation，Scene 与 renderer decorator 使用同一 revision、identity、exact target order 实现；不复制一份渲染专用 delta 规则。它只产生拥有的候选值，无 Scene/graphics/procgen 实现依赖、回调、全局服务或 RNG。重复 current identity、stale revision、错误 target order 被拒绝，输入保持不变。新的公开 API 明确标注成本：delta 当前仍完整重建受影响 batch 的 ECS renderer，Scene 业务 identity 的复用不等于 ECS entity 复用。

最终严格 SHARED 构建及 22 项定向组合测试通过。真实 FBX/Unity prefab import→EVA→cook→evpack→实际 GPU upload→标准 PointSet publish→renderer 链路已经运行：过期 build、第二个 batch 缺资产、外部 prepared-owner 失败后，原像素 mask 保持一致，revision 与退役数未变；成功 delta 将对象移出视野，退役清理后，再 deserialize 已保存的 RuntimeGeneration cell 为 PointSet 并作为新 Scene publication 发布，像素 mask 恢复一致。该用例验证 cache 内容恢复后重建投影，不代表整个 RuntimeGeneration/Scene revision 协调恢复或直接导入 Scene JSON 会自动恢复 renderer。另有 CPU 用例以非零 pitch/yaw/roll、反射与非均匀缩放，逐元素核对 renderer 矩阵与 Scene world * prefab local 一致。Native 批次帧为 build/road-parity/host-profile/test/out/static-prefab-batch-vulkan.png，来自引擎 readback，已目视检查。

日志：prefab-batch-configure.log、prefab-batch-build.log（初次测试编译报 API 名称错误，已修正）、prefab-batch-final-build.log、prefab-batch-tests.log、prefab-batch-composition-tests.log、prefab-batch-architecture.log，均在 build/road-parity。增量架构审阅 100 条通过，diff 空白检查通过；最终 default make check 另记 prefab-batch-make-check.log。适用 Result/nodiscard、唯一作者/派生投影、代际句柄、prepared commit、显式昂贵边界与六面契约；没有新增持久格式或架构豁免。

仍未完成：默认引擎启动/脚本 UI 自动建立该 native composition host、资产热重导入与跨 variant/device 上下文、Scene JSON 直接加载的 renderer 自动恢复、Scene Link 跟随、编辑器保存/撤销的完整 renderer 端到端案例，以及真实 EasyRoads 设施帧（含 prefab 根姿态）和非 forward 图验证。完整核心对齐保持未完成，不把此功能测试当成这些验收已通过。

最终补充：实际 road recipe 的 placements 已直接复制进标准 Procgen PointSet handle，经 installed decorator 发布，renderer part 数与绑定 side.left prefab 的设施点数量一致，总实例数与所有 recipe placements 一致；删除和清理也通过。该组合走真实作者 network/recipe 和资源身份链，CPU mesh 工厂仍为不透明 mock token；真实 GPU 的标准 PointSet/缓存恢复用例另行通过，两者不混称道路设施双端画面已对齐。最新 22 项组合回归全部通过，procgen-core-only 严格 OBJECT 的 EVProcgenCoreCheck 编译通过（L1 核心闭包，不代表 renderer OBJECT/L2 或该 profile 的渲染可用性）。

本轮最终 make check 不是绿色：固定开始检查时的 origin/dev SHA c98016fd5008523169750e3a96b8d6f139047138 再次检查，仍有 7 项：IStateProvider.h 三项 bool、RenderSystem.h 两项生命周期文档，加 sensing/Sensing.cpp:626 的 path-scoped interface 与 vehicle/Vehicle.cpp:254 的 runtime requireModInst。后两文件相对 HEAD 完全未改动；origin/dev reflog 显示 2026-10-15 12:12:14 +0800 从 66ea250e9 快进到 c98016fd5，故此前五项记录是历史检查状态。相对当前 HEAD 的增量架构审阅通过（100 条）；未改旧基线、allowlist 或源码以掩盖这些发现。完整日志 prefab-batch-final-make-check.log，检查基线记录 prefab-batch-check-base.txt。

新帧和证据记录为 prefab-batch-evidence.json，记录 native unit_test_asset 可执行文件、DLL 搜索目录 inventory、相关源码与最新测试日志哈希；它不冒充操作系统已加载模块清单或 Unity 对照证据。CPP native composition 安装/清理契约、同设备约束、资源峰值预算和 Source-cache 恢复范围见上文，完整核心目标仍未完成。

重导入/历史版本支持已扩展：PrefabBoundsCatalog 与 ProcgenPrefabSceneSink 的目录键均改为 immutable (AssetRef id, package build id)，同一资产的多次 cook 可同时接纳；完全相同版本重复 admission 仍拒绝，未知 build 仍明确失败。没有“最新版本”隐式替换，作者绑定选择确切版本，旧快照可恢复旧几何。CPU 目录只复制 bounds/identity，依然不持有 GPU 或 prefab；读取已接纳历史版本不会被后续版本覆盖。

新增 StaticPrefabResourcePin 是固定分配的 owning token，不创建材质或 ECS。渲染目录为每个接纳版本持有一个 pin，独立于 active/retired renderer pin；因此即使某版本暂时没有可见实例，外部显式 release 也会被拒绝，直至目录释放。GPU 工厂必须存活到所有 token 与 prefab 之后，操作在 graphics owner 线程，无脚本、锁或重入。容量 4096 的计数现在是 admitted asset/build versions，而不是仅不同资产数；历史 GPU 资源保留成本明确，不引入无界历史。CPU bounds 可在 GPU release 后继续使用。

真实 GPU 回归修改同一 GUID 的 source prefab：纹理由红改绿，根 X scale 从 1 改为 2，重新 import/cook/load 后 AssetRef 不变、build id 改变。接纳新版本不影响现有红色画面；发布新 build 后出现加宽绿色网格；重新发布已保存旧 build 后红色 mask 精确恢复。两个版本的 CPU bounds 同时可查且 minX 不同；旧 renderer 退役清理后，admission pin 仍阻止资源提前释放；全部目录释放后 GPU release 成功，CPU bounds 仍可查询。输出 build/road-parity/host-profile/test/out/static-prefab-reimport-vulkan.png 已通过引擎 readback 生成并目视检查。该用例是 native 显式重导入与历史绑定恢复，不是文件 watcher、编辑器 undo 命令或 Unity 设施双端外观验收。

最终严格 SHARED 构建和 22 项组合回归通过；新 pin 的 null/released 参数拒绝也通过。证据日志 prefab-versions-build.log、prefab-versions-final-build.log、prefab-versions-tests.log、prefab-versions-composition-tests.log；default/source gate 另记 prefab-versions-make-check.log。遵循 Result、唯一资源分配所有者/拥有型 pin、原子 admission、精确版本 link 和显式成本规则，没有新增 persistent schema 或架构豁免。原默认启动/脚本 UI composition、Scene JSON 自动 renderer 重建、Scene Link 与跨 variant/device、完整 Undo 工作流及 EasyRoads 根姿态/设施对照仍待完成。

本轮最终门禁结果：固定 origin/dev SHA c98016fd5008523169750e3a96b8d6f139047138 的 make check 仍有上一轮记录的 7 项（IStateProvider.h 3、RenderSystem.h 2、sensing 1、vehicle 1），相对 HEAD 的增量架构审阅通过，未增加 baseline/allowlist 或架构豁免。最终 22 项组合测试通过，git diff --check 通过。版本实帧与运行时/源码/测试日志哈希记录在 build/road-parity/prefab-versions-evidence.json；DLL 搜索目录 inventory 不冒充 OS 已加载模块列表。目录保留多版本 GPU 资源以支持历史绑定恢复，自动历史淘汰、watcher、编辑器事务和默认宿主仍未完成，完整目标继续保持未完成。

## 2026-10-15 原生设施根姿态契约

共同协议 `eve::PrefabRootPose` 的稳定值为 Authored=0、ReplaceTranslationRotation=1。新建 RoadSideAssetBinding 默认选择后者；通用 ProcgenInstanceAssetBinding 与通用 prefab API 保留 Authored 默认。道路快照/预设的 sideTerrain.assetRootPose 使用整数枚举；缺失字段明确迁移为 Authored，非法值拒绝恢复，不能悄悄改变旧世界。PointSet 的 asset_root_pose 使用 authored/placement，仅与 canonical asset/build 身份一起有效；快照、cell 内容和 undo/redo 均沿用已有权威数据流程。Scene 投影将模式写入 pcg.asset-root-pose 标签；直接 Scene JSON 到 renderer 的自动恢复仍属现有未完成范围。

加载 prefab 时同时遍历 authored 与 placement 两套不可变派生矩阵，后者仅以 root signed scale 起始，子节点始终消费原局部矩阵。无需求逆，避免零 scale 和反射的逆矩阵问题。两套 AABB 从相同 mesh bounds 和对应矩阵聚合；PrefabBoundsCatalog 每个 asset/build 保存两套 CPU 值，不新增 GPU owner。道路 bake 的临时查询缓存包含模式，渲染 preparation 消费相同模式，因此 footprint 和画面不会分别解释根节点。目录仍按 asset/build 钉住同一份资源，两种模式不复制 mesh/image。生产者算法修订升级为 road-core-2026-10-15-prefab-root-pose-v11，使旧 baked 输出不会被误认成新算法缓存。

本轮变更适用 Result/nodiscard、唯一作者 owner、强类型跨模块协议、旧数据迁移、失败前不发布、成本/线程/lifetime 契约及六面目录要求；没有新增 ECS System、服务注册或模块边界，没有架构例外。验证结果另按实际构建/测试日志记录；EasyRoads 五组位置/旋转/缩放/子节点参考已非空，尚未完成设施外观双端画面对齐。
