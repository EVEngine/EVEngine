# 场核的笛卡尔系数递推

状态：内部核递推已实现，默认策略与公开接口不变；百万整帧验收仍未通过。

## 数学与实现

令 F(u)=-1/|d+u|，r=|d|，c_alpha 为其普通 Taylor 单项式系数。
恒等式 `(r² + 2 d·u + |u|²) ∂ᵢF = -(dᵢ+uᵢ) F` 给出递推。
对非零多重指标 alpha，选择任一 alpha_i=a>0，并把负指标系数视为零：

```text
c_0 = -1/r
r² a c_alpha = -[(2a-1) d_i c_(alpha-e_i)
                 + sum_(j!=i) 2a d_j c_(alpha-e_j)
                 + (a-1) c_(alpha-2e_i)
                 + sum_(j!=i) a c_(alpha-2e_j)]
```

每项只依赖较低总次数的系数；Basis 按总次数递增，提前缓存最多六个依赖、权重和 1/a。
运行时只需一个结果数组，每个系数计算一次。原实现反复构造截断二项式幂，核成本为
O(p C(p))；新递推为 O(C(p))，C(p)=(p+1)(p+2)(p+3)/6。
M2L 平移求和、节点构建和误差账本仍保留各自成本，不能把这一改进说成整个求解器已达标。

生产支持仍为 2–6 阶；默认 p4、误差、opening ratio、源/目标集合及工作预算均未修改。
递推与原卷积在实数运算下生成相同 Taylor 系数，但浮点求值顺序不同，不承诺逐位一致。
公开 truncationErrorBound 仍只覆盖解析截断，不包含 roundoff；没有声称认证总误差。

