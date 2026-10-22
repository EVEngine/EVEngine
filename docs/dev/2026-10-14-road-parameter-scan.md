# 参数扫描采样与统一帧入口

2026-10-14。整体核心对齐目标仍未完成。

后续默认对齐：78个真实API探针现在在任何resolution/angle/subSegments setter之前记录创建读回，均为2m、45°、1段；先前 .375m/4°是校准显式值。新RoadStyle默认改为2/45/1与ParametricDistanceTurn，标准作者/recipe/artifact从同一拥有的style消费。旧roadNetwork v1缺失字段仍解码成0/4/1与MaximumIntervals，既有显式字段不变；历史网络不被静默改为新密度。固定pathSegments只在style spacing为0时生效，测试验证新默认输出不随旧固定段数选项4/400改变。

roadPresets升级到schemaVersion3：接受v1/v2/v3；v1仍迁移根roll为Linear，v1/v2每个历史帧的根类型缺失采样字段都补为0/4/MaximumIntervals。子类型保持缺失继承，显式0和显式策略保留。恢复私有验证全部历史后提交，undo/redo与再保存v3保持原行为；未知版本和非法字段仍拒绝。新生产者缓存修订为road-core-2026-10-14-sampling-defaults-v5。

新增sampling-created-default实际场景：参考适配器不调用采样setter，并检查CreateRoad真实读回与原生style相同；透视/俯视 .994634493/.995802030通过固定 .98。原生54三角形、参考52，说明默认值对齐并不证明端部离散过滤完成。既有34个校准场景显式保持旧密度，生成器更新后输入哈希逐一未变化。参数扫描具有方向相关的保留点顺序，不能承诺反向后每个离散顶点都相同；底层曲线反向合同仍保留，旧固定段数的精确顶点集合合同由显式legacy模式验证。

最终默认迁移验证：STATIC作者63 cases / 7028 assertions、实际artifact/store/全部LOD 1 / 156、Spline16 / 372通过，SHARED作者与Spline同一合同通过；断言计数随新默认较少的派生顶点改变，不把数目下降当作旧场景未跑。正式EVProcgenCoreCheck严格编译通过，14项证据检查器、ruff、相对HEAD架构与差异格式检查通过。35场景/70对真实图像为33 passed、2 different、0 blocked。默认make check仍仅在未修改IStateProvider.h三项bool操作API与RenderSystem.h两处指针生命周期文档失败；未改基线或新增豁免。適用规范为唯一作者、版本与全历史迁移、Result及恢复原子性、继承presence、预算与公开成本、六面/核心裁剪，无例外。

随机状态隔离：探针现在逐例设置并记录 UnityEngine.Random seed，结束时通过 finally 恢复调用前状态。47 个真实案例包含相同 seed=12345 的两份相同输入和 seed=17 的对照；同种子两份 tValues 和 145 点 centerline 完全相同，换种子改变后段细采样及中心线。该结果将此前 Refresh 微小变化落实为可控制的参考随机状态影响；它不证明每种参考 controller 都只使用这一随机源，也不改变 native 当前无随机曲面模型的剩余差异。

两个独立 Unity 进程再次导出后，三份 seeded 案例的 centerline+tValues 指纹逐一一致；12345 指纹 c26393bedd05c6ecd2bc0788f199b2d45f60df640525c5e47d7c666a4963a911，17 指纹 c7100057bbadb767d2fa1ce18808f4882dfa03c6dd66b82c524fa3caac58aa47。这里的指纹仅用于参考探针重复性取证，不进入 native 生产缓存身份。

后续补充：实际 Unity 探针已扩展至 44 个，新增 0.4–2.2 倍几何长度与共线道路 .2/1/10m 间距。共线 10m 首段保留 u=.112500012、.7249996、1；第一处对应约 1m 距离，尽管平面路径没有转角。这表明当前初始方向与角度触发模型尚不能解释一般规则，不能把既有曲线序列吻合视为完整算法对齐。共线参考仍出现微小 Y 扰动，randomnessMarkerFlag=False、randomYPosition=0 不足以证明整条生成链路没有内部扰动。不同尺度的端部保留位置也不支持直接采用统一 u>.9 或 2m 过滤常数。本轮仅扩展参考取证，没有依据这些假设修改生产扫描器；细端部过滤、初始保留规则和新默认迁移继续列为未完成。

