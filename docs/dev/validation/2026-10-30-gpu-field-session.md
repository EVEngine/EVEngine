# 默认关闭的 GPU 场会话：接入与边界

新增 `physics_field_gpu` 作为 L5 可选模块，依赖 `physics_field`、`gpgpu`、
`graphics`，默认 OFF。它不注册引擎启动服务，不改现有 CPU GravityField、
GravitySimulation、Physics 或渲染默认路径。三个 GLSL shader 的权威源码在
`src/modules/physics/field/gpu/`；`scripts/compile_gpu_field_shaders.py`
再生编入模块的 SPIR-V 数组。

`GpuGravitySession` 独占两份源缓冲和两份目标缓冲。每个明确的
`SimulationStep` 调用 `prepareStep`，设备构场、求值并积分候选，
只回读一个拒绝字。通过后返回单次 `PreparedGpuGravityStep`；丢弃
候选不发布时间，`commit` 校验 owner、generation、base tick/revision 后
在 O(1) 切换已发布缓冲。`snapshot` 才执行整批回读。源和目标初值各
上传一次；步进不上传 N 份位置或速度。

## 已验证配置

- Windows MSVC Debug/SHARED，严格警告，显式打开模块：
  `EVENGINE_MODULE_PHYSICS_FIELD_GPU=ON`，203/203 模块配置，
  `unit_test_graphics` 完成编译和跨链接组链接。
- `ctest -R '^physics\.field\.gpu\.session'`：真实 Vulkan 窗口与设备的
  会话契约用例通过。涵盖初值快照、候选取消、提交后的 tick/revision、
  对 1024 源的一步 binary64 逐源参考、重复 tick 和源位移超限拒绝、
  近场整帧拒绝后状态不变。
- 重新配置默认 OFF：202/203 模块，`unit_test_graphics` 再次链接通过；
  会话测试从 CTest 清单消失。独立 GPGPU 场组合用例在该配置下仍通过。

`make check` 在修正 L5 模块声明段位置后通过：533 个脚本测试，
5 个跳过，source-quality gate `base=origin/dev` 通过；原始日志为
`build/gpu-gravity-session-check.log`（本地构建目录，不提交）。
`git diff --check` 也通过。当前 GPU 数学路径只有固定八个紧凑
符号簇、牛顿 G=1、半隐式 Euler、binary32、有界相反 x 位移。
初始源簇半径和后续位移受检；近场或力误差界超限明确拒绝。
解析界不包含浮点舍入。正式百万对象一般分布、近场处理、GPU
自适应层次、设备丢失恢复、源间相互作用、GPU 渲染直连及
1080p 整帧 16.67 ms 均尚未验收。

## 生产会话百万行计时

在 Windows MSVC Debug/SHARED、AMD Radeon AI PRO R9700 上，显式设置
`EVE_GPU_FIELD_BENCH_ROWS=1000000` 和
`EVE_GPU_FIELD_BENCH_FRAMES=120`，运行
`ctest --test-dir build/win32-debug -V -R '^physics\.field\.gpu\.sessionMillionRowTiming$'`。
测试在真实引擎 Vulkan provider 上创建 100 万紧凑八簇源及 100 万独立远场目标，
初值上传一次，连续提交 120 个固定 tick；每步记录同步
`prepareStep` 加 `commit` 的主机墙钟时间（包含 GPU 准入字回读与等待）。
初始化 322.149 ms；步进 P50 0.3061 ms、P95 0.3855 ms、
P99 0.6442 ms、最大 0.6655 ms。原始输出在本地
`build/gpu-gravity-timing-million.log`。这只证明该受限分布的会话步进，
未覆盖一般源分布、近场、生成、可见性、绘制或 1080p 整帧，
不能作为 16.67 ms 总目标的通过证据。

后续在 `evaluate.comp` 为加速度、积分速度和积分位置增加 NaN/Inf
拒绝字；有限初值但两秒步长导致位置溢出的 Vulkan 用例通过，已发布
tick 与目标速度保持不变。重编 shader 数组后的同配置 120 步百万对象
三次复测，P50 分别为 0.4146、0.3093、0.3073 ms，P95 分别为
0.4734、0.4702、0.3592 ms；原始日志是本地
`build/gpu-gravity-finite-million*.log`。这些独立进程的波动不能归因于
单一 shader 改动，也不能证明整帧预算。对应生产构建日志为
`build/gpu-gravity-finite-build.log`。
这一版本的完整 `make check` 退出 0，533 个脚本测试、5 个跳过，
日志为本地 `build/gpu-gravity-finite-check.log`。

GPGPU 缓冲与管线的现有析构器要求 Graphics Vulkan device 存活。
会话公开生命周期契约规定先销毁会话再销毁提供者；运行方法另使用
Graphics 的 weak lifetime 提前拒绝失效提供者。这不是跨提供者销毁
顺序完全独立的实现，后续设备恢复与热重载必须补齐。
