# 逐 marker 横坡轴心

2026-10-14。此项实现不代表完整道路核心功能已对齐。

唯一作者值为 RoadControlPoint::rollCenterOffset，范围 [-.5,.5]：-.5 左驾驶面边缘、0 中心、.5 右驾驶面边缘。对应 EasyRoads SetMarkerTiltingCenter 的值为 offset + .5；参考实际 API/mesh 证据确认左右端值固定对应边缘。使用有符号 offset 能让反向精确取负，双反向恢复作者位模式，避免 1-fraction 的浮点舍入漂移。零值反向保持正零。

native edgeToSpline 使用实际驾驶面宽度，换算为 SplinePoint::rollPivotSide。局部 offset 与 roll 共同使用已有逐段 SmoothStep/Linear 策略，并乘当前 scaleX；generic 默认 Linear 保持原约定。逐 marker 变化实际探针区分了 Linear、SmoothStep 和 Catmull 候选，native 采用通过真实驾驶面轮廓对照的 SmoothStep 道路默认。截面旋转后，frame 原点平移 oldSide*pivot - rolledSide*pivot，因此选定轴线位置保持不变。普通 evaluate/travelResult 仍返回作者中心线位置；sampleFrames/travelFrameResult 返回实际截面原点，frame 消费方的道路 mesh、导航、标线、设施与 placement 使用同一投影。任意截面的驾驶面范围由已有 asphaltHalfWidth 定义；独立左右非对称 lane 配置仍是另项缺口。

原生和 editor 使用共同可选 point codec。roadNetwork v1 和 preset v2 新字段缺失均默认 0；未知对象字段沿既有策略忽略，未知版本拒绝。shared type / road / marker 覆盖继承相同字段；保存所有历史与 redo cursor，恢复构建私有候选后发布。非法 live edit、预设 prepare 和 snapshot restore 由共同作者验证拒绝。按参数拆分的新增 marker 保存插值轴心；反向与 roll 一起取负而不转成派生缓存。

实际双端新增 bank-pivot-left/right：左 perspective/top .998012/.999955；右 .998945/1.000000，全部通过固定 .98。逐 marker 变化的 bank-pivot-varying perspective/top .999175/.999978，通过固定 .98。当前 25 场景 / 50 对图像为 24 passed、1 different、0 blocked；镜像 Circular 仍未通过透视，保留清单。驾驶面 unlit 轮廓验证没有扩展成 terrain、UV、材质、交通或完整 prefab 渲染一致的声明。

测试覆盖固定边缘几何、单独 travelFrame、作者中心线不变、标准 recipe 实际 mesh 消费、JSON round trip、旧字段迁移、非法编辑/预设原子拒绝、反向、拆分、undo/redo 与完整历史保存恢复。接口值自有，无异步任务、回调、额外 provider、时钟或 RNG；同 owner 线程同步访问，派生帧不持有作者指针。成本仅在既有采样/bake 边界增加每帧常数运算。

架构适用项：唯一作者所有权、Result/nodiscard、原子恢复、可选字段迁移及未知字段策略、六面契约、无向上模块依赖、分级裁剪；无例外。增量及默认源码门禁、STATIC/SHARED 与正式 core 构建的执行结果在本轮验证记录中明确列出。

最终验证：STATIC 集中道路 50 cases / 8650 assertions 与真实 artifact/store/LOD 1 case / 71 assertions 通过；SHARED 集中回归通过，正式 EVProcgenCoreCheck 严格警告构建通过。参考证据检查器 10 项、ruff、git diff --check、相对 HEAD 架构检查通过。默认 make check 已执行，未通过原因仍为未修改 IStateProvider.h 的三项 bool API 和 RenderSystem.h 的两项生命周期文档，未增加豁免或更换基线。

19-case 实际 Unity 数据探针包含逐 marker 变化轴心；最终变化场景的非中点拆分断言区分缓动与线性插值，位移溢出以结构化失败返回。
