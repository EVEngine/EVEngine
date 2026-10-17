# 道路米制、转角及横向采样

2026-10-14。此轮实现仍需真实双端验收，不代表整体核心目标完成。

outer-package scene 手册确认 Resolution 单位为米，Angle Threshold 用于保留弯道细节，Sub Segments 增加网格细节且动态路口不支持。实际参考探针（相同四 marker）在 resolution 1 / angle 4 下生成 100 vertices / 98 triangles；resolution 10 / angle 4 为 38 / 36；resolution 10 / angle 1 为 68 / 66；resolution 10 / angle 20 为 16 / 14。直接把公开 subSegments 设为 3 后，当前默认道路仍为 100 / 98；不能用字段 setter 成功证明参考已经横向细分。手册 GetAngleThreshold 与此 DLL 不符，实际证据读取公开 angleTreshold 数据。

作者数据集中在 RoadStyle：samplingSpacingMeters > 0 选择米制中心线间距；0 保留既有 pathSegmentsPerEdge 固定段数行为。samplingAngleDegrees 在 metric 模式中约束累计截面方向转角，0 关闭角度细化；crossSectionSubdivisions 为每个截面段的分割数 [1,32]。这些是 owning 作者值，不保存派生采样点。共享 type / road style 覆盖采用同一字段。既有 native 默认仍为固定段数；参考默认、精确离散规则和自定义形状的 Sub Segments 消费仍需继续确认，不将这些列为已完成。

RoadSampling.cpp 是独立纯值采样实现。使用既有中心线弧长表、初始米制间距与每段最少内部采样，细化到相邻帧方向满足细化目标，然后按累计转角和距离保留点。全路最多 4096 intervals；不足以满足要求时返回结构化预算失败，不静默降低精度。规则覆盖 forward/side/up，使坡度与横坡变化也能增加密度。与参考的顶点数量不要求逐点相等，也没有复制 DLL 算法；渲染与参考约束验收单独记录。

弧长回归定位到既有 SplinePath 跳段问题：arcTable 原来省略连续下一段的 u=0；距离刚超过 marker 时会直接选择该段 u=1/24，产生有限位移和方向跳变。现在每段保留自己的 u=0，精确 marker 位置仍保留原相邻 endpoint 参数化。独立测试在 marker 前后 1mm 查询，验证位置连续。这影响所有距离采样消费者，需要重新生成全部双端场景，不能沿用旧候选帧。

截面细分先解析原始每段材料，再线性分割 side/up，保留单位为米的宽度、UV 和整体轮廓。否则将新增节点继承为默认 curb 会错误改变 asphalt 分组。总顶点+索引预算在临时 profile 分配和写入之前检查。主驾驶面、桥面和隧道 portal asphalt 消费该字段；设施自有 shape 和 tunnel shell 维持各自独立策略，动态路口未加入该细分能力。

RoadBakeOptions.samplingScale [1,64] 是明确的操作密度参数，在 metric 模式中放大米制间距与转角目标；标准 recipe 消费它。RoadArtifact 每级 LOD 放大 scale，与既有 path/turn counts 和 junction chord error 一起降低密度；没有新增第二个生成入口或作者缓存。超出 scale/网格预算的 LOD 生成失败仍不会发布半个 artifact。原生源数据 schema v1 和 preset v2 采用可选字段，缺失默认为 0/4/1；未知对象字段沿既有策略忽略，未知版本和非法已知值拒绝，完整历史私有恢复后发布。

测试覆盖米制间距随长度增加、metric 作者策略不受固定段数覆盖、角度变化增加细分、截面细分数量与材料几何、生产 recipe、真实 artifact/store 与全部 LOD、旧字段迁移、原子非法编辑、预算失败、共享 preset undo/redo 和完整保存恢复。采样公共成本仍在既有显式 bake 边界；没有逐帧字符串查找、时钟、RNG、回调或外部指针。适用架构：唯一作者所有权、Result/nodiscard、原子恢复、格式/未知字段契约、六面接口和 core 裁剪；无例外。

## 当前双端与剩余差异

重拍全部 30 场景 / 60 对图像后为 28 passed、2 different、0 blocked。最后加入 marker 精确保留后，resolution 1 的 perspective/top 为 .995019/.995717；resolution 10 为 .991424/.991463；angle 1 为 .992365/.992072；transverse 3 的轮廓为 .992095/.995458。angle 20 为 .985079/.974611，俯视未达到固定 .98；镜像 Circular perspective 为 .972002，仍未通过。没有降低门限，也没有以增加截图像素或改变 camera 掩盖差异。

SplinePath::sampleFramesResult 的 preserveMarkers 选项使用同一个帧传输/旋转实现，插入每个连续 span 的精确作者 anchor，并标记派生 authoredMarker；普通调用默认不插入 marker。抽稀必须保留这些帧，闭合 holonomy 使用实际采样位置。距离模式下重复/不可表示的零长度 span 和超出 4097 帧预算明确失败，分块 spline 的该选项不支持；原有分块默认调用继续遵守原契约。原有通用 Spline 13 cases / 287 assertions 已运行通过。

公开 tValues 提供更强的离散证据：当前四点的三个 marker 弦长分别为 18、sqrt(265)、sqrt(336) 米，步进与 1/(弦长*5) 一致。例如 angle20 的保留 u 为 .600000262、.6142947、.349148721、.8728708，分别对应约 54、50、32、80 次 float 累加。resolution1 的首段 u=.1/.177777782/.244444445 同样落在该网格。由实际点距离可见，参考通常在达到分辨率距离后保留，也会因转角提早保留；当前 native 采用最大间距与累计帧转角，尚不等价。参数扫描、跨 marker 的保留方向、初始方向及最小角度触发间距需继续验证/替换，不能将本项整体标为对齐。

仍需处理：参考默认策略与旧 preset 默认迁移；mixed controller 真正不连续接缝下的 metric 转角语义及组合测试；参考自定义形状 Sub Segments 的实际消费；派生 artifact 的算法修订/旧缓存识别（既有 buildKey 仅覆盖 recipeId 与 Params，不把它当作代码版本证据）。这些继续保留在整体核心目标中。

最终本轮验证：STATIC 道路作者 53 cases / 8763 assertions、实际 artifact/store/LOD 1 / 71、原 Spline 13 / 287 全通过；SHARED 道路作者及 Spline 契约通过。正式 EVProcgenCoreCheck 严格警告编译通过；相对 HEAD 架构检查、10 项参考证据检查器、ruff、git diff --check 通过。samplingScale 已提供但类型不支持或非有限时返回结构化失败，不退回默认 1。默认 make check 仍仅在未修改 IStateProvider.h 三项 bool API 和 RenderSystem.h 两项生命周期文档处失败，未改基线或添加豁免。
