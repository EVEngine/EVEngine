# 递归模型稀疏编辑与原子发布

日期：2026-10-26，未提交工作树。本记录推进阶段四，仍未完成该阶段全部验收。

## 实现与责任

[RecursiveInstance](../../../src/modules/procgen/recursive/RecursiveInstance.h) 独占定义、
稀疏叶编辑及当前 Revision；祖先摘要是派生缓存，不能单独修改。
不可变 RecursiveModel 可由当前状态和准备中的候选共享，不保持 scene/rigid-body 指针。
叶序号不依赖当前展开层级，编辑包含质量倍数、局部位移和局部速度。
质量倍数0表示删除该源；正倍数、位移、速度均经过有限值/范围检查。
这不是新增叶槽位或接管物理积分；容量改变和动态几何适配仍待实现。

prepareEdits 接收完整稀疏账本，按叶序号规范排序；遗漏条目恢复基础规则。
它只建立候选，不改 live 状态。commit 检查弱 owner 凭证和基准 Revision 后交换状态。
丢弃候选取消发布，owner 销毁撤销候选权利；move 转移身份，外来或过期候选拒绝。
修订号耗尽明确拒绝。创建/预算/算术/内存失败不发布部分缓存。

只访问受影响的祖先路径，逆序用完整子摘要相加重建质量、质心和动量，
不从父摘要减去大量销毁叶子的矩，避免接近完全销毁时的严重抵消。
全删除子树得到精确零质量；微小剩余质量的质心由存活子摘要合成。
叶位移更新边界，父边界取子边界并集。误差仍为经测试的 binary64 容差，
尚无覆盖整个生成过程的认证算术/几何误差界。

预算分别限制编辑数 E 和缓存祖先数 S，深度 D<=52；准备成本
O(E D + E D log S + E log E + S D)，存储 O(E+S)。
查询为 O(D+log S)。源集合重建保留原有完整覆盖检查并返回当前编辑 Revision。
这些编辑/源集合准备成本不能伪装成百万场循环的 O(N) 实时性能。

capture 返回独立拥有的运行时定义/账本/Revision，没有 runtime owner 凭证。
**当前 capture 不是磁盘存档**：下一步应复用 common/Snapshot.h 的 SnapshotEnvelope，
实现 schema/version、严格未知字段、完整校验和原子恢复，不另造 JSON 序列化真源。

## 验证

MSVC Debug DLL 聚焦测试34用例、21835断言通过。GCC15同一组产品源和用例通过。
Windows 静态路径在补充取消/move/修订耗尽用例之前33用例已通过。
现有测试入口见 [数值契约构建](../../../benchmarks/physics_field/README.md)。

新增验证包括：

- 137叶模型删除一个叶、另一个增重/移动/赋速度；准备时旧状态不变。
- 父摘要与完整子源集合的质量、一阶矩和动量一致；粗化查询不丢弃编辑账本。
- 删除全部137叶质量精确为0；只保留1e-20倍质量时质心不被减法残差污染。
- 重复序号、质量比下溢、祖先预算拒绝，旧账本和Revision保持。
- 跨 owner、过期候选、owner先销毁、候选先销毁、move转移和Revision耗尽。

新公共头文件和实现全部行通过现有架构 lint。完整 make check 终态529 tests / 4 failures / 1 error / 5 skipped；
失败仍为第三方缓存及 SDK 的 Windows 路径错误，不能以这些聚焦测试替代整体验收。

## 兼容与剩余工作

仍为默认关闭的 procgen_recursive，未修改旧 Procgen、Scene、Physics 或渲染接口与执行路径。
适用唯一 owner、typed Result、显式 Revision、成本、线程/重入、DLL 导出和事务失败原子性规则，
没有兼容豁免。无新增 ECS System、provider、脚本入口或 MCP 协议。
版本化保存恢复、取消异步任务、具体几何叶生成、物理/视觉组合、完整回归及百万60FPS仍未完成。
