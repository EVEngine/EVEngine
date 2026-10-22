# 变化横坡的真实参考契约

2026-10-14。整体核心对齐仍未完成。

通过 `python scripts/road_parity.py probe-unity-contracts --unity <Unity.exe>`，在已准备的外层 EasyRoads 项目中真实创建 13 个独立场景。每个场景导出实际 API 值、145 个距离采样、真实 renderer mesh 顶点/索引；不读取或复制 DLL 算法源码。报告及源码/DLL/Unity exe SHA256 证据保存在 build/road-parity/unity-road-contracts*.json。此路径使用独立 adapter，不改变现有 surface-only UnityCapture.cs 的证据。

实测：默认逐 marker spline strength 为 0.5；输入 0 被 API 限制到约 0.01，输入 2 限制到 1；Spline / StraightXZ / StraightXZY / Circular 均能在 marker 1 设置并实际改变路径。StraightXZ 保留平滑 Y，StraightXZY 的 Y 为线性。手册对 GetMarkerTilting 与 GetMarkerTiltingCenter 的返回类型标注 Vector3，但实际外层 DLL 返回 float，已由 Unity C# 编译和实际执行确认。后续以实际 API 与几何事实为准。

变化横坡夹具为三个共线 marker x=-18,0,18，角度 -10,15,-10、中心轴心。参考道路有 82 个成对截面；按已知 Catmull 中心线逆解段参数 u 后，角度与 `lerp(a,b,u*u*(3-2*u))` 吻合，最大角误差约 0.0000173 度。以弧距或世界 x 直接计算同一 easing 无法表达同一契约。参考 GetPosition 的 Y 有约毫米量级插值扰动；精确角度取实际截面边向量。

新增 RoadRollInterpolation（Linear / SmoothStep）。新 RoadStyle 使用 SmoothStep，原生 generic SplinePoint 默认 Linear；路网的共同 bake 和 split 采样投影此作者政策，导航、设施等共用该路径。没有新增独立横坡缓存/第二作者状态。枚举及作者角度继续在 native validate 中校验。

既有 roadNetwork schemaVersion 1 加可选 rollInterpolation，缺失迁移为 Linear，因此旧 JSON 不改变驾驶面。完整 preset 默认根语义必须迁移：roadPresets 保存升为 version 2，load v1 时对无该覆盖的根类型插入显式 Linear，使所有子类型及绑定继承旧行为；新 v2 根使用 SmoothStep。整段历史仍先私有解析、验证再发布，未知版本拒绝。政策可被共享类型、实例覆盖、保存/恢复和 undo/redo 承载。

测试新增：真实参考段采样数值、generic 默认 Linear、非法枚举拒绝、网络 JSON 缺字段迁移、新 preset v2、v1 根默认迁移和政策编辑 undo/redo。STATIC/SHARED 及实际双端图像验收另见本轮执行证据。适用架构规则：Result、唯一作者事实、持久格式迁移与原子恢复、六面边界、分级裁剪，无例外。

逐段 controller、strength、可移动轴心尚未实现；此次探针是它们的真实参考数据，不能算功能完成。先进桥隧/设施/地形真实双端验收仍待补。

当前实际 silhouette compare：16 场景、32 对图像，16 passed/0 different/0 blocked，IoU 阈值固定 0.98。variable-bank 透视 0.997533909，俯视 0.999887151；curve-width-bank 透视 0.994755298，俯视 0.995364385。Unity 当前夹具全部重新生成，EVEngine 重新捕获受变化横坡影响的两场景；其他既有帧通过当前 fixture SHA256/图像 SHA256/真实 DLL/adapter 证据校验，未发生输入或几何变化。这是驾驶面 unlit 轮廓结论，未验收 UV、光照、交通、完整桥隧/设施/地形。

最终验证：STATIC focused 42 cases / 6528 assertions 加实际 artifact 1 case / 70 assertions 均通过；SHARED focused 同样 42 / 6528 通过；正式 EVProcgenCoreCheck 重建 28 个受影响对象并通过。10 个证据工具单测及 ruff 通过；11 个高级作者场景以当前代码重走标准 recipe 并与直接 bake positions/indices 一致（roll-functional-scenes）。默认 make check 仍仅报告此前 IStateProvider.h 三项 bool 返回及 RenderSystem.h 两项裸指针寿命说明；本轮 HEAD 增量架构检查通过，没有基线/allowlist 改动。
