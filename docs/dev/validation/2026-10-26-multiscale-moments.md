# 递归模型二阶质量矩

日期：2026-10-26。基线 `5f2cd482ad1f88a7e11116000823a2b139c54a61`，工作区未提交。

## 实现

`EditedNodeSummary::centralSecondMomentPerRootMass` 保存相对当前质心的对称二阶质量矩，
按 xx、yy、zz、xy、xz、yz 排列，以名义根质量归一化，长度使用根局部单位。
物理量需乘名义根物理质量与 `2^(2*lengthExponent)`；惯性张量为 `trace(Q)I-Q`。
巨大尺度不能先将此缩放强转为 double；后续适配须保留尺度或显式检查范围。

均匀盒体叶子的对角项为 `massFraction * halfExtent² / 3`，平移不改变叶子的中心矩。
两个子摘要合并采用：

```text
Q = Qa + Qb + (ma * (mb / (ma + mb))) * (ca - cb)(ca - cb)^T
```

这样无需从巨大原点二阶矩中减去质心项。零质量分支不贡献偏移项；正质量合并权重下溢、
二阶矩溢出返回 Unsupported，候选不发布。误差仍是 binary64 容差契约，未声称认证总误差。
摘要缓存从权威定义与修改重建，不改变快照 schema，也不增加第二个可编辑真源。
每个祖先增加固定六个分量，渐进时间/空间复杂度保持原编辑预算约束。

## 验证与边界

Windows MSVC Debug DLL：38 cases、21914 assertions 全部通过。
Windows Debug 静态链接、Linux GCC Debug 静态链接同源 CTest 均通过。
测试验证有限体积解析值、不同质量与非轴向相对平移的交叉项、共同平移不影响中心矩、
全删除精确零、保存恢复重建一致，以及超范围准备失败不改变当前摘要。
新增公开值字段有 Doxygen 单位/所有权解释；完整新增模块 lint 和 diff 空白检查通过。
没有增加 ECS、Link、模块依赖、脚本入口、全局状态或架构例外。

当前点源 GravityField 尚未消费该张量。这是有限体积场/惯性适配的数据前提，
不代表巨型刚体潮汐、力矩或跨模块集成已完成。百万独立状态的 60 FPS 整帧目标仍未达标。
本轮 `make check` 运行日志：`build/multiscale-moments-source-check.log`。
终态为 529 项脚本测试、4 failures、1 error、5 skipped，源质量未通过。
