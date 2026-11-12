# 场误差预算与核计算改进

日期：2026-10-26，工作区未提交；基线 HEAD `5f2cd482ad1f88a7e11116000823a2b139c54a61`。

## 改动与约束

原实现将每个远场交互限定为总误差预算乘源质量占比。直接求和没有截断误差，
但其预留份额无法被其他交互使用。现用目标树累计额度：

```text
subtreeError(v) = localCharge(v) + max(subtreeError(child))
used(v) = subtreeError(v) + sum(localCharge(ancestor))
```

仅当新交互的解析余项上界加 used 小于配置预算才接受，并向祖先更新最大值。
因此先处理后代再接受祖先时也不会遗漏已支出的额度。直接对没有截断额度支出。
累计计算向上取邻近浮点值，仍不将该账本声称为所有算术误差的认证上界。
深度有显式上限；每次交互增加 O(depth) 查账/更新，API 成本说明同步更新。
贪心分配可能把剩余额度用尽而造成后续直接对增加，并不保证更快或普适线性准入。

核展开使用最大 84 项的栈上数组和预计算后继索引，移除每次交互的三个临时 vector 分配。
算术次序、展开阶数、余项公式、精度目标和无距离截断语义保持。

## 实测

相同 Windows Release 单 CPU、seed 20261026、绝对解析误差 1e-3：

- 原 p4/leaf16 的 1000 均匀点：996650 直接对，五次中末次整段场计算 10.9107 ms。
- 仅核临时存储优化：直接对、checksum、最大余项相同，末次 10.6573 ms。
  小样本噪声较大，不作统计显著提速声明。
- 增加累计预算后：981784 直接对，1968 远场交互，末次 11.2326 ms，
  最大余项 0.00099999564635；此小输入更慢，不能据此声称整体性能改善。
- 10000 均匀点，累计预算 p4/leaf16 在 63.6968 ms 触发近场预算拒绝；
  p6/leaf16 在 207 ms 触发相同拒绝。提高阶数没有解决这组输入的准入问题。
- 旧质量占比分配的 p6/leaf4 在 3496.48 ms 触发遍历预算拒绝；
  p6/leaf16 在 1033.13 ms 触发近场预算拒绝。拒绝用时不是完成一帧的用时。

原始 CSV 留在 build/field-before-kernel*.csv、field-after-kernel.csv、field-residual*.csv。
基准新增显式 order/leaf-capacity 参数并输出 CSV，默认值保持不变。

## 验证与未完成项

Windows Debug DLL：39 cases、24227 assertions 全部通过；Windows/GCC 静态链接 CTest 通过。
新增场模块全文件 architecture-contracts lint 与 `git diff --check` 通过。
运行中的 `make check` 已取得终态：529 项脚本测试，4 failures、1 error、5 skipped，
仍为缓存/安装路径相关失败，记录于 build/multiscale-moments-source-check.log。
新增混合深度/质量的 384 点直接参考，p2/p4/p6 均检查逐目标累计上界及实测误差；
原事务、坐标、生成与恢复用例继续通过。测试中的独立 roundoff allowance 不冒充认证上界。
无新增公开 API、ECS、跨域依赖或持久格式；只更新已有新模块的成本说明，无架构例外。

百万独立对象 60 FPS/16.67 ms 整帧仍未达标。当前结果说明仍需重新优化空间划分、
误差分配与并行求解；不能靠缩小对象数、扩大误差或删除远源完成验收。

## 被撤回的占用中心实验

在保持八叉树分区不变的前提下，将每个展开中心移到实际点包围盒中心，再重算半径。
1000 均匀点直接对降为 964065，但第三次场总耗时为 13.399 ms，构树/远场工作增加；
10000 均匀点仍在 108.097 ms 因近场预算拒绝。比较上一节累计预算版本的 11.2326 ms，
没有实际收益证据，因此已撤回该代码，不作为已交付优化。实验 CSV 保留在
build/field-occupied-1000.csv 和 build/field-occupied-uniform.csv。

为推进完整引擎集成，已按锁定提交初始化四个 external 子模块，并启动仓库标准命令：

```text
cmake/with-msvc.cmd make build/win32-debug JOBS=8 CMAKE_EXTRA_ARGS="-DEVENGINE_MODULE_SPATIAL_MULTISCALE=ON -DEVENGINE_MODULE_PHYSICS_FIELD=ON -DEVENGINE_MODULE_PROCGEN_RECURSIVE=ON"
```

初始配置已报告 full profile 199/199 模块；日志 build/multiscale-engine-build.log。
构建正在准备依赖，尚未取得完整引擎编译/链接或运行通过证据。
