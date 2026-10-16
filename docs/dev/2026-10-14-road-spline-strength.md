# 出段 Curve Strength

2026-10-14。核心对齐的一项进展，整体仍未完成。

RoadControlPoint.splineStrength 是 marker 到下一 marker 的 Catmull/Hermite 切向强度，默认 .5，作者合法范围 .01–1。d1=(next-previous)*strength，d2=(following-current)*strength；两端位置不变。只改变当前出段的切向系数，width/roll 的作者插值政策保持独立。generic SplinePoint.spanStrength 使用同一边界；非 Catmull 的 kind 不应用它。默认 .5 保持旧计算路径，避免无意义的全局浮点变化。

来自真实外层 DLL 的参考探针确认：默认 .5，API 将 0 限为约 .01、2 限为 1；非默认强度的中段 XZ 几何与该 Hermite 模型残差约 1.2e-6 米以内。参考 Y 存在约 .005–.02 米生成扰动，不能宣称逐顶点完全一致。native 作者核心显式拒绝非法范围而不偷偷修改作者输入，遵循已有 Result/强约束规则；后续 UI 的输入归一化是另一边界。

全部共同路径投影包含强度：road bake（含设施/导航）、拆分采样、结构区间长度分配和编辑器交点采样。反向时先逆序几何，再把出段强度移动到对应反向跨度；未使用的终点值也保留，使两次反向恢复完整原作者快照。按 marker 拆分保留字段；段内插入点承接原跨度强度。现有拆分会重建邻居 Catmull 切向，未新增“拆分后连续曲线严格保持”的承诺。

native/editor 共用 pointProfiles codec 新可选字段，缺失保持 .5；共享类型/道路默认与逐 marker 覆盖均可拥有该值，包含保存、原子恢复、undo/redo 和标准 recipe/artifact 消费。预设 root 默认没有变化，仍使用 roadPresets v2；没有第二套作者状态或派生缓存。

真实双端加入 strength-quarter（marker 1=.25）与 strength-full（marker 1=1），其余 marker=.5；实际 EasyRoads SetSplineStrength 调用生成参考 mesh 和帧，EVEngine engine-owned readback 读取当前 native 输出。当前 comparison.json 18 场景/36 对图像全部通过原固定 IoU>=.98：quarter 透视 .993817804/俯视 .996937235；full 透视 .995407037/俯视 .997044519。仍仅验收 unlit 驾驶面轮廓，不代表全设施/地形/交通/UV 对齐。

适用架构规则：唯一 mutable 作者所有权、Result、版本化可选字段迁移、私有候选原子恢复、无向上 include、六面边界与正式 core-only 裁剪。无例外。逐段 StraightXZ/XYZ/Circular、显式 handles、轴心及其余清单事项继续实现。

最终验证：STATIC focused 44 cases / 7049 assertions，加生产 artifact 1 case / 70 assertions；SHARED 同样 44 / 7049，全部通过。正式 EVProcgenCoreCheck 28 个受影响对象重建通过；11 个高级作者场景从当前标准 recipe 生成并逐个与直接 bake 相同，目录 strength-functional-scenes。10 个参考工具单测和 ruff 通过，增量 HEAD 架构审计通过。默认 make check 仍仅报告既有 IStateProvider 三项 bool-return 与 RenderSystem 两项 raw-pointer lifetime 说明；没有修改基线/allowlist。
