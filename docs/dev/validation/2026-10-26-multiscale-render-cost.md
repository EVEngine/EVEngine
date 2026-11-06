# 多尺度批量视图：静态与全量重建成本

本记录不代表百万独立状态整帧 16.67 ms 验收通过。
基线 5f2cd482ad1f88a7e11116000823a2b139c54a61，工作树未提交。

## 可复现入口与测量范围

扩展既有 benchmarks/render 可选目标，仅 graphics_multiscale 启用时编译 multiscale.cpp。
没有新增模块或默认启动项，既有 all workload 不变。启用
EVENGINE_BUILD_RENDER_BENCHMARK=ON 后，调用：

```
render_benchmark multiscale-static 60 1000000 1920 1080
render_benchmark multiscale-rebuild 8 1000000 1920 1080
```

本次为 Windows MSVC / Debug / SHARED 构建，真实交换链隐藏窗口、VSync 关闭、
EVENGINE_VULKAN_VALIDATION=0。这是当前调试构建定位瓶颈的数据，不能冒充 Release 发布性能。
确定性网格包含 N 份独立位置和外观，全部在视锥内，不用代理合并或删掉输入。
每帧重建模式移动全部位置，重新构造 CPU 快照、上传 GPU 快照并退役旧资源。
静态模式仅复用已经上传的快照。两者都先绘制四帧预热，冷准备另计。

CPU 循环墙钟包含当前调用线程的准备/提交/present/event pump；不等同于同一帧 GPU 完成延迟。
GPU 队列时间来自既有 IGpuTimer，按 frames-in-flight 延迟，样本数单独记录。
没有场求解、积分、碰撞、生成或交互工作。小球可能没有覆盖像素，不测试亚像素恒星通量。
128*N 字节只是记录与矩阵有效载荷，不含 CPU 镜像、尾部填充及峰值内存。

## 初次实测

- 10,000 静态实例，60 帧：CPU 循环 P50 0.5172 ms、P95 0.6341 ms；
  GPU 队列 P50 0.33464 ms。冷 CPU 准备 58.4115 ms、冷上传 1742.41 ms。
- 1,000,000 静态实例，60 帧：CPU 循环 P50 1.8949 ms、P95 2.9424 ms；
  GPU 队列 P50 1.77992 ms。冷 CPU 准备 5142.89 ms、冷上传 482.347 ms。
- 10,000 全量移动重建，8 帧：CPU 准备 P50 52.056 ms、上传/退役 P50 13.5297 ms、
  循环 P50 66.7022 ms，最大 82.5965 ms。
- 1,000,000 全量移动重建，8 帧：CPU 准备 P50 5415.74 ms、上传/退役 P50 426.714 ms、
  循环 P50 5869.52 ms，最大 5950.85 ms；GPU 队列 P50 1.86488 ms。

日志分别为 build/multiscale-render-static-10000.log、static-1000000.log、
rebuild-10000.log、rebuild-1000000.log（后面三者同样带 multiscale-render- 前缀）。
构建为 build/multiscale-render-benchmark-configure.log / -build.log，均成功。
8 帧不足以估计生产长尾；最近秩 P95/P99 在这个样本数下就是最大值。
初次管线缓存/驱动冷状态未控制，10k 冷上传高于百万不能据此推出规模关系。

补充版本在 JSON 中记录实际 framebuffer 和选中 GPU。10 万、50 万、百万的复测均报告
1920x1080、AMD Radeon AI PRO R9700，静态循环 P50 分别为 0.5423、1.1181、1.8586 ms；
CPU 冷准备分别为 519.797、2627.02、5174.77 ms，冷上传分别为 82.5806、213.242、452.781 ms。
GPU 队列 P50 分别为 0.48616、1.0374、1.72072 ms。每项 60 帧，独立进程依次运行，
日志为 build/multiscale-render-static-metadata-100000.log、-500000.log、-1000000.log。
这些点说明当前构建的成本趋势，不是任意输入线性上界证明。

新增基准已构建并实际执行；基准版本完整源码门禁已通过
（build/multiscale-render-benchmark-source-check.log，529 项测试、5 项既有跳过）。
这不等于后续优化版本或完整兼容验收已经通过。

## 结论与未完成项

批量提交降低了复用帧的提交成本，但全量变化时 CPU 准备和不可变资源重建仍远超预算。
必须继续优化准备及资源更新，不能把静态结果换名为整帧通过，也不能跳过移动对象来达标。
需要补 Release 配置、更多规模/分布/长尾样本、真实峰值内存及包含模拟的完整工作流。
新基准不改变公共 API、ECS 或持久格式；只使用现有 graphics 硬依赖和 GPU timing capability。

