# 多尺度球体参考视图：投影矩形绘制

日期：2026-10-26。基线 5f2cd482ad1f88a7e11116000823a2b139c54a61，工作树未提交。

## 改动与成本

SphereFrame 原来计算可见性后仍为每个球提交全屏四边形。现在复用已计算的 AngularBounds，
并与 float GPU 参数表示的球体投影范围取并集，将矩形放入既有 push constant 第四个 vec4。
顶点着色器将固定单位四边形映射到这个矩形，片元射线与共同的对数深度公式不变。
SPIR-V 由 scripts/compile_multiscale_shaders.py 重新生成，没有手工修改生成数组。

对 GPU 表示的半径加 `128 * float epsilon * (distance + radius)` 保护余量，
以球的轴对齐包围盒投影端点计算矩形；浮点矩形端点向外舍入后裁到视口。
数学范围与 GPU 范围取并集；相交相机平面或扩张后的近面不确定时保留全屏。
该保护余量不是完整着色器数值误差证明，不能据此宣称任意后端/输入的像素误差已认证。

准备仍 O(N)，每个可见球仍一次提交。片元工作跟随投影矩形覆盖像素之和，最坏情况仍为
O(visible N * viewport pixels)。本次没有减少百万 draw calls，也没有整帧性能达标数据。
旧渲染器、相机、深度默认约定、模块开关和接口语义均未变化。

## 验证

- Windows 严格警告 SHARED 构建通过：build/sphere-rectangle-build.log。
- 开启 Vulkan 验证层，7/7 图形多尺度用例通过：build/sphere-rectangle-tests.log。
  完整输出保留在 build/sphere-rectangle-validation.log，无 Validation Error / VUID 错误；
  既有未使用顶点属性警告仍存在。
- 原生渲染测试新增 (15,6,10)、(-15,-6,10)、(0,0,0) 三个半径 2 的球，
  覆盖左右视口边缘、正负 Y 方向及相机位于球体内部。
  CPU 射线交球与实际 GPU 读回逐像素比较，每个视角超过 56,000 个像素；
  轮廓距离在 0.001 场景单位以内的数值不确定带不作像素相等断言，检查数量另有强断言。
  这不是全像素总误差认证，也不是完整任意场景测试。
- 正常 main.nut 示例通过第 60/300 帧的引擎呈现帧捕获：
  build/sphere-rectangle-capture-near.log / sphere-rectangle-capture-far.log。
  图像为 build/multiscale-captures/rectangle-near.png / rectangle-far.png。
  与之前全屏版本 script-near.png / script-far.png 比较，尺寸均 1920x1080，
  两组 RGB 像素差均为零；结果保存在 rectangle-image-comparison.json，远球画面已查看。
- 标准示例 smoke 12 秒通过：build/sphere-rectangle-smoke.log。
- 完整源码门禁 build/sphere-rectangle-source-check.log 已退出 0，529 项测试通过、5 项既有跳过。

后续验收修正：这里的原生参考测试仍含 CHECK 颜色断言，进程通过不能证明这些断言成立。
新增的逐像素轮廓检查原本就使用 REQUIRE；颜色/遮挡检查后来统一改为 REQUIRE，并按呈现后
颜色而非线性片元数值验证。修正后的证据见[批量绘制验证](2026-10-26-multiscale-sphere-batch.md)。

仅改变新增参考路径的内部帧包与着色器，未增加 ECS、持久格式、跨模块依赖或第二注册通道。
保留输入原子准备、Result、provider 弱生命周期和图形线程契约，没有架构例外。
GPU 批量提交、完整误差认证、其他后端与百万独立状态整帧 16.67 ms 仍未完成。
