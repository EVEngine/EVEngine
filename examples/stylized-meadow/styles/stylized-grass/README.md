# 默认风格化草坪 / Stylized Grass

草叶由 CPU 上的矢量曲线定义。初始化时在内存中栅格化两种草型、各四帧的 alpha 遮罩，并直接上传 GPU。蓝噪声根点也在 CPU 生成并上传；不读取或写出 PNG、RGBA、SPIR-V、草根二进制文件。内嵌 shader 负责采样、三阶配色和 GPU 实例化，不在 shader 中生成草叶形状。

## 接入

新构建的引擎提供 CPU 生成接口；旧 SDK 不支持。复制此目录的脚本到游戏 `styles/stylized-grass/`，SDK 中对应 `share/eve/styles/stylized-grass/`。运行时只需要游戏代码与引擎，不需要草的素材目录。

```squirrel
dofile("styles/stylized-grass/style.nut");
dofile("styles/stylized-grass/scatter.nut");
local grass=null,field=null,seconds=0.0;
local identity=[1.0,0.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,0.0,1.0];

// eve_init 内：先创建相机、光照、世界坐标地面网格。
local settings=stylizedGrassScatterDefaults();
field=stylizedGrassBuildField(settings,
    function(x,z) {return 0.0;}, // 替换为自己的地表高度
    function(x,z) {return true;}); // 可排除小径、水域等
// 一次生成并上传，之后可释放 field.records，只保留 banks。
grass=StylizedGrassStyle(field.records,{});
local reference=grass.options.referenceGround;
groundRenderable.setTint(reference[0],reference[1],reference[2],1.0);
groundRenderable.setRoughness(1.0);
groundRenderable.setReceiveShadow(true);
gfx.setMsaaSamples(1);

// eve_update(dt) 内，暂停时停止推进 seconds：
seconds+=dt;grass.setTime(seconds);

// eve_render 内：
gfx.clear();gfx.render3D();
local bank=field.banks[0];
grass.draw(groundMesh,identity,bank.first,bank.count);
```

`groundMesh` 与根点使用世界坐标；其网格必须与场景中已经渲染的地面一致。`identity` 为单位矩阵。每个场景每帧调用一次 `draw`，多个覆盖区先合并根点。构造函数上传内存并生成遮罩，不在帧循环中调用。

## 默认参数

`stylizedGrassDefaults()` 每次返回独立的表；`StylizedGrassStyle(records, overrides)` 第二个参数可覆盖同名字段。

- 三阶线性 RGB：嫩绿 `[0.30,0.53,0.065]`、绿 `[0.09,0.29,0.025]`、深绿 `[0.028,0.12,0.014]`。
- `darkThreshold=0.14`、`lightThreshold=0.23`；固定参考地面 `[0.25,0.43,0.11]` 测光，草与可见地面补色使用同一张同帧快照，避免底色反向改变草的分档。
- `frameSeconds=0.80`、`swayPeriodMultipliers=[1.0,1.25,1.5,1.75]`；四组周期 3.2、4.0、4.8、5.6 秒。2 米空间分组固定，每簇独立相位。
- `rareVariations=true`；预先选出的稀疏蓝噪声点执行深绿→绿、嫩绿→绿、绿→深绿。动态开关用 `setRareVariations(enabled)`。

构造后修改 options 表不会重新上传；动画秒数用 `setTime(seconds)` 注入。散点选项来自 `stylizedGrassScatterDefaults()`：种子 7319、半边长 17.8、间距 `[0.1633,0.27,0.36,0.48]`、宽 0.64、高 0.65、稀疏比例 0.006、稀疏点间距 2.7。所有生成只发生在初始化阶段。

原生 Bridson 蓝噪声每点 30 次候选；固定种子可复现。脚本再按调用方规则剔除根点、贴合高度，并对真实根点作独立硬核排斥选取稀疏点。回放由调用方注入时间、种子和高度函数；跨编译器/GPU 浮点边缘不承诺位级一致。

## 内存及 API 契约

`gfx.buildStylizedGrassPoints(radius,extent,seed)` 返回 Result.value 中交错的 `[x,z,...]`，原生 CPU 生成；半径和范围必须为正有限数，`radius >= extent/1000`，种子非负，最多 49,999 点。

`gfx.buildStylizedGrassShader(records,"grass"|"ground")` 返回结构化 Result 和由 Graphics 拥有的 shader 借用句柄；错误不返回可用的部分 style。`records` 为平坦数值数组，每 16 个 float 为列主序有限可逆仿射矩阵：`(宽,0,0,0)`、`(0,高,0,0)`、`(素材组,稀疏标记,1,0)`、`(世界x,世界y,世界z,1)`。组/标记均为 0/1，宽高为正；上传最多 64 MiB。接口同步复制数据，调用后原数组可释放。`ground` pass 不生成遮罩。

所有 API 限定渲染/VM 所在线程。Graphics 拥有原生 GPU 资源，style 包装对象保留脚本句柄到场景结束；初始化临时 CPU 遮罩和矩阵副本在同步上传后释放。高度/剔除回调仅在散点初始化期间同步调用，未持有锁；无帧内回调、逐草簇脚本更新或全局 RNG 状态。每个失败 Result 都检查并转为有上下文的异常。

成本：生成与上传是一次性的初始化工作；默认四档共约 4.7 万条矩阵，草与地面各上传一份。每帧一次全分辨率 SceneColor 快照、一次地面补色和一次草实例绘制。已验证 Vulkan 单采样；其他后端或无同帧快照时明确返回失败。被遮挡根点仍可能采到可见物体颜色。

## 维护

草叶矢量参数与 CPU 栅格化代码在 `src/modules/graphics/ShaderResourceScriptBindings.cpp`，固定 96×96 单元、4 列 2 行、3×3 超采样；CPU 生成白色 RGB + alpha。初始化后的四帧遮罩仅在内存/GPU 中存在。

GLSL 源在 `src/modules/graphics/shaders/stylized-grass/`，用 `python scripts/compile_stylized_grass.py` 生成内嵌 C++ shader 代码，再编译引擎。GLSL 编译器仅为维护依赖，部署无需携带。修改脚本后同步示例副本 `examples/stylized-meadow/styles/stylized-grass/`。本例不新增引擎持久化格式或模块边界。
