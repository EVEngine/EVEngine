# GPU 常驻两阶段场：共点源验证

这是早期共点源版本的历史记录；当前同一路径的基准程序已扩展为
[八簇源验证](2026-10-30-gpu-field-clusters.md)，下方命令与输出仅对应
本记录所述的早期版本。

日期：2026-10-30。此验证在独立 `benchmarks/physics_field/gpu_probe/` 进行，
不接入引擎默认物理或渲染。N 个正质量源共点于原点，N 个目标位于远处；
该特殊分布中单个质量摘要是数学精确的。三个 Vulkan compute dispatch
分别完成 O(N) 源归约、O(N/256) 部分和归约、O(N) 目标求值与固定步长积分。
源和目标存储缓冲跨帧常驻，CPU 每帧只录制三个 dispatch 并等待 fence；
没有逐目标遍历全部源，也没有每帧全量上传或读回。

Windows MSVC Release 构建成功；AMD Radeon AI PRO R9700 上，每次独立进程、
预热 4 帧后各测 120 帧：

| Ns=Nt | CPU 等待完成的整段 compute P50 | P95 | 最大值 | 64 目标最大位置误差 |
| ---: | ---: | ---: | ---: | ---: |
| 100,000 | 0.1097 ms | 0.1828 ms | 0.4163 ms | 2.806e-6 |
| 500,000 | 0.1790 ms | 0.2439 ms | 0.5440 ms | 2.805e-6 |
| 1,000,000 | 0.1379 ms | 0.2739 ms | 0.4698 ms | 2.805e-6 |

本机系统波动使 1M P50 低于 500k，不能用这些 wall 数字证明严格线性伸缩；
线性工作量来自三个核的下标/循环界。质量误差低于 1e-4，抽样位置误差
低于 1e-4 的验证门限。开启 `VK_LAYER_KHRONOS_validation` 的 1k 用例
退出 0，未报告验证错误。日志：`build/gpu-field-probe-configure.log`、
`build/gpu-field-probe-build.log`、`build/gpu-field-probe-{100k,500k,1m}-solo.log`、
`build/gpu-field-probe-validation.log`。
额外验证 N=1、257 的工作组边界，并在开启 validation 时运行 1M、10 帧，
均退出 0；日志 `build/gpu-field-probe-{1,257,1m-validation}.log`。
完整 `make check` 退出 0（533 项脚本测试、5 项平台跳过），日志
`build/gpu-field-probe-make-check.log`；`git diff --check` 退出 0。

这不是一般分布的多极场：源位置在输入模型里全部共点，没有近场、树或
分层误差分配。时间也不含构建/初次上传、碰撞、生成、可见性、绘制和
设备恢复。下一步是把源记录扩展为带位置的设备缓冲，做多簇质量与
一阶矩归约，并以 CPU 直接求和/保守误差界决定哪些目标可由摘要处理；
近场及不能满足误差的目标必须进入显式预算队列或拒绝，而非静默截断。
