# GPU 八簇质量与一阶矩场验证

日期：2026-10-30。独立 Vulkan compute 基准现在把源位置、质量和目标状态
常驻设备。每帧三个 dispatch 分别对 N 个源按坐标符号归入八簇并计算
质量/一阶矩、归约工作组部分结果、对 N 个目标读取八个质心作求值与
半隐式 Euler 积分。八簇是固定容量，所以工作量 O(N)，但它尚不能表达
任意密度和任意近场分布。旧引擎模块和默认调用路径均未改动。

源被生成在八个角点附近，目标在 x≈10 的远处。GPU 使用源簇的
`(Σmx,Σmy,Σmz,Σm)` 得出质心；CPU 逐源验证实际最大质心半径
0.0131912，小于 GPU 使用的保守半径 0.04。对点引力核，位置导数的
算子范数不超过 `2/r³`，故每簇加速度误差用
`2 M a/(d-a)³` 上界；八簇求和并以 1e-3 为准入阈值。
目标靠近源或上界超限时 GPU 设置拒绝状态，基准退出 1；近场用例
`build/gpu-field-probe-clusters-near-final.log` 验证了这一分支。
这个隔离基准拒绝后退出进程，尚未实现引擎所需的候选状态事务提交。

Release MSVC、AMD Radeon AI PRO R9700、1080p 之外的纯 compute、
固定 dt=1/60。预热 4 帧后，百万源与百万目标测 120 帧：
CPU 录制/提交/等待完成 P50 0.2929 ms、P95 0.4504 ms、
最大 0.8504 ms。CPU 缓存中的源数组用于事后参考检查，未计入帧耗时；
GPU 输入/目标缓冲跨帧常驻。不同运行的 P95 曾为 0.1994、0.4905 ms，
说明当前 wall 观测有系统波动，不能由单次样本推断稳定上界。

八簇 GPU 一阶矩相对 CPU 归约的最大绝对差 5.12322e-9；
16 个目标的 CPU 全源直接求和与八簇近似最大加速度差 1.10465e-8，
对应保守上界最大 8.14938e-5；120 帧后 16 个目标的 GPU/CPU
近似轨迹最大位置差 1.77168e-6。二进制浮点舍入未纳入上述解析上界，
轨迹用独立的 1e-4 门限。百万输入在 `VK_LAYER_KHRONOS_validation`
开启时退出 0，日志没有验证错误。

证据：`build/gpu-field-probe-clusters-build.log`、
`build/gpu-field-probe-clusters-1m-final.log`、
`build/gpu-field-probe-clusters-validation-final.log`、
`build/gpu-field-probe-clusters-near-final.log`。
边界输入 N=1、257 的正例均通过，日志
`build/gpu-field-probe-clusters-{1,257}.log`。
完整 `make check` 退出 0（533 项脚本测试、5 项既有平台跳过），
日志 `build/gpu-field-clusters-make-check.log`。

该原型仍需：动态多层空间树、近场直接/局部展开工作队列、跨尺度坐标、
源/目标修订戳、候选状态原子提交、设备内存与上传策略，以及渲染/生成
组合测量。不能以纯 compute 的亚毫秒成绩宣称百万对象整帧 16.67 ms
已通过，也不能把八簇近似应用到未验证的任意源分布。