## 准备阶段重复工作消除

源码检查发现 SphereFrame 对每个球调用 projectSphere 时，重复验证同一相机的正交基底，
随后又调用 projectCellRelative 并重复计算相机空间中心。新增模块内部 PreparedAngularView
将基底/范数验证在一帧内复用；ProjectedSphere 同时返回保守边界、已计算中心与半径。
公共 projectSphere 仍是一次性受检入口，复用同一内部实现，没有新增更弱的公开捷径。

每个对象仍检查半径、指数、规范地址、投影误差与颜色，不缓存调用方可变指针；
区间向外舍入、范数膨胀、近面不确定保留和帧级原子失败保持原有算法。
改动不改变渐近复杂度，目的是减少同一帧的重复工作。
受影响的角投影数值用例统一改成 REQUIRE；Windows 严格警告 SHARED 构建通过
（build/multiscale-prepared-view-build.log），8/8 聚焦用例通过
（build/multiscale-prepared-view-tests.log），Vulkan 验证未观察到 Validation Error / VUID 错误。
架构门禁通过（multiscale-prepared-view-architecture.log）。
正常示例第 60/300 帧捕获为 prepared-near.png、prepared-far.png，与优化前 batch 图片
RGB 像素差均为零、均为 1920x1080；比较记录在 build/multiscale-captures/prepared-image-comparison.json。

等待上述完整源码门禁终止后再计时，避免与它并行。相同 Debug 构建/设备/1920x1080 的百万
静态 60 帧复测：冷 CPU 准备 4399.91 ms、冷上传 410.375 ms、循环 P50 1.8152 ms。
全量移动重建 8 帧：CPU 准备 P50 4217.39 ms、上传/退役 P50 416.538 ms、循环 P50
4658.63 ms、最大 4703.57 ms；GPU 队列 P50 1.81484 ms。
日志为 build/multiscale-prepared-static-1000000.log 与 multiscale-prepared-rebuild-1000000.log。
相较初次重建数据，CPU 准备和循环观测中位数分别降低约 22% 和 21%；这是两次运行的观察，
不是交错 A/B、Release 或长尾保证。全量重建仍约 4.66 秒，明确未达到 16.67 ms。
优化最终源码门禁已通过（build/multiscale-prepared-view-source-check.log，529 项测试、5 项既有跳过）；
没有用户批准的架构例外。

## 独立优化构建与运行

为区分 Debug 开销与实际运行成本，在 build/win32-multiscale-release 单独配置：
CMAKE_BUILD_TYPE=Release、EVENGINE_PROFILE=minimal、EVENGINE_MODULE_LINKAGE=OBJECT，
启用 graphics_multiscale/spatial_multiscale 和既有 render_benchmark，关闭 Demo、测试全集及样例资产下载。
没有改动 build/win32-debug 的配置或模块默认值。

Release 依赖仍是仓库锁定 v0.1.1，下载器校验 SHA256 为
003eb481cda2bc097d769fb7d896a1bf05d417ce6c8b34a8ae09b74a2e0dc8f8，
聚合 pin ea850e476dd506d8b15d9f15da2d07c7ebe1e4c0。
配置/构建日志为 build/multiscale-release-configure.log、multiscale-release-build.log。
此时构建尚未完成，不能宣称 Release 性能、OBJECT 链接或 L2/L4 裁剪验收通过。

首次配置的编译器切换触发 CMake 清缓存并回退 Debug/full，已停止该错误配置。
改用包装器绝对路径并核对缓存后，发现 Release flags 仍为空；因此显式设置 C/C++ 的
/O2 /Ob2 /DNDEBUG，C++ 另加 /fp:strict。compile_commands 已确认角投影 TU 具有
/O2、/DNDEBUG、/fp:strict、/W4、/WX；资源验证 TU 具有 SPIR-V provider 宏。
这避免仅凭 CMAKE_BUILD_TYPE 标签误称优化构建。修订配置/构建日志为
multiscale-release-optimized-configure.log / multiscale-release-optimized-build.log。

