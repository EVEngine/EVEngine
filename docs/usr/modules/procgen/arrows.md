# 2D / 3D 对象指向箭头

`eve.ProcgenArrowGenerator()` 生成从 A 指向 B 的 CPU 三角形网格。零弯曲量为直线，
非零弯曲量为二次 Bézier；控制点 = `(A + B) / 2 + bend`。

```squirrel
function arrowValue(result) {
    if (!result.ok) throw result.status.summary;
    return result.value;
}
function arrowCheck(result) {
    if (!result.ok) throw result.status.summary;
}

local arrow = eve.ProcgenArrowGenerator();
arrowCheck(arrow.setMode("3d")); // "2d" 为 XY 平面实心箭头
arrowCheck(arrow.setEndpoints(0.0, 0.0, 0.0, 4.0, 0.0, 0.0));
arrowCheck(arrow.setBend(0.0, 1.5, 0.0)); // 改为 (0,0,0) 得到直线
arrowCheck(arrow.setSize(0.06, 0.24, 0.4)); // 箭身全宽、箭头全宽、箭头轴向长度
arrowCheck(arrow.setResolution(32, 12)); // 曲线细分、3D 圆周边数
local cpuMesh = arrowValue(arrow.buildMeshResult());
// 使用现有 Procgen 实例和 Graphics 上传，返回引擎管理的 mesh handle。
local uploadedMesh = arrowValue(procgen.uploadMesh(cpuMesh, gfx));
```

`procgen` 是调用方创建的 `eve.Procgen()` 实例。上传后继续使用现有 Mesh / Renderable3D
材质与场景流程；CPU 网格本身不会自动显示或替你选择对象。2D 模式输出同样的 MeshBuild，
放在 XY 平面，法线为 +Z；它不是 SDL 系统鼠标光标，也不自动转换为 UI 坐标。

参数单位遵循调用方坐标系。所有模式的箭尖均精确位于 B，箭身从 A 起始；flat 的宽度和厚度同时收束为一个几何尖点。2D 要求 A/B 的 Z 相同、
bend.z 为零。3D 是带尾盖、肩部和锥形头的封闭表面。`shaft` / `head` 三角形组可提取后
单独着色。2d/3d UV 的 u 按采样路径距离归一化；不承诺纹理横向无缝。

所有 setter（含 setBevel）和构建均返回 Result，必须检查。失败 setter 不修改原参数，构建失败不影响旧网格。
坐标与弯曲偏移须有限且绝对值不超过 1e6；尺寸至少 1e-5 且不超过 1e6，头宽必须大于身宽。
重合端点、头长不适合当前路径、反向控制曲线、无法用 float 表示的退化三角形都会被拒绝。
头长应小于端点距离的 90%。曲线细分为 2–256，圆周边数为 3–64。
箭身过宽或弯曲过急仍可能自交；该生成器不做布尔修复或碰撞网格处理。

游戏逻辑在更新阶段解析 A/B 对象的有效 handle，传入位置值。目标销毁或不可选时由调用方隐藏
或移除指示器；生成器不保存对象地址。颜色、透明度、遮挡/穿透显示、屏幕恒定像素宽度及动画
由现有材质、相机和更新逻辑负责。尺寸为世界单位；对象跟随时合并同一帧的更新后再构建和上传，
不要在 render 回调反复分配网格。

## 模块边界与验证

归属 `procgen/mesh`，不是新模块。六面边界：capability 输入/输出均无新增；ECS/Link 无新增；
events 无新增；script 新增 `ProcgenArrowGenerator`；tool protocol 无新增。
唯一状态 owner 是调用方的 builder；输出是独立 owning MeshBuild，无持久格式、后台任务、
随机数、时钟、callback 或 provider。setter 与 build 在 owner 线程同步调用，无共享全局状态。
重复构建在相同工具链中相同，跨平台只承诺浮点容差，不承诺位级一致。

`test/unit/procgen/procgen_arrow.cpp` 验证直线/弯曲、正反弯曲、三个轴方向、索引/法线、
3D 表面边闭合与绕向，以及错误输入不改变配置。`EVProcgenCoreCheck` 包含生成器翻译单元，
不新增对 graphics / scene / editor 的依赖。独立 CPU 链接验证属于 L2；完整脚本、GPU 上传、
可见帧和选择交互仍需宿主集成运行验证，不能由 CPU 检查推断。

## 扁平圆角实体（flat）

`setMode("flat")` 保留 XY 平面的扁箭头轮廓，并沿 Z 轴生成封闭薄片。
使用 `setBevel(thickness, radius, segments)` 控制完整厚度、圆润倒角半径和细分数。
例如先设置 `setSize(0.26, 0.70, 0.80)`，再设置 `setBevel(0.12, 0.02, 6)`。
每个调用返回的 Result 都必须检查；其余端点和弯曲设置保持相同。

