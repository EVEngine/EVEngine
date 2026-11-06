# 多尺度球体：GPU 常驻记录到绘制图像

`ResidentShaderImageUpdate` 是 graphics 所有的显式 RGBA32F 资源更新入口。
`prepare` 只拥有绑定与尺寸，`apply` 即时借用同设备
`GpuResidentBufferView`，同步复制到既有、唯一拥有的 mesh shader 图像；
不保存 native handle，不重建 shader、pipeline、矩阵或描述符，不经过 CPU
整批回读或 staging 拷贝。尺寸、缓冲范围、后端、共享/去重身份和活动 pass
在提交前检查；设备提交后的错误仍需主机恢复。WebGPU 明确返回 Unsupported。

`SphereBatch::updateResidentPackets` 在初次 `prepare` 的容量内复用该操作，
成功后才更新 draw count。此接口要求上游已完成 GPU 写入及包内容准入；
它只验证缓冲范围和资源形状，不宣称验证每个球的几何/误差，也不替代
`SphereFrame::build` 的 CPU 参考路径。旧渲染和脚本默认行为保持不变。
源 buffer 必须存活到同步 `apply` 返回；Graphics 持有图像和 shader。

四轴机制是一个已知的调用者对一个已知批次的高频定向调用，使用 typed
接口。六面仅扩展 C++ graphics 资源接口；不新增 ECS/Link、事件、脚本、
MCP 或注册表。`graphics_multiscale` 只依赖 common 的借用视图类型，
GPGPU 组合测试只在 GPGPU 存在的配置下编译。没有持久格式。

严格 Windows MSVC Debug/SHARED 图形测试目标编译链接通过，日志为本地
`build/resident-sphere-separated-build.log` 与
`build/resident-sphere-timing-build.log`。真实 Vulkan 用例
`graphics.multiscale.sphereBatchAcceptsResidentPackets` 验证蓝到红的像素变化、
短缓冲、超容量和活动 pass 拒绝；原有 CPU 更新用例同时通过。

显式设置 `EVE_RESIDENT_SPHERE_BENCH=1` 并运行
`ctest --test-dir build/win32-debug -V -R '^graphics\.multiscale\.residentMillionFrameTiming$'`：
AMD Radeon AI PRO R9700、MSVC Debug/SHARED、实际 1920x1080、VSync 关闭、
隐藏但真实交换链、100 万个预先打包的球体记录，4 帧预热后量测 120 帧。
每帧执行常驻 buffer 到 RGBA32F 图像复制、批量 draw、present 和事件泵。
最终可复现用例的 CPU 循环墙钟 P50 2.4281 ms、P95 2.8960 ms、
P99 3.4992 ms、最大 6.1215 ms；原始日志为本地
`build/resident-sphere-million-final.log`。先前版本同配置独立进程测得
P50 2.7295 ms、P95 3.3298 ms、P99 3.9188 ms，
记录在 `build/resident-sphere-million.log`。进程间波动不能归因于单一改动。
跨帧同步的复制先等待旧图像使用，因此循环包含该等待；尚无同帧
GPU 时间戳分解。记录始终来自同一预先上传的 buffer，未测 GPU 投影
生成、位置全量移动、场求解、递归生成、可见性或整帧组合，
不能作为百万对象 16.67 ms 整帧通过证据。

默认运行同一计时用例时执行 16 个记录、2 帧，实际检查更新与绘制，
不静默跳过。该用例、常驻记录像素用例及原有 CPU 更新像素用例 3/3 通过，
最终严格编译日志为 `build/resident-sphere-final-build.log`。
无 GPGPU 的优化 Release/OBJECT、minimal + font + multiscale 配置下，
`render_benchmark` 完成编译链接，日志为
`build/resident-sphere-release-build.log`；这是 L1/L2 裁剪证据，
不代表该配置下运行了常驻更新或其他平台后端。
最终 `make check` 退出 0：533 个脚本测试、5 个跳过，source-quality
gate 使用 `origin/dev` 通过；日志为本地
`build/resident-sphere-final-check.log`。`git diff --check` 通过。
