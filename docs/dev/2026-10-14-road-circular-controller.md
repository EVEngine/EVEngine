# Circular 出段控制

2026-10-14。整体核心对齐尚未完成。

road pathControl=3 与 generic SplineSpanControl::Circular 在 XZ 平面构造圆弧，Y 使用继承的高度插值。默认圆弧起始切向取前 marker 到当前 marker 的水平弦线。双精度圆弧系数由独立 pure-value setup 返回，采样保持首尾坐标精确；圆弧系数是派生产物，不保存为作者状态。近共线同向使用可观察的 line limit；XZ 重合、零方向、反向平行无有限圆弧及超出 float 表示能力的几何明确失败。

四点参考 [-24,0,0],[-8,2,8],[8,5,8],[24,1,0]、marker 1 Circular 的中点 XZ 为 [0,sqrt(320)-8]。已审计实际 EasyRoads API/mesh 生成显示该平面弧形，并显示后继 Spline 的水平起始方向受圆弧末端切向影响。native 采用解析末端切向；参考生成采样切向有轻微离散差异，不宣称逐顶点完全相等。

circularFromEnd 是 owning 作者方向参考，默认 false；反向时随出段重排并切换为端点的后 marker 方向，保存原几何而不依赖反向后另一个错误的前 marker。圆弧前/后 Spline 自动使用圆弧解析导数延伸的虚拟邻点，与本段弦线共同构造 Hermite 导数；高度保留圆弧的 Catmull 导数，不套用 Straight 的零高度导数规则。反向按起/末端对换约束。193 个全路参数采样比较反向曲线与原曲线逆向位置；双反向恢复完整作者快照。该状态可由预设/道路/marker 覆盖、JSON 保存、恢复和 undo/redo 携带。该邻接模型是当前 native 候选，尚未宣称等同参考全部转角和高差组合。

Native/editor 共用可选 circularFromEnd codec，缺失为 false；roadNetwork v1 / preset v2 的既有默认没有变化。非法导入构建私有候选；live setEdgeControlPoints 的无连接快捷分支也必须在发布前验证圆弧，不能仅在下一次 bake 失败。setNodePosition、merge、reverse 的完整候选验证包含圆弧约束。

段内拆分保留控制与方向参考，仍沿既有重建邻居模型；没有新增连续几何严格保持的拆分保证，撤销保留完整原记录。单 road closed loop 与显式 handles 等其余清单继续补齐。

正向 control-circular 与镜像 control-circular-mirror 通过真实 EasyRoads API 生成参考，候选由当前 native bake 并用 EVEngine engine-owned readback 读取。图像验收仍保持 .98 阈值，范围是 unlit 驾驶面轮廓；实际执行结果另列，不把 CPU 数学通过当作渲染通过。

当前实测：22 场景中 21 通过，1 different，0 blocked。正向 Circular perspective/top 为 .996500/.997424；镜像为 .974120/.988611，透视未通过。其余原有 20 场景保持通过。参考正向出口采样中心约 [16.1515,3.18336,3.94007]，镜像出口约 [16.0843,3.18937,-3.82137]；两次实际生成的出口形状不严格镜像。该事实不足以认定参考错误，也不足以为 native 添加按转向拟合的常数。需要补充非共线、不同转角与高差参考，确认出口约束和离散采样规则。Circular 的整体对齐保持未完成。

架构适用项：Result、唯一作者所有权、原子恢复、未知字段及可选字段迁移、六面接口、无向上依赖与分级裁剪，无例外。

验证记录：STATIC 集中道路回归 48 cases / 8564 assertions 与真实 artifact 消费 1 case / 70 assertions 通过；最终 SHARED 集中回归通过，正式 EVProcgenCoreCheck 严格警告构建通过。参考证据测试 10 项、ruff、git diff --check、相对 HEAD 架构检查通过。默认 make check 已执行，仍在未修改的 IStateProvider.h 三项 bool API 与 RenderSystem.h 两项生命周期文档处失败，未修改基线或添加豁免。

