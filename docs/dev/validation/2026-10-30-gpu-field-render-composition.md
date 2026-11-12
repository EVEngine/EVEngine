# GPU 场到球体绘制组合验证（2026-10-30）

## 实现边界

`physics_field_gpu` 仍默认关闭。显式 C++ 调用方创建 `GpuGravitySession` 和
`SphereBatch` 后，可通过 `updateSphereBatch` 将已发布目标的设备缓冲区直接
投影为球体记录，再复制到批量绘制图像。每次投影只读回一个拒绝标志；
`snapshot` 才读回全部源和目标。投影失败时既不发布新的物理状态，也不更新
绘制批次。新路径没有注册到旧物理循环或旧相机。

投影使用有限范围的视图相对 binary32 坐标。默认使用统一半径和颜色；
调用 `setSphereAppearances` 后，每个目标拥有独立的半径、深度指数和颜色，
外观数组在设备上复用，完整替换失败保留旧数据。近裁剪面不确定、
非有限值和不可表示的矩形会使整批拒绝。本次没有实现任意尺度坐标转换、
GPU 可见性层次、通用场树、碰撞或递归生成的逐帧接入。

## 验证

- Windows Debug SHARED 严格编译：`make unit-test/win32-debug DOMAIN=graphics JOBS=8` 通过。
- 真实 Vulkan 像素测试：`publishedTargetsReachSpherePixels` 及
  `simulationProjectsAndRendersEveryTarget` 通过。前者检查初始红色、
  commit 后蓝色和投影拒绝后的旧图像保持。
- `individualSphereAppearanceSurvivesFailedReplacement` 通过：两个目标使用
  不同半径和颜色，GPU 绘制像素分别呈红、蓝；非法替换不改变已发布外观。
- `make check` 通过（`CI_BASE=origin/dev`，脚本测试 533 项）。重新配置
  `EVENGINE_MODULE_PHYSICS_FIELD_GPU=OFF` 后，Windows Debug SHARED 图形测试目标
  编译通过；14 项 `graphics.multiscale.*` 测试通过。新 GPU 场投影测试在该配置下
  未注册。已有独立 GPGPU 场序列测试仍按自身模块条件注册。
- 本机 AMD Radeon AI PRO R9700，1920x1080，关闭 VSync，固定模拟步长
  16,666,667 ns；八个紧簇的 1,000,000 独立源与 1,000,000 独立目标，
  连续 120 帧。每次计时包含 `prepareStep`、`commit`、GPU 投影、同设备
  图像复制、批量绘制、`present` 及 SDL 事件轮询。

| 统计量 | 毫秒 |
| --- | ---: |
| P50 | 3.5774 |
| P95 | 4.4324 |
| P99 | 8.7521 |
| 最大观测 | 51.0168 |

命令：设置 `EVE_GPU_FIELD_RENDER_BENCH=1` 后执行
`ctest --test-dir build/win32-debug -V -R '^physics\.field\.gpu\.simulationProjectsAndRendersEveryTarget$'`。
原始本机输出保留在未跟踪的 `build/gpu-gravity-render-million.log`。

## 可解释范围

这证明固定八簇输入、统一球体外观、预分配批次的特定组合在该设备上
P99 小于 16.67 ms。最大观测值超过 16.67 ms，因此不满足逐帧硬上限。
测试不计入场会话创建、批次构建、递归生成、全场景可见性、碰撞、
游戏脚本和编辑突发；也没有覆盖均匀、薄片或任意质量分布。
不能将这项结果称为完整游戏帧的百万对象验收。

## 逐目标外观复测

同一 R9700 与 1080p 配置，源/目标仍各 1,000,000 个、120 帧；目标交替采用
两种 GPU 常驻半径及颜色，外观上传发生在计时前。P50 3.1825 ms、
P95 4.5453 ms、P99 6.8570 ms，最大观测 58.9269 ms。原始本机输出为
`build/gpu-appearance-million.log`。这项测量增加了逐目标属性读取，
并未覆盖逐帧外观编辑或动态生成，也未满足每帧 16.67 ms 的硬上限。

## 稀疏外观编辑

`editSphereAppearances` 接受按目标索引严格递增的修改列表。设备上先把当前
外观复制到备用缓冲区，再上传被修改的记录；提交成功才切换已发布缓冲区。
CPU 验证及上传量为 O(K)，设备复制为 O(Nt)，K 为本次修改数。非法索引、
乱序或无效外观在提交前拒绝。像素测试验证单目标从蓝变绿、未修改目标保持红色，
随后乱序编辑被拒绝且绿色仍保持。

在同一百万源/目标基准中，每 30 帧计入一次单目标编辑。最终诊断运行的
120 帧 P50 3.2027 ms、P95 4.4359 ms、P99 6.1153 ms，最大观测
58.9397 ms，发生在第 1 帧；四个编辑帧分别为 3.5120、3.3586、
3.3359、3.3908 ms。原始本机输出为 `build/gpu-sparse-appearance-timing.log`。
首帧尖峰不能从稳态百分位中抹去；其具体驱动/管线成因尚未单独证明。
这仍不是动态生成或完整游戏帧验收。

## 首次绘制成本与显式预热

对冷启动的 120 帧逐段计时：最慢的是第 1 帧 73.7225 ms，其中模拟
1.0715 ms、投影与图像更新 1.7492 ms、首次绘制提交 50.4712 ms、
首次呈现 15.2933 ms。上述阶段为主线程墙钟时间，并非独立的 GPU 时间。
这说明尖峰主要位于首次绘制/呈现路径；尚未进一步分离管线、纹理和交换链
各自成本。

随后在百万对象基准的计时循环前，明确执行一次初始目标投影、绘制和呈现，
作为加载阶段预热，耗时 43.6783 ms。预热后的 120 个游戏帧包括
模拟、每 30 帧一次的单目标编辑、投影、绘制、呈现和事件轮询：
P50 3.3201 ms、P95 4.2427 ms、P99 4.6932 ms、最大观测 6.0163 ms；
第 1 个游戏帧 4.2604 ms，四个编辑帧为 3.2095、3.5146、3.2737、
3.4855 ms。原始输出为 `build/gpu-first-frame-stages.log` 和
`build/gpu-warmup-million.log`。预热成本必须计入启动/加载预算，
不能把它说成从未发生。结果仅证明当前固定八簇和预分配球体组合的
预热后游戏帧测量，不证明任意分布、动态生成或完整游戏帧的上界。
