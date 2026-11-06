# 点引力与原刚体世界的事务接入

日期：2026-10-26，未提交工作区；基线 `5f2cd482ad1f88a7e11116000823a2b139c54a61`。

## 接口与兼容边界

新增默认关闭的 L5 `physics_field_coupling` 卫星模块，依赖原 physics 与 L0 physics_field。
声明放在 physics 自有 manifest 片段，公开头前置声明提供者，cpp 才包含实现依赖。
新 `PointGravityBatch` 不进入自动启动列表，不修改旧局部重力或现有模拟步骤。

prepare 同步捕获选中活动动态刚体的质心/质量，生成点源场并求值，得到质量乘加速度的载荷，
使用原 ForceBatch 暂存。commit 再次观测相同句柄，比较世界、tick、质心和质量，再原子提交。
仅保存值、句柄和可移动候选，不保存世界/刚体指针；销毁候选取消未提交工作。
提交成功只施力，World3D::step 是唯一积分者；重复提交拒绝，原 step receipt/retry 语义保留。

同 tick 的位置或质量变化也拒绝；仅检查 tick 不足以保证观测有效。改变后恢复成相同值
对本点模型等价。速度变化不影响此瞬时位置场，有限体积形状与角运动则不在此接口模型内。
后端 float 载荷范围溢出或非零力转换归零拒绝；质量乘加速度在 double 阶段归零也拒绝。
解析场余项、float 量化和积分误差是不同来源，目前仍未提供认证总误差上界。

prepare 为场本身成本加 O(B+N) 观测/暂存；commit 为 O(B+N)。无逐对象树遍历、距离截断、
隐式 dt 或第二个积分器。公开 Result/nodiscard、成本、所有权和线程契约已写入头文件。
使用已有 PhysicsLink 和 ForceReceipt；没有新增 ECS System、事件、脚本、MCP 或持久格式，
没有架构例外。

## 验证

启用 EVENGINE_MODULE_PHYSICS_FIELD_COUPLING 后 manifest 检查通过：200 个声明。
Windows MSVC 严格警告 SHARED 真实模块编译、EVDomains 及 unit_test_physics 链接通过。
31 个适配/施力 CTest 全通过的记录为 build/multiscale-coupling-verified-tests.log。
其中新增三例覆盖双体解析加速度、保留旧局部重力、准备/提交不自行积分、状态变化拒绝、
预算失败、float 载荷下溢、移动、foreign、销毁与重复提交。

首次范围测试曾失败：测试包含新断言，但对象文件时间早于范围检查源码。
重建实现对象及 DLL 后该测试通过；不把首次失败日志覆盖为通过，最终源码仍需独立复验。
随后补充 double 乘积归零保护的最终构建/测试日志为
build/multiscale-coupling-product-range-build.log / build/multiscale-coupling-product-range-tests.log。
最终回归已退出 0，31/31 用例全部通过；以这一轮作为当前源码证据。

新模块全文件 architecture-contracts lint 通过，模块清单检查和 diff 空白检查通过。
make check 日志 build/multiscale-coupling-source-check.log 已取得终态：
529 项脚本测试、4 failures、1 error、5 skipped，与此前缓存/安装路径问题相同，门禁未通过。
后续修复上述路径问题后，完整门禁已通过；独立结果见[源码门禁恢复](2026-10-26-multiscale-source-gate.md)。

## 未完成范围

这是真实局部 SI 刚体的质心点模型接入，不是有限体积潮汐/力矩或多尺度参考系迁移。
默认关闭的配置选择不替代完整裁剪/禁用回归。OBJECT、跨平台、渲染、原生示例及
百万独立对象 60 FPS/16.67 ms 整帧验收仍未完成。
