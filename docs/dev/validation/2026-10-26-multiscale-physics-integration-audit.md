# 真实引擎构建与物理接入成本审查

日期：2026-10-26。当前工作区基线 `5f2cd482ad1f88a7e11116000823a2b139c54a61`。

## 构建证据

四个 external 子模块已按工作树锁定提交初始化。标准 Makefile Windows Debug 配置启用
spatial_multiscale、physics_field、procgen_recursive，报告 full profile 199/199。
自动依赖路径成功安装第三方 release v0.1.1 的 win32-x86_64-Debug 包，
校验 aggregate `ea850e476dd506d8b15d9f15da2d07c7ebe1e4c0`。
使用 SHARED 七个链接组，严格警告开启；没有更改旧模块默认开关。

配置与生成成功，2589 个任务的原生 SHARED 构建已退出 0。
完整命令与日志见 `build/multiscale-engine-build.log`。

## 可复用机制与不能直接接入的原因

`physics/forces/ForceBatch.h/.cpp` 已具备单次提交、句柄复验、精确 tick/duration、
无部分施力和 pending step receipt。物理适配应复用此生命周期机制，
保持 World3D 为刚体积分唯一 owner；不得再对相同刚体调用 GravitySimulation 积分。

改动前的成本审查发现：

- `World::findBody` 和 `World3D::findBody` 遍历 bodies_ 查找运行时句柄。
- `PhysicsLink::resolve` 调用上述查找。
- `ForceBatch::aggregate` 对每条 load 调用 resolve；随后使用 std::map 聚合。
- commit 施力阶段再次逐目标 findBody。

若世界有 B 个刚体、请求有 L 条，此路径的句柄查找为 O(L*B)，还包含 O(L log L)
的聚合。现有 ForceBatch 文档只描述聚合复杂度，不能把它当成完整成本证据。
百万对象路径直接复用逐条 resolve 不满足需求；此发现要求在适配实现前完成下列工作。

## 接入前置条件

1. 增加批量解析或固定深度句柄索引，避免每条请求遍历世界；索引必须由物理 owner
   派生并随创建、销毁、恢复更新，不增加第二套可编辑真源。
2. 保留已有单条 API 行为和 stale/foreign 诊断。覆盖先销毁 body/world 两种顺序，
   恢复后旧句柄、复用槽位、失败事务和新模块关闭。
3. 聚合应使用固定宽度键的线性方法，并保留每个目标原输入的求和顺序与错误语义。
   不以未证明的哈希最坏复杂度代替强线性声明。
4. 准备与提交之间复验源/目标观测版本，防止场计算后刚体移动或质量变化导致过期施力。
5. 明确旧局部重力叠加或替换策略；默认旧行为不变。巨型有限体积、潮汐和力矩
   仍需额外适配，质心点载荷不能冒充其完成状态。

本记录是当前源码取证，不是接口实现或复杂度验收。没有新增架构例外。

## 批次内部线性匹配实现

ForceBatch 内部已替换逐条解析与 std::map 聚合，不改变公开签名、World/World3D 布局，
也不改变单条 findBody/PhysicsLink API。每次 prepare/commit 同步执行：

1. 从世界唯一 bodies_ 集合收集当前有效刚体；只在调用栈内借用指针。
2. 将刚体和请求按 `(index,generation)` 的 64 位排序键，使用八轮稳定字节基数排序。
3. 合并两个有序序列，为每条请求确定目标下标。
4. 回到原请求顺序校验并累加，保留浮点相加顺序和第一条错误的诊断顺序。
5. commit 按原句柄排序施力，直接使用本次已验证指针，不再二次 findBody。

缺失/跨世界/无效句柄仅在首条错误处调用原 resolve 取得既有诊断，随后立即返回。
故成功和失败路径均为 O(B+L) 工作/存储，而非 O(B*L)；没有按最大句柄值分配巨数组。
指针不跨帧、任务、锁或回调，批次保存的仍是原值与句柄；提交时重建匹配并复验。
世界销毁、恢复、刚体销毁不需要维护新常驻缓存，也不引入第二个 owner。

最多 8 条请求保留直接 resolve 路径，不排序整个世界。固定次数扫描与固定大小聚合仍为
O(B+L)，避免小批次无谓付出全世界排序成本；两条路径共用校验和原序累计实现。

这是对旧施力机制的内部抽象调整；未加入场/刚体适配接口。
新增 `physics_force_batch_linear.cpp` 覆盖逆序/重复目标、交错大数相消、foreign 与准备后销毁。
真实引擎已编译 ForceBatch.cpp 并链接 EVWorld.dll；新增核心模块所在 EVFoundation.dll 也已链接。
完整 SHARED 引擎构建成功后再次配置，ForceBatch.cpp 和新增测试均重新编译，
最终 unit_test_physics 链接成功，日志 build/multiscale-physics-build.log。

实际执行 67 个相关 CTest 用例全部通过（15.00 秒）：27 个施力用例，包括 3 个新增用例，
39 个新核心契约，以及名称匹配到的 1 个旧 spatial 图形用例。首次旧版本批次也通过既有
24 个施力用例，但最终验收以上述重编译后的 67 个结果为准。

```text
ctest --test-dir build/win32-debug --output-on-failure -j 2 -R "^(physics\.forces\.|physics\.field\.|procgen\.recursive\.|spatial\.)"
```

结果日志 build/multiscale-engine-contract-tests.log。这是相关路径兼容证据，不能推导
全平台、OBJECT、关闭新增模块、全量场景兼容或百万性能验收已通过。
本轮源质量日志 build/multiscale-batch-source-check.log 的终态仍为 529 项脚本测试中
4 failures、1 error、5 skipped；最新改动的架构形状 lint 与 diff 空白检查通过。

## 批量质心观测扩展

新增 `ForceBatch::captureCenters(World3D&, span<PhysicsLink>)`，复用同一匹配机制，
返回拥有 world/tick 与排序行的 ForceBodySnapshot，每行只有身份、SI 质心和质量。
只接受活动动态刚体；重复源、失效和 foreign 句柄拒绝，避免后续重复累计源质量。
快照不是第二个可编辑物理真源，也不携带刚体指针；读状态不施力、不唤醒、不积分。
成本为 O(B+L)。快照内容不能仅凭 tick 判定仍有效，因为位置和质量可以在同一 tick 内改变。
后续适配器必须在提交前重新观测并比对这些量与世界身份，不能直接使用旧载荷。

本扩展不增加跨模块 include、ECS System、事件、脚本或工具协议；公开 Result/成本/
线程与生命周期契约均在原物理头文件定义，仍由 World3D 独占物理状态。
无架构例外。真实 SHARED 的 unit_test_physics 已重新编译链接；28 个施力相关 CTest
全部通过，包含快照复制隔离、排序、筛选、重复与销毁后的失效用例。
日志 build/multiscale-center-capture-build.log、build/multiscale-center-capture-tests.log。
本轮完整 make check 在运行，日志 build/multiscale-center-source-check.log，未宣称通过。
观测接口仍是接入前置步骤，不代表长程场施力、巨型有限体积潮汐或跨尺度迁移已经完成。
