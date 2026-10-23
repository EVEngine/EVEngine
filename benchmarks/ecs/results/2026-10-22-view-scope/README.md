# View 作用域与迭代排查记录

日期：2026-10-22。基于上一批位图/空提交优化继续排查，本轮交付两个正确性修复和一个待收紧的语义问题。实体类型分类、组件布局与 Handle 模型保持不变。

## 已修复：构造失败泄漏 defer 深度

原构造函数先 `begin_defer()`，再准备组件。默认构造函数或准备阶段抛异常时，View 对象没有构造完成，其析构函数不会运行，导致多出的 defer 层一直保留。之后正常创建的实体被标记 staging，普通 View 退出也无法回到零深度发布它们。

[修复前日志](constructor-before.log)记录两个真实 View 回归用例均失败：普通构造失败后 Table 仍处于 deferred；嵌套 View 失败后，外层 scope 退出仍无法发布实体。此前只在 ComponentBuffer 层验证构造异常重试，没有覆盖真实 View 构造入口，本轮补齐该生产模板路径。

最终实现保留原 View 字段与析构路径，在准备阶段异常处理分支中恢复本次 defer 层，再重新抛出异常。嵌套外层 scope 不会提前提交；已成功构造的组件仍保留以供重试，不宣称整次准备具备回滚语义。没有新增公共 API 或成员缓存。

## 已修复：无组件 View 依赖零长度数组扩展

`View<Entity>` 的组件包为空，原比较实现会生成零长度 bool 数组。GCC 的 GNU 模式可能接受，但严格 C++17 会拒绝，见[修复前编译诊断](cpp17-before.log)。

最终实现仅为空组件包增加 `if constexpr` 分支；非空组件比较仍走原路径。新增独立 `ecs_entity_only_compile` 目标，强制 C++17、关闭语言扩展，并在 GCC/Clang 上使用 `-pedantic-errors`。运行时还覆盖基类/派生实体、无组件遍历中的创建/销毁、staging 发布及最终空表比较。

## 未采用的性能候选

曾测量“成员 ScopedDefer + 短路折叠比较”的候选组合。该组合在部分场景变慢，没有稳定收益证据，因此没有作为最终实现。最终补丁采用不增加 View 成员的异常恢复，并保留非空包的旧比较路径。

[候选 CSV](candidate.csv)中的 `after` 指未采用的候选，**不是最终修复**；不得将这些数字当作最终版本的性能变化。测试用相同输入、同一 CPU affinity、三轮交替顺序，样本是三次 tick 的平均值；共享 VM、频率及外部竞争未受控。dense4 的一个批次均值 p95 达 11140.34ms，说明该轮尾延迟有严重离群值，不能据此确认代码改动与抖动的因果关系。本轮不宣称提升吞吐或消除了性能回归风险。

## 新发现：已销毁槽位仍被补建组件

创建 130 个实体、完成一次组件 View、销毁其中 129 个，再创建同样 View：

```text
live=1 new_default_constructions=129
```

`engageUpTo` 按 registry 的历史槽位范围补建组件，未结合实体存活状态。对于昂贵默认构造组件，这不仅是扫描成本，还会重新构造已销毁槽位的值。该现象与原有实现一致，本轮保留它，避免把异常修复和补建语义变更混为一次提交。

[独立诊断源码](dead-slot-probe.cpp)可从仓库根目录复现：

```sh
g++ -std=c++17 -O2 -pedantic-errors -Iexternal/ECS.hpp/src \
  benchmarks/ecs/results/2026-10-22-view-scope/dead-slot-probe.cpp \
  -o /tmp/ecs-dead-slot-probe
/tmp/ecs-dead-slot-probe
```

该程序用于验证当前现象，没有加入回归门禁以冻结这种行为。下一项实施前应明确 View 只补建哪些活体/staging 槽位，并补充构造/析构次数、删除复用、继承及提交边界的契约；同时测量一次稀疏准备与稳态扫描，分别归因。

## 最终验证与范围

- [CTest](ctest.log)：14/14 通过，包括原生、OBJECT、SHARED 三个核心契约入口及严格 C++17 入口。
- [核心契约](contracts.log)：13 个用例、1012 个断言通过；本轮新增普通/嵌套 View 构造失败恢复和无组件查询用例。
- [百万实体 smoke](final-smoke.csv)、[配置与校验日志](final-smoke.log)：最终实现全部 10 个场景通过，samples=3、warmup=1、iterations=1；仅作容量/正确性验证，不用于性能比较。
- ASan/UBSan 最终契约运行通过；ptrace 环境下关闭 LeakSanitizer，不宣称泄漏验证。
- 补丁的初次应用、重复应用和整个序列重放通过。
- `make check` 完整通过（base=origin/dev，313 个脚本测试）；clang-format 18、ruff 与文档链接检查通过。

本次修复没有增加 View 成员或改变其布局；上一批优化增加 Table 内部状态的完整重编译要求仍有效。未运行完整图形引擎启动或跨平台测试，不据此宣称 Scene、脚本和 GPU 的性能变化。

适用架构规则：`R-COST-5` 更新失败/准备成本契约；`R-MECH-1/2` 保留类型化 View、无逐实体字符串或能力查询；`R-FACE-1/2` 没有新增模块交换或平行契约目录；`R-BOUND-1/3` 只对真实构建运行的核心 OBJECT/SHARED 路线作结论。保持单一 Table 生命周期权威、代际身份、嵌套 defer 和已有组件补建语义，无架构例外。

[环境、源码与二进制 hash](environment.json)及[候选原始日志](candidate.log)用于追溯。补丁通过既有 CMake 依赖修补机制接入，引擎和独立 benchmark 使用同一份实现，子模块 pin 未变。
