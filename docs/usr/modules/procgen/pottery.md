# 截面旋转器皿生成器

`mesh.vessel` 将径向截面绕 Y 轴旋转，生成有内外壁、实心底及圆口沿的 CPU
三角网格。单位与场景一致，底面 Y=0。支持花盆 `planter`、缸 `jar`、碗
`bowl`、盘 `plate`、碟 `saucer`、直筒 `cylinder`。

## EveScript

```squirrel
local pr = procgen.newParams();
if (!pr.ok) throw pr.status.summary;
local p = pr.value;
p.setString("shape", "jar");
p.setFloat("vesselHeight", 1.5);
p.setFloat("radius0", 0.45);
p.setFloat("radius1", 1.2);
p.setFloat("radius2", 1.1);
p.setFloat("radius3", 0.6);
p.setFloat("wallThickness", 0.04);
p.setFloat("bottomThickness", 0.06);
p.setString("material", "celadon");
local result = procgen.generateMesh("mesh.vessel", p, gfx);
if (!result.ok) throw result.status.summary;
// Retain result.value, create Renderable3D, and assign a generated PBR material.
```

外壁为三次 Bézier：`r(t)=(1-t)^3 radius0 + 3(1-t)^2 t radius1 +
3(1-t)t^2 radius2 + t^3 radius3`，`y(t)=vesselHeight*t`。四个控制半径与高度
缺省或为 0 时采用器形预设；其他无效值报错。可生成鼓腹、收口、直壁、外撇等轮廓。
`curveSegments` 默认 48，范围 4–256；`radialSegments` 默认 96，范围 3–512。

`wallThickness` 默认 0.045，是同一曲线参数位置的**径向偏移**，不是沿法线的
恒定厚度；内壁高度从 `bottomThickness`（默认 0.05）到 `vesselHeight`。
口沿以半径 `wallThickness/2` 的半圆连接，因此总高额外增加半个壁厚。
壁厚必须小于所有控制半径；底厚必须小于高度。自交截面会被拒绝，失败不覆盖旧网格。
当前不生成排水孔、把手或非旋转对称装饰。

## 自定义完整截面（C++）

`revolveVesselSection(section, radialSegments)` 接受自己采样的任意折线截面，
可由多段 Bézier、样条或手工轮廓生成。顺序是底面轴点、外壁、口沿、内壁、内底轴点，
在 `(radius,height)` 平面逆时针，沿轴闭合；中间点必须离开轴。不允许自交、重复边或
反向截面。返回 `Result<MeshBuild>`，失败码为 `InvalidArgument`，消息前缀
`VesselSectionInvalid`。输入仅在调用期间借用，输出独立拥有数据；无回调，可重入。
没有 ECS、Link、持久化协议或随机状态。时间无关；跨平台三角函数允许浮点误差。
CPU 生成与 GPU 上传分两步，复杂度为 O(P² + P×S)，应生成后复用，不应每帧生成。

## 材质

五套纹理同时注册为 `tex.*` 和 `pbr.*`：`terracotta`（粗糙红陶）、
`glazed_ceramic`（蓝釉陶瓷）、`porcelain`（白瓷）、`celadon`（开片青瓷）、`stoneware`（棕色粗陶瓦罐）。
PBR 输出包含 albedo、normal、roughness、metallic、height、AO；均为非金属，
每类材质使用独立表面模型：红陶的随机气孔和砂粒、蓝釉的斑驳与流釉、
白瓷的细微橘皮、青瓷的 Voronoi 开片、粗陶的窑烧色斑与拉坯纹。
同一个高度场同步生成颜色、法线、粗糙度和 AO，凹陷处颜色较深、粗糙度较高。
釉裂仅影响表面贴图，不切开网格，也不模拟真实烧制过程。

`Params.setSeed` 决定随机分布，相同 seed 与参数可重复生成；更换 seed 可获得
不同表面。示例为每件器皿使用不同 seed，纹理提升至 512×512、64 色阶。
可通过 `setSize`、`scale`、`normalStrength`、`roughnessLow/High` 调整精度、
纹理尺度、凹凸强度及光泽；不要每帧重新生成。正方形纹理与整数 `scale` 下，无缝模式沿 U/V
周期平铺，包括法线的环绕采样；非整数尺度不保证周期闭合。
`material=auto` 为花盆推荐陶土、其他器形推荐白瓷；CPU 网格 metadata
`materialRecipe` 保存推荐 ID，不创建渲染资源。

