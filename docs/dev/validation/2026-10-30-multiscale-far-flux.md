# 远景点源通量：CPU 数学帧与独立 GPU 显示

日期：2026-10-30。此记录仅覆盖默认关闭的 `graphics_multiscale` 新接口，
不是阶段五或百万状态整帧验收。

`FarFluxFrame::build` 接受同一二进制格点内的点源及相机，用经过验证的
正交视图基和相对坐标计算方向。每个源的物理通量为
`powerMantissa * 2^powerExponent / (4π distance²)`，RGB 通道分别乘权重。
距离以尾数/指数拆分后再求通量，避免先构造可能溢出的绝对物理距离。
没有固定最远裁剪距离；超出有限地址、投影误差或 binary64 通量范围时
明确返回失败。方向区间跨像素边界时，最多分摊到 2×2 像素，保持通量和；
更大不确定区间明确拒绝。构建工作量 O(N+P)，存储 O(P)，N 是点源数，
P 是图像像素数。没有每个源遍历全部像素的路径。

原生单位测试证明 2^900 米格点、10^18 格远方的高功率源仍有正通量，
两个独立源的 RGB 分量相加，视野外源不进入图像；还验证共点奇异、
下溢、负权重和源预算拒绝。Debug SHARED `unit_test_graphics` 构建和
`graphics.multiscale.farFlux` 2/2 通过。用例按仓库测试源布局门禁合并在
`test/unit/graphics_multiscale/angular_view_unit.cpp`，测试名与覆盖点保持不变。
最终 Debug SHARED `unit_test_graphics`、Release OBJECT `eve` 构建均退出 0；
`graphics.multiscale` 聚焦用例 11/11 通过。完整 `make check` 退出 0：
533 项脚本测试、5 项既有平台跳过，比较基线 `origin/dev`。
日志 `build/multiscale-far-flux-consolidated-build.log`、
`build/multiscale-far-flux-consolidated-tests.log`、
`build/multiscale-far-flux-release-build.log`、
`build/multiscale-far-flux-source-check-final.log`。

`FarFluxView::prepare` 通过现有资源感知着色器接口，将完整通量图上传为
RGBA32Float 图像；独立的全屏绘制在 GPU 上执行指数曝光和远景深度测试。
首次创建的普通 mesh 管线不能直接引用资源集合 1，否则 Vulkan 管线创建
在本机崩溃；资源感知替换步骤负责建立对应布局。推送常量按现有
`float[32]` ABI 提供，图像和单个实例矩阵均由资源接口持有。
`setExposure` 只更新下一次绘制的常量，无需重新投影或上传。
后续新增的 `FarFluxView::update` 在尺寸不变时复用同一图像、描述符和管线，
通过拥有型 `ShaderImageUpdate` 完整替换纹理内容；更新在渲染通道外调用，
仍同步复制并上传 O(P) 字节。原生像素测试验证同一显示对象从红色点源
更新为绿色并恢复红色，旧的曝光与前景遮挡断言保持通过。它减少重复
管线创建，但还不是源和相机状态的 GPU 常驻求解。
新增更新路径后，Debug SHARED `unit_test_graphics` 构建退出 0，
`graphics.multiscale` 12/12 通过，开启 Vulkan validation 的更新测试通过；
完整 `make check` 再次退出 0（533 项脚本测试、5 项跳过）。对应日志为
`build/multiscale-far-flux-update-build.log`、
`build/multiscale-far-flux-update-all-tests.log`、
`build/multiscale-far-flux-update-validation.log` 和
`build/multiscale-far-flux-update-make-check.log`。

Windows Debug SHARED 原生像素读回测试验证 10^18 格远点源出现在中心像素、
降低曝光后红色像素值下降、近处蓝色球体遮住远景红色点源。
`graphics.multiscale` 共 12/12 项通过；日志为
`build/multiscale-far-flux-view-test.log` 和
`build/multiscale-far-flux-graphics-tests.log`。GPU 显示为默认关闭的新接口，
未改旧相机和绘制调用。完整 `make check` 再次通过 533 项脚本测试、5 项跳过，
日志 `build/multiscale-far-flux-make-check.log`。

CPU 仍逐源投影并累加，且每次准备上传 O(P) 图像；目前不支持 GPU 常驻源、
设备端场求解或每帧增量更新。点通量图也不含 PSF、扩展光源表面、时间变化、
光行时和浮点求和的认证总误差。百万状态整帧 16.67 ms 尚未通过，仍需
引擎截图和整帧性能证据。
