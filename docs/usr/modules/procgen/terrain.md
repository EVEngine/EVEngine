# Procgen 地形

[返回模块概览](../procgen.md)

高度图印章、侵蚀、蒙版、生成会话、草地/树木放置，以及 GTS 切片与 PCG 运行时辅助。

## 使用要点

### 高度图印章与顺序蒙版（CPU）

`ProcgenHeightmap.blendMask(source, mode, strength, invert)` 在接收者上顺序组合
同尺寸蒙版，返回标准 Result，`value` 是变化的采样数。`mode` 为
0 Multiply、1 Maximum、2 Minimum、3 Add、4 Subtract。先按 `invert`
选择 `source` 或 `1-source`，执行运算，再按 `[0,1]` 内的 `strength`
从原值插值；中间值不钳制，所以连续调用的顺序有意义。

`TerrainStampSettings()` 是脚本持有的参数对象：

- `setGrid(originX, originZ, spacingX, spacingZ)` 设置接收高度图的世界原点与采样间距。
- `setCenter(x, z)`、`setSize(width, depth)`、`setRotation(radians)` 设置印章位置和旋转。
- `amplitude`、`baseHeight`、`blendStrength` 为可写浮点字段，默认值分别为 1、0、0.5。

脚本浮点参数使用 `1.0` 这样的浮点字面量；当前 VM 为单精度。C++ 世界坐标
配置保留 double，脚本不能提供超出 VM 数值精度的坐标。

`ProcgenHeightmap.applyStamp(stamp, settings, operation, localMask, globalMask)`
返回标准 Result，`value` 为变化的采样数。`operation` 为 0 Raise、1 Lower、
2 Blend、3 Set、4 Add、5 Subtract。Raise/Lower 取高度包络；Blend 使用
`blendStrength`；Add/Subtract 对已有高度加减印章高度。

印章和两个蒙版使用同一旋转矩形内的 UV，分别双线性采样，分辨率可不同。
局部蒙版作用于 `stamp * localMask`，再乘 `amplitude`、加 `baseHeight`；
全局蒙版钳制到 `[0,1]` 后控制最终高度变化。值为 1 的单像素蒙版表示无过滤。
旋转矩形外不变；地形 `(x,y)` 的世界位置为
`(originX+x*spacingX, originZ+y*spacingZ)`，相邻瓦片可共享边界坐标。

```squirrel
local terrain = procgen.newHeightmap(65, 65).value;
local stamp = procgen.newHeightmap(2, 2).value;
stamp.setHeight(0, 0, 0.0); stamp.setHeight(1, 0, 0.0);
stamp.setHeight(0, 1, 0.0); stamp.setHeight(1, 1, 1.0);
local one = procgen.newHeightmap(1, 1).value;
one.setHeight(0, 0, 1.0);
local settings = eve.TerrainStampSettings();
settings.setCenter(32.0, 32.0);
settings.setSize(40.0, 24.0);
settings.setRotation(0.3);
settings.amplitude = 12.0;
local applied = terrain.applyStamp(stamp, settings, 0, one, one);
assert(applied.ok);
```

调用同步执行，不保留传入对象。调用者必须独占目标高度图；同一对象作为输入
与输出也按修改前数据计算。输入尺寸、有限值、枚举与参数先检查，结果溢出也
拒绝整个操作，不留下部分写入。内存分配失败按 C++ 分配异常处理。
该接口使用原生地形高度单位，不模拟 Unity packed height、自适应基底或
MixHeight；本接口自身不创建编辑器撤销记录，事务历史由拥有式 workspace/session 提供。

### 蒙版生成与曲线纹理

以下都是 `ProcgenHeightmap` 方法，修改接收者并返回标准 Result（`value` 为变化
的采样数）。运算先构建候选结果，再统一发布；源、曲线和目标允许别名。
所有输入必须有限，曲线必须是非空的一行高度图。

- `transformMask(source, curve)`：以源标量为 UV，通过 Clamp/Bilinear 曲线纹理变换。
- `rangeMask(source, minimum, maximum, filterCurve, strengthCurve)`：先计算
  `smoothstep(minimum, maximum, source)`，再依次采样过滤曲线和强度曲线。
  `minimum < maximum`；用世界高度范围即可生成高度适应度。
- `deriveSlope(heights, spacingX, spacingZ, heightScale)`：从按世界间距缩放的高度
  导数生成 `1-normal.y`，**不是坡度角度**。输出可继续传入 `rangeMask`。
  内部用中心差分，边缘单边差分；单采样轴的导数为零。分块处理时应提供
  邻接 halo 再裁切，不能把没有邻块的边缘差分当成跨瓦片连续性保证。
- `distanceMask(settings, axis, filterCurve, strengthCurve)`：距离过滤后再应用
  强度曲线。`axis` 为 0 圆、1 X、2 Z、3 圆角方形。

`TerrainDistanceMaskSettings()` 提供浮点字段 `offsetX`、`offsetZ`、`scaleX`、
`scaleZ`、`rotation`（弧度）、`roundness`，以及布尔字段 `tiling`。
默认偏移和旋转为零，缩放为 1，圆角指数为 0.5，不重复。缩放非零、圆角指数
严格为正。圆角方形范围外的过滤值为零，仍需经过强度曲线，因而不一定输出黑色。

