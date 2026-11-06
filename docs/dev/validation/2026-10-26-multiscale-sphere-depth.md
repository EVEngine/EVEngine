# 独立球体视图的跨尺度深度参考路径

本切片在默认关闭的 graphics_multiscale 中新增 SphereFrame / SphereView，不改旧
Camera、投影、深度清理值、后处理或启动注册。它是明确调用的独立完整视图，不能把
旧深度几何与这里的对数深度混画；任意网格和其他旧效果的兼容仍未完成。

## 数据与数值

SphereFrame::build 接收相同 lattice 的中心数组、半径 mantissa/exponent、线性 RGB、
相机基和数量预算，先完整验证，再生成拥有的固定 32-float 数据包。输入失败不提交 GPU
工作，销毁候选即取消。帧不保留模型/相机指针，也不是新的可编辑场景状态。

每个球按二次幂归一化，使 GPU 使用有限浮点中心和半径。片元沿视图射线求与球的交点，
使用 cross(center,ray) 求垂距，并用 (r-perp)*(r+perp) 计算判别式。
写入同一深度约定：

`depth = (log2(normalizedRayDistance) + physicalExponent + 2048) / 4096`

所有球共享这一单调物理距离编码，近远遮挡由同一深度缓冲决定，不依赖提交顺序。
归一化指数限制 [-2000,2000]，半径/距离小于 1e-5 或相机距表面不足相对 1e-5 时
明确 Unsupported；浮点 FOV tangent 范围也有限制。小星点的接收通量不能用此拒绝逻辑
作为最终实现，仍需单独的通量聚合路径。

对数深度仍量化，相近表面可能无法区分；射线/球交点也有 float 误差。
这不是认证总误差方案。当前每个可见球用全屏四边形，片元成本是 O(可见球数*屏幕像素)，
只作为正确性参考，不代表百万独立对象整帧 16.67 ms 达标。

## 资源与兼容

SphereView 以 Graphics 弱生命周期保护资源，不延长 provider 生存期。消费者先销毁时，
releaseMesh/releaseShader 后删除已交回的 CPU facade；provider 先销毁时禁止解引用。
同次审查修正了 frontier_mesh_render 测试中 releaseMesh 后漏 delete 的 CPU facade。
没有修改旧资源释放语义。实际 provider-first 销毁和故障注入仍需补充独立证据。

目前显式支持 Vulkan，其他后端 Unsupported，不静默切换。GLSL 位于模块 shaders/，
SPIR-V include 由 scripts/compile_multiscale_shaders.py 生成，非手工编辑。
全部输入检查在绘制前；GPU 提交异常返回 Failed，但不能回滚已经记录的命令，宿主需恢复
帧/provider 状态。模块 README 与公共头写明所有权、线程、成本、六面边界和限制，无架构例外。

## 验证

Windows MSVC 严格警告 SHARED 编译/链接通过，日志 build/multiscale-sphere-view-build.log。
开启 Vulkan 验证层，定向运行图形多尺度与原网格生命周期测试：6/6 全通过。
原生测试以 2^900 为 cell 尺度，两个球距离分别为 10 和 10^18 cell，角尺寸相同，
交换绘制顺序后近球仍遮挡远球；移除近球后远球显示，失败候选及 moved-frame 检查通过。
日志 build/multiscale-sphere-view-tests.log，完整验证层输出保存在
build/multiscale-sphere-view-validation.log，未发现验证错误；既有属性未使用警告仍存在。

新增实际帧图像捕获后的构建退出 0，原生测试再次 1/1 通过，日志
build/multiscale-sphere-capture-build.log / build/multiscale-sphere-capture-tests.log。
完整验证层输出 build/multiscale-sphere-capture-validation.log 未发现验证错误。
图像来自 Graphics::newImageData 的 GPU 呈现帧读回，保存原始 RGBA8 的 RGB 数据，
不是桌面截图。build/multiscale-captures/sphere-near-occludes-far.png 与
sphere-far-revealed.png 已查看：前者中心为近处红球，后者在相同角尺寸处显露蓝色远球。
PPM 到 PNG 仅作无损格式转换。当前完整 make check 日志
build/multiscale-sphere-source-check.log 已退出 0，529 项测试、5 项既有跳过，源码门禁通过。
跨平台、裁剪、亚像素星光、任意几何、透明/阴影/拾取、时间历史、原生用户示例和最终性能
仍未完成。此前角尺寸查询版本的完整源码门禁已通过，不替代当前新增渲染器的门禁。

## 宿主提交入口

补充 SphereView::draw(frame)：验证后开启并绘制专用 pass，将 present 留给宿主。
原有 render(frame) 调用相同绘制实现后自行 present，行为保持。
依据 src/scripts/load.nut 的循环契约：eve_render 返回或抛出后，循环统一提交画面。
因此后续脚本绑定须使用 draw，不能在脚本回调中嵌套调用 render。
这两个入口区分提交职责，不提供第二套成本或画面语义；两者均要求新帧边界、专用深度。
无效或 moved 帧在开启 pass 前失败；GPU 部分提交失败仍由宿主负责恢复。

Windows SHARED 严格警告构建退出 0（build/multiscale-draw-build.log）。
开启 Vulkan 验证层的图形多尺度定向测试 5/5 通过（build/multiscale-draw-tests.log），
包含宿主提交后远球可见、旧 render 路径以及 moved 帧失败后继续正常绘制。
完整源码门禁 build/multiscale-draw-source-check.log 已退出 0，529 项测试、5 项既有跳过。
本次遵守公共接口 Result/nodiscard、成本、线程及所有权约定；没有修改旧循环或默认路径，
没有新持久格式、ECS System、跨模块依赖或架构例外。脚本绑定与用户示例仍未完成。