使用 `procgen.applyPbrRecipeDefaults` 然后 `procgen.generatePbrMaterial` 生成对应材质；
检查每个 Result。示例将 albedo、normal、roughness 分别打包成双层图集，供高度混合 shader 同步采样。
metallic、height、AO 仍保留在生成数据中；该示例的非金属 shader 没有额外绑定这些通道。
资源与实体应在应用层保留至最后一次绘制完成。见 `examples/pottery`。

现有 MeshRecipeRegistry 的 bool/error 回调仅作为兼容注册适配；内部构建使用
Result<MeshBuild>，公共 C++ 截面 API 同样返回 Result，不引入新的 lastError 状态。


### 实物参考

- [故宫博物院：青瓷的开片](https://theme.npm.edu.tw/exh104/Celadons/en/page-4.html)：不规则釉裂及染色裂纹。
- [Cleveland Museum of Art: Stoneware Jar (lid)](https://www.clevelandart.org/print/art/1946.96.b)：斑驳釉色、颗粒与露胎。
- [LACMA: Covered Jar with Crackle Glaze](https://collections.lacma.org/object/70079)：釉面开片的器皿实例。

这些参考用于选择可程序化的视觉特征；本实现不复制藏品纹样或使用其照片作贴图。


## 按局部高度混合材质的 Shader

`examples/pottery/shaders/pottery_height.{vert,frag}` 是可直接复用的静态 Mesh3D
着色器，随示例提交预编译 SPIR-V，不需要运行时 GLSL 编译器。默认以随机红陶
作为底层，以每件器皿选择的材质作为上层：底足露胎、器身逐渐施釉、口沿积釉。
颜色、切线法线和粗糙度共用同一混合权重，口沿略微变深并降低粗糙度。

顶点阶段传递局部坐标，脚本从 **实际生成网格** 测量 minimumY / heightRange，
不重复维护各器形高度表。模型平移、旋转、非均匀缩放后，区域仍附着在器皿上。
按整个实心网格高度归一化，内外壁处在同一局部高度时使用相同的分层规则。
噪声在局部三维位置采样，因此不会沿圆周 UV 接缝突然跳变。

每件器皿拥有独立 shader 参数；所有纹理、材质和 shader 由示例持有至绘制结束。
生成、图集拼接与上传只在初始化执行；逐帧只更新相机。`H` 键循环切换正常材质、
混合权重诊断、归一化高度诊断；空格暂停/恢复相机环绕。shader 参数修改只在输入事件发生时提交。

| Uniform | 语义 |
| --- | --- |
| minimumY / heightRange | 从实际 CPU 网格测得的最低点与总高 |
| baseEnd | 露胎到施釉的归一化高度，示例按器形设置 |
| blendWidth | 过渡半宽，默认 0.07 |
| rimStart | 口沿积釉起点，默认 0.91 |
| noiseAmount / noiseScale / seed | 不规则边界的扰动幅度、局部空间频率与随机种子 |
| rimDarkening / rimStrength | 积釉压色程度与作用强度；粗陶的作用较弱 |
| debugView | 0 正常、1 权重、2 局部高度 |

图集每层有一像素环绕边框，采用双线性、无 mip 采样以防止跨层渗色；此布局面向
近距离材质展示。颜色、法线、粗糙度图集分别使用现有 Mesh3D binding 1、2、6。
**binding 6 在本 shader 中承载粗糙度，不是位移高度图**。法线在切线空间归一化
混合后再转换到世界空间，粗糙度直接参与非金属 GGX 高光。
这是示例自己的直接光/环境光着色路径，标准材质路径与其它示例不受影响。

重编译并验证（SDK bin 在 PATH 中）：

```sh
glslc shaders/pottery_height.vert -o shaders/pottery_height.vert.spv
glslc shaders/pottery_height.frag -o shaders/pottery_height.frag.spv
spirv-val shaders/pottery_height.vert.spv
spirv-val shaders/pottery_height.frag.spv
```

在 `examples/pottery/` 目录执行上述命令。
