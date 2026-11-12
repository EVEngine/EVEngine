# 无固定远裁剪的保守视图查询

新增默认关闭模块 graphics_multiscale（graphics/multiscale，L4），依赖现有 graphics
与 spatial_multiscale。公开接口为 projectSphere，使用同一 dyadic lattice 的相机/物体
CellPosition、物理半径 mantissa/exponent 和显式相机基向量；不改变旧相机任何参数。

## 数学和复杂度

先以 CellPosition 的整数差求相对位置，再在 lattice 单位计算，不把绝对世界位置转为 float。
设相对中心估计为 d，各轴误差界为 e，视图轴为 b。投影中心为 b·d；其误差按
sum(|b_i| e_i) 加乘加舍入项向外扩大。包围球投到该轴的半径使用 r*|b|，
不假定输入基向量在浮点中恰好单位长度。基向量正交/单位偏差超过 1e-12 拒绝。

得到视图坐标区间 X、Y、Z。若 Z 完全为负，可按几何规则排除；若包含相机平面，
返回全屏待细化状态，绝不以除零、远场近似或中心点裁剪丢弃它。Z 为正时用区间除法求
X/(Z*tan(FOV_y/2)*aspect)、Y/(Z*tan(FOV_y/2))，上下界分别向外舍入。
只有整个矩形位于视口外时才排除。每个球是 O(1) 工作/存储，没有逐对象树查询或远距离阈值。

输入不是完整视图遍历器：场景候选数量、空间树遍历、细化和生成突发仍需单独预算。
矩形是占屏范围上界，不是精确球轮廓或像素级几何误差。过宽结果可能增加工作，
不允许把这点偷换成任意场景固定 O(屏幕像素) 的保证。

## 边界

公开 Result/nodiscard、纯值生命周期、线程、成本和输入误差责任已写入头文件。
四轴选型为已知调用者、逐候选高频、单接收者、原子值返回，因此使用直接查询。
六面仅有 C++ API，无新 ECS System/Link、事件、脚本、MCP、能力注册或持久格式。
模块 README 记录恢复后重建和数值假设；没有架构例外。

本接口没有输出深度缓冲值，不允许据此独立叠加近/远色彩图层并声称遮挡正确。
球包含相机时还需细化；发光通量、阴影、透明、拾取、时间历史、连续跨尺度移动和
完整原生示例都未由这项查询完成。固定宽度地址/指数范围也不等于数学无限精度。

## 验证状态

全文件源码架构 lint 通过，真实 Windows CMake 配置成功。
新增三个用例覆盖 ±900 物理指数、大共同地址下的小位移、等角尺寸远源、
旋转视图中 840 个球面样点的包含性、相机平面/后方/侧方情况和无效输入。
Windows MSVC 严格警告 SHARED 构建已退出 0，实际 AngularView.cpp 与测试消费者
均重编译，EVWorld.dll 和 unit_test_graphics 链接通过，日志 build/multiscale-view-build.log。
ctest --test-dir build/win32-debug --output-on-failure -j 2 -R '^graphics\.multiscale\.'
已退出 0，3/3 用例通过，日志 build/multiscale-view-tests.log。
完整源码门禁 build/multiscale-view-source-check.log 已退出 0：529 项、5 项既有跳过。
跨平台数值、完整裁剪和多尺度渲染组合不由此定向测试代替。
后续独立视图的进展见[跨尺度球体深度](2026-10-26-multiscale-sphere-depth.md)。
