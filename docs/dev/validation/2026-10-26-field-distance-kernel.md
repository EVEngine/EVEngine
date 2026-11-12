# 场求解性能复验与距离核优化

基线工作树 HEAD：5f2cd482ad1f88a7e11116000823a2b139c54a61，Windows Release。
本轮保持 seed=20261026、uniform、绝对解析误差 1e-3、leaf capacity=16、
原有独立源/目标数及资源预算，不改变已有接口或默认场策略。

## 保留的改变

归一化坐标的普通距离使用 sqrt(x*x+y*y+z*z)。平方和低于最小正规数或非有限时，
仍使用缩放的 std::hypot，避免极小的非零位移因平方下溢而被误判为重合。
没有启用 fast-math，没有修改直接对、远场列表、阶数或误差目标。
浮点舍入路径可能不同，仍按原有容差契约验证，不承诺 bitwise 一致。

1,000 点 p4，运行 7 次、去掉第一次后的场计算中位数：
原距离核 11.52635 ms，优化后 9.15965 ms。优化前后均为 981784 个直接对、
1968 个远场交互，CSV 输出精度下最大余项与 checksum 一致。
这是场核心小规模观测，不是整帧或跨机器的统计性能保证。
原始数据：build/field-fair-before-1000.csv、build/field-fast-norm-1000.csv。

百万独立源/目标、p4 的复验仍失败：阶段 A 在 4848.91 ms 后拒绝，原因是近场对预算超限。
未执行阶段 B，不能把拒绝时间当作完成一帧。日志 build/field-fast-norm-million.log。
当前瓶颈仍包含远近交互准入和整体算法/并行布局，距离核优化不解决最终 16.67 ms 目标。

## 已撤回的实验

按剩余源质量分配误差的尝试，使 1,000 点中位数变为 14.5384 ms，10,000 点 p6
仍在 1199.07 ms 被近场预算拒绝，已撤回。
先收集几何可接受交互、按目标的预估总误差分配额度再细化的尝试，中位数为 12.1921 ms，
10,000 点 p6 仍在 955.145 ms 拒绝，也已撤回。临时实验实现只留在 ignored build/ 中。

另试验了更紧的解析梯度余项。令 H_n(u)=|u|^n P_n(cos(theta))，
Legendre 绝对值界和三角多项式 Bernstein 导数界分别给出径向、角向导数各不超过
n|u|^(n-1)，所以使用保守和界 2n|u|^(n-1)。求和 n>=p+1 得
2q^p[(p+1)/(1-q)+q/(1-q)^2]。这是推导出的解析界，不包含实现舍入误差。
依据：[NIST DLMF 18.14.1](https://dlmf.nist.gov/18.14.E1)，以及
[Queffélec 与 Zarouf 的 Bernstein 不等式论文](https://arxiv.org/abs/1903.10801)。
该界单独替换后中位数 11.599 ms，10,000 点 p6 在 264.887 ms 拒绝。
当前没有总成本收益证据，因此同样撤回，生产代码保留原有平方系数余项界。
原始数据保留为 build/field-fair-after-*.csv/log、field-gradient-*.csv/log、field-planned-*.csv/log。

## 验证与规则

Windows 数值 DLL 契约：40 用例、24237 断言通过；WSL GCC 静态数值契约也通过。
新增回归使用二次幂精确表示的极小间距，覆盖平方和成为 subnormal 和归零两种情况；
验证加速度非奇异且符合解析值。既有直接求和、误差账本、事务、坐标与递归用例仍通过。
日志 build/field-norm-contract-build.log / build/field-norm-contract-tests.log。

本次仅改变内部热点实现，没有公共 API、持久格式、ECS 或模块边界变更，没有架构例外。
真实 Windows SHARED 引擎构建退出 0，随后 physics.field/fieldCoupling/forces 定向回归
46/46 用例通过，包含点场适配和旧施力接口；日志 build/field-norm-engine-build.log /
build/field-norm-engine-tests.log。完整源码门禁 build/field-norm-source-check.log 已通过，
529 项脚本测试通过，5 项既有跳过；退出码 0。
