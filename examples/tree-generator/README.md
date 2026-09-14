# Arbor Lab — 随机树木生成器

`mesh.tree` 是 EVEngine 的原生程序化网格配方。相同 seed 与参数会生成一致的网格，可通过
`procgen.buildMesh` 获取 CPU 数据，或通过 `procgen.generateMesh` 直接上传 GPU；两者都返回
Result 投影，必须检查 `ok` 后才能读取 `value`。

## 快速使用

```squirrel
local paramsResult = procgen.newParams();
if (!paramsResult.ok) throw paramsResult.status.summary;
local p = paramsResult.value;
p.setSeed(31415);
p.setString("style", "lowpoly");
p.setString("branchAlgorithm", "weberPenn");
p.setString("leafMode", "clusters");
p.setFloat("leafDensity", 0.75);

local meshResult = procgen.generateMesh("mesh.tree", p, gfx);
if (!meshResult.ok) throw meshResult.status.summary;
local mesh = meshResult.value;
```

`branchAlgorithm` 只能选择一种骨架算法：`weberPenn` 是默认值，规则稳定、生成较快；
`spaceColonization` 通过树冠吸引点迭代生长，轮廓更不规则。主干、枝条层级和叶片系统由两种算法共享。

## 叶片模式

- `clusters`（默认，推荐）：在每个结果枝的合适位置放一个"叶片丛"。丛是一个球体，由
  `clusterPlanes` 张绕 Y 轴分层旋转、各自带轻微倾斜和偏移的叶片平面组成；每张平面上的
  叶片中心由蓝噪声（Poisson-disk）采样得到，分布均匀，既不成团也不留空洞。叶片顶点法线取自
  丛球面，所以一堆平面卡片仍然按球体受光，树冠有体积感；叶片同时输出正反两个绕向，开启背面
  剔除的管线也不会把它剔掉。空白处不生成任何几何体——"只有叶片着色、其余透明"落到网格上
  就是"没有叶片就没有面"，既省几何体，也避开了半透明排序。
- `cards`：沿枝条随机撒独立叶片，实现最简，但容易看出成团和空洞。
- `canopy`：用椭球团块堆树冠，几何体最少，最"低多边形"。
- `none`：只输出枝干骨架。

叶片丛参数：

- `clusterSize`：丛半径占 `crownRadius` 的比例，默认 `0.30`。
- `clusterSeparation`：相邻丛心的最小间距（单位：丛半径），默认 `0.55`。调大→丛更少更大，
  调小→丛更多更碎。
- `clusterPlanes` / `clusterCaps`：叶片平面数（默认 `10`）与封住两极的近水平面数（默认 `2`）。
- `clusterTilt`：环形平面偏离竖直方向的最大倾角（度），默认 `26`。
- `clusterLeafScale`：丛内单片叶长相对 `leafSize` 的比例，默认 `0.85`。
- `clusterSpacing`：蓝噪声中心间距（单位：叶长），默认 `0.80`；`leafDensity` 在此基础上缩放。
- `clusterLeaves`：每张平面的叶片数上限，默认 `28`。若请求的间距会超出该上限，采样器会放大
  间距而不是截断，所以留下的叶片依然均匀。
- `clusterLimit`：单棵树的丛数上限，默认 `120`。

丛心按"随机顺序 + Poisson 排斥"从结果枝锚点中挑选：枝条越均匀地填满树冠，叶片丛就越均匀，
`clusterLimit` 则给出几何体量的硬上限。

## 通用参数

- `style`: `lowpoly` / `realistic`
- `leafMode`: `clusters` / `cards` / `canopy` / `none`
- `leafDensity`: `0..1`
- `height`, `trunkRadius`, `crownRadius`, `leafSize`
- `foliageStart`: 树冠起始高度比例，`0.1..0.9`
- `radialSegments`: 枝干横截面精度，`3..24`
- `curveSegments`: 主干/枝条弯曲分段数，`2..20`
- `trunkCurve`: 主干整体弯曲强度，`0..0.45`
- `branchCurve`: 枝条弯曲强度，`0..0.5`
- `curveBack`: S 形回弯，`-0.5..0.5`
- `tropism`: 向上生长趋势，`-0.5..0.8`
- `droop`: 枝梢下垂，`0..0.8`
- `branchLengthFalloff`: 随树冠高度增加的枝长衰减，`0..0.9`
- `branchRadiusFalloff`: 随树冠高度增加的枝径衰减，`0..0.9`
- `lowerLeafCoverage`: 下层老枝沿枝条分布叶片的概率，`0..1`
- `upperLeafCoverage`: 上层新梢沿枝条分布叶片的概率，`0..1`

## Weber–Penn 参数

- `branchLevels`: `1..5`
- `branchCount`: 每层主要分枝数量，`2..20`
- `branchAngle`, `branchAngleVariation`: 分枝角度与随机变化（度）
- `phyllotaxis`: 枝序旋转角（度），默认 `137.5`
- `apicalDominance`: 顶端优势，`0..1`

## 空间殖民参数

- `attractorCount`: 树冠吸引点数量，`12..1200`
- `colonizationIterations`: 最大迭代数，`4..160`
- `influenceRadius`, `killRadius`: 吸引范围与到达判定距离
- `growthStep`: 每轮枝梢生长步长
- `branchInertia`: 延续上一段方向的惯性，`0..4`
- `maxTurnAngle`: 相邻生长段允许的最大转角（度）
- `maxCumulativeAngle`: 枝条偏离初始生长轴的最大角度，用于阻止 U 形回卷
- `maxChildren`: 单个生长节点允许的最大子枝数，`1..4`

## 运行示例

```sh
make run/linux-debug GAME=examples/tree-generator
make run/win32-debug GAME=examples/tree-generator
```

快捷键：`R` 换 seed，`1/2` 切换 Low Poly/写实，`A` 切换分枝算法，`L` 循环叶片模式
（clusters → cards → canopy → none），`[` / `]` 调节叶片密度。

树木网格应在加载、换 seed 或修改参数时生成并缓存，不要每帧重新生成。