轮廓转角采用二次曲线柔化，正反两面边缘为四分之一圆弧倒角，保留中间平面和侧壁。
flat 模式保留箭身和箭肩圆角，头部前 45% 区间的宽度、厚度同时渐消，最终汇聚到目标 B 的一个几何尖点；尖端不保留挤出端面或竖直棱线。
三角形组为 `face` 和 `bevel`；面部 UV 是逐三角形映射，不承诺整面无缝纹理。

厚度须大于两倍倒角半径；半径至少 1e-5，且构建时须小于箭身宽度的四分之一。
倒角细分为 2–12。过紧的弯曲或过大的倒角可能让内缩轮廓相交，此时返回失败，
不会发布破损网格。该模式仍要求端点 Z 相等、bend.z 为零。

flat 的轮廓检查为二次复杂度，凹多边形面三角化最坏为三次复杂度；适合参数变化后重建，
不要在不变的帧重复执行。没有新增依赖、持久状态或对象引用。

### 头部独立加厚

`setHeadThicknessScale(scale)` 单独控制 flat 模式头部相对于箭身的 Z 厚度倍率，范围 `[1,4]`，
默认为 `1`（等厚）；其他模式忽略该参数。调用返回 Result，失败保留原倍率。
例如保持 `setBevel(0.12, 0.02, 6)`，再检查 `setHeadThicknessScale(2.5)` 的结果，
即可得到箭身厚 `0.12`、头部厚 `0.30` 的箭头。倒角轮廓保留，头部倒角的 Z 截面随厚度缩放。

厚度关于输入 XY 平面对称增加。箭肩之前四分之一头长的区间内平滑增厚，箭肩之后保留主体厚度，头部前端再同时收窄、变薄至尖点。
先沿厚度过渡区间切分表面，再做厚度变形和法线重算，避免大面三角形把增厚坡度延伸到整个箭身。
浮点切面吸附容差为头长的 `1e-6`，输出仍为封闭薄片；UV 和原 `face` / `bevel` 分组保留。
新增切分只增加固定倍率的 CPU 构建开销，不改变 flat 模式既有的最坏复杂度。

几何契约测试覆盖倍率 `1` / `2.5` / `4` 的直线、正反弯曲、头部厚度、远端箭身厚度、
封闭边与方向、法线及非法倍率（含 NaN）回滚。没有新增模块依赖、ECS/Link、事件或持久格式。


### 流线带状轮廓（flowing）

检查 `setFlatProfile("flowing")` 的 Result，可在 flat 模式启用流线轮廓；
默认 `"uniform"` 保持原等宽轮廓。未知名称返回 InvalidArgument，原配置不变；
该参数不影响 2d / 3d 管状模式。

flowing 将名义尾端宽度和厚度收至箭颈的 8%，沿路径逐渐饱满；箭肩附近平滑接入头部轴向，
并为凹角圆润过渡预留空间，避免曲线密集采样把圆角挤成小折角。尾部倒角半径按局部宽度缩小。
默认端点与默认尺寸同样支持该轮廓；细尾是小幅圆润的收尖，不是零宽退化三角形。

推荐外观参数（每次调用的 Result 都须检查）：

- `setMode("flat")`，`setFlatProfile("flowing")`。
- `setSize(0.55, 1.50, 1.40)`：较饱满的箭身和拉长的宽箭头。
- `setBevel(0.24, 0.035, 12)`：适度厚度与圆润边缘。
- `setHeadThicknessScale(1.3)`：头部略微增厚，保持一体感。
- 端点 `(-2,-0.5,0)` 到 `(2,0.5,0)`，`setBend(0,1,0)`。

构建先细分厚度变化区域，再进行尾部厚度渐变，避免大面三角形产生斜折纹。
法线按角度加权平滑；若共享平均法线偏离某一面超过 75 度，该面保留折痕法线，
确保不会因跨越过大的转角而让法线反向。UV 与 `face` / `bevel` 分组保持原约定。
轮廓、细尾厚度、平滑法线都来自真实 CPU 网格；预览中的黄色及高光来自离线材质光照。

这仍是同一 procgen 模块内的同步 owning builder，没有新增依赖、ECS/Link、事件或持久协议。
新增开销为局部路径查询、固定数量的面切分及法线归并，仍受 flat 构建的既有最坏复杂度约束。
契约测试覆盖默认参数、正反弯曲、两种轮廓、各厚度倍率、法线方向、封闭性和未知轮廓回滚。

flowing 的面部沿宽度方向带有不超过 35% 的额外鼓起，让头部和箭身具有柔和的带状体积。
因此该轮廓下 `setBevel` 的厚度和头部倍率描述名义厚度；实际最大厚度至多为名义值的 1.35 倍。
测试对该上界、上下对称性和法线方向做独立检查。


尖端是一个几何位置，UV 接缝仍可使用多个具有相同坐标的顶点索引。构建会移除尖端收束后
没有剩余面积的鼻端三角形，重新计算法线，并保留 `face` / `bevel` 分组和 UV。
契约测试要求目标 B 处不存在有宽度或厚度的端面，并继续验证收束后的封闭性、正向绕序和非退化面。
