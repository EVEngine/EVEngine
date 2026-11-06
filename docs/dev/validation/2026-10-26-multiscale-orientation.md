# 多尺度参考系：静态姿态与区间误差传播

日期：2026-10-26。基线 5f2cd482ad1f88a7e11116000823a2b139c54a61，工作树未提交。
扩展已有可选 spatial_multiscale 的 FrameDefinition/FrameGraph，不改变旧 Scene/Transform/Camera。

## 数学与接口

每个子参考系新增默认单位矩阵的 row-major rotation：

`parentPosition = translation + 2^exponent * rotation * localPosition`。

根仍必须严格为单位变换。输入旋转检查有限值、行正交归一与正行列式，测量容差 1e-12；
反射、剪切和非有限值拒绝。不会自动正交化：实际映射由调用者提供的二进制浮点系数定义，
误差界涵盖计算舍入，不涵盖这些系数与调用者期望的理想姿态之间的差。

构建时为每个节点计算到根轴的旋转区间。区间加法与乘法的端点每步 nextafter 向外扩张，
乘法取四个端点乘积的最小/最大值，随后扩张；累计矩阵乘法保持对精确实数系数乘积的包围。
查询仍先找到共同祖先：公共平移不形成绝对浮点位置。剩余点项使用其参考系到根的姿态，
剩余平移项使用父参考系到根的姿态，分别缩放后叠加。同参考系先做全宽 cell 差，再旋转。
点投影区间与旋转区间做矩阵向量乘法，以区间中点及向外扩张的最大端点距离返回值/界。
非有限范围或超出显式误差预算即 Unsupported，失败不发布部分坐标。

单位姿态分支跳过旋转运算，保留原有平移路径的算术与误差界。新增姿态默认单位矩阵，
既有调用者不必提供新字段。构建仍 O(F)、查询 O(depth)、深度上限 64，查询固定栈存储，
无每帧字符串查询或新 ECS 系统。节点增加固定大小的姿态和区间矩阵存储。
区间依赖会使深层级误差界变宽；不能保证任意输入都满足任意精度预算。

## 实证

在模块已有 cell_position_unit.cpp 中新增强断言覆盖：

- 2^900 公共祖先、INT64 极值平移与巨大共享局部地址下，保持微小局部差的旋转。
- 不同旋转、尺度和父轴平移的分支投影，公共祖先平移在转换前抵消。
- 32 层任意角度组合，30 组点对逐步 long double 参考包含在返回误差界内。
  MSVC long double 与 double 精度相同；GCC Linux 分支提供更宽 long double 参考，
  不能将 Windows 的参考误称为任意精度验证。
- 极紧预算拒绝、反射/剪切/NaN 拒绝、非单位根拒绝。

Windows 严格警告 DLL 数值构建及测试通过：44 用例、24,377 断言、0 警告/失败，
日志 build/frame-rotation-contract-build.log / frame-rotation-contract-tests.log。
WSL GCC 静态数值构建与 CTest 通过：build/frame-rotation-linux.log。
真实引擎 core 域构建 build/frame-rotation-engine-build.log 已退出 0，15/15 坐标与参考系用例通过
（build/frame-rotation-engine-tests.log）。完整源码门禁
build/frame-rotation-source-check.log 已退出 0，529 项测试通过、5 项既有跳过。
FrameGraph.h 的所有消费 TU 已识别，既有 cell_position_unit.cpp 已触发重编译，
避免 Ninja unscanned 依赖下旧布局对象混用。

## 适用规则与剩余范围

保留 Result/nodiscard、输入拥有值语义、类型化运行 handle、图所有权/销毁失效、
静态线程读契约和成本约定；没有新增跨模块 include、持久格式、动态系统或注册通道。
没有架构例外。

本扩展支持静态姿态，不代表随时间转动的参考系动力学。角速度、科里奥利/离心项、
动态迁移、旋转状态持久化及场/刚体/相机组合仍需实现；整体架构与百万对象帧率目标未完成。

源码门禁要求合并同模块短测试文件，旋转用例已并入 cell_position_unit.cpp，未删减断言。

## 精确区间操作的极端值回归

后续最小回归发现两种过度保守拒绝：旋转后的精确零位移在 denorm_min 预算下失败；
64 次精确四分之一转组合积累了不必要的误差宽度。修复前数值套件结果为 44 通过、2 失败
（build/frame-exact-before.log）。区间加法现在保留零加数，乘法保留零/单位/负单位因子，
输入零误差保持单点区间，输出单点区间保持零误差。这些都是 IEEE 下精确操作，
一般乘加仍 nextafter 向外扩张，没有缩小非精确运算所需的界。

修复后 Windows DLL 数值套件 46 用例、24,396 断言、零失败/警告；
日志 build/frame-exact-contract-build.log / frame-exact-after.log。
WSL GCC 静态套件也通过（build/frame-exact-linux.log）。
新增用例使用强断言，并保留原来的任意角度长链误差包含性回归。
这是拒绝过严的修复，不把极小非零距离近似成零，也不改变既有轴对齐路径。
真实引擎构建 build/frame-exact-engine-build.log 已退出 0；17/17 定向用例通过
（build/frame-exact-engine-tests.log）。最终源码门禁 build/frame-exact-source-check.log 已退出 0，529 项测试通过、5 项既有跳过。