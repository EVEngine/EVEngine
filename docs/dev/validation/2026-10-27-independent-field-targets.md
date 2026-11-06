# 独立源与目标批次

状态：新增纯数值入口及契约验证；百万整帧性能和完整系统验收仍未完成。

`GravityField::buildAtTargets` 显式接收 Ns 个正质量源、Nt 个目标及共同 tick/revision。
源必须非空，目标允许为空或重复。结果按目标输入顺序返回；输入全部复制，调用结束后
修改或释放调用方缓冲不影响快照。原 `build` 保留单树、每个源也是目标的路径，不自动切换。

源树与目标树在共同归一化范围内独立构造，目标不贡献质量。
阶段 A 包含两树构建、源多极矩、源到目标交互编译、目标局部展开传播和近场预算计数。
阶段 B 扫描目标叶，读取局部展开和已编译近场列表，不对每个目标重新走树。
`metrics().particles` 继续表示 Ns，新增 `targets` 表示 Nt；节点数和工作计数包含完整准备成本。

新入口的 maxParticles 是 Ns+Nt 的显式存储行预算；百万独立源和百万独立目标需要设为至少
二百万，不能把两批的内存隐藏在一个百万源计数里。空目标仍验证/复制源，但不编译交互。
固定阶数/深度、受控节点/交互/直接对预算下保持关于总行数的线性资源约束，
不保证任意输入都能接纳，也没有把这次功能扩展说成达到 16.67 ms。

`FieldTarget::excludedSource` 使用源的原输入索引，只有目标位置与该源精确一致时才有效。
任意远源屏蔽被拒绝，避免局部展开仍包含被屏蔽源。空间排序后的源秩映射用于近场计数，
排除对不消耗直接对预算；未排除的共点源/目标仍返回奇点失败。
由于重合点所在源/目标包围球不可能分离，不会通过远场接受路径偷偷保留自作用。

新增五个用例覆盖 2–6 阶的直接求和对照、真正远场交互、重复目标及输入顺序、
输入所有权、stamp、空目标、零直接对预算的自作用、空间重排后的精确对数、
无效排除关系、非有限值、奇点和组合行预算。
一个解析两体测试最初要求逐位相等，但其归一化含三分之一；现显式使用 16 个 binary64
epsilon 的舍入容差，不修改生产误差策略。一般场测试仍逐目标检查原解析界加独立舍入容差。

Windows DLL 数值契约通过：57 用例、36607 个断言。
日志 build/field-targets-contract-build-final.log / field-targets-contract-tests-final.log。
GCC 数值契约、真实 SHARED 引擎及旧施力/耦合聚焦回归通过，日志为
field-targets-linux-contract.log、field-targets-engine-build.log、field-targets-engine-tests.log。
该 API 扩展的源码门禁通过，532 项测试、5 项既有跳过，日志 field-targets-source-check.log。
字段扩展后已触发相关消费者重新编译，避免旧布局对象残留。
真实引擎聚焦回归为 52/52 项。原单批次 Release 千点 p6/leaf16、seed20261026 前后对照的
节点数、遍历对、远场交互、直接对、打印精度下最大界及 checksum 一致；日志
field-targets-legacy-before.csv / field-targets-legacy-after.csv。单次且与门禁并行的时间数据
不构成性能回归统计，更不能代表旧引擎所有场景的兼容证明。

## 可复跑的独立目标基准

benchmarks/physics_field 的可选末尾参数为 `[目标数量|-] [verify]`，保留原调用方式和原模式 CSV 列。
独立模式 scope=field_only_independent，N 仍是源数，追加 target_count/target_seed；
源种子保持 20261026，目标采用独立流 20261027。组合行数决定显式行/交互/直接对预算。
verify 在场计时后对最多 64 个等间距目标输入索引做全源补偿求和，单独记录 verification_ms。
加速度逐样本检查解析界加显式舍入容差，势误差只报告而没有新增解析势界。
任何参考误差失败返回 3；原构建/求值失败仍返回 1，非法参数仍返回 2。
零目标的验证明确报告零样本，不代表完成任何物理更新。

Windows Release 基准通过 7 组有效输入和 4 组非法 CLI 检查，涵盖均匀/团簇/薄片、
空目标、重复原模式和独立行数；结果见 build/field-target-benchmark-cli-check.json。
隔离编译器测试直接包含实际基准实现，验证解析正确样本通过，并注入超差、NaN、
超预算/负截断界和非有限势值，确认不会误报通过；日志 field-reference-check-tests.log。
GCC 构建、独立批次 verify 和数值契约通过，日志 field-target-benchmark-linux.log。
基准扩展后的完整源码门禁仍待终态，日志 field-target-benchmark-source-check.log。

单源、p6、leaf16、uniform、目标种子 20261027、单次 Release 运行：

- Nt=10000：阶段 A 8.6255 ms，阶段 B 2.4826 ms，总计 11.1081 ms。
- Nt=100000：阶段 A 92.3092 ms，阶段 B 35.6868 ms，总计 127.996 ms。
- Nt=1000000：阶段 A 868.13 ms，阶段 B 530.6309 ms，总计 1398.7609 ms。

三者均完成，直接对数分别等于 Nt，远场交互为零；64 个抽样目标均通过。
日志 build/field-target-benchmark-one-source-{10000,100000,1000000}.log。
这是与源码门禁重叠的单次测量，仅说明该输入完成及实际成本，不能证明渐近复杂度或稳定性能。
尤其 Ns=1，不能替代用户要求的 Ns=Nt=1000000，更不是整帧验收。

适用架构规则：Result/nodiscard、同步 owning batch、成本显式、线程/所有权说明、
单一快照所有者和默认关闭模块；无全局注册、逐帧字符串查询、ECS/Link 或持久格式变更。
没有架构例外。跨进程保存、完整 provider 裁剪矩阵、有限体积/力矩、总浮点误差认证和
百万对象实时性能仍需后续完成。