本轮参考 [Dehnen 的 FMM 研究](https://arxiv.org/html/1405.2255v1) 中对展开阶数、误差准入和
M2L 成本的联合讨论。论文的经验误差准则没有替换本项目的逐目标累计上界；上述递推直接来自
势函数微分恒等式，不依赖把经验误差当作保守保证。

## 数值与引擎验证

场模块测试中的关键断言统一为 REQUIRE / REQUIRE_EQ，确保数值失败使测试失败。
新增所有 2–6 阶、坐标循环排列、正负方向、物理尺度 2^-40/1/2^40 的直接求和对照。
每次要求真正出现远场交互、系数数量匹配阶数、累计余项不超预算，并显式附加独立 roundoff allowance。

Windows DLL 数值契约 47 用例、36,276 断言通过；WSL GCC 静态数值契约也通过。
日志 build/field-recurrence-contract-build-final.log、field-recurrence-contract-tests-final.log、
field-recurrence-linux-contract.log。真实 Windows SHARED 引擎构建通过，随后
physics.field / fieldCoupling / forces 聚焦回归 47/47 通过，覆盖新求解器与旧施力接口；
日志 field-recurrence-engine-build.log / field-recurrence-engine-tests.log。

1,000 均匀点、同一 Release 基准、p4/leaf16、每次 7 次运行去掉首次：
阶段 A 中位数 3.06715 → 2.726 ms；阶段 B 6.1575 → 6.1729 ms；总计 9.1861 → 8.91065 ms。
两者均为 981784 个直接对、1968 个远场交互，CSV 输出精度下最大余项和 checksum 一致。
日志 field-recurrence-before-1000.csv / field-recurrence-after-1000.csv。
这不是交错统计实验，也不是整帧成本；小幅总时间差不应扩展成跨机器性能保证。

生产 p4 的百万复验仍被近场预算拒绝：阶段 A 4393.72 ms，未进入阶段 B。
日志 field-recurrence-million.log。拒绝时间不是完成一帧的耗时。

## 未进入生产的高阶组合实验

build/field-order-probe 中隔离扩展到 12 阶，保留原始 1e-3 误差、N*1024 直接对和 N*512 遍历预算。
一万均匀点、leaf16 的结果：

- 原累计预算：p8/p10 拒绝；p12 完成，约 32.9999 秒、6691307 个直接对。
- 加源质量配额：p8/p10 拒绝；p12 完成，约 26.5760 秒、8143944 个直接对。
- 加既有推导的较紧梯度余项：p6/p8 拒绝；p10 完成，约 12.1325 秒、8210876 个直接对。
- 该 p10 组合再用未缓存依赖的递推核：约 9.9977 秒，相同直接对和打印精度下 checksum。

这些探针不构成高阶 API、精度认证或生产性能交付；大阶数的平移求和成本仍很高。
文件为 build/field-order-probe-10000-p*.log、field-order-mass-10000-p*.log、
field-order-gradient-mass-10000-p*.log、field-recurrence-probe-10000-p10.log。
百万 p10 质量配额组合探针最终因近场预算被拒绝，阶段 A 约 1.84712e6 ms（约 30.8 分钟），
未进入阶段 B；日志 build/field-order-gradient-mass-recurrence-million.log，退出码 1。
与其他工作重叠的拒绝耗时不是完成时间或受控性能统计，不得把一万点完成冒充百万通过。

后续在独立 contract-out 构建目录中，把坐标排列/符号/物理尺度对照循环扩展为 2–12 阶，
验证同一高阶、梯度余项、质量配额、未缓存递推组合。该对照用例通过，但完整契约为
46 用例通过、1 用例失败（48220 个断言通过、1 个失败）。失败为
sharedTargetBudgetBoundsMixedDepthInteractions 的 farInteractions>0：质量配额使该输入不再
产生远场交互，无法满足原用例对混合深度远场预算路径的覆盖要求。没有删除或放宽断言。
日志 build/field-order-probe-contract-configure.log、field-order-probe-contract-build.log、
field-order-probe-contract-tests.log。此结果不构成生产契约通过，也不是总浮点误差认证。

为隔离上述失败，再建立 greedy-contract-out：只移除质量配额条件，保留高阶、较紧梯度余项、
未缓存递推及原累计误差预算。47 用例、50532 个断言全部通过（含 2–12 阶对照）；
日志 build/field-order-greedy-contract-configure.log、field-order-greedy-contract-build.log、
field-order-greedy-contract-tests.log，断言计数见该构建目录 Testing/Temporary/LastTest.log。
一万均匀点 p10/leaf16/1e-3 完成，1019303 个远场交互、5050696 个直接对、
1472805 个遍历对，打印最大截断界 0.001；阶段 A 8524.7333 ms、阶段 B 70.1621 ms。
日志 build/field-order-gradient-greedy-10000-p10.log。运行与其他验证重叠，不作为受控性能比较；
更少直接对和成功准入是该固定输入的证据，不代表任意分布保证或总浮点误差认证。
十万点同参数准入实验最终因交互遍历预算被拒绝，阶段 A 309640 ms，退出码 1；
日志 field-order-gradient-greedy-100000-p10.log。该失败是遍历预算，不是近场对数预算。

本轮没有公开 API、持久格式、ECS 或跨模块生命周期变化，没有架构规则例外。
源码门禁 `make check` 已通过（base=origin/dev）：532 项脚本测试，5 项既有跳过；
日志为 build/field-recurrence-source-check.log。该结果是本地源码门禁，不代表远端 CI 已运行。
后续仍需改善误差准入、平移核与并行布局，不能通过放宽误差或删除源来达标。

## M2L 按目标系数分组

后续内部布局改动将 M2L 项按目标局部系数分组，每个目标系数在标量中累加后只写回一次。
每组内保持原有源系数顺序，奇偶符号在 Basis 构建时计入二项式系数。
计数、前缀和、稳定分发共 O(T+C) 次操作，额外保存 T 个源/核索引与带符号系数、C+1 个偏移；
T 为原有平移项数，C 为展开系数数。M2M/L2L 仍使用原表，不改树或准入策略。
这减少热循环的输出访存及奇偶计算，不改变 M2L 的 O(T) 算术复杂度。

接口成本注释同步校正了原有遗漏：总次数截断的 Cartesian 平移有
T(p)=binomial(p+6,6) 项，而非只有 C(p)=binomial(p+3,3) 项。
例如 p6 是 84 个系数、924 个平移项，p12 是 455 个系数、18564 个平移项。
构建还计入 Basis 的 C(p)^2 枚举、叶节点/位移单项式的 p*C(p) 运算；
评估计入节点扫描和逐目标单项式计算。固定阶数、深度、节点和交互预算时仍可约束为
关于 N 的线性工作，但阶数变化不能隐藏在同一个小常数里。此修正只改变成本说明。

Windows DLL 与 GCC 静态数值契约通过；实际 Windows SHARED 引擎构建及物理聚焦回归通过。
日志 field-local-gather-build.log / field-local-gather-tests.log、field-local-gather-linux.log、
field-local-gather-engine-build.log / field-local-gather-engine-tests.log 均位于 build/。
1,000 点 p6、leaf16、seed20261026 的前后 Release 输出均为 5610 次远场交互、945163 个直接对，
打印精度下上界 0.000999999953808、checksum -936.925810955 一致。
日志 field-local-gather-before.csv / field-local-gather-after.csv。
这些运行与高阶探针及其他验证重叠，只作结果对照，不据此宣称性能提升。
该布局改动后的独立源码门禁通过，532 项测试、5 项既有跳过，日志 field-local-gather-source-check.log。
成本说明的额外架构检查也通过，日志 field-cost-contract-check.log。

## 单项式前缀复用

后续实现把 P2M、M2M、L2L、L2P 所需单项式改为每个位移一次递推。
对非零多重指标选择最后一个非零轴，用已计算的低一阶单项式乘该轴坐标；
因此维持原先先 x 后 y 后 z 的乘法顺序。梯度直接索引同一表中的低一阶项。
计算 C 个单项式只需 C-1 次乘法，消除原先每项至多 p 次乘法的重复工作；
使用固定大小栈数组，不增加逐粒子堆分配。公开成本说明相应移除单项式部分的 p 因子，
保留平移项 T、节点扫描、树深度和近场对数，不能据此声称整个算法常数已满足实时预算。

Windows DLL 数值契约和 GCC 静态数值契约通过，日志 build/field-monomials-build.log、
field-monomials-tests.log、field-monomials-linux.log。Release 千点 p6 前后输出对照保存在
field-monomials-before.csv / field-monomials-after.csv；与其他运行重叠，不作为受控性能统计。
实际 Windows SHARED 引擎构建与 47/47 项物理聚焦回归通过，日志 field-monomials-engine-build.log、
field-monomials-engine-tests.log。本轮源码门禁通过：532 项测试、5 项既有跳过，
日志 field-monomials-source-check.log。
千点对照的交互计数、打印精度下上界及 checksum 一致。没有公开接口、默认行为或误差策略变化。

## 高阶实际分布的抽样参考

隔离的 field_accuracy_probe 使用上文高阶累计预算组合，在场计时结束后选取最多 64 个
等间距输入索引，对每个目标遍历所有源，使用 std::hypot 和补偿求和建立加速度参考。
逐样本要求实测误差不超过自身 truncationErrorBound 加 1e-10*max(1,参考加速度模长)；
超出或非有限值返回非零。此参考容差与数值契约一致，不是严格的总浮点误差证明。
参考求和不进入场阶段 A/B 计时；不能把抽样覆盖等同于全体目标认证。

一万均匀点、seed20261026、p10、leaf16、误差目标 1e-3 的 64 个样本通过，
最大实测误差 4.1899717383622062e-7，样本最大截断界 0.00099999999989847316，
无样本超过其独立允许值。日志 build/field-order-accuracy-build.log、field-order-accuracy-10000.log。
团簇与薄片同参数均被近场预算拒绝（exit 1），分别在阶段 A 580.368 ms、3776.41 ms，
未得到阶段 B 或抽样参考结果；拒绝耗时不是完成耗时。日志
field-order-accuracy-10000-clusters.log / field-order-accuracy-10000-sheet.log。
这直接说明均匀样本的成功不能推广至其他分布。生产阶数和误差策略没有随此实验改动。