额外启用严格 Release 警告时，Filesystem、ModuleRegistry、Runtime 的断言专用局部变量
在关闭断言后触发 C4189/WX。最初尝试的两处 maybe_unused 注解已撤回，未改这些旧模块。
查验 Makefile 的 WIN32_CMAKE_ARGS：标准 Release 不带 DEBUG_STRICT_WARNINGS_FLAG，
该策略仅用于 Debug 配方。因此性能构建回到标准 Release 警告策略（显式 /W3），
保持 /O2 /Ob2 /DNDEBUG /fp:strict 和 OBJECT；严格 Debug 验证仍保留。
这项数据将标记为标准 Release，不宣称严格 Release 全仓门禁通过，也不进行无关跨模块清扫。
最终配置/构建记录为 multiscale-release-standard-configure.log / multiscale-release-standard-build.log，
该配置完成编译但最终链接失败：Graphics.cpp / DamageNumbersBindings.cpp 引用 Font 的
构造/析构、度量、字符解析和 FontData 符号，minimal 未包含 font。日志是实际 L2 失败证据，
不能仅凭依赖图或配置成功宣称 font-absent 可裁剪。没有修改旧字体或文字 API。
随后显式加入 EVENGINE_MODULE_FONT=ON，构建 minimal + font + multiscale 组合；
配置/构建日志为 multiscale-release-font-configure.log / multiscale-release-font-build.log，已退出 0。
最终启用 54/202 模块，eve 和 render_benchmark 均完成 OBJECT 链接。

运行仍为 AMD Radeon AI PRO R9700、实际 1920x1080、隐藏交换链、VSync/验证层关闭：

- 百万静态记录、60 帧：冷 CPU 准备 396.224 ms、冷上传 1835.41 ms；循环 P50
  1.7191 ms、P95 3.342 ms；GPU 队列 P50 1.6116 ms。
- 百万全量移动重建、16 帧：CPU 准备 P50 406.519 ms、上传/退役 P50 155.064 ms；
  循环 P50 577.403 ms、最大 837.477 ms；GPU 队列 P50 2.90756 ms。

日志是 build/multiscale-release-static-1000000.log、multiscale-release-rebuild-1000000.log。
这不是同配置的纯编译器 A/B：同时采用了裁剪组合和 OBJECT 链接；冷管线缓存状态未冻结。
标准优化构建仍远超 16.67 ms，而且数据仍只包含渲染阶段。

使用这份 Release eve 的标准 smoke 12 秒通过（multiscale-release-smoke.log）。
正常入口第 60/300 帧引擎捕获 release-near.png、release-far.png 与 prepared 对应图片
RGB 像素差均为零、均为 1920x1080；结果为 build/multiscale-captures/release-image-comparison.json。
捕获日志为 multiscale-release-capture-near.log / -far.log。该组合获得实际 L1/L2 及这一个
示例工作流的 L4 证据；不覆盖所有旧功能、provider-absent、其他后端或完整裁剪矩阵。

## 普通 binary64 的向外舍入路径

角投影保守区间的向外舍入现在通过模块内部 DirectedRounding.h 处理。
严格位于最小/最大正规数之间的有限值用相邻位模式得到下一个可表示数；
零、次正规数、正规数边界、无穷和 NaN 继续调用 std::nextafter。
没有更改区间公式、公开 API、默认模块开关或旧相机路径。
该实现要求 IEEE binary64，由静态断言约束；不使用 fast-math。

新增测试对一百万组确定性位模式及显式正负边界逐位比较两个方向的结果，
并比较边界向上舍入的浮点异常标志。严格 Debug SHARED 构建成功；
开启 Vulkan validation 的 graphics.multiscale 聚焦测试 9/9 通过。
证据为 build/multiscale-rounding-build-resume.log、multiscale-rounding-tests.log。

相同 Release OBJECT 配置重新构建后，百万全量移动、16 样本实测：
CPU 准备 P50 344.887 ms，上传/退役 P50 130.128 ms，
循环 P50 483.432 ms、最大 536.229 ms，GPU 队列 P50 2.38636 ms。
日志为 build/multiscale-rounding-release-build.log、
multiscale-rounding-release-rebuild-1000000.log。
与前次 406.519 ms CPU 准备相比约减少 15.2%，但这不是交错 A/B；
上传与 GPU 波动也存在，不能把全部循环差异归因于此优化。
短样本仍不能证明长期尾延迟，更未满足 16.67 ms 整帧验收。

同一 Release eve 正常入口第 60/300 帧截图 rounding-near.png、rounding-far.png
与 release 对应图片的 RGB 差为零，分辨率均为 1920x1080。
结果保存在 build/multiscale-captures/rounding-image-comparison.json，
捕获日志为 multiscale-rounding-capture-near.log / -far.log。
这验证两个示例状态的画面一致，不扩展为任意输入的总像素误差保证。

完整 make check（CI_BASE=origin/dev）退出 0，脚本测试 529 项、5 项既有平台跳过，
未新增跳过；日志为 build/multiscale-rounding-source-check.log。git diff --check 通过。
适用约束为新增模块内聚、公开契约不变、保守误差方向保持、源码架构门禁与聚焦验证；
本轮没有架构规则例外。此处不宣称完整跨平台 CI 或整套旧功能兼容矩阵已完成。
