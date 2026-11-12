# 静态参考系位置转换

状态：新增显式位置转换原语，动态参考系及刚体迁移仍未完成。

`FrameGraph::reexpress` 接受同一图中的源位置、目标 frame handle 和目标局部单位下的
逐轴误差预算。返回规范化 `FramePosition` 和必须保留的逐轴算术误差。
不修改旧 Transform、相机、Scene、Physics API 或已有对象，不增加默认路径遍历。

同 frame 仅做规范化，保留 int64 完整地址和原有二进制小数。
跨 frame 先取消共同祖先，计算相对于目标原点、以目标尺度为单位的根轴偏移，
再用目标根轴姿态的区间逆矩阵变换。逆矩阵通过区间伴随矩阵和行列式构造，
行列式包含零、范围超限或误差超预算时返回 Unsupported。
不能把已在 1e-12 内验证正交的浮点矩阵当作精确正交矩阵而直接取转置；
测试特意覆盖一个可接纳但不精确正交的表示，区分两种计算。

转换保持 O(depth) 工作和固定有界栈存储。返回的是坐标与不确定度，不是零误差新真源；
调用方必须保留误差，不能反复转换并丢弃误差后声称精度未损失。
同图身份、owner/generation 检查与既有查询一致：移动转移身份，旧/外来身份拒绝，
图不保留调用方指针，结果句柄也不延长图寿命。查询只读、无回调、可并发读取，
不得与移动/销毁并发。没有 ECS System、持久格式、provider 或事件变更。
连接机制仍为多调用者到已知实现的同步值查询，不需要能力注册或异步 token。

Windows DLL 数值契约通过，新增测试覆盖巨大共同祖先、旋转和尺度、完整 int64 地址、
非精确正交的逆、无效预算/NaN/范围、外来和 moved-from handle。
后续增加一般三轴旋转的正向残差验证，显式分离参考求值的浮点容差，并验证过严预算拒绝。
最终 Windows 契约日志 build/frame-relocation-contract-build-final.log / frame-relocation-contract-tests-final.log。
Windows 最终契约为 52 用例、36345 个断言通过；GCC 最终布局复验也通过，
日志 frame-relocation-linux-contract-final.log。

真实引擎的生成目标为 unit_test_core；最初误用 unit_test_spatial 被 Ninja 明确拒绝，没有运行测试。
修正目标后 SHARED 构建及 21 项空间回归通过，日志 frame-relocation-engine-build-final.log、
frame-relocation-engine-tests.log。一般旋转新增用例及最终文件布局的引擎复验通过，22/22 项，
日志 frame-relocation-engine-build-consolidated.log / frame-relocation-engine-tests-consolidated.log。
旧空间 2D/3D 索引及工厂无效构造契约另行复验，3/3 项通过，日志
build/frame-relocation-legacy-spatial-tests.log；这仅覆盖旧空间索引接口，不扩张成全部旧引擎功能验收。
现有 build/win32-multiscale-release 的 Release/OBJECT、spatial_multiscale=ON 配置也完成 eve 构建，
日志 build/frame-relocation-object-build.log。该配置沿用此前 minimal+font 的发布验证范围，
不是全模块 OBJECT 或完整平台矩阵；构建中既有导出警告不等于严格警告配置通过。
首轮源码门禁拒绝同模块两个过小单元测试文件，现已将新测试合并回 cell_position_unit.cpp，
保留所有断言；最终源码门禁通过，532 项测试、5 项既有跳过，日志 frame-relocation-source-check-final.log。

本原语不变换速度或加速度，不转移刚体世界所有权，不定义时间依赖姿态或非惯性力。
完整参考系迁移必须进一步处理这些量及原子发布，不能把此切片标记为阶段三完成。
适用规则为 Result/nodiscard、公开生命周期与成本说明、强类型身份、单一数据所有者及
默认关闭的模块隔离；本轮没有批准或使用架构例外。
