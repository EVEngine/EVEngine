# GPU 场探针：每帧变化的源分布

这次验证仍是独立 Vulkan 探针，不是 EVEngine 的正式 GPU 场后端，也不是
1080p 百万对象整帧 16.67 ms 验收。其作用是检查场方程随源变化时，
GPU 常驻源数据、两阶段场计算和候选状态发布能否共同工作。

## 改动

`benchmarks/physics_field/gpu_probe/reduce.comp` 在读取每个源后，按帧相位将
相邻源沿 x 轴反向移动 0.001，然后再生成八簇的质量和一阶矩。
源数组保持 GPU 常驻；每帧仅更新 24 字节 push constants。两个相位的
CPU binary64 参考矩量独立计算。直接逐源加速度、每帧积分轨迹、
最终矩量、簇半径及误差界均按当帧源位置校验。
这仍限定为八个紧凑簇和双状态运动，不能推导一般分布的误差或成本。

## Windows 本机实测

`cmake/with-msvc.cmd cmake --build build/gpu-field-probe -j 4` 成功。
AMD Radeon AI PRO R9700，Release，百万源和百万目标，120 个计时帧：

- 单独重跑时，每帧 GPU 命令录制、提交、fence 等待和状态回读的墙钟
  P50 0.3078 ms，P95 0.422 ms，最大 1.4015 ms。
- GPU 与 CPU 最终矩量最大分量差 7.60542e-9；16 个采样目标的
  直接逐源加速度最大误差 1.10455e-8，解析界 8.1489e-5；
  轨迹最大位置差 1.77608e-6。
- 混合远近目标按预期拒绝，`published_state_unchanged=1`，
  `far_candidate_written=1`。拒绝的进程退出码为 1。

计时不含初始化、最初上传、CPU 参考、渲染、碰撞、生成、设备丢失恢复或
一般层次重建。单次运行也不能作为稳定的性能上界。
正式方案仍需 GPU 自适应层次、近场、任意源运动和受检的误差预算。

最终相邻源反向运动版本的 `make check` 通过：533 项脚本测试、5 项跳过，
日志为 `build/gpu-field-moving-source-check.log`。Vulkan 验证层启用时的
一万源/目标两帧运行成功，未报告验证错误；近场与混合拒绝按预期退出。
