# Arbor Lab — 随机树木生成器

`mesh.tree` 是 EVEngine 的原生程序化网格配方。相同 seed 与参数会生成一致的网格，可通过
`procgen.buildMesh` 获取 CPU 数据，或通过 `procgen.generateMesh` 直接上传 GPU。

## 快速使用

```squirrel
local p = procgen.newParams();
p.setSeed(31415);
p.setString("style", "lowpoly");
p.setString("branchAlgorithm", "weberPenn");
p.setString("leafMode", "canopy");
p.setFloat("leafDensity", 0.75);

local mesh = procgen.generateMesh("mesh.tree", p, gfx);
```

`branchAlgorithm` 只能选择一种骨架算法：`weberPenn` 是默认值，规则稳定、生成较快；
`spaceColonization` 通过树冠吸引点迭代生长，轮廓更不规则。主干、枝条层级和叶片系统由两种算法共享。

## 通用参数

- `style`: `lowpoly` / `realistic`
- `leafMode`: `cards`（独立双面叶片）/ `canopy`（整片冠层网格）/ `none`
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

快捷键：`R` 换 seed，`1/2` 切换 Low Poly/写实，`A` 切换分枝算法，`L` 开关叶片，
`C` 切换整片冠层，`[` / `]` 调节叶片密度。

树木网格应在加载、换 seed 或修改参数时生成并缓存，不要每帧重新生成。