实际 API 数据探针扩展为 18 个 case，新增镜像与非共线出口；镜像出口的参考 horizontal direction 为 [.8735404,-.4867517]（Unity XZ），不同于正向出口方向的严格镜像。这进一步证明需审阅离散圆弧与邻段 ghost 点规则；不以 reference direction 直接回填 native 作者数据。探针读取公开字段和生成 mesh，未复制或反编译 DLL 方法实现。

新增 18-case 实际探针的非共线出口 control-angled-circular 在参考公开数据中 cpradius=0、cpangle=0，对应 mesh 的 Circular 内部没有正常圆弧采样。该输入只改变后续 marker 的 XZ，前一段方向与圆弧首尾 marker 未变。手册仅称按前 marker 夹角生成圆弧，因此当前证据不足以一般化出口兼容规则；不把零半径生成当作可移植的参考算法，不把 native 非共线行为标为已对齐。

2026-10-14 后续：真实 API 探针已扩展至78例，加入平地/上升/下降的正向与镜像、零幅度 SetRandomBumpiness、半倍/双倍几何。road/randomnessFlag 与 randomnessMarkerFlag 实际均为false，但显式将起伏幅度设为0仍改变部分出口高度方向；不能仅按开关判断参考没有采样扰动。默认参考场景保持原参数，新增独立 control-circular-zero-bump / control-circular-mirror-zero-bump，不用它们覆盖默认失败样本。

从零幅度场景的实际 paired mesh 位置和 tValues 反推末段 Hermite 起导数，再扣除当前弦线，得到虚拟邻点向量长度。半倍/原尺度/双倍、各自正向和镜像的六次测量为8.139413、8.139409、16.278910、16.278882、32.557504、32.557572；对应 marker 3D弦长8.139410、16.278821、32.557641，比例偏差不超过约6e-6。这证明邻接向量尺度取3D marker弦长，不取圆弧参数导数长度。

原生已统一通过 scaleCircularTangentToChord 将解析XZ导数和高度导数归一到拥有该圆弧的3D弦长，再与当前弦线按既有强度组合。公共 Result 入口预检不可表示的弦长，unchecked内部只消费已验证条件；公开纯值数学函数返回 owning Result/数组，不保留借用输入，任意线程无callback。正反向共享同一尺度规则，保留既有保存/撤销和193参数反向契约。生产者缓存修订同步更新为 road-core-2026-10-14-circular-ghost-v4。

方向仍为解析切向。参考镜像末端额外非常短的采样点与强制marker端点使离散方向不同，零起伏并未使镜像渲染通过；这一方向/采样关系未确认，不加入镜像专用经验偏移。新增校准输入 referenceZeroBumpiness 是可选布尔（缺失false、错误类型拒绝），仅属于参考测试协议，不是第二个原生作者状态或生产RNG实现。具名道路起伏模型及默认镜像Circular继续在剩余清单中。

最终实际对照34场景/68对图像：32 passed、2 different、0 blocked。正向默认/零起伏透视为 .998057/.998234；镜像默认/零起伏透视为 .970056/.972165（俯视 .988013/.988312），仍未通过固定 .98。两个失败样本同时保留，不将零起伏校准当作默认场景修复。其它驾驶面场景仍通过，范围仍为单面 forward unlit 轮廓。

最终STATIC作者60 cases / 8907 assertions、实际artifact/store/全部LOD 1 / 156、Spline16 / 372通过，SHARED同一作者与Spline合同通过。极端有限Y导致不可表示3D弦长时，实际setEdgeControlPoints原子拒绝并保持原快照；新数学函数直接导出和失败合同也通过。正式EVProcgenCoreCheck严格编译、14项证据检查器、ruff、相对HEAD架构与差异格式检查通过；96项契约目录覆盖新增纯值数学边界。默认make check仍仅在未修改IStateProvider.h三项bool操作API与RenderSystem.h两处指针生命周期文档失败，未换基线或增加豁免。适用规则为Result/nodiscard、唯一作者、原子失败、格式兼容、导出/线程/借用契约与六面/裁剪，无例外。