曲线按 GPU 纹理坐标采样：第 i 个 texel 中心在 `(i+0.5)/width`。因此两像素
曲线 `[0,1]` 在 UV=0.25 处为 0、0.5 处为 0.5、0.75 处为 1，不能视作
两个端点之间的普通线性函数。可以提供原包导出的 256×1 曲线数据；目前没有
实现 Unity AnimationCurve 资产解析。距离蒙版的输出 UV 也取像素中心。

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/procgen/`](../../../src/modules/procgen/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `procgen`；跨模块 hex 关卡见 [`test/hex_level_simulation.cpp`](../../../test/hex_level_simulation.cpp)、[`test/hex_level_data.cpp`](../../../test/hex_level_data.cpp)、[`examples/hex-levels/`](../../../examples/hex-levels/)。

### Terrain contrast

`ProcgenHeightmap.applyContrast(mask, strength, featureSize)` returns a structured Result with the changed sample count. C++ exposes `applyTerrainContrast` in `TerrainEffect.h`. The mask must match the destination dimensions and contain values in [0,1]; strength is finite and nonnegative (values above 1 extrapolate; Pcg defaults to 2), and featureSize is a nonnegative texel offset. Fractional offsets use clamped bilinear sampling. The nine-tap average weights the center and axial samples by 1 and diagonal samples by 0.75 (total weight 8). The output is `height + 0.5 * (height - average) * strength * mask`.

The operation reads an immutable snapshot, including when the mask aliases the destination, and publishes atomically. Invalid inputs and float overflow leave the destination unchanged. Heights remain in native caller units without Unity packing or saturation. Filtering across tiles requires a supplied neighbor halo and cropping. Execution is synchronous with exclusive destination access and no retained references, callbacks, RNG or time dependency.

### 地形效果（CPU）

`TerrainEffect.h` 中的原生操作均同步执行，借用输入，不保留指针、回调或外部状态。
目标必须由调用者独占；先计算完整候选再发布，失败不留下部分高度修改。
Squirrel 接口返回标准 Result，`value` 为最终变化的采样数。蒙版与目标尺寸相同，
混合权重在 `[0,1]`；重复效果中的蒙版始终使用调用开始时的数据，允许输入别名。
这批操作没有新 ECS、持久数据格式、可选服务、时钟或 RNG。

- `applySmooth(mask, settings)`，参数对象 `eve.TerrainSmoothSettings()`：
  `radius=10`（非负 texel 间距）、`verticality=0`（`[-1,1]`）、`strength=1`。
  先水平后垂直，每遍为中心加七对加权采样。`verticality=-1` 只降低、`1` 只抬高。
  保留原 Shader 两遍都使用 X 方向 texel 大小的行为：非方形栅格的垂直采样间距
  为 `radius*height/width`。Clamp/Bilinear 边缘寻址；提供 halo 后裁切时须保持
  该采样比例，不能假定不同宽高比的独立块自动无缝。
- `applyRidges(mask, settings)`，参数对象 `eve.TerrainRidgeSettings()`：
  `mixStrength=0.5`、`exponent=16`、`strength=1`、`minimum=0`、`maximum=0.5`、
  `passes=18`。每遍先做水平区间幂变换，再将更新后的值用于垂直区间幂变换，
  与四邻域均值混合，最后按蒙版插值并裁剪。`passes` 是实际应用次数，
  Pcg 编辑器迭代参数 N 对应 `floor(N)+2` 次；0 次保持不变。
  裁剪在蒙版插值之后，蒙版为零也会裁剪超出 bounds 的值。
  世界高度调用者须设置 `minimum/maximum`；默认值对应原 Shader 的标量域。
- `applyTerrace(mask, settings)`，参数对象 `eve.TerrainTerraceSettings()`：
  `count=100`、`bevel=0`、`strength=1`。count 是高度单位的倒数，必须为正。
  高度乘 count 后按最近偶数舍入；只有正方向余量严格大于 `1-bevel` 时保留
  原高度，随后插值。没有加入原源码中已注释掉的 jitter 或未使用的外侧 bevel。
  半整数舍入遵循 [HLSL round 文档](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-round)。
- `applyPower(mask, power)`：指数是 `4-power`，不是 power 本身。
  不自动归一化输入；遮罩内负高度，以及指数非正时的零高度，返回失败以避免
  未定义幂运算。零权重样本不求幂。超出有限 float 范围也拒绝整个操作。
- `applyHeightCurve(mask, curve, minimum, maximum)`：曲线为非空一行 LUT，
  使用 `smoothstep(minimum,maximum,height)` 和 Clamp/Bilinear texel-center 采样。
  曲线值乘 `maximum-minimum` 后与原高度按蒙版插值；保留原公式不加回 minimum
  的行为。minimum 必须小于 maximum。
- `applyHeightMix(local, global, settings)`，参数对象 `eve.TerrainHeightMixSettings()`：
  `minimum=0`、`maximum=1`、`midpoint=0.5`、`strength=0.5`、
  `clipMinimum=0`、`clipMaximum=0.5`。增量为
  `(local-midpoint)*(maximum-minimum)*strength*global`，加到原高度后裁剪。
  local 为有限标量，可超出 `[0,1]`；global 和 midpoint 为单位范围，strength
  可大于 1（原编辑器允许到 2）。允许 minimum 等于 maximum，表示零高度跨度。
  global 为零仍执行最终裁剪。

```squirrel
local target = procgen.newHeightmap(33, 33).value;
local mask = procgen.newHeightmap(33, 33).value;
for (local y=0; y<33; ++y) for (local x=0; x<33; ++x) {
    target.setHeight(x, y, x == 16 && y == 16 ? 0.5 : 0.0);
    mask.setHeight(x, y, 1.0);
}
local smooth = eve.TerrainSmoothSettings();
smooth.radius = 1.0;
assert(target.applySmooth(mask, smooth).ok);
local terrace = eve.TerrainTerraceSettings();
terrace.count = 100.0;
assert(target.applyTerrace(mask, terrace).ok);
```

所有算子保留调用者传入的标量域，不自动模拟 Unity 的高度纹理打包。非线性
效果若要与原参数对应，调用方必须提供对应的高度标量域和裁剪范围。这里处理
已光栅化的整个操作区域；旋转笔刷、范围外保留及多瓦片的上下文仍由上层提供。
CPU 使用固定采样顺序和 double 中间值，float 存储；跨平台/CPU-GPU 比较需使用
数值容限，不承诺位级一致。这些接口尚不能作为原版 GPU 或可视化验收的证据。

### 八邻域热侵蚀

`ProcgenHeightmap.applyThermal(sediment, settings)` 对接收高度图和指定泥沙累计图
同时计算，再统一发布；返回标准 Result，`value` 是任一输出发生变化的网格数。
C++ 入口为 `TerrainErosion.h` 中的 `applyTerrainThermal`。两个目标必须是尺寸
相同的不同对象，由调用者独占；高度为非负有限值，泥沙允许有限的负值。
输入或中间结果无效、浮点溢出、内存分配失败，都不发布任何一个输出。

`eve.TerrainThermalSettings()` 字段：`spacingX=1`、`spacingZ=1`、`heightScale=1`、
`reposeSlope=11.430052`、`dt=0.00025`、`iterations=3`。间距和高度比例严格为正；
坡度阈值、dt、迭代数非负。reposeSlope 是休止角的正切，不是角度值。
例如默认值约为 tan(85°)。dt 是调用者注入的热侵蚀子步时长；对齐 Pcg 的
组合模拟时，它等于 thermalTimeDelta * hydroTimeDelta。没有隐式时钟或 RNG。

每个子步读取旧高度快照，用 Clamp 邻域的四个正向样本和四个对角样本求坡度。
仅绝对坡度严格大于 reposeSlope 的高度差参与求和；对角权重为 0.707，正向
权重为 1。差值先乘 heightScale。移动量为 `clamp(dt*sum/16,-height/2,height/2)`，
从高度扣除并加到泥沙图中。泥沙正值表示移除，负值表示加回，不能将它直接
解释为非负悬浮泥沙浓度。零高度格子的移动上限也为零，这是原公式的行为。

本接口保留原 Thermal.compute 的标量语义，不替换现有 TerrainPipeline 的
保守 talus 重分配。高度与泥沙之和在每格保持平衡（受 float 舍入影响），不
承诺仅高度图的总质量守恒。重复调用可传回这两张输出继续迭代。无保留引用、
回调或跨帧可变状态；同步调用期间不能并发观察或修改任一目标。仍不代表完整
水力/热侵蚀联合管线或 Unity GPU 对照已经通过。

### 水流状态与分阶段计算

`eve.TerrainWaterField()` 创建一个独占拥有水深、速度和四向通量的空状态。
`reset(width, height, depth)` 返回 Result，成功后重置网格及均匀水深，并清空速度
和通量；尺寸乘积必须不超过 int 上限。`getWidth()` / `getHeight()` 返回尺寸。
`advance(terrain, settings)` 推进一步，成功返回任意字段发生变化的网格数。
它只借用地形，既不修改也不保留地形引用，暂不进行泥沙溶解或沉积。

`sample(x,z)` 返回 Result，成功 value 是独立的值记录，包含 `depth`、`velocityX`、
`velocityZ`、`fluxRight`、`fluxLeft`、`fluxBottom`、`fluxTop`，不是内部缓冲区引用。
越界或空状态查询明确失败。C++ 类型为 TerrainWaterField，内部使用 Pimpl；
禁止复制，可移动，移出后的对象为空且可 reset。脚本持有状态直到释放对象，
无需外部注册或跨域 Link。reset/advance 要求独占访问；空闲时才允许只读查询。

`eve.TerrainWaterSettings()` 提供以下有限浮点参数：

- `spacingX=1`、`spacingZ=1`、`heightScale=1`、`waterScale=1`，必须为正。
- `dt=0.05`，非负，0 表示不改变状态；没有隐式时钟或 RNG。
- `precipitation=0.000004`、`evaporation=0.000004`，非负。
- `flowAcceleration=-0.00049`，有符号值，对应原调用方的 flowRate*gravity。

参数是 Shader 接收到的值，不隐式重复 UI 转换。Pcg 调用方对 UI 雨量/蒸发
乘 1e-5，对 flowRate 乘 1e-4 再乘 gravity，对 simulationScale 乘 0.001
再与 terrain texel spacing 相乘。保留原公式的流量符号和 clamped 邻域。

计算明确分为完整通量场、速度与水深更新两个阶段，避免源 GPU 核同次 dispatch
内读取尚未完成的邻格通量。平均水深严格为零时速度定义为零。保留原源码最后
以旧水深加降雨减蒸发覆盖输运水深的行为，因此它不是保守浅水模拟器。
内部 double 中间计算、float 存储，固定阶段顺序；不承诺与原版存在竞争读写的
GPU 核位级等价。任意输入错误或溢出均不提交已算出的局部通量/速度/水深。

```squirrel
local flow = eve.TerrainWaterField();
assert(flow.reset(terrain.getWidth(), terrain.getHeight(), 0.0).ok);
local water = eve.TerrainWaterSettings();
water.precipitation = 0.000008;
assert(flow.advance(terrain, water).ok);
local sample = flow.sample(0, 0);
assert(sample.ok);
print(sample.value.depth + "\n");
```

水力侵蚀仍需接入泥沙、热侵蚀调度与诊断层；不能以此水流阶段宣称完整水力模拟
已经移植，或替代渲染水面的波浪/反射/折射功能。

`eve.generateTerrainWaterFlowMap(target,source,settings)` 对应 Pcg 独立的旧版
`WaterFlowMap.CreateWaterFlowMap`，不复用上述水流状态。`TerrainWaterFlowMapSettings` 字段为
`dropletVolume`、`absorptionRate`、`smoothIterations`。它从每个非边缘 texel 投放水滴，按 X-major、
Z-minor 顺序查找八邻域中严格更低的第一个点；没有低点时只抬高私有地形副本。每一步总是沉积完整
absorptionRate，因此最后一步可以超过初始体积。完成后按原 `HeightMap.Flip` 交换 X/Z 轴，再执行
顺序相关的原地四邻域平滑并钳制到 [0,1]。矩形输入要求目标尺寸为 source height × source width。
输入无效、数值不再前进或溢出时目标保持不变；API 无随机数、隐式时间或保留引用。

`eve.generateTerrainVelocityFlowMap(target,source,iterations)` 对应同一 Pcg `HeightMap` 类型的另一项
`FlowMap` 运算。它从每格 `0.0001` 水量开始，以源码固定 `TIME=0.2` 累积左、右、下、上四向通量，
限制单步总流出不超过当前水量，更新邻格流入后从最终通量计算二维速度幅值，并按整图 min/max 归一化；
范围小于 `1e-12` 时输出全零。目标必须同尺寸，可以与 source 为同一对象；负迭代或任何非有限中间值
返回失败并保留目标。该 API 与 WaterFlowMap 水滴追踪及 `TerrainWaterField` 水力状态均为独立合同。

### 去除高度图台阶

`eve.analyzeTerrainTerraces(classes,source,settings)` 与
`eve.removeTerrainTerraces(target,source,classes,settings)` 移植 Pcg 4.2.2
`HeightmapTerraceRemover` 的两阶段工具。分类值为 0=Black、1=Red、2=Green、3=Blue，优先级与源码一致：
terrace、flat、mountain、slope terrace、black。边缘保持 Black。

`TerrainTerraceRemovalSettings` 暴露 `perlinScale`、`perlinStrength`、
`slopeTerraceThreshold`、`flatThreshold`、`verticalGradientThreshold`、
`minimumTerraceThreshold`、`maximumTerraceThreshold`、`excludeRed`、`excludeBlack`、
`terrainWorkflow` 和 `noiseSeed`。处理顺序固定为：选中区域 3×3 平均、梯度衰减 Perlin 噪声、
`1/2/1` 三向滤波、sigma=1 的 5×5 高斯模糊。`terrainWorkflow=true` 保留源码
`0.00042/0.0035` 的噪声强度比例。EVEngine 使用具名 seed 的原生 Perlin 实现，保持确定性；Unity
`Mathf.PerlinNoise` 的私有排列不作为跨引擎逐像素合同。

两个调用都先完成验证与候选计算再发布。apply 允许 target 与 source 相同，但分类图必须是独立对象；
失败不会留下部分平滑结果，也不保留任何输入引用。

### HeightMap 曲率与坡向派生图

`eve.generateTerrainHeightmapCurvature(target,source,mode)` 精确对应 Pcg `HeightMap.CurvatureMap`，mode
沿原枚举为 0=Average、1=Horizontal、2=Vertical。它以归一化像素间距计算 dx/dy、dxx/dyy、dxy，按
Pcg 水平/垂直曲率公式限制到 ±10000，再映射至 [0,1]。平坦或退化分母按源码得到中值 0.5。

`eve.generateTerrainHeightmapAspect(target,source,mode)` 对应 `HeightMap.Aspect`，mode 为 0=Aspect、
1=Northerness、2=Easterness。完整坡向除以 360；北向性和东向性分别用 cosine/sine 映射到 [0,1]。
实现保留源码自定义 Sign 和边缘 clamp 规则，包括平坦样本的 0.5 full-aspect 结果。

两项都要求至少 2×2 的有限同尺寸栅格，允许 target/source 别名，先读取完整旧快照并在成功后原子发布；
非法枚举、尺寸或数值失败不会修改目标。它们是 `HeightMap.cs` CPU 派生图，不等同于 shader 风格的
`generateCurvatureMask` 径向模糊操作。

### HeightMap 邻域修复

`eve.filterTerrainHeightmapNeighborhood(target,source,radius,mode)` 对应 Pcg `HeightMap.DeNoise`、
`GrowEdges` 与 `ShrinkEdges`，mode 为 0、1、2。DeNoise 只处理拥有完整邻域的内部像素，把离群值限制到
邻居最小值与最大值之间；GrowEdges/ShrinkEdges 在边界跳过越界邻居，并把像素向邻域极值移动一半。

实现刻意保留 Pcg 的 X 优先、Y 次优先原地遍历语义：同一调用中靠后的像素会读取已经更新的靠前像素。
target 可以与 source 相同；实现先复制输入快照，在全部验证成功后发布，因此非法半径、枚举、尺寸或非有限值
不会留下部分结果。DeNoise 要求 radius 至少为 1。

`eve.smoothTerrainHeightmap(target,source,iterations)` 对应四邻域 `HeightMap.Smooth`，保留边缘 clamp、
[0,1] clamp 和每次写回立即影响后续 X→Y 样本的行为。`eve.smoothTerrainHeightmapRadius(target,source,radius)`
对应缩放滑窗版本，像源码一样把 radius 至少提升到 5，并保留每列第一个完整窗口只计算而不写回的行为。
`eve.convolveTerrainHeightmap(target,source,kernel)` 对应原地卷积；只有完整覆盖 kernel 的像素会改变，结果
限制到 [0,1]，近零 divisor 使用 1。为避免 Unity 方法对畸形数组的越界行为，原生 API 明确要求有限、
奇数边长的正方形 kernel。

`eve.generateTerrainHeightmapSlope(target,source)` 精确对应 `HeightMap.SlopeMap`：使用归一化像素间距、
边缘重复采样、源码固定 0.5 高度比例和 `g/sqrt(1+g²)` 映射。它与旧 `GetSlopeMap` 的前向差分不同；
后者在最后一行/列直接访问越界，因此没有作为安全公共 API 暴露。

`eve.quantizeTerrainHeightmap(target,source,divisor)` 对应标量 `HeightMap.Quantize(float)`，使用 Unity
`Mathf.Round` 的 midpoint-to-even 规则。除数必须有限且非零；别名安全，失败不修改 target。

### HeightMap 栅格代数

`eve.applyTerrainHeightmapScalarArithmetic(target,source,operand,operation,clampResult,min,max)` 和
`eve.applyTerrainHeightmapRasterArithmetic(target,source,operand,operation,clampResult,min,max)` 覆盖 Pcg
的 Add、Subtract、Multiply、Divide 及全部 Clamped 重载；operation 依次为 0–3。栅格尺寸不同时，右操作数
按 Pcg 的 `x/targetWidth`、`y/targetHeight` 坐标和源尺寸双线性采样。原生合同拒绝除零和非有限结果，
避免把 C# Infinity 写入后续地形管线；错误时 target 保持不变。

`eve.lerpTerrainHeightmap(target,source,values,mask)` 对应 `HeightMap.Lerp`，values 和 mask 可各自采用不同
尺寸并按相同规则重采样；插值量按 `Mathf.Lerp` 限制到 [0,1]。三项均允许 target 与任一输入别名。

`eve.transformTerrainHeightmap(target,source,transform,parameter)` 覆盖基础 `HeightMap.Invert`、
`Normalise`、`Power`、`Contrast`，transform 为 0–3。Normalise 对常量图保持原值；Power 的 parameter 是
源码直接指数，不是 terrain effect 中的 `4-power`；Contrast 使用 `(h-0.5)*parameter+0.5` 且不隐式 clamp。

`eve.copyTerrainHeightmap(target,source,mode)` 对应 `HeightMap.Copy`，mode 为 0=AlwaysCopy、
1=CopyIfLessThan、2=CopyIfGreaterThan；条件比较以 target 调用前的当前值为基准。异尺寸 source 使用 Pcg
归一化坐标重采样。`eve.copyTerrainHeightmapClamped(target,source,min,max)` 对应 CopyClamped，先采样再限制。
两项都以 target 尺寸为输出权威，允许 source/target 别名，非法枚举或区间失败时不修改 target。

`eve.flipTerrainHeightmap(target,source)` 对应 Pcg `HeightMap.Flip` 的矩阵转置，会把 W×H 输出调整为
H×W；它不是水平或垂直镜像。实现先复制 source 并构造完整候选，因而支持原地调用。

`eve.measureTerrainHeightmap(source,measure)` 统一提供当前样本的 Minimum、Maximum、Sum、Average、
BaseLevel（measure 0–4）。Sum/Average 保留 Pcg X→Z 顺序的 float 累加；BaseLevel 从 0 开始扫描四条边，
因此全负边界仍返回 0。每次调用直接读取权威样本，不要求调用者维护 Unity 的 dirty 统计缓存。

`eve.quantizeTerrainHeightmapTerraces(target,source,startHeights,curves)` 对应 Pcg
`Quantize(float[],AnimationCurve[])`。startHeights 是 N×1 严格递增栅格；curves 是 W×N 栅格，每行以
线性采样表示一条 Unity 曲线。区间仍从最高 terrace 向下搜索，因此公共边界归入较高层。输入高度必须由
首个 start 到 1 完整覆盖；这把 Unity 的负索引异常转成原子 InvalidArgument。

`eve.measureTerrainHeightmapSlope(source,x,y,mode)` 暴露 Pcg 三个同名但不同单位的点查询：mode 0 是
整数格点向 +X/+Z 的梯度长度；mode 1 是归一化坐标、0.9 texel 中心差并乘 10000 后限制到 [0,90]；
mode 2 是归一化坐标四向绝对高差平均再乘 400。整数模式要求右/下邻居存在，归一化模式要求坐标在 [0,1]。

`eve.fillTerrainHeightmap(target,value)` 对应 `SetHeight` 并将统一高度限制到 [0,1]；
`setTerrainHeightmapSafe` 把越界整数坐标限制到最近边界后写入。`setTerrainHeightmapRow` 的 row 固定 X、
需要 target-height 长度的一维 strip；`setTerrainHeightmapColumn` 固定 Z、需要 target-width strip，与 Pcg
命名和数组方向一致。`resetTerrainHeightmap` 把对象清为 0×0。所有非 reset 写入均先验证并原子发布。

`eve.sampleTerrainHeightmapSafe(source,x,z)` 将整数坐标限制到最近边界，对应 `GetSafeHeight`。
`sampleTerrainHeightmapNormalized(source,x,z)` 接受 [0,1] 坐标并严格采用 Pcg 的 `x*width,z*depth`
双线性规则；这与 EVEngine `sampleBilinear` 的像素坐标合同不同。`heightmap.hasData()` 验证正尺寸及完整
样本存储，`heightmap.isPowerOfTwo()` 要求两个维度均为正二次幂。

Pcg 的 MinVal/MaxVal/SumVal、UpdateStats 和 dirty 标志是同一 Unity 编辑器缓存协议。EVEngine 的
measure API 每次读取权威样本，因此不会暴露可能过期的统计缓存或手工 dirty 开关。元数据由带
schema/version 的 TerrainAsset、TerrainFile 或上层资产定义拥有，不附着无结构字节到通用 Heightmap。


### 泥沙与联合侵蚀

`heightmap.applyLegacyDistributedErosion(settings)` 对应 `HeightMap.Erode`：只扫描内部格点，若最大
四向下降量位于阈值区间，就从中心移走其一半，并按所有正下降量的比例同步分配到四邻域。
`TerrainLegacyDistributedErosionSettings` 提供 minimumThreshold、maximumThreshold 和 iterations。

`heightmap.applyLegacySteepestErosion(hardness, settings)` 对应活跃的 `HeightMap.ErodeThermal`：按 X 外层、
Z 内层原地扫描，严格依次比较下、左、右、上邻居，将最大下降量的一半乘 `(1-hardness)` 后移给首个
最陡邻居。hardness 可使用不同尺寸，并按 Pcg 的归一化宽深双线性规则采样。
`TerrainLegacySteepestErosionSettings` 提供 iterations、talusMinimum 和 talusMaximum。
两个调用都原子发布、使用显式迭代数，且不依赖隐式时钟或随机数。

`eve.TerrainLegacyHydraulicSettings()` 与
`heightmap.applyLegacyHydraulic(sediment, hardness, rain, settings)` 对应旧版
`HeightMap.ErodeHydraulic`。设置包含 `iterations`、正整数 `rainFrequency` 与 [0,1]
`sedimentDissolveRate`。每隔 rainFrequency 轮加入 rain；四向通量保留跨轮状态并使用固定 TIME=0.2；
更新水深后，按八邻域严格下降量与 `(1-hardness)` 搬运泥沙，地形限制到 [0,1]，泥沙保留有符号值。
该调用先快照输入并私下完成所有迭代，成功才同时发布地形与泥沙；无隐式时钟或随机数。
它与下面的 GPU 来源 Water→Sediment→Thermal 管线及 `WaterFlowMap` 水滴累积图是三套独立合同。

`eve.TerrainSedimentSettings()` 提供 `effect=-1`、`depositRate=0.00004`、
`bankDeposit=1`、`bedDeposit=5`。这些是实际计算系数，不是 Pcg UI 的缩放前数值。

- `heightmap.applySediment(sediment, velocityX, velocityZ, waterSettings, sedimentSettings)`
  返回结构化 Result，成功值为两个输出中发生变化的格子数。
- `waterField.advanceHydraulic(heights, sediment, waterSettings, sedimentSettings, thermalSettings, iterations)`
  顺序执行水流、泥沙、热侵蚀，并返回三个所有者中发生变化的格子数。
  任一轮失败均保留调用前的地形、泥沙和水流状态。

所有栅格必须同尺寸且有限，地形非负；地形与泥沙必须是不同对象。
调用同步执行、无回调，调用方负责线程串行访问；不保留传入对象的引用。
独立泥沙操作允许速度栅格与输出别名，始终读取调用前快照。
`waterSettings.dt` 控制沉积和回溯；热侵蚀使用独立的 `thermalSettings.dt`，
若匹配 Pcg 调度，应由调用方先乘水流时间步。

该泥沙算子保留参考源码的特殊权重、混合坐标索引与叠加行为，
不是标准双线性输运：无速度且无沉积时非负泥沙每轮翻倍，即使 dt 为零。
源码硬度固定为零，溶解项无效，因此不暴露不起作用的溶解系数。
平坦地形的未定义归一化明确取坡度零，越界整数采样取零。
联合调度始终读取最新完整阶段并发布最后一轮，未复制参考 C# 固定输出缓冲区
及重置热侵蚀缓冲索引的问题；不承诺与原 Unity GPU 执行逐位相等。


`waterField.exportChannel(target, channel)` 将一个完整水流通道原子写入同尺寸、
有限的目标栅格，返回变化格子数的 Result；不调整尺寸，不保留引用，失败不修改目标。
通道编号为 0 深度、1 X 速度、2 Z 速度、3 右向通量、4 左向通量、5 下向通量、6 上向通量。
输出保留原始符号和单位，不取绝对值、不归一化。调用需独占目标且禁止并发修改水流场。
Pcg 的 SimpleHeightBlend 读取红通道，因此原 WaterVelocity 遮罩对应 1，
WaterFlux 对应 3，而不是速度长度或总通量。导出栅格可直接传给现有
`transformMask` / `blendMask` 操作；完整 PaintContext UV 与原始地形混合仍需上层组合。


### 空间笔刷混合

`eve.TerrainBrushBlendSettings()` 配置独立的地形与笔刷仿射 UV。
地形字段为 `heightXX`、`heightXZ`、`heightZX`、`heightZZ`、
`heightOffsetX`、`heightOffsetZ`；笔刷字段为 `brushXX`、`brushXZ`、
`brushZX`、`brushZZ`、`brushOffsetX`、`brushOffsetZ`。
两个矩阵默认单位矩阵，偏移默认零，`strength=1`。
坐标公式为 `(XX*u+XZ*v+offsetX, ZX*u+ZZ*v+offsetZ)`，输入 UV 是目标像素中心。

`target.blendBrush(oldHeights, newHeights, brush, settings)` 返回变化格子数的 Result。
三个输入可独立分辨率，采用 Clamp/Bilinear 像素中心采样；笔刷 UV 超出闭区间
[0,1] 时输出采样后的旧地形，区间内输出 `lerp(old,new,strength*brush)`。
权重不钳制，支持参考公式的外插；输入已是原生标量，不做 Unity 高度打包。
所有栅格和系数必须有限，溢出不提交；目标可与输入别名，读取调用前快照。
调用同步、无回调、不保留引用，调用方需独占目标并保证输入不被并发写入。
这是 SimpleHeightBlend 的空间组合步骤，侵蚀遮罩的反转和后续滤镜仍需显式组合。


### 强度滤镜与侵蚀遮罩组合

`target.applyStrength(source, curve, mode, strength, invert)` 对同尺寸输入先查一行曲线，
再按 invert 取 `1-curveValue`，然后计算模式结果，最后从输入向结果插值。
mode 为 0 替换、1 最大值、2 最小值、3 相加、4 相减。参考 shader 将 0 命名为
Multiply，但实际公式是替换曲线值，不是相乘。strength 必须在 [0,1]；结果不钳制。

`target.generateErosionMask(oldHeights, erosion, brush, spatial, curve, mode, strength, userInvert)`
先执行空间笔刷混合，再执行强度滤镜，按 Pcg 侵蚀分支将滤镜反转设为 `!userInvert`。
erosion 是已计算的泥沙或通过 exportChannel 导出的右向通量/X 速度；此调用不推进模拟。
spatial 为 TerrainBrushBlendSettings，曲线为有限非空一行栅格。
两步整体原子提交，后一步失败也不会留下前一步结果；返回变化格子数的 Result。
输入与目标允许别名，使用调用前快照；同步独占目标、输入不可并发修改，不保留引用或调用回调。
该组合采用原生标量，仍需调用方根据世界/地形范围提供 UV 映射和模拟参数。


### Grow/Shrink 遮罩

`target.growShrinkMask(source, curve, distance)` 返回变化格子数的 Result。
source 与 target 同尺寸且有限，curve 为有限非空一行强度曲线。
distance 是世界距离除以操作范围所得的 UV 半径，正值增长、负值收缩；
`2*abs(distance)*sourceWidth` 必须不超过 int 最大值以表达采样索引。
两个方向步长均为 `1/sourceWidth`，X 外层、Z 内层遍历；每个候选值先按径向
smoothstep 与当前累积值混合，再做最大/最小选择，因此不是普通形态学滤波。
越界候选跳过，采样使用 Clamp/Bilinear 像素中心；最终经过 curve，距离零也不省略曲线。
整数索引和 double 偏移明确定义采样序列，不复现 shader 浮点累加误差。
最坏采样成本随半径增长；实现跳过确定越界区间，但不改变区间内的访问顺序。
失败保留目标，别名输入读取快照。同步独占目标、不可并发修改输入，无回调或引用留存。


### 曲率遮罩

`eve.TerrainCurvatureSettings()`：`radius=0.001` 为 UV 模糊半径，`worldUnits=-400`
缩放有符号高差，`intensity=0.7` 为正幂指数，`steps=32` 为每方向样本数，
`directions=16` 为方向数。半径非负，采样数为正，乘积必须小于 INT_MAX。

`target.generateCurvatureMask(input, heights, curve, settings, mode)` 返回变化格子数的 Result。
目标与 input 同尺寸，heights 可独立分辨率，curve 为有限非空一行强度曲线。
中心及各径向样本平均后，计算 `clamp(abs(pow((origin-blur)*worldUnits,intensity)),0,1)`，
再查曲线并按 mode 混合：0 相乘、1 最大、2 特殊 Smaller、3 相加、4 相减。
Smaller 的原公式是 `filter < 1-input ? filter : input`，可能增加输入，不能当作普通 min。
该 shader 不使用 strength 或 invert。负底数的非整数幂显式失败；没有提前对底数取绝对值。
正幂溢出的无穷幅值按曲率饱和公式变为 1，最终标量输出溢出则不提交。
径向循环以整数索引固定样本数，不承诺原 shader 浮点循环的逐位等价。
所有栅格和系数必须有限。输出别名读快照，失败原子保留目标；同步独占目标，
输入禁止并发修改，不保留引用、无回调。径向模糊设计的 MIT 归属与许可保留在实现中。


### 凹凸遮罩

`eve.TerrainConcavitySettings()` 提供 `featureSize=10`（高度纹素距离）与
`concavity=1`（有符号凹凸系数）。featureSize 为正有限且可表示为 uint32；
邻域偏移取整数截断值，边缘衰减使用未截断值。

`target.generateConcavityMask(input, heights, curve, settings, mode)` 返回变化格子数的 Result。
目标与 input 同尺寸，heights 的坐标映射在两个轴上都使用 heightWidth/inputWidth，
映射坐标须可表示为 uint32。曲线是有限非空一行数组，读取
`floor(clamp(value,0,1)*(curveWidth-1))`，不做双线性曲线采样。
mode 为 0 相乘、1 最大、2 最小、3 相加、4 相减，未加入 strength 或 invert。
原计算的无符号邻域运算及包含 width/height 的上界保持；越界纹素读零，
零长度梯度定义为零。边缘衰减后再倍增、钳制并查曲线。
使用 double 中间值，不承诺原 GPU 逐位一致。所有输入有限，失败不提交目标；
别名读调用前快照，同步独占目标，无回调或引用留存，输入不得并发修改。


### 图像遮罩

`eve.TerrainImageMaskSettings()` 提供 `offsetX=0`、`offsetZ=0`、`scaleX=1`、
`scaleZ=1`、`rotation=0`（弧度）、`tiling=false`，以及选择颜色 `red=1`、
`green=1`、`blue=1` 和 `accuracy=0`。缩放不能为零，accuracy 在 [0,1]，所有值有限。

`target.generateImageMask(input, red, green, blue, alpha, curve, settings, filter, mode)`
接收四个同尺寸有限 RGBA 标量平面，独立于目标分辨率；目标和 input 同尺寸。
返回变化格子数的 Result，目标可与任意输入别名，所有失败均不提交。
filter 为 0 RGB 最大通道（不是亮度）、1 Lab 颜色选择、2 红、3 绿、4 蓝、5 Alpha。
mode 为 0 相乘、1 最大、2 最小、3 相加、4 相减，不额外使用 strength/invert。

图像在目标像素中心做中心偏移、缩放和旋转后双线性采样；平铺保留源码负整数
映射到 1 的约定。不平铺时范围外筛选值为零，再经过一行 curve，因此最终未必为零。
颜色选择保留源码 sRGB→XYZ→Lab 与 CIE76 距离，距离钳制到 [0,100]；
匹配条件严格为 `difference < (1-accuracy)*100`，命中后得到
`1-difference/100*accuracy`。accuracy=1 连相同颜色也不命中。
传入色彩数值不做隐式解码或额外色彩空间转换，Alpha 不参与 Lab 距离。
调用同步独占目标，输入不得并发修改；无回调和引用留存，图像解码/GPU 资源仍由调用方管理。


`eve.generateTerrainImageMaskFromImage(target, input, imageData, curve, settings, filter, mode)`
是完整宿主的 ImageData 适配入口，返回变化格子数的 Result。目标可与 input/curve 别名。
它按 ImageData 当前像素格式读取 RGBA 浮点平面，再调用相同图像遮罩核心；
不额外做 sRGB 转换或垂直翻转。输入须为可读取且存储完整的图像，非有限像素、
无效设置和尺寸不匹配都不会修改目标。同步独占目标，图像及输入不得并发修改；
不保留 ImageData、像素指针或 GPU 资源。完整宿主需要 image 模块，核心裁剪配置
继续使用 generateImageMask 的四平面接口，不隐式提供图像解码回退。

### 碰撞遮罩栈

`eve.TerrainCollisionMaskStack()` 接收调用方已经从场景、树或对象缓存烘焙出的有限标量栅格。
`addLayer(mask,type,active,invert)` 深拷贝一层；type 为 0=RadiusTree、1=RadiusTag、
2=LayerGameObject、3=LayerTree。`getLayerCount()` 包含禁用层，`clear()` 清空所有层。

`apply(target,input,curve,mode)` 对每个输出像素按 Clamp/Bilinear 重采样所有 active 层，从白色开始依次
取最小值，随后应用一行强度曲线和 ImageMask blend。Radius 类型在 `invert=true` 时反转；Pcg 的
Layer baked texture 以白色表示占用，因此 Layer 类型使用相反的 invert 约定。目标与 input 尺寸一致，
各层可使用不同分辨率；输入会被快照，允许 target alias。非法栅格、类型、模式或有限范围失败时不
修改 target。栈不持有场景对象或渲染缓存；几何查询和缓存失效由场景/provider 负责。

### 多边形笔刷遮罩

`eve.TerrainPolygonMask()` 按顺序拥有控制节点。`addNode(worldX,worldZ,radius,strength)` 复制有限世界
坐标、正半径和 strength 元数据；`getNodeCount()` 返回节点数，`clear()` 清空。`rasterize(target,brush,
originX,originZ,spacingX,spacingZ,type)` 将节点绘制到目标网格，type 为 0=Open、1=Closed。Open 只绘制
节点笔刷；Closed 在至少三个节点时先用 Pcg `SH_PolygonFill.shader` 相同的奇偶射线规则填白多边形，
再按节点顺序叠加笔刷。每次叠加钳制上限到 1，并把小于 0.25 的结果清零。笔刷红通道由 Heightmap
标量表示，节点方形范围外明确为零，范围内使用 Clamp/Bilinear 纹理中心采样。

Pcg 4.2.2 的 `PolyMask.cs` 会把 `PolyMaskNode.Strength` 传给 `ApplyBrushStroke`，但该函数和
`SH_AdditiveBrush.shader` 都不设置或读取 strength；EVEngine 保留字段以便资产转换，同时按该版本
可观察行为不以它缩放笔刷。地形射线吸附、编辑器选择、可视化网格和缓存失效属于编辑器/provider，
核心不持有场景对象。输出先在私有缓冲完成后一次发布，非法网格、类型、非有限值或间距失败不修改目标。

### WorldBiome baked-mask cache

`eve.TerrainBakedMaskCache()` 以 `(terrainId,maskGuid)` 为稳定复合键拥有已烘焙有限 Heightmap。
`store` 深拷贝或原子替换条目并标记 fresh；`markDirty(maskGuid)` 使所有地形上的同 GUID 条目显式 stale；
`copyMask` 仅导出 fresh 条目，并按目标分辨率使用 Clamp/Bilinear 采样。missing 与 dirty 都返回结构化
NotFound，且保留旧输出。provider 完成世界图规则重算后再次 `store` 即重建条目。`erase` 删除精确复合键，
`clear` 清空全部缓存，`getEntryCount` 包含 dirty 条目。

这对应 Pcg `GetWorldBiomeMask`/`BakedMaskCache` 的缓存、跨分辨率 Graphics.Blit 与 GUID 失效边界。
世界图规则求值和磁盘资产读取属于上层 provider；缓存不持有场景、Terrain、GPU texture、回调或文件句柄。
provider 先销毁时 dirty/missing 明确失败，cache 先销毁时只释放其值副本；热重载通过 markDirty 后重新 store，
不会把旧图无声当作当前结果。

`ProcgenHeightmap.applyGlobalSpawnerMask(input,source,curve,settings,mode)` 对应 ImageMask 的
`GlobalSpawnerMaskStack` 分支。source 是被引用 Spawner 的完整蒙版栈输出；接口把标量复制为通道语义，
强制选取 Red，再复用 ImageMask 的 UV 偏移、缩放、旋转、tiling、强度曲线和五种 blend。settings.filter
不会改变标量结果，其余字段按 `generateImageMask` 验证。调用只借用 source，不持有另一个 Spawner 或
RenderTexture；目标与 input 可别名，失败时不发布部分结果。

### NoiseMask

`ProcgenHeightmap.generateNoiseMask(input,curve,settings,type,mode)` 生成 NoiseMask 并立即按强度曲线与
五种 ImageMask blend 合并。type 为 0=Perlin、1=Value、2=Billow、3=Ridge、4=Voronoi。
`TerrainNoiseMaskSettings` 提供 `translationX`、`translationZ`、`scaleX`、`scaleZ`、弧度 `rotation`、
`octaves`、`amplitude`、`frequency`、`persistence`、`lacunarity`、`warpIterations`、`warpStrength`、
`warpOffsetX`、`warpOffsetZ` 与显式 `seed`。
octaves 和 warpIterations 支持 Pcg fBm 风格的分数最后一步，范围为 `[0,16]`；默认值来自包内
PcgNoiseSettings/FbmFractalType。输出按固定扫描与固定 hash 流确定，不读取帧时钟或全局随机状态。

本实现复用 EVEngine `NoiseField` 的 CPU hash/Perlin 基础，因此保留 Pcg 的五类形态、TRS、fBm 与 warp
参数语义，但尚不声明与 Unity HLSL `sin` hash 逐像素相同；跨后端比较采用数值与形态容限。所有输入
先验证并在私有缓冲计算，非法枚举、零 scale、非有限参数或溢出不修改目标。

### Smooth ImageMask

`ProcgenHeightmap.smoothMask(input,verticality,blurRadius)` 对应 ImageMask 的 Smooth 分支。它按
`SmoothHeight.shader` 固定七对权重先水平再垂直处理；`verticality=-1` 只降低，`0` 取加权平均，`1`
只抬高。垂直 pass 保留原 shader 使用 X texel size 的行为，非方形栅格也按该约定采样。shader 虽读取
HeightTransformTex，但最终返回值没有使用它，因此该入口不接受强度曲线或额外 blend。


### 世界坐标到笔刷 UV

`eve.TerrainSampleGrid()` 描述高度纹理的样本中心，`width=1`、`height=1`；
`setOrigin(x,z)` 设置首样本世界坐标，`setSpacing(x,z)` 设置正采样间距，默认均为 1。

`spatial.configureWorld(world, contextWidth, contextHeight, heightGrid)` 将世界配置转换为
TerrainBrushBlendSettings 的两个 UV 矩阵，返回变化系数数目的 Result。
world 是 TerrainStampSettings：其 grid 描述目标首样本与间距，center/size/rotation
描述笔刷世界矩形；高度操作、amplitude/baseHeight/blendStrength 不参与映射。
heightGrid 描述传给 blendBrush 的旧高度纹理，spatial.strength 保持不变。

转换显式处理半像素偏移，使相同网格生成单位高度 UV 映射；旋转以 X/Z 平面逆映射
将世界位置变为笔刷坐标。C++ 先在 double 中计算原点差，再转换为有限 float 系数；
Squirrel 当前是 float VM，传入大绝对坐标前需调用方完成世界原点偏移，不能恢复已丢失精度。
尺寸、间距和笔刷范围必须为正，坐标有限；跨度或系数溢出时不修改 spatial。
调用同步独占 spatial，不保留输入引用，也不会移动地形或修改任何栅格。


### 地形生成会话

`eve.TerrainGenerationSession()` 创建独立、初始为空的地形会话。支持记录原生印章和下述七种标量地形效果。
`reset(baseline)` 复制有限基线并清空历史；`stamp(stamp, settings, operation, localMask, globalMask)`
复制印章、设置和两个遮罩为命令输入，成功后追加记录。操作编号与 applyStamp 相同。
`copyTerrain(output)` 将权威地形复制到同尺寸有限栅格；修改这个副本不会改变会话。

`undo()`、`redo()` 改变已应用历史前缀并从基线回放，`replay()` 重算当前前缀。
`setOperationEnabled(index, enabled)` 切换命令并重算；`operationEnabled(index)` 返回布尔值 Result。
`getOperationCount()` 包含待重做的后缀，`getAppliedCount()` 包含前缀中的禁用命令。
index 只是会话内索引，不是跨重置/分支的稳定 ID。撤销后成功追加新操作会截断重做后缀，
追加失败不会截断；切换早期命令导致后续溢出时，历史和地形都保持原样。

`setAccess(access)` 返回 void Result，0 为 Editable、1 为 Locked；`getAccess()` 返回当前策略。
锁定拒绝 reset、stamp、undo、redo、replay 和命令开关，允许读取快照和解锁。
其余修改操作返回变化地形样本数 Result，reset 返回基线样本数，失败返回结构化错误。
所有状态由会话单独持有，公共读取为副本；调用在所属线程串行执行，无外部引用留存或回调。
撤销/回放复制候选状态后完整重算，内存和耗时随历史输入大小增长；同一构建下用固定输入顺序
保证浮点重算一致性，不依赖隐式时钟/RNG。不承诺跨平台逐位一致。
这不是现有分块点集 RuntimeGeneration 调度器的替代，也不包含后台线程或其他操作种类。

`snapshotJson()` 写出严格的 `eve.procgen.terrain-generation-session` schema version 1，拥有 baseline、
全部命令输入与设置、enabled 标志、已应用 cursor 和访问策略。`restoreJson(json)` 拒绝未知根字段、
版本、非法枚举、非有限数值、尺寸/数量越界、截断或尾随 payload；它先以全部命令启用验证完整历史，
再按保存的 enabled/cursor 重建最终地形、泥沙和七个水通道，成功后一次交换。Pending 会话和 Locked
目标拒绝恢复；快照不会保存临时 replay 候选，恢复后的调度状态为 Idle。

调用方可用 `beginReplay()` 启动分步重放，再用 `stepReplay(maxOperations)` 按正数命令预算推进。
状态枚举为 0=Idle、1=Pending、2=Completed、3=Cancelled、4=Failed；`getReplayStatus()` 和
`getReplayCompletedOperations()` 可轮询当前生命周期与已访问命令数。Pending 阶段只修改私有候选，
`copyTerrain`、`copySediment` 和 `exportWater` 继续返回最后一次已发布状态；完成全部命令时三类数据
一次发布。`cancelReplay()` 丢弃 Pending 候选，失败也不修改权威状态。Pending 时拒绝历史编辑、访问
策略切换和第二次 begin；取消或终态之后可以重新开始。这提供与 Pcg coroutine 对应的显式可观察
调度边界，但由所属线程逐步驱动，不创建后台 worker，也不跨线程调用脚本。


地形会话还提供 `contrast(mask,strength,featureSize)`、`smooth(mask,settings)`、
`ridges(mask,settings)`、`terrace(mask,settings)`、`power(mask,power)`、
`heightCurve(mask,curve,minimum,maximum)`、`heightMix(localMask,globalMask,settings)`。

`eve.bakeTerrainCurveTexture(output, curve)` 将单行 Heightmap 曲线 LUT 写入单行 ImageData，
对应 Pcg `TerrainToolsUtility.AnimationCurveToRenderTexture`。采样坐标为 `i / outputWidth`；
为保持源实现行为，像素 0 保持透明黑，但返回范围从曲线在 0 的值开始计算。成功 Result 的
`value` 包含 `minimum`、`maximum` 和 `writtenPixels`。输入必须是非空单行图像与有限单行曲线；
失败不会改变输出图像。
这些接口使用对应的 Terrain*Settings 和原生效果公式，复制栅格、曲线与设置作为历史输入；
成功返回变化地形样本数 Result。所有命令共用锁定、禁用、撤销、重做和原子回放契约。
修改调用方的遮罩或曲线不影响已有命令。混合历史重算失败时也不修改命令开关和当前地形。
会话快照记录足以重建水流/泥沙状态的完整命令输入，但仍不记录异步资源操作。


### TerrainGenerationSession 侵蚀历史

`reset(baseline)` 现在同时初始化零泥沙、零水深、零速度和零通量。标量地形命令保留
已有模拟状态；热侵蚀和水力侵蚀使用历史中前一步的状态。所有输出与历史一起原子发布，
撤销、重做、禁用操作和回放覆盖全部通道。共享的内部候选结果仅为不可变快照，没有对外
可写别名。调用者仍须在所属线程串行访问；时间步长和迭代次数显式传入。

- `TerrainGenerationSession.resetSimulation(depth)`：记录可撤销的模拟重置，清零泥沙、
  通量和速度，将水深设为有限非负值；地形不变。需要一次全新侵蚀时先调用它。
- `TerrainGenerationSession.thermal(settings)`：记录热侵蚀，累积泥沙，保留水流状态。
- `TerrainGenerationSession.hydraulic(water, sediment, thermal, iterations)`：记录完整水流、
  泥沙、热侵蚀迭代；复制设置，延续已有模拟状态，不隐式应用 Unity UI 单位换算。
- `TerrainGenerationSession.copySediment(output)`：复制当前有符号泥沙到匹配尺寸的有限高度图。
- `TerrainGenerationSession.exportWater(output, channel)`：复制原始水流标量通道；整数通道
  0=Depth、1=VelocityX、2=VelocityZ、3=FluxRight、4=FluxLeft、5=FluxBottom、6=FluxTop。

新增修改方法均返回 `Result<int>` 的**地形变化样本数**；模拟状态改变但地形不变时可为 0。
输出方法返回输出变化样本数，锁定时仍可读；修改方法在锁定时拒绝执行。
非法输入、后续命令失败或分配异常不会提交部分模拟状态或截断重做历史。
会话 reset 清空历史；resetSimulation 是历史中的命令，两者语义不同。
该实现沿用前文记录的原生侵蚀内核约定，不复刻源 C# 包装层固定输出缓冲索引错误，
不声明与未定义的 GPU 调度行为逐位一致。会话仍无异步世界创建或植被资源历史；分步重放
只调度本会话已拥有的地形/侵蚀命令。


### TerrainDetailLayer 整数草地密度

`TerrainDetailLayer` 是独立的整数细节密度所有者，不是 `Grid2D` 的语义编号层。
一个实例对应一张完整对齐的地形细节图，不持有资源注册表或图形对象。

- `TerrainDetailLayer.reset(width, height, count)`：初始化正尺寸与非负整数数量，返回单元数。
- `TerrainDetailLayer.getWidth()`、`TerrainDetailLayer.getHeight()`：查询尺寸，空实例返回 0。
- `TerrainDetailLayer.sample(x, z)`：返回 `Result<int>` 的数量副本。
- `TerrainDetailLayer.apply(fitness, settings, mode, seed)`：从匹配尺寸的有限 Heightmap 生成密度，
  返回变化单元数。mode 为 0=Replace、1=Add、2=Remove；seed 为显式有符号 32 位随机种子。
- `TerrainDetailSettings.minimumFitness`：严格适应度下限，默认 0.5，范围 [0,1]。
- `TerrainDetailSettings.fadeStart`：低于此值按适应度概率稀疏，默认 0.6，范围 [0,1]。
- `TerrainDetailSettings.density`：规则密度乘全局密度后的有效值，默认 16，有限非负且小于 INT_MAX。
- `TerrainDetailSettings.namespaceId`：稳定资源层身份；零为兼容默认层，非零用于同一 terrain 上独立保存和清理多个 detail prototype。
- `TerrainDetailPlacementSettings.densityNamespaceId`：选择要导出为实例的密度资源层；`namespaceId` 继续定义输出点及其 `spawnNamespace` 来源身份。

Replace 先清零目标资源层；Add/Remove 保留未通过适应度或随机门槛的单元。通过的单元按
`InverseLerp(minimumFitness,1,fitness)*density` 增减，然后钳制到 [0,density]，
按最接近的偶数舍入。这意味着 Add 也可能降低原先超过目标密度的数量，这是源码行为。
随机稀疏采用源码 XorshiftPlus（种子 0 按 1 处理，负种子按 uint32 位模式），X 外层、
Z 内层遍历，只在适应度位于淡出带时抽样。每次 apply 重建独立 detail-thinning 流；
同构建、相同整层输入和种子可重复，不承诺分割区域后仍与单次完整层调用一致。
所有输入先验证，候选数量计算完再提交；失败或分配异常不改变原层。
调用者在所属线程串行访问，不保留借用，也不提供可写数量视图。
资源层由稳定 namespace 拥有；实际草叶生成和渲染通过下述 PointSet 导出及 graphics 接口完成。


### 草地密度到 PointSet

`eve.exportTerrainDetailPoints(output, layer, heights, settings)` 把 TerrainDetailLayer 的
每个整数数量展开为现有 PointSet，成功返回发射点数，失败保持原 output 不变。
高度图可以与密度图分辨率不同，端点覆盖同一世界矩形，双线性取样后乘高度比例。
输出使用现有 `asset` 字符串属性，并记录 `detailCell`（行优先单元编号）和
`detailOrdinal`（单元内序号）。资源引用只传递给下游，不在此加载资产。

`TerrainDetailPlacementSettings` 字段：

- `originX`、`originZ`：世界矩形起点，默认 0。
- `width`、`depth`：正的世界宽度、深度，默认 1。
- `heightScale`：有限高度比例，默认 1。
- `minimumScale`、`maximumScale`：正的实例缩放范围，默认均为 1。
- `seed`：位置、旋转、缩放三个独立命名随机流的显式 32 位种子，默认 1。
- `namespaceId`：非零实例身份命名空间，默认 1；实际场景须按资源/瓦片分配不同值。
- `maxPoints`：显式输出预算，默认 1000000，允许 0。超限拒绝整个输出，不截断。
- `asset`：非空资源引用，必须由调用者指定。

ID 来自命名空间、单元和单元内序号，与总密度及遍历行号无关。修改其他单元不会
移动或重编号现有点；改变网格尺寸或命名空间改变身份范围。零 ID 保留，极端命名空间
映射为零时明确失败，调用者应改用其他命名空间。位置采用单元内原生随机分布，
朝向使用 Y 轴角度、法线向上；这部分替代 Unity 内部实例化，不宣称 Unity 位置一致。
原输入只在本次调用借用，输出在私有 PointSet 完成后一次移动发布；没有场景对象
所有权或回调。调用者在所属线程串行访问所有输入输出。


### PointSet 草地渲染接入

`eve.bakeTerrainGrass(field, points, asset, width, height)` 是完整宿主适配器：按 `asset`
属性精确选择 PointSet 中的实例，保留其根位置与独立横向/纵向缩放，上传到既有 GrassField，
返回 `Result<int>` 的上传数量。无匹配实例时清空已发布网格。非空资源选择器、有限
根位置、正宽高、非零稳定 ID 以及大于 0.001 的横向/纵向缩放是前置条件；非法参数不改变
现有可见草地。X/Z 缩放必须相同；Y 缩放独立，不再丢失草地宽高差异。

内置路径采用 GrassField 四帧草地贴图；图像路径由调用者显式上传已解码资源，asset
只做分组选择。64 位身份留在 PointSet；未染色实例折叠为可精确存入 float 的 24 位
动画相位键，染色实例把 RGB 量化为 RGB8 并由局部根坐标生成动画相位，因此整体模型
变换不会改变风相位。这不是场景对象身份的替代。法线和 yaw 不参与当前向上 billboard 的构建。

C++ `GrassField::bakePoints(points,width,height)` 接受已有 `grass::Point` 数组，不再
进行 Poisson 重新采样。CPU 验证/网格构建及 GPU 创建在发布字段前完成；失败保持
原字段。新增 GPU 对象按现有约定由 Graphics 管理到其析构，包括中断上传产生的对象。
调用必须发生在 Graphics 所属线程、draw/capture 回调之外；不保留输入数组或 PointSet。
此适配器需要完整 graphics provider，核心裁剪构建只包含密度与 PointSet 算法。


`eve.bakeTerrainGrassImage(field, points, asset, width, height, image)` 使用一张已有
RGBA8 ImageData 替代内置动画 atlas。实例分组选取和失败原子性与 bakeTerrainGrass
相同，图像必须具有完整 RGBA8 存储；包括空点组也先校验图像。上传保留 RGB/alpha，
不合成帧、不使用 RGB 推断透明度，采用单帧 UV 和中性白色/灰色明暗调制。
原图仅在上传期间借用，GPU 纹理由 Graphics 拥有。C++ 对应
`GrassField::bakeTexturedPoints(points,width,height,image)`，与内置路径共享同一发布实现。
该图像 billboard 支持类型化风参数和健康/枯萎颜色；颜色 alpha 仍由纹理所有。
`eve.bakeTerrainGrassFoliage(field,points,asset,width,height,albedo,normal,mask,settings)` 提供
Pcg/PW 静态植被材质路径。三张图必须是同尺寸完整 RGBA8；遮罩通道为 metallic、occlusion、
thickness、smoothness。`GrassFoliageSettings` 控制线性色调、alpha cutoff、normal strength、相机距离
淡出和高度雪覆盖。EVEngine 使用自身方向光、环境光与级联阴影合同，未复制 Unity Standard 内部实现。
全部 DetailRenderMode 仍不属于此适配器。


独立宽高参数：`TerrainDetailPlacementSettings.minimumWidth`、`maximumWidth`、
`minimumHeight`、`maximumHeight` 默认均为 1，都是正有限乘数，最大值不得小于最小值。
分别用 width/height 命名随机流选择，与已有 minimumScale/maximumScale 统一比例相乘；
输出 X/Z 为横向比例、Y 为纵向比例。修改宽高范围不改变位置、朝向、ID 或其他单元。
乘积不能用正 float 表示时拒绝完整输出。实际物理宽高还要乘上传时传入的 billboard 宽高。

颜色参数 `healthyR`、`healthyG`、`healthyB`、`healthyA`、`dryR`、`dryG`、`dryB`、`dryA`
均为线性 [0,1] 端点；`noiseSpread` 为 [0,1]，
`noiseSeed` 为显式有符号 32 位种子。每个点按世界矩形内的归一化坐标采样确定性的平滑
格点噪声，再在 dry 与 healthy 之间插值。`noiseSpread=0` 固定取中点。该噪声是对 Unity
Terrain 内部不可见实现的原生重建，保证 EVEngine 内重放稳定，不声明与 Unity 数值逐点一致。
颜色配置不参与位置、缩放或稳定 ID；任何非法端点或 spread 都在发布前拒绝并保留旧输出。

### 地形树木放置

`eve.exportTerrainTreePoints(output, fitness, heights, settings)` 将一张归一化适应度图与独立
分辨率的高度图展开为带资源属性的 PointSet。它按 `spacing / spawnDensity` 扫描世界矩形，
并使用 Pcg XorshiftPlus 相同的条件抽样顺序执行 `failureRate`、`jitterPercent`、
`minimumFitness` 羽化、缩放随机值、Y 偏移和 0..360 度朝向。成功返回实例数；任何非法
配置、超出 `maxPoints` 或属性发布错误都保留旧 output。

`TerrainTreePlacementSettings` 包含以下脚本字段：`originX`、`originZ`、`width`、`depth`、
`heightScale`、`spacing`、`spawnDensity`、`jitterPercent`、`failureRate`、`minimumFitness`、
`snapToTerrain`、`seaLevel`、`customOffset`、`minimumYOffset`、`maximumYOffset`、
`minimumWidth`、`maximumWidth`、`minimumHeight`、`maximumHeight`、`widthRandomPercentage`、
`heightRandomPercentage`、`healthyR`、`healthyG`、`healthyB`、`healthyA`、`dryR`、`dryG`、
`dryB`、`dryA`、`bendFactor`、`boundsRadius`、`seed`、`namespaceId`、`maxPoints` 和 `asset`。

`setScaleMode(mode)` / `getScaleMode()` 使用 0=Fixed、1=Fitness、2=Random、
3=FitnessRandomized。`setYOffsetMode(mode)` / `getYOffsetMode()` 使用 0=TerrainHeight、
1=SeaLevel、2=Custom。`snapToTerrain=true` 总是采用采样地形高度；关闭后才按 Y 模式选择
地形、海平面或自定义基准，再叠加随机偏移。

输出点携带世界位置、独立宽高、yaw、适应度颜色、局部 bounds，以及 `asset`、
`treeCandidate`、`bendFactor` 属性。稳定 ID 由 namespace 和扫描单元生成；修改外观、颜色
或缩放范围不改变实例身份和位置。当前 `bendFactor` 是供树木材质/风适配器读取的下游
元数据；调用者可按原型创建 `gfx.newTreeWindShader()`、配置 `eve.TreeWindProfile`，再把同一
shader 赋给该原型的 Renderable3D。显式适配避免普通 Renderable3D 猜测资源类型或引入第二份
原型状态。

`eve.removeTerrainTreePoints(output, input, fitness, settings)` 实现 Pcg Remove：仅处理
`asset` 匹配且位于配置矩形内的点，在当前 fitness **严格大于** `minimumFitness` 时移除，
并完整保留其他资源、矩形外点、行顺序和属性。output 可以与 input 是同一 PointSet。

`TerrainTreeRescaleSettings` 与 `eve.rescaleTerrainTreePoints(output, input, settings)` 实现
原型刷新。字段 `previousMinimumWidth`、`previousMaximumWidth`、`previousMinimumHeight`、
`previousMaximumHeight` 描述上次生成区间；`minimumWidth`、`maximumWidth`、`minimumHeight`、
`maximumHeight`、`bendFactor`、`boundsRadius`、`asset` 描述新原型。它也提供
`setScaleMode()` / `getScaleMode()`。Fixed 直接采用新最小值；其他模式按旧区间
InverseLerp 并钳制到 [0,1]，再映射到新区间，与 Pcg 原型刷新逻辑一致。刷新只改变匹配
点的宽高、bounds 和 bendFactor；身份、位置、旋转、颜色及其他属性保持不变，失败不发布。

`grass::Point.widthScale` 为独立横向比例，0 表示沿用已有 height scale；直接上传时
非零值须大于 0.001。内部顶点 normal.z 保留旧 0/1 的等比编码，新横向比例 w 在
普通层编码为 -w、暗层编码为 1+w。Vulkan/WGSL 都先解码宽度和层标记，再各自应用
横向、纵向比例；正的小比例不再被 shader 强制抬到 0.05。此为运行时网格编码，
未引入持久格式或新的权威数据副本。

### 多地形高度图事务

`mapTerrainOperationMultiTile(tiles,settings,domain,worldMap,names)` 提供不修改资源的通用 C++
窗口计算。`TerrainOperationTile` 只描述名称、世界矩形、二维分辨率和地图域；返回值沿输入顺序给出
每块 tile 的 local/operation 像素矩形。`Heightmap` 域按 Pcg 规则使用 `resolution-1` 的 tile
步长并补一个共享接缝像素；`Texture`、`TerrainDetail`、`Tree`、`GameObject` 与 `BakedMask`
使用完整 resolution，边界没有重复单元。不同域因此可共享筛选、旋转 AABB、白名单和溢出检查，
同时保留各自正确的接缝语义。

`applyTerrainStampMultiTile(tiles,stamp,settings,localMask,globalMask,worldMap,names)` 是 C++
批量入口。每个 `TerrainHeightTile` 提供唯一名称、借用 Heightmap、世界原点/尺寸以及普通或
world-map 域。所有 tile 必须使用相同高度图分辨率和世界像素间距，原点按完整 tile 跨度对齐；
不同分辨率不再像 Pcg 那样静默跳过，而是返回 InvalidArgument 并保持全部 tile。

操作像素范围遵循 Pcg MultiTerrainOperation 的高度图规则：世界范围以 floor/ceil 转为像素，
使用 `resolution-1` 作为相邻 tile 偏移，并包含共享接缝像素。仅与旋转矩形 AABB 边缘接触的
邻 tile 不计入；实际修改仍由旋转 stamp 精确判断。普通与 world-map tile 分域，C++ 可传精确
名称 allow-list。报告包含 operation 矩形、各 tile 的 local/operation 矩形、受影响 tile 数与
改变样本数。

`eve.TerrainMultiTileWorkspace()` 是拥有式脚本会话。`addTile(name,heightmap,x,z,width,depth,worldMap)`
复制输入；成功 stamp 后 topology 固定。`stamp(stamp,settings,operation,localMask,globalMask,worldMap)`
先复制整个工作区，在候选 tile 上完成所有计算、报告和历史分配后整体交换。任何晚期数值或
分配失败都不会留下部分 tile。`copyTile` 只导出副本；`undo`/`redo` 恢复所有 tile 与对应报告，
在 undo 后 stamp 会截断 redo 分支。`getTileCount`、`getLastAffectedTiles`、
`getLastChangedSamples`、`getLastMappingCount`、`getOperationCount` 和 `getAppliedCount` 提供只读状态。

`applyTerrainDetailMultiTile(tiles,operationFitness,detailSettings,operationSettings,seed,worldMap,names)`
把通用窗口直接用于细节层。共享 fitness raster 的尺寸必须等于计算出的 operation 矩形；每块
`TerrainDetailLayer` 先复制后计算，全部成功才共同发布。Replace 与 Pcg 一致：受影响 tile 的旧层
先整体清零，只在映射矩形内生成；Add/Remove 保留矩形外数据。所有 tile 按描述符顺序共享同一个
显式 XorshiftPlus 流，因此淡出带的条件随机抽样不会在 tile 边界重新播种。

脚本使用 `eve.TerrainMultiDetailWorkspace()`。`addTile` 复制输入层，`apply` 接受共享 fitness、
detail settings、世界操作矩形、mode、seed 与地图域；`copyTile` 导出独立副本。`undo`/`redo`
恢复所有层及其报告，undo 后的新 apply 截断 redo 分支。只读计数接口与高度图工作区保持一致。
两个会话都单线程拥有全部状态，不保留外部 raster、场景对象、回调或隐藏时间/RNG。

`applyTerrainTreesMultiTile` 将 Tree-domain operation raster 应用于多个 `PointSet`。Add/Replace
沿用 Pcg `SetTerrainTrees` 在该阶段的追加语义，Remove 仅删除 operation 矩形内匹配 asset 且
fitness 严格超过阈值的点。所有 tile 共用 settings.seed 初始化的一个 XorshiftPlus 流；扫描范围
在局部窗口末端增加一个生成步长，使 jitter 能从 tile 外回落到边缘。全部候选 PointSet、属性和
总数完成后才共同发布。

脚本使用 `eve.TerrainMultiTreeWorkspace()` 深拷贝每块 tile 的初始 PointSet 与高度图；`apply`
的 mode 为 0=Add、1=Replace、2=Remove。`copyTile`、`undo`、`redo` 和统计接口与其他多地形
工作区一致，历史快照同时覆盖所有 tree owner。

### 地形对象规则

`TerrainObjectPlacementSettings` 对应 Pcg `ResourceProtoGameObject` 与 SpawnRule 的外层扫描；
一个 prototype 可通过 `addInstance(TerrainObjectInstanceSettings)` 添加多个子资源。每个子资源
独立配置数量区间、失败率、XYZ 偏移、地形/海平面/自定义 Y 基准、固定/随机/fitness/
fitness-randomized 缩放、XYZ 旋转、沿坡面 Y 偏移、前向坡度对齐及完整坡度旋转。

外层脚本字段包括 `originX`、`originZ`、`width`、`depth`、`heightScale`、`spacing`、
`spawnDensity`、`jitterPercent`、`startOffsetX`、`startOffsetZ`、`failureRate`、`minimumFitness`、
`minimumInstanceFitness`、`minimumDirection`、`maximumDirection`、`boundsRadius`、
`boundsCheckQuality`、`prototypeScale`、`boundsCollisionCheck`、`seaLevel`、`seed`、`namespaceId`、
`maxPoints` 与 `prototype`。子资源字段包括 `asset`、`minimumInstances`、`maximumInstances`、
`failureRate`、`minimumOffsetX`、`maximumOffsetX`、`minimumOffsetY`、`maximumOffsetY`、
`minimumOffsetZ`、`maximumOffsetZ`、`customOffset`、`commonScale`、`minimumScale`、`maximumScale`、
`minimumScaleX`、`maximumScaleX`、`minimumScaleY`、`maximumScaleY`、`minimumScaleZ`、
`maximumScaleZ`、`scaleRandomPercentage`、`scaleRandomPercentageX`、`scaleRandomPercentageY`、
`scaleRandomPercentageZ`、`minimumRotationX`、`maximumRotationX`、`minimumRotationY`、
`maximumRotationY`、`minimumRotationZ`、`maximumRotationZ`、`yOffsetAlongSlope`、
`alignForwardToSlope` 与 `rotateToSlope`。`setScaleMode` 和 `setYOffsetMode` 接受对应枚举序号。

`exportPoints(output,fitness,heights)` 使用一个显式 Pcg XorshiftPlus 流，依次执行规则失败、
半幅 jitter、中心 fitness、自碰撞、bounds 范围平均 fitness、方向、子资源数量与失败、偏移后
fitness、缩放、Y 偏移和旋转。结果进入原生 PointSet，携带 `asset`、`objectPrototype`、
`objectCandidate`、`objectResource`、法线、变换和 bounds。稳定 ID 来自 namespace、候选位置、
资源序号和实例序号；资产解析与 Scene/ECS 所有权由下游负责。`removePoints` 按 prototype、
世界矩形和严格大于阈值的 fitness 原子删除。

跨地形版本使用 `eve.TerrainMultiObjectWorkspace()`。`addTile` 深拷贝 PointSet 与高度图；
`apply(fitness,settings,operation,mode,worldMap)` 的 mode 为 0=Add、1=Replace、2=Remove。
所有映射 tile 按确定顺序共享一个 seed 随机流及一个自碰撞中心集合，因此接缝不会重新播种或
允许重叠。Replace 先删除操作窗口内同 prototype 的旧点再生成；Remove 只删除 fitness 严格大于
`minimumFitness` 的匹配点。`copyTile`、`undo`、`redo` 和统计方法与其他拥有式工作区一致。
工作区不保留外部指针，且一次操作的所有 PointSet 和历史只在成功后整体发布。

坡度对齐在 EVEngine 中由采样地形法线推导 Euler 旋转；它保持 Pcg 的选项和数据依赖，但不声明
与 Unity Quaternion 组合逐位相同。bounds 范围 fitness 使用 EVEngine 明确定义的含边界栅格采样。

### 地形纹理层权重

`eve.TerrainSplatmap()` 拥有任意层数的浮点权重。`initialize(width,height,layers,defaultLayer)`
建立唯一 topology 并把全部权重赋给默认层；`paint(paint,layer)` 以同尺寸归一化 Heightmap 覆盖
目标层，其他层保持相对比例并共同归一化。原目标层为 1、其他层全零时，剩余权重确定性均分；
单层 splatmap 只能保持权重 1。所有 texel 先在候选缓冲计算，再一次发布。

`copyLayer(layer,output)` 将一个有效层完整复制到同尺寸 Heightmap，并返回变化样本数。这是 Pcg
ImageMask `TerrainTexture` 分支的原生数据桥：调用方按稳定资源引用解析出层，再把该标量图交给
`generateImageMask` 的变换、曲线和混合阶段。层不存在或输出 topology 不匹配时不修改 output；
返回的是独立副本，后续绘制 splatmap 不会隐式改变已导出的遮罩。

`eve.TerrainTextureAlignSettings()` 以 `terrainAOriginX`、`terrainAOriginZ`、`terrainAWidth`、
`terrainADepth`、`terrainBOriginX`、`terrainBOriginZ`、`terrainBWidth`、`terrainBDepth` 保存两块
地形的世界原点和尺寸，并提供 `blendStrength`、`blendWidth`、`adjacencyTolerance`。默认相邻容差
为 Pcg 的 1.5 世界单位。`eve.alignTerrainSplatTextures(a,b,settings)` 按 Pcg `TerrainTextureAligner` 的边定义
识别 X/Z 相邻关系，在边缘向内逐行执行 SmoothStep 衰减及同步二维噪声调制的双向层混合，并逐像素
重新归一化。两张 splatmap 使用私有候选副本，只有完整成功后才同时发布；非相邻、自别名、非法
几何或参数不会留下单边修改。返回值是两张图合计发生变化的 texel 数。

C++ `paintTerrainSplatLayerMultiTile` 使用 Texture-domain 窗口将一张共享 operation raster 映射到
多块 `TerrainSplatmap`，验证不同 tile 的所有 owner、topology、目标层及操作尺寸后统一提交。
脚本 `eve.TerrainMultiSplatWorkspace()` 深拷贝每块 splatmap；`paint`、`copyTile`、`undo`、`redo`
和统计接口与高度、detail、tree 工作区采用相同事务语义，历史快照同时恢复全部纹理层。
该公式依据 Pcg `SetSplatmap` 对 Unity `CopyTerrainLayer` pass 1 的数据流重建；Unity 内置 shader
源码不在 Pcg 包中，因此不声明逐位 GPU 等价。

### 相邻地形高度缝合

`eve.TerrainHeightStitchSettings()` 提供 `extraSeamSize` 和 `maxDifference`；后者保留 Pcg profile
字段，但 Pcg 4.2.2 当前活动算法同样没有读取它。`TerrainMultiTileWorkspace.stitch(terrainA,terrainB,settings)`
按两块地形原点差选择 North、South、West 或 East，验证共享边、分辨率、采样间距和栅格对齐，再复现
`StitchBordersWithSeam`：从两侧内边界建立线性高度，向接缝逐点增加混合权重，最终用最靠近接缝的
两个点平均并写入两侧共享边。操作成功后形成一个覆盖全部 owned tile 的 undo/redo 快照。

C++ `stitchTerrainHeightmaps(a,b,settings)` 可用于已有编辑事务；它同步借用两个不同 Heightmap，先在
两个候选副本完成计算，再同时发布。无共同边、部分边未按样本对齐、非法 seam 或非有限数据不会修改输入。

`TerrainWorldWorkspace.setHeightWorldUnits(heightWorldUnits,worldHeightSpan)` 对应 Pcg Terrain Height
Adjuster。它将目标值转换为 `clamp(heightWorldUnits/worldHeightSpan,0,1)`，覆盖工作区内每块地形的
全部归一化高度，并将整个世界作为一次可撤销事务发布。成功值是实际变化的样本总数；非有限高度、
非有限或非正垂直尺寸在创建候选状态前失败。

`eve.TerrainSplatPalette()` 用 `addColor(r,g,b,a)` 按层添加归一化线性颜色，`getColorCount()` 返回
当前颜色数；颜色数必须与 splat
层数相同。`eve.bakeTerrainSplatAlbedo(output,splatmap,palette)` 将全部层权重混合进同尺寸可写
`ImageData`，成功时返回像素数。实现先写私有 ImageData 副本再 adopt，因此 palette、尺寸或格式
验证失败不会改变旧图像。生成的 RGBA8 可直接传给 `gfx.newTexture`，示例右侧 terrain 已使用此路径。

`eve.GtsHeightBlendSet()` 按 splat 层顺序拥有原始高度 raster，并通过
`addLayer(height,contrast,brightness,increase)` 保存 GTS alpha 高度变换。层数限制为 1–8，输入会复制。
`eve.applyGtsHeightBlend(output,input,set,blendFactor)` 先计算每层“变换后高度 × 原权重”，再执行 GTS 的
最大高度、transition、epsilon 与归一化公式，一次发布所有层权重。失败时 output 保持不变。

`eve.GtsPackedLayerSettings()` 配置 `triPlanar`、`stochastic`、`triPlanarSizeX`、`triPlanarSizeZ`、`tileSizeX`、`tileSizeZ`、`offsetX`、`offsetZ`、`tintR`、`tintG`、
`tintB`、`normalStrength`、`aoMin`、`aoMax`、`smoothnessMin`、`smoothnessMax`、`geoAmount` 和
`detailAmount`，以及 `heightContrast`、`heightBrightness`、`heightIncrease`、`displacementContrast`、
`displacementBrightness`、`displacementIncrease` 和 `tessellationAmount`。`GtsPackedLayerSet.addLayer` 复制每层
albedo+height 与 normal+AO+smoothness 纹理；`eve.bakeGtsPackedLayers` 按归一化 splat 权重重复双线性采样
并一次生成 albedo、renderer-ready packed normal、geological strength 和 detail strength。平面路径使用地形
局部 UV；stochastic 路径复现 GTS 的 `TriangleGrid` 与固定 sin/hash 偏移；triplanar 路径使用世界空间
`ZY/XZ/XY` 投影、`(4,15,4)` 法线指数、轴符号翻转、`0.33/0.67` 偏移，并按 C# 上传规则将 size 乘 10。
四个输出原子提交。
`eve.bakeGtsPackedLayerDisplacement(displacement,tessellation,heights,splat,layers,cameraX,cameraY,cameraZ,
tessellationMultiplier,originX,originY,originZ,spacingX,spacingZ)` 对权重最高的四层复用同一平面 UV，读取
albedo alpha 并按每层 displacement 参数变换，再分别加权位移和 tessellation amount。它复现 GTS shader
的 100000 平方距离硬截止，并在最后乘全局 tessellation multiplier；相机和地形变换均为显式输入。
两个 Heightmap 原子提交，随后传给 weather PBR 时雪位移按原 shader 顺序覆盖混合。

`eve.generateGtsGlobalBlendDistance(output,heights,cameraX,cameraY,cameraZ,blendDistance,blendRange,
originX,originY,originZ,spacingX,spacingZ)` 精确生成 GTS 共用的近远混合 raster：世界空间相机距离平方乘
`blendDistance/10000`，饱和后取 `blendRange` 次幂。相机位置和地形变换全部显式输入，便于回放时在同一
相机状态重建；失败保留旧 output。

`eve.GtsColorMapSettings()` 配置 `alphaIntensity`、`colorIntensity`、`nearIntensity` 与
`farIntensity`。`eve.bakeGtsColorMapAlbedo(output,colorMap,globalBlendDistance,settings)` 使用与输出同尺寸的
颜色图及归一化近远混合 Heightmap，按 GTS 公式限制颜色和 alpha 后混入现有 albedo。它应在天气层之前调用。
`eve.GtsMacroVariationSettings()` 配置三档 `sizeA/B/C`、最低亮度 `intensity` 和 `objectSpace`；
`eve.bakeGtsMacroVariationAlbedo(output,map,settings,originX,originZ,spacingX,spacingZ)` 按
`sizeA/1000`、`sizeB/10000`、`sizeC/100000` 三次重复双线性采样红通道并相乘，随后在天气层之后调制
albedo。两个步骤均同步、原子，不读取相机或隐藏时钟。

`eve.GtsGeologicalSettings()` 保存 GTS Geological 的 `enabled`、`objectSpace`、`nearStrength`、
`nearNormalStrength`、`nearScale`、`nearOffset`、`farStrength`、`farNormalStrength`、`farScale` 和
`farOffset`。`eve.bakeGtsGeologicalSurface(albedo,packedNormal,
heights,layerStrength,globalBlendDistance,geoAlbedo,geoNormal,settings,originY)` 沿地形高度轴重复采样 near/far
颜色条和 DXT5nm 法线条，用显式的逐像素地质层强度及近远距离混合它们。颜色图在 `[0,1]` 饱和，法线结果
继续保持 renderer-ready packed RG，原有 AO 与 smoothness 通道不变。albedo 与 packedNormal 使用候选副本
一起提交，任一 raster、纹理或参数非法时两者都不改变。

`eve.GtsDetailNormalSettings()` 提供 `enabled`、`objectSpace`、`nearTiling`、`nearStrength`、
`farTiling` 与 `farStrength`。`eve.bakeGtsDetailSurface(albedo,packedNormal,detailGreyscale,
layerStrength,globalBlendDistance,detailNormal,settings,originX,originZ,spacingX,spacingZ)` 以世界或对象 XZ
坐标进行 near/far repeat-bilinear 采样，按距离混合 RG 法线，再以逐像素层权重叠加到 packed normal。
它同时按 GTS 的法线 XY 平方长度给基础 albedo 添加微阴影，并输出未加权 `detailGreyscale`。
随后用 `eve.bakeGtsWeatherAlbedoDetailed(...,detailGreyscale,originX,originZ,spacingX,spacingZ)` 可复现 GTS
对雪纹理额外施加的细节衰减。三个 detail 输出一起提交，失败不产生半更新。

`eve.GtsSnowSurfaceSettings()` 和 `eve.GtsRainSurfaceSettings()` 保存 GTS profile 的表面天气参数。
雪参数为 `enabled`、`power`、`minimumHeight`、`blendRange`、`slopeBlend`、`age`、`scale`、
`colorR`、`colorG`、`colorB`；雨参数为 `enabled`、`power`、`minimumHeight`、`maximumHeight`、
`darkness`。
`eve.bakeGtsWeatherAlbedo(output,heights,snowAlbedo,snowMask,snow,rain,originX,originZ,spacingX,spacingZ)`
按 GTS 顺序先混合雪色，再施加雨水变暗。雪使用世界高度平滑带、由高度梯度求得的坡度遮罩、积雪年龄、
强度、重复纹理和 tint；雨使用最小/最大高度各 30 个世界单位的固定过渡，并将强度限制为 0.9。
调用同步且原子，失败保留旧 output。

`eve.bakeGtsWeatherPbr(normal,mask,displacement,tessellation,heights,snowNormal,snowMask,rainData,snow,rain,timeSeconds,originX,originZ,spacingX,spacingZ)`
补充 GTS 的其余表面输出。`normal` 采用可直接交给 `Renderable3D.setNormalTexture` 的 packed 格式：
RG 是以 `[0,1]` 编码的切线法线 XY，B 是 AO，A 是 smoothness；绑定后调用
`setPackedNormalMask(true)` 让 Vulkan/WGSL 重建法线 Z 并逐像素读取 AO/光滑度。`mask` 同时保留原始
四通道 GTS 材质数据，两个 Heightmap 分别保存位移与细分强度。雪支持 `normalStrength`、`setMaskRemapMin`、
`setMaskRemapMax`、`heightContrast`、`heightBrightness`、`heightIncrease`、`displacementContrast`、
`displacementBrightness`、`displacementIncrease` 和 `tessellationAmount`。雨支持 `speed`、`smoothness`
和 `scale`。雨滴相位只读取显式 `timeSeconds`，因此回放不会依赖隐藏引擎时钟。四个输出先在私有副本
完成，任一纹理、数值或结果非法时全部保持原状。

### 世界创建与统一历史

`eve.TerrainWorldCreationSettings()` 对应 Pcg `WorldCreationSettings` 的运行时拓扑字段：`tilesX`、
`tilesZ`、`tileSize`、`tileHeight`、`centerX`、`centerZ`、`heightmapResolution`、
`controlTextureResolution`、`detailResolution`、`treeResolution`、`objectResolution`、`splatLayers`、
`defaultSplatLayer`、`defaultDetailDensity`、`worldMap`、`namePrefix` 与 `nameSuffix`。高度分辨率必须为
`2^n+1`，control/detail 分辨率必须为 `2^n`。tile 原点按 Pcg 公式从完整世界中心向负半轴偏移；
普通名称为 `prefix_x_z-suffix`，world-map 使用 Pcg 的 `World Map_x_z-suffix` 前缀。

`eve.TerrainWorldWorkspace()` 同时拥有每块 tile 的 Heightmap、TerrainSplatmap、TerrainDetailLayer、
tree PointSet 与 object PointSet。`create` 原子建立零高度世界；`stamp`、`paintSplat`、`applyDetail`、
`applyTrees`、`applyObjects` 复用对应多 tile 内核。每次成功操作的历史快照覆盖五个领域，故
`undo`/`redo` 不会产生从未共同存在过的高度与资源组合。`copyHeightmap`、`copySplatmap`、
`copyDetail`、`copyTrees`、`copyObjects` 仅导出独立副本；tile 名称和原点查询返回结构化 Result。
工作区单线程拥有状态，不保留外部 raster、回调、场景对象或隐藏时间源。

`flatten()` 对所有 tile 的高度执行 Pcg 式归零并形成一次可撤销操作。`TerrainWorldClearSettings()`
通过 `details`、`trees`、`objects`、`probes` 选择生成域，`clearSpawns(settings)` 在所有 tile 上一次性清除所选域；
至少要选择一项。`sourceNamespace=0` 清理任意来源；非零时只清理由对应 detail/tree/object/probe placement
`namespaceId` 生成的资源层或实例。`TerrainDetailLayer.sampleResource(namespaceId,x,z)` 查询单个资源层，`clearResource(namespaceId)`
只删除该层并返回发生变化的单元数；普通 `sample` 返回所有资源层之和。Splat 权重不属于 Pcg
`ClearSpawns`，因此不会被该操作修改。

查询 API 为 `getTileName(index)`、`getTileOriginX(index)`、`getTileOriginZ(index)`、`getTileCount()`、
`getLastChangedSamples()`、`getLastAffectedTiles()`、`getOperationCount()` 与 `getAppliedCount()`。

### 有序 Spawner 计划

`eve.TerrainSpawnPlan()` 以添加顺序拥有 terrain modifier stamp、texture、detail、tree 和 object 规则。
`addSplat`、`addDetail`、`addTrees`、`addObjects` 接收稳定且唯一的 `ruleId`，并深拷贝 paint/fitness raster、
领域设置、操作窗口、目标纹理层、模式和显式种子；
之后修改调用方对象不会改变计划。`setEnabled(ruleId,enabled)` 按稳定身份切换规则，`getRuleCount()`
返回规则总数。

`addModifierStamp(ruleId,stamp,settings,operation,localMask,globalMask)` 加入 Pcg TerrainModifierStamp 资源，
其中 operation 使用与 `applyStamp` 相同的 0..5 枚举。
印章及局部/全局蒙版均在加入时深拷贝，并与其他资源规则按同一顺序事务执行。

`TerrainWorldWorkspace.spawn(plan)` 按顺序执行全部启用规则，但只复制一次完整世界并只发布一次；任意
后续规则失败会丢弃此前规则的候选结果，历史中只出现一个混合生成操作。空计划或全部禁用的计划会
返回结构化失败，不制造无意义历史。执行仍使用各领域的跨 tile 映射、稳定资源 namespace 和显式 RNG。

`snapshotJson()` 输出确定性的 `eve.procgen.terrain-spawn-plan` schema version 3 JSON。根对象严格只允许
`schema`、`version`、`payload`；payload 是完整规则、raster、设置、顺序与启用状态的固定小端二进制
十六进制表示。`restoreJson(json)` 拒绝未知根字段、未知版本、非有限数值、非法枚举、重复规则 ID、
尺寸/数量上限、截断或尾随 payload，并在完整候选通过后一次交换。Version 0 作为旧格式迁移入口，
其规则没有 enabled 字节，恢复时统一迁移为启用；version 0/1/2 均可恢复，再次保存会写为 version 3。

### Probe 资源点

`eve.generateTerrainProbes(output,fitness,heights,settings,type)` 导出 Pcg Probe 资源的拥有式 PointSet；
type 为 0=Reflection、1=Light。`eve.TerrainProbePlacementSettings()` 提供 `name`、`originX`、
`originZ`、`width`、`depth`、`heightScale`、`spacing`、`jitterPercent`、`minimumFitness`、
`seaLevelActive`、`seaLevel`、`reflectionOffset`、`lightOffset`、`reflectionResolution`、
`reflectionClipDistance`、`reflectionShadowDistance`、`seed`、`namespaceId` 和 `maxPoints`。

扫描使用显式 Pcg XorshiftPlus 流。ReflectionProbe 在启用海平面时位于
`max(terrainHeight,seaLevel)+reflectionOffset`；禁用时保留源码的 `500+seaLevel+0.2` 回退。
LightProbe 仅在地形严格高于 seaLevel 时生成，并增加 lightOffset。输出携带稳定 ID、probe 类型、
资源名、namespace、反射分辨率、裁剪距离和阴影距离；场景节点及实际反射捕获由 graphics/scene provider
消费这些值，不由 procgen 保存。

`TerrainSpawnPlan.addProbes(ruleId,fitness,settings,operation,mode,type)` 和
`TerrainWorldWorkspace.applyProbes(...)` 将 Probe 纳入与其他生成域相同的跨 tile 事务；mode 为
0=Add、1=Replace、2=Remove。Replace/Remove 只匹配操作窗口内相同 `probeResource` 的点，整次操作失败时
所有 tile 保持原值。`copyProbes`、`clearSpawns`、undo/redo 和分步 SpawnPlan 都读写完整世界快照中的
独立 Probe 状态。SpawnPlan version 3 持久化 Probe 类型、资源名、操作模式、fitness 与全部设置。

`eve.publishTerrainProbes(batchId,points)` 通过可选 `IProcgenProbeSink` 将完整 PointSet 原子发布给 graphics。
Reflection 点创建并注册拥有式 `ReflectionProbeCapture`，Light 点作为同批稳定光照采样位置保留；再次发布
同一 batch 会先构造完整候选再替换。`removeTerrainProbeBatch` 销毁批次及捕获资源，
`tickTerrainProbeBatches(faceBudget,filterBudget,filterSamples)` 按显式预算推进六面捕获和过滤。
`getTerrainProbeBatchCount`、`getTerrainReflectionProbeCount`、`getTerrainLightProbeCount` 提供可观察状态；
graphics provider 缺失时发布、删除和 tick 返回 `Unsupported`，查询返回零。

### Biome Preset 生成栈

`eve.TerrainBiomePreset()` 对应 Pcg `BiomePreset.m_spawnerPresetList`。`addSpawner(entryId,plan,
activeInBiome,activeInStamper,autoAssignResources)` 按列表顺序深拷贝一个非空 Spawner 计划；entry ID
必须稳定且唯一。两个 active 标志分别控制 biome controller 自动生成和 stamper 联动生成，
`setActiveInBiome`、`setActiveInStamper` 按稳定 ID 修改。`getAutoAssignResources` 保留 Pcg
`m_autoAssignPrototypes` 的资源准备意图，实际资源仍由各规则的稳定 namespace/asset 引用拥有。
`getSpawnerCount()` 返回 preset 当前拥有的 Spawner 条目数。

`TerrainWorldWorkspace.spawnBiome(preset)` 与 `spawnStamper(preset)` 只选择对应 scope 的条目，再按
entry 顺序和内部 rule 顺序编译为一个计划。不同 Spawner 可复用局部 rule ID；编译时会用长度定界的
entry ID 形成无歧义复合身份。整个栈仍只发布一个完整世界快照，因此跨 Spawner 的后置失败不会留下
先前纹理或植被结果。没有对应 active 条目时返回结构化失败。

长生成栈可用 `beginSpawn(plan)`、`beginSpawnBiome(preset)` 或 `beginSpawnStamper(preset)` 启动，再用
`stepSpawn(maxRules)` 按正数规则预算推进。状态 0=Idle、1=Pending、2=Completed、3=Cancelled、4=Failed；
`getSpawnStatus()` 和 `getSpawnCompletedRules()` 提供轮询。Pending 候选深拷贝完整世界及历史，规则执行
产生的纹理、detail、tree、object 中间结果均不可通过 copy API 观察；最后一条规则成功后才形成一次
历史提交。`cancelSpawn()` 或任意规则失败会丢弃完整候选。Pending 时 create、所有同步修改、undo/redo
均返回结构化失败，避免无声替换或分叉候选；读取最后发布快照仍然可用。

`snapshotJson()` 输出确定性的 `eve.procgen.terrain-biome-preset` schema version 1，包含有序条目、三个
标志和每项完整的嵌套 SpawnPlan snapshot。`restoreJson(json)` 严格拒绝根或条目的未知字段、重复/空 ID、
非法嵌套计划及尺寸上限，并在全部条目通过后一次交换。Version 0 没有 `autoAssignResources` 字段，迁移
时采用 Pcg 原型自动准备的兼容默认值 true；再次保存写为 version 1。
# Pcg PhotoMode 地形质量

`PcgTerrainPhotoModeAuthority` 通过显式 `setAuthority(true)` 成为照片模式 Terrain 域的唯一所有者。它保存并验证 draw-instanced、detail density、detail distance、heightmap pixel error 与 basemap distance。`selectLod` 将当前 pixel-error 直接用于既有 screen-space-error 地形 LOD 选择；`textureTier` 在相机距离越过阈值时返回命名的 Basemap 材质级别。细节实例渲染器读取 `state()` 中的 density、distance 和 instancing 标志，使同一份状态驱动网格、材质与植被细节。撤销或析构会释放 capability listener。

## GTS 地形网格切片

`GtsMeshSplitResult` 保存 `splitGtsMesh(output,source,xSplits,zSplits,pivot)` 产生的行优先网格切片。
切割数量表示平面数，因此列数和行数分别为 `xSplits+1`、`zSplits+1`。每个源三角形会真正沿 X/Z
边界裁剪，交点同步插值位置、法线和 UV，跨越边界的三角形会进入相邻切片。

`pivot` 为 0 时保留源坐标，1 使用每块的最小 X/Z，2 使用中心 X/Z。通过 `getColumnCount()`、
`getRowCount()`、`getTileCount()` 查询布局，`getTileOffsetX(index)`、`getTileOffsetZ(index)` 返回恢复
世界位置的偏移，`copyTileMesh(index)` 返回调用者拥有的独立 `ProcgenMeshBuild`。无效索引返回 null，
无效拓扑、非有限顶点、越界索引及非法切分数通过 Result 报错，旧 output 保持不变。

`simplifyGtsMesh(output,source,quality,options)` 提供 GTS 网格导出的确定性 QEM 简化核心。`quality` 是
`[0,1]` 三角形比例；折叠同步更新位置、法线和 UV，移除退化三角形后压紧顶点，并保留三角形组。
`GtsMeshSimplificationOptions.preserveBorderEdges` 可锁定开放边界，`preserveUvSeamEdges` 可锁定同位置但
UV 不同的 seam，`preserveUvFoldoverEdges` 可锁定 smart-link 中位置及 UV 均相同的 foldover 顶点组，
`preserveSurfaceCurvature` 将法线变化纳入折叠代价。迭代使用 `aggressiveness` 控制的 GTS 阈值逐步放宽；
每次坍缩会检查受影响面的新旧法向点积并拒绝翻面。边界保护允许内点坍缩到边界端点，因此不会移动
边界轮廓。输出和输入可为同一 MeshBuild；所有验证与候选简化完成后才替换输出。

`buildDefaultGtsTerrainLods(output,source,xSplits,zSplits,pivot)` 把切片和逐级简化组合成 GTS 默认四层
LOD：质量依次为 1、0.5、0.25、0.125，屏幕相对高度为 0.95、0.7、0.6、0.02。每一级以上一级
结果为输入。`GtsTerrainLodSet` 拥有全部行优先 tile，通过布局查询、tile offset、
`getLevelQuality(level)`、`getLevelTransitionHeight(level)` 以及 `copyLevelMesh(tile,level)` 供脚本消费；
无效 mesh 索引返回 null。C++ 的
`buildGtsTerrainLods` 还接受逐层独立的简化选项，并在整个结果成功前不发布部分层级。

启用 `enableSmartLink` 时，简化器会按 `vertexLinkDistance` 连接重合的开放边界顶点。内部三角形分别保存
几何拓扑索引与属性索引，因此绑定后的顶点共享 QEM 拓扑和坍缩位置，但仍输出各自的法线与 UV。
重合顶点 UV 不同标记为 seam，UV 相同标记为 foldover；对应 preserve 选项会阻止该分类边被坍缩。

运行时可用 `selectLevel(relativeHeight)` 按 GTS/Unity 的屏幕相对高度选择层级；低于最后一个 transition
返回 -1 表示剔除，非法输入返回 -2。`getLevelSwitchDistance(level,worldDiameter,verticalFovDegrees)` 将
同一阈值换算成 `Renderable3D.setMeshLod` 使用的相机距离，`selectLevelForCamera` 可直接按距离、包围体
直径和垂直 FOV 选择。换算采用透视投影关系，因而不依赖固定分辨率。

`GtsTerrainLodSet.snapshotJson()` 输出 `eve.procgen.gts-terrain-lod` version 2，完整包含布局、逐层质量、
transition、全部简化选项、tile offset、位置/法线/UV/index 流和每个三角形的 group 名称。
`restoreJson(json)` 严格拒绝未知字段、未来版本、尺寸超限、非有限数值、无效索引或不匹配的 stream，
并只在所有 tile 和 level 解码成功后一次替换当前结果。

`Procgen.configureGtsTerrainTileLods(lods,tile,renderable,graphics,worldDiameter,verticalFov,originX,originY,originZ)`
先把指定非空 tile 的全部层级上传到同一 Graphics owner；全部成功后一次清空并配置 Renderable3D 的
四层 LOD、三个切换距离、末级剔除距离和带 pivot offset 的世界位置。上传失败或参数无效时 renderable
保持不变。上传 mesh 由 Graphics 持有，Renderable3D 只保存借用引用。

`GtsTerrainLodRuntime.replace(lods,procgen,graphics,worldDiameter,verticalFov,originX,originY,originZ)` 为所有
非空 tile 创建候选 Renderable3D；候选在完整配置前保持不可见，全部成功后才显示并销毁旧批。
`getRenderable(tile)` 按原 row-major 槽位返回 ECS 借用实体，空 cell、无效或 stale 槽位返回 null；
`getTileCount()` 和 `getRevision()` 查询已提交状态。`clear()` 销毁全部实体并返回销毁数量。
`GtsTerrainLodRuntime.applyMaterial(material)` 把同一个 Graphics 所有的材质借用引用应用到当前所有存活
tile，并返回实际更新数量；运行时不取得材质所有权。`planGtsTerrainLodAssets(output,lods,terrainName,
meshFolder)` 生成确定性的导出清单。`GtsTerrainLodAssetPlan` 通过 `getEntryCount()`、`getTileIndex(index)`、
`getLevelIndex(index)`、`getObjectName(index)`、`getMeshName(index)` 和 `getRelativePath(index)` 查询条目。
对象名兼容 GTS 的 `<terrain> SubTile<tile>_LOD_<level>`，mesh 名兼容其无分隔 level 后缀；EVEngine
清单路径使用 `<folder>/<mesh>.eve.mesh.json` 作为稳定的作者侧标识；它不是可直接加载的独立文件。
`asset_procgen::prepareGtsTerrainLodPackage` 会把全部非空层实际编码为 `eve.mesh/3` definition 与 EVMESH/3
bulk，并生成可交给 `buildEvaArchive`、AssetCooker 和 EvpackGraphicsLoader 的完整 `.eva` 候选包。非法名称、
路径、数组、索引或预算不会发布部分候选。

`GtsTerrainLodRuntime.replaceAndHideSource(lods,procgen,graphics,sourceTerrain,worldDiameter,verticalFov,
originX,originY,originZ)` 在完整 LOD 批成功发布后隐藏源 Renderable3D，并记住它进入交接时的可见性。
替换失败不会修改源或旧批；换用另一个源时先恢复前一源，`clear()` 和运行时析构也恢复当前源。
源引用保存为 ECS generation handle，因此源先销毁时 `getSourceTerrain()` 返回 null，后续清理不会访问
陈旧实体。受运行时管理的 LOD tile 不能同时作为源。

`buildGtsTerrainBaseMesh(output,heightmap,saveResolution,sizeX,sizeY,sizeZ)` 完成 GTS 转换的高度场前置阶段。
`saveResolution` 为 0=Full、1=Half、2=Quarter、3=Eighth、4=Sixteenth，对应 1/2/4/8/16 的源样本步长。
输出覆盖完整 terrain 尺寸与 `[0,1]` UV，Y 为高度样本乘 `sizeY`；法线从实际导出三角形重新计算。
高度场间隔不能被步长整除、样本非有限、尺寸非法或网格超预算时返回结构化错误，旧 output 保持不变。

`buildDefaultGtsTerrainLodsFromHeightmap(output,heightmap,saveResolution,sizeX,sizeY,sizeZ,subTiles,pivot)`
原子执行 Pcg 的完整默认转换链。`subTiles` 与 GTSProfile 一致，表示 X/Z 两轴各自的切割平面数，因此
默认值 3 产生 4×4 个 row-major tile；允许范围为原编辑器的 0–5。任一基础网格、切片或顺序简化阶段
失败都不替换旧 LOD set。

`GtsTerrainMeshSettings` 对应原包的 `GTSMeshSettings`，默认保存 Full、4 个 LOD、
`100/50/25/12.5` 百分比和 3 个 split。LOD count 限制为 1–4，质量允许编辑器同样的 0–100；最后一个
激活 LOD 总使用 0.02 剔除阈值，因此 1/2/3/4 层配置分别得到正确的 GTS transition 序列。
`snapshotJson/restoreJson` 使用严格的 `eve.procgen.gts-terrain-mesh-settings` version 1 schema，未知字段、
非法范围或错误数组长度不会修改旧设置。`buildGtsTerrainLodsFromHeightmap(output,heightmap,settings,...)`
直接消费这份配置。
脚本用 `getSaveResolution/setSaveResolution`、`getLodCount/setLodCount`、`getSubTiles/setSubTiles`、
`getLodQuality/setLodQuality` 和 `getLodTransitionHeight` 查询或修改各字段。

### GTS 地形导出配置

`GtsTerrainExportSettings` 对应原包的 `GTSExportTerrainSettings` 中源地形与 impostor 两组 LOD
配置。`appendSourcePreset(mode,level,quality,transition)` 与
`appendImpostorPreset(mode,level,quality,transition)` 中 mode 取 0=Impostor、1=LowPoly；它们精确应用
原版 `SetLODToImpostorMode` / `SetLODToLowPolyMode` 的采样分辨率、纹理分辨率、锐边、烘焙方式、
法线图、顶点色与材质策略。transition 必须按列表严格递减，quality 使用 `[0,1]`。
`getSourceLodCount()` 和 `getImpostorLodCount()` 返回两组数量。

`snapshotJson()` / `restoreJson(json)` 使用严格的
`eve.procgen.gts-terrain-export-settings` version 3 schema，保存顶层工作流、每层所有可执行字段和 QEM 参数；未知字段、
非法枚举、纹理分辨率、非递减阈值或尺寸超限会失败；version 1 自动迁移并采用原 Pcg 顶层默认值；version 2 的 OBJ 面格式迁移为 Triangles，恢复只在整个候选有效后提交。C++ 的
`compileSourceLevels()` 将源导出记录直接编译成现有顺序 LOD 网格管线使用的设置。

`buildGtsTerrainExportLodsFromHeightmap(output,heightmap,settings,sizeX,sizeY,sizeZ,subTiles,pivot)`
使用 source LOD 第一项自己的 SaveResolution 构造基础网格，再按完整 source 列表逐级简化；设置中的
分辨率不再只是持久化元数据。`buildGtsTerrainColliderMeshFromHeightmap(output,heightmap,settings,...)`
使用 workflow 的独立 colliderResolution 和 colliderSimplifyQuality 生成 MeshCollider 三角网格；只有
addTerrainCollider=true 且 colliderType=Mesh 时接受调用。两条入口都在最终候选完成后替换 output。

`encodeGtsTerrainObj(heightmap,saveResolution,sizeX,sizeY,sizeZ,faceMode)` 返回确定性的 UTF-8 Wavefront
OBJ 文本。faceMode 为 0=Triangles、1=Quads；坐标按原 Pcg 导出器写成
`(-terrainZ,height,terrainX)`，`vt` 同样保持原版的 `(v,u)` 顺序和一基 face 索引。它使用固定经典 locale
和足以往返 float 的精度，不依赖系统小数点设置。导出设置 version 3 保存 workflow 的 objFaceMode；
version 2 迁移时采用原默认 Triangles。

`encodeGtsMaskedTerrainObj(heightmap,maskmap,saveResolution,sizeX,sizeY,sizeZ,faceMode,threshold,invert)`
复现 Pcg `MaskedTerrainMesh` 的 masked OBJ 路径。它以每个输出格左下角的归一化 mask 样本分类整格：
低于 threshold 属于 outside，其余属于 inside；invert=false 输出 inside，true 输出 outside。原版默认调用
threshold=0.2。maskmap 可以使用与 terrain 不同的尺寸，采样采用原版 `normalized * dimension` 双线性规则；
输出继续使用原版的 X/Z 顶点去重、UV 缩放和逆时针面序。

`bakeGtsTerrainVertexColors(output,source,bakedTexture,edgeMode,smoothingIterations,terrainSizeX,terrainSizeZ,linearize)`
执行 Pcg `ProcessEdgesAndColorBaking`。Smooth 保留共享顶点并按世界 X/Z 采样；Sharp 为每个三角形角展开顶点、
重算法线，并把三角形三个纹理样本的平均色写给整面。平滑迭代使用原版四邻域、边缘 clamp 算法；linearize
显式对应 SRP OrthographicBake 的 `.linear` 分支。颜色是 `ProcgenMeshBuild` 的可选 RGBA 顶点流，可通过
`hasVertexColors()`、`getColor(vertex,component)` 查询，以及 `setColor(vertex,r,g,b,a)`
写入（无颜色流时按白色分配）。裁剪、QEM、group copy、变换及 LOD version 2
快照都会保留颜色；旧 version 1 LOD 快照仍可迁移读取。

### Pcg Mask Map Export

`combinePcgMaskMapChannels(output, red, green, blue, alpha, activeChannels)` 与 Pcg 的
`PW_ChannelCombine.shader` 一致：四张输入图各自仅取 red 分量，按 RGBA bit mask 写入目标；未启用通道写 0，
输入尺寸或 bit mask 非法时目标保持不变。`placePcgMaskMapTileInto(atlas, tile, x, y, width, height)` 使用
像素中心双线性缩放并事务写入 atlas 矩形，对应 `OneCombinedTexture` 的逐 terrain 拼接。文件编码继续使用
ImageData 既有 PNG/JPG/EXR 输出接口。

### Pcg 通用 Mesh LOD Generator

`PcgMeshLodProfile.appendLevel(transitionHeight, fadeWidth, quality, combineMeshes, combineSubMeshes)`
按近到远加入最多四级配置；transition 必须严格递减，三个标量均使用 0..1 范围。
`buildPcgMeshLods(output, sourceMesh, profile)` 为每一级从同一份原始 MeshBuild 独立执行简化，完整成功后
才替换 `PcgMeshLodSet`。可用 `copyLevelMesh(index)` 取得拥有式副本，或用 `selectLevel`、
`getSwitchDistance` 检查投影阈值。

`procgen.configurePcgMeshLods(lods, renderable, graphics, worldDiameter, verticalFovDegrees)` 会先上传全部层，
然后一次替换 Renderable3D 的 LOD 链并设置最远层裁剪距离。上传或参数校验失败时 renderable 保持原状。
`profile.setFadePolicy(mode, animate, duration)` 保存 Unity `LODFadeMode`：0=None、1=SpeedTree、
2=CrossFade。各级 `fadeWidth` 与全局策略会随 output 原子写入 Renderable3D。距离模式在切换点前按当前
级距离区间乘 fadeWidth 生成互补权重；末级可渐隐到 cull。动画模式由
`Renderable3D.advanceMeshLodTransition(distance, dt)` 使用调用方注入的 dt 确定推进。

`profile.setLevelRendererState(level, skinQuality, shadowMode, receiveShadows, motionMode,
skinnedMotionVectors, lightProbeUsage, reflectionProbeUsage)` 保存 Pcg `LODLevel` 的逐级 renderer 策略。
枚举数值与 Unity 一致，包括 `Bone4=4` 与 `CustomProvided=4`。`configurePcgMeshLods` 在所有 mesh 上传后
把整条网格链和状态一次写入 Renderable3D；选中级别的 Off/On/ShadowsOnly、receive-shadow、Object/
ForceNoMotion 和 reflection-probe Off 已进入主渲染路径。

`PcgMeshLodBackup.capture(renderable)` 在替换前保存完整 MeshRenderer 状态以及实体 id/generation；
`restore(renderable)` 仅对同一实体 generation 原子恢复并消费快照，错误目标不会改变自身或消费备份。
原 mesh、parts、material 等资源仍由同一个 Graphics 实例拥有，Graphics 与这些资源必须比 backup 活得更久。
`discard()` 可只丢弃备份。该对象对应 Pcg `LODBackupComponent` 与 `DestroyLODObject` 的恢复职责。
脚本查询与矩阵接口为 `getAnimateCrossFading()`、`getCrossFadeAnimationDuration()`、`getFadeMode()`、
`getFadeWidth(level)`、`getLevelRendererState(level,field)`、`getQuality(level)`、
`getTransitionHeight(level)`、`getSourceCount()`、`getElement(row,column)`、`setElement(row,column,value)`；
备份可用 `getEntityId()`、`getEntityGeneration()` 与 `isCaptured()` 检查身份和状态。

`PcgMeshTransform` 保存 root-relative 4×4 row-major 矩阵；用
`PcgMeshCombinePlan.appendSource(mesh, transform, defaultMaterialId)` 按 renderer 顺序复制输入。
`combinePcgStaticMeshes(output, plan)` 依次变换顶点并按首次遇到的 material id 合并 triangle group；任一输入
带颜色时，无颜色输入会补白色 RGBA。`buildPcgCombinedMeshLods(output, plan, profile)` 先组合一次，再让所有
启用了 `combineMeshes` 的 level 从同一份组合结果独立简化。混合开启/关闭 combine 的 profile 会明确失败，
避免生成与 Pcg renderer 拆分结构不一致的结果。两个入口都只在完整成功后替换 output。
### Pcg Terrain Watcher

`PcgTerrainWatcher` 将 Pcg 编辑器中基于 hierarchyChanged 的 Terrain 创建/删除检测改为
显式、确定性的快照事务。调用 `beginScan()` 后按场景稳定顺序调用 `addTerrain(id)`，最后以
`commitScan()` 原子发布；首次提交只建立缓存，不产生 Created 事件。
`PcgTerrainWatcher.getTerrainCount()` 返回当前缓存规模；后续结果通过 `getChangeCount()`、
`getChangeTerrainId(index)` 和 `getChangeType(index)` 读取，其中类型 0 为
Created、1 为 Removed。新增项按本次扫描顺序排列，随后删除项按上次扫描顺序排列。
`cancelScan()` 丢弃候选；重复/空 id 或错误调用顺序返回 Result 且不改变已发布集合。

### Pcg Tree Manager

`PcgTreeManager` 汇总多个地形生成出的世界空间树位置。先以 `reset(minX, minZ, width, depth)`
建立 Pcg Quadtree 对应的半开世界边界，再用 `addTree(x, z, prototype)` 或
`addTrees(pointSet, prototype)` 加入树；批量输入会完整校验后一次发布。
`countInRange(x, z, range)` 与 Pcg 源码一致使用包含边界的轴对齐正方形查询，而不是圆形距离；
`PcgTreeManager.getCount()` 返回当前总树数。管理器复制坐标与 prototype index，不保留 PointSet。

### Pcg Biome Controller 自动加载边界

`eve.PcgBiomeController()` 保存一个 biome 的世界位置、生成半径与 TerrainLoader 模式。`configure`
使用 Pcg 原始公式计算 regular 立方边界：边长为 `range * 2 - 0.5`，用于避免范围恰好落在 tile
边缘时加载多余相邻地形；正数 impostor range 直接加到该边长，零值关闭 impostor 边界。模式编号与
原包一致：0 Disabled、1 EditorSelected、2 EditorAlways、3 RuntimeAlways。

`fitToTerrain(minX, originY, minZ, sizeX, sizeY, sizeZ)` 使用单块地形 X/Z 中心、地形原点 Y 和 X
半宽；`fitToAllTerrains` 使用聚合边界完整中心及 X 半宽。`tierAt` 返回 0 Regular、1 Impostor 或
2 Unloaded，可直接驱动调用方的地形驻留调度。`getRange`、`getLoadMode`、`getRegularCenterX`、
`getRegularCenterY`、`getRegularCenterZ`、`getRegularSize` 与 `getImpostorSize` 提供脚本侧状态读取。
所有更新先验证完整候选，失败时保持旧边界。
### Pcg Runtime Stamper

`eve.PcgRuntimeStamper()` 移植 Pcg Pro `RuntimeStamper.cs` 的完整运行时编排。调用
`configure(address, showGui, showDebug)` 后，由宿主资源系统加载高度图并传给
`loadStamp(stamp, resourceName)`，再用 `execute(target, originX, originZ, spacingX, spacingZ)`
执行清平、适配完整地形范围、应用高度 6、零旋转和中心到边缘的线性距离遮罩。执行采用候选副本，
任一步骤失败都不会留下部分修改。资源路径会把反斜杠统一为 `/`；缺失资源可通过
`reportMissingStamp()` 进入可观察的失败状态。`getUpdateTimeAllowed()` 固定返回 Pcg 原示例的
`1/15` 秒预算，`updateLayout()` 保留 300x20 底部居中进度标签的位置语义。脚本可通过
`getStatus`、`getStampAddress`、`getProgressText`、`getLabelCenterX` 与 `getLabelCenterY` 读取状态。

#### 第二百一十九批：RuntimeStamper

- 对照 `RuntimeStamper.cs` 保留资源地址默认值、路径归一化、加载/失败/完成状态与进度文本。
- 将 `FlattenTerrain -> FitToTerrain -> height=6 -> linear distance mask -> rotation=0 -> Stamp`
  组合为一次原子操作，同时复用 EVEngine 的 `Heightmap` 与 `applyTerrainStamp`。
- 控制器拥有 stamp 副本，不跨帧保留调用方指针；无回调、无锁、无隐式时间或随机数。

### Pcg Spawn Progress

`eve.PcgSpawnProgress()` 保存跨 spawner 的规则进度。`updateRule` 设置名称、总规则计数和当前
spawner 计数，`updateRuleFraction` 更新当前规则的小数进度；`getProgress` 使用
`(totalCompleted + fraction) / totalCount`。`getTitle`、`getSubtitle`、`getStatus` 提供 UI 所需快照，
`requestCancel` 发布取消请求，`clear` 隐藏并清空状态。所有更新先验证候选，非法计数或进度不修改旧值。
