# 出段 StraightXZ / StraightXYZ

2026-10-14。整体核心对齐仍未完成。

RoadControlPoint.pathControl 定义本 marker 到下一 marker 的跨度，0 Spline（默认）、1 StraightXZ、2 StraightXYZ。XZ 使用线性 X/Z 和原 Catmull 平滑 Y；XYZ 使用线性三维位置。非 Spline 控制不应用 splineStrength；width/roll 的独立作者政策不变。generic SplinePoint.spanControl 默认 Inherit，保留既有全路径 kind；road 适配器通过共同 spline 投影绑定控制。

控制包含 bake（设施、导航等）、拆分点采样、结构区间长度和编辑交点采样。反向控制按出段重排而不简单依附逆序 marker；两次反向恢复原完整作者快照。段内插入点继承控制，按 marker 切分保留数据；没有新增拆分后原连续曲线完全保持的承诺。

native/editor 共用 pointProfiles 新可选 pathControl；旧快照缺失保持 Spline，未知值拒绝候选。共享类型/道路/marker 覆盖支持 optional 控制，包含 canonical 保存、私有恢复、undo/redo 和标准 recipe/artifact 输入。预设默认值不变，仍为 v2，不引入第二作者 owner。名字 StraightXYZ 在实际 EasyRoads DLL 中为 StraightXZY，真实参考 adapter 明确映射并校验 getter。

测试覆盖 XZ 平滑高度、XYZ 线性高度、逐采样反向几何和双反向作者相等、版本迁移、非法字段拒绝、共享覆盖及保存后的 undo/redo。真实双端加入 control-xz / control-xyz 四 marker 混合控制场景；验收继续使用固定 .98 阈值，仍仅衡量 unlit 驾驶面轮廓。

适用规则：Result/强状态校验、唯一作者所有权、持久格式可选字段迁移、原子恢复、无向上依赖、六面边界与 core-only 裁剪，无架构例外。Circular、显式 handles、横坡轴心及其他清单功能仍未实现。

真实参考进一步确认：直线段影响后继 Spline 的水平起始切向，取后段弦线在前段直线方向上的投影乘 2*strength，垂直切向置零。新增不等长 control-short-straight 探针区分前段长度/平均切向/后段投影，实测后段 x 切向 24（前段 8，平均为 16）。未约束后段时两场景透视约 .93、俯视约 .958，不能算通过。

road marker 新 alignStraightStart/End 默认 true/false，generic spline 默认 false/false。反向时随出段交换起/末端约束，保留原几何并使双反向恢复原作者状态；这些 owning author policy 可被预设覆盖、保存和撤销。约束端投影 XZ 并将垂直切向置零；跨度内仍使用 Hermite/Catmull 高度插值。水平退化轴不作投影。Straight 控制自身不应用 strength，但它的后继 Spline 仍使用其自己的 strength。

高差 control-steep-straight 探针把终点 y 改为 -20，反推后继曲线起始 Y 切向约 0.016（参考扰动约 0.003–0.024），明显区别于旧 Catmull 的 -11。已据此把约束端 Y 切向置零，而不是只满足初步 .98 门槛。

最终当前 comparison.json：20 passed / 0 different / 0 blocked（40 对图像，IoU .98 未变）。control-xz 透视 .992541069、俯视 .995384704；control-xyz 透视 .992069598、俯视 .995435643。参考 API/几何探针已扩为 15 场景，包含不等长与高差验证。

最终 STATIC focused 46 cases / 7568 assertions 加真实 artifact 1 case / 70 assertions，SHARED focused 46 cases 均通过；正式 core-only 重建 28 个受影响对象通过；11 个高级场景重走标准 recipe 与直接 bake 相同（controllers-functional-scenes）。10 个工具单测与 ruff、HEAD 增量架构检查通过。默认 make check 仍仅被 IStateProvider 三项 bool 返回和 RenderSystem 两项裸指针寿命说明阻断；无基线/allowlist 变更。仍不宣称完整引擎 runtime/provider L3/L4 或高级 Unity 双端验收完成。