31 个实际 Unity API/data 探针覆盖 fine .2m、1m、10m、angle 0/1/4/20、不等长 marker、4/6/10m 道路宽度及重复 Refresh。公开 tValues 与步进 1/(3D marker 弦长*5) 的 float 累加一致；新增不等长组合提供了不同弦长证据。SetAngleThreshold(0) 的实际字段读回为 1。minNodeDistance 字段读回 5，但该字段名称不足以证明它就是扫描密度，不据此新增作者属性。宽度变化不改变已观察的保留 u；重复 Refresh 的细角度保留点有小幅变化，未将仅一个最终序列视为完整一般规则。

RoadSamplingPolicy::ParametricDistanceTurn 是 owning style 策略，采用上述参数网格，并按候选到上一个保留点的 3D 距离或保留弦线之间的夹角触发。初始保留方向在 XZ 平面，后续方向跨 marker 延续；marker 首尾强制保留。角度触发的 native 最小候选距离为 1m，这一模型符合当前实测序列，但 reference 细角度端部过滤尚未完整确认，native 没有加入未经证明的端部经验常数。扫描不注入参考坐标，也不复制 DLL 方法实现；所有位置由当前 native SplinePath 计算。

MaximumIntervals=0 保留上一阶段已有最大间距/累计帧转角策略；ParametricDistanceTurn=1。roadNetwork v1 和 preset v2 的可选 samplingPolicy 缺失为 0，旧最大间距快照不被静默切换。共享 type/road override 通过同一 codec 继承和保存；非法枚举或 scan angle<1 私有拒绝。参考 setter 将零值钳到 1，与 native 明确拒绝非法 scan 作者值的差异是输入结果契约，而非丢失该角度能力。当前新建 native style 的固定段数和默认策略没有切换；新默认与历史 preset 迁移仍在清单中。

SplinePath::sampleFramesAtParametersResult 接收严格递增的有限 [0,1] 参数，输出拥有的帧及真实弧长表相位。普通计数采样和显式参数采样都调用唯一 orientFrameSamplesResult，避免为 road 建第二套 transport、pitch/yaw、roll/pivot 或闭合 holonomy 代码。部分闭合区间不强制首尾相接；输入 span 仅在调用期间借用，结果不持有 path/参数指针。公开 API 声明同步 owner-thread、非重入、查询/传输成本及 4097 帧上限。

maximumSamplingEvaluations 是一次 network bake 内、跨所有 edge 共享的参数扫描 probe 预算；默认 65536，支持范围 [1,4194304]。它不替代已有网格/placement 输出预算或统一帧生产的输出上限。每个 artifact LOD 分别执行有界 network bake；不是整个 artifact 的合计 probe 预算。明确 samplingWorkBudget 的非法已知值不会替换为默认；过预算在局部产物发布前失败。测试在第一条 edge 已生成后让第二条 edge 触发共享预算，并验证作者快照不变。

20° coarse 原生中心点/14 triangles 对应实际参考保留位置，CPU 参考回归通过。该场景真实 EVEngine engine-owned readback 与 Unity perspective/top 为 .992489/.994959，通过固定 .98。五个采样场景两视图全部通过，当前 30 场景/60 对图像为 29 passed、1 different、0 blocked；镜像 Circular perspective .975594 未通过。驾驶面 unlit 轮廓通过不代表逐顶点/材质/地形/交通整体一致。细角度下 native 74 triangles、参考 66 triangles，细端部过滤仍待完成。

测试覆盖统一姿态与 roll pivot、实际弧长相位、部分闭合帧、非法/超量参数、scan 中心点、标准 recipe、实际 artifact/store/全部 LOD、策略 legacy migration、共享 preset 保存/undo/redo、共享 scan 预算失败原子性。适用架构：唯一作者所有权、Result/nodiscard、格式与未知字段契约、输出/工作成本、六面接口和 core 裁剪，无例外。没有新 ECS System、跨域 Link、provider、事件或 callback；既有脚本协议未因 native-only 帧 API 增加第二个作者入口。

最终验证：STATIC 道路作者 56 cases / 8800 assertions，实际 artifact/store/LOD 1 / 71，通用 Spline 16 / 372 全通过；SHARED 道路与 Spline 契约通过。正式 EVProcgenCoreCheck 严格警告编译通过（zeroerr 复合断言已用括号明确为布尔表达式）。最终 native generator 的 30 个 fixture 哈希与已捕获帧输入一致；相对 HEAD 架构检查、10 项证据检查器、ruff、git diff --check 通过。默认 make check 仍仅在未修改 IStateProvider.h 三项 bool API 和 RenderSystem.h 两项生命周期文档处失败，未更换基线或增加豁免。
