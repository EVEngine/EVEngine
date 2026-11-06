# GPU 场候选状态整批发布

日期：2026-10-30。在八簇独立 GPU 基准中，目标位置/速度改为两组设备缓冲，
两个预先绑定的 descriptor set 交替指向已发布输入和候选输出。
GPU 逐目标计算后只写候选；所有目标的误差准入状态通过单个原子标志汇总。
CPU 等待该帧 fence 并读这个标志，全部通过才切换下一帧的输入缓冲。
这是 O(N) 目标求值、O(1) CPU 准入和 O(N) 额外候选内存，
不再有拒绝时部分目标状态已发布的问题。

`mixed` 输入交错远、近目标。远目标写出不同的候选位置，近目标拒绝；
原已发布位置保持不变，日志分别报告
`published_state_unchanged=1 far_candidate_written=1` 与明确拒绝。
全近场也拒绝且已发布输入不变。该原型进程在拒绝后退出；资源所有权、
修订戳和引擎内的恢复/重试语义仍需后续集成。

AMD Radeon AI PRO R9700 上，Release 构建、百万源和百万远场目标、
预热 4 帧后 120 帧，设备端八簇归约、求值、积分、候选写入及 CPU
录制/提交/等待的 P50 为 0.278 ms，P95 为 0.6443 ms，最大 1.9701 ms。
数值与前一八簇版本一致：最大一阶矩差 5.12322e-9，16 目标直接求和
加速度差 1.10465e-8，解析界 8.14938e-5，轨迹差 1.77168e-6。
开启 `VK_LAYER_KHRONOS_validation` 的百万目标 10 帧测试退出 0。
这些数字不含首次上传、渲染、近场求值或完整模拟帧，不能用来验收 60 FPS。

证据：`build/gpu-field-probe-atomic-build.log`、
`build/gpu-field-probe-atomic-far-1k.log`、
`build/gpu-field-probe-atomic-mixed.log`、
`build/gpu-field-probe-atomic-near.log`、
`build/gpu-field-probe-atomic-1m.log`、
`build/gpu-field-probe-atomic-validation.log`。
边界 N=1、257 用例通过，日志
`build/gpu-field-probe-atomic-{1,257}.log`。
完整 `make check` 退出 0（533 项脚本测试、5 项平台跳过），日志
`build/gpu-field-probe-atomic-make-check.log`。
