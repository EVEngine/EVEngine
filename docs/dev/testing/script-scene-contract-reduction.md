# 脚本场景集成契约精简

本次仅调整 `test/integration/script-api/script_api_graphics_scene_tests.cpp`，保留原来的单 CPP / 单 OBJ 合并编译。未增加 Unity 规则、来源选择宏、公共测试 header 或生产接口。按实际跨边界职责审阅，150 个父用例精简为 108 个，42 个具名 subtest；保留独有的模块缺席、回调异常、对象失效、事务回滚及保存失败检查。

## 契约和共享初始化

- Runtime：作用域和反射卸载保持独立；五种编译/执行错误、位置和栈恢复检查共享一个 Runtime。关键观察改为 REQUIRE。
- Rx：将三个仅单步验证 subject/filter/map 的片段替换为一条真实 script→native filter→map→callback 链，断言完整输出 `4,8,`。Behavior/Replay/Take 仍为具名 subtest，共享 VM 和绑定；每行独立 subject 并重置结果。跨 PlatformEvent pump 和回调抛错仍独立。
- Scene ECS：实体生命周期、view cache、延迟 detach 合成一个契约；登记/未登记组件快照合成一个；callback/update 错误传播合成一个。每组共享脚本 VM，各行使用独立场景/实体并分别清空错误计数。首次 ECS 查找、物理链接、mount、事件、序列化和 editor 事务仍独立。
- Compiler：native binding lowering 的七种变化共享 Runtime，源码元数据、诊断和工具查询五种变化共享另一个 Runtime。保留生成模块契约的真实执行，并用内存 provider→typed export/import lowering→VM 结果 42 作为小型代表。不是全仓脚本兼容性的等价替代。
- 文档脚本：attributes/transaction 共享 Runtime，保留 ECS、教程规则和教程保存失败/reload 生命周期；不再对所有章节逐一初始化并执行。
- native→script 参数：literal、pointer、string/scalar 和异常的三个 subtest 共享 VM、绑定及一次脚本编译；固定父级 stack top，行间恢复，错误上报计数使用 REQUIRE。
- 回调提供者：map layer identity/multiple layers、motion float/vector shapes、scene push/pull/script binding、sensing query-space/malformed answers 分别共享初始化。所有旧身份保留为完整名字的 subtest。provider 缺席、异常取消、销毁/退订、pull 拒绝后状态保全和坏目标不阻塞兄弟仍独立。motion 使用单调 tick，完成 optional run；scene 每行重建 observer，避免旧值假通过。

新增/修改的断言 helper 显式接收当前 zeroerr TestContext；SUB_CASE 仅借用父 VM/fixture 指针，避免 `[=]` 复制 VM。保留真实 VM、原生绑定与状态转换，使用已有内存 provider 和虚拟文件系统隔离 I/O；没有 mock 掉需要验证的跨边界执行。

## 有意减少的覆盖

15 个原身份退出父用例和 subtest，其中三个 Rx 片段被代表性链路取代；全部身份及逐项理由记录在 [历史账本](integration-audit.md)，不存在未解释缺口。

- `scriptCompiler.compilesRepositoryNutCompatibilityBaseline`：移除递归扫描仓库、为每个脚本建立新 Runtime 的 compile-only corpus。常规集成不再证明仓库所有脚本都能编译；本次没有声称已有另一条完整 corpus job 接替。
- `userDocs.economy/decision/definitions/tags/gameEvent/policyRegistry/profiler/rope`：退出逐章执行；不声称剩余代表覆盖这些章节的全部绑定或文档正确性。
- `SimpleSquirrelTest.GetterTest`：其脚本 getterTest 直接返回 true，没有 getter 观察值。`TestDefClass` 主要枚举并打印第三方 VM 私有布局，随后调用默认类构造片段；删除后不再独立证明该片段行为。公开成员/属性枚举、native direct-member 类型转换和生命周期，以及 ExportClass/RefTest 仍保留。删除不再需要的三条私有 VM header，及预览删除后不再使用的 graphics/window/spatial 等直接 include。
- `Scene.render.parentChildOrbitPreview`：90 次 present 和每帧 16ms 等待，最终只观察 handTravel，缺少像素 oracle。保留 scene 单元变换及集成同步契约；不将其等同于视觉证明。

不同生命周期/模块的 VM 不强行共享。修改没有把全文件 bundle 成一个进程，CTest 仍按父用例隔离。 无来源选择宏或额外 Unity 规则。重复的 `PrivateTestScope.h` 用于重置平台宏，属于必要边界清理，未机械删除。

## 验证方法

Windows x64 / MSVC Debug SHARED，CTest 每用例独立进程，4 并发、120 秒超时，`EVENGINE_VIEW_SECONDS=0.3`、`EVENGINE_PERF_FRAMES=30`。前后三轮顺序测量，保留全部样本；每个预期身份恰好出现一次，失败或跳过不作为有效性能结果。构建、发现和额外诊断不计时。原始日志位于 `.local-debug/script-contract-reduction/`。

修改前 150/150 每轮均通过：墙钟 65.000 / 66.541 / 64.718 秒，进程累计 102.478 / 105.826 / 101.633 秒。中位数分别为 65.000 和 102.478 秒。

首次严格断言诊断实际发现三处旧测试缺陷，均未纳入正式计时：

- 生命周期的 `beginNode/addNode` 原来缺少绑定要求的 name 参数，原 warning-only CHECK 给出名义通过；本次 REQUIRE 暴露错误，补齐三个空 name 参数后验证完整生命周期。
- 补全 API 按实现合并指定 URI 的符号与 native registry，生成的 `rts.playerCount` 合法匹配原 `player` 前缀，原 size==1 的 CHECK 假设错误。现在使用 `playerSpeed` 受控前缀，显式注入 `playerSpeedNative`，精确断言 local/function 两个候选的 label、kind、insertText，保留 hover 和诊断观察。
- 测试 helper 原来以 `sq_newslot` 写 Driver INSTANCE。底层 sqapi.cpp 只对 TABLE/CLASS 真正写入，对 INSTANCE 返回成功却未修改字段。改用 `sq_set` 并增加 x/y/z/yaw 字段回读断言，原所有 pull 坐标断言从 CHECK 升为 REQUIRE。`linkScriptAt` 确实同步调用 transform update，无需虚构额外同步时机。

独立 fresh VM 复测复现后两项，排除了最初的共享污染推断；修正根因后恢复同契约共享。三个初次失败均使进程以 1 退出，证明 child 的严格断言能令父用例失败。初次日志和修复后执行分别保留。基线 150/150 是原断言等级下的名义通过，不能据此证明旧检查有效。

最终三轮均为 **108/108 通过**，无失败、跳过或超时；13 个共享父用例另以独立进程直接运行，完整输出确认 42 个具名 subtest 全部执行并通过：

- 第一轮：墙钟 8.917 秒，进程累计 27.646 秒。
- 第二轮：墙钟 7.295 秒，进程累计 26.273 秒。
- 第三轮：墙钟 7.292 秒，进程累计 26.408 秒。

本机中位数：4 并发墙钟 **65.000 → 7.295 秒（-88.8%）**；用例进程累计 **102.478 → 26.408 秒（-74.2%）**。前后各三轮顺序测量，不是 ABBA 对照，不删除第一轮或其他较慢样本；只代表本文件本机成本，不代表全部集成测试或其他平台。

主要收益包含退出工作量：旧全仓扫描的单项中位数为 64.386 秒。排除该项后，原其余用例累计中位数为 38.092 秒，最终整个文件为 26.408 秒；这部分同时包含章节/展示删减和共享初始化，也不能解释为完全相同场景的纯执行提速。退役范围已逐项说明。原始逐例结果、配置及源码/二进制哈希见 [机器结果 JSON](script-scene-contract-reduction.json)。

保持 1 CPP / 1 OBJ；源码 5789 → 5590 行，直接 include 156 → 128 条，本地 define 和 SDL_Delay 均为 0。该 OBJ 为 31,118,530 → 30,530,929 字节。最终 graphics 目标构建通过；随后删除该目标的 EXE/PDB/ILK 并重新链接通过，当前测试 PDB 为 444,952,576 字节，原有增量 PDB 为 625,586,176 字节。PDB 下降包含清理历史增量链接残留，不全部归因于脚本重组；本次没有重新进行全库冷编译耗时/干净 PDB 对照，不据此声称编译速度的百分比提升。

冻结源码后的完整 `make check CI_BASE=20394d720951b2aa13f035dd9b16104c3de3096e` 在 MSVC 开发环境通过，退出码 0；511 项脚本回归，3 项跳过，无失败，含格式、架构、测试发现与分类、断言策略及绑定检查。固定 base 是本工作起点，避免已前进的 origin/dev 将未改动旧代码纳入差异。历史账本为零未解释缺口，未扩大断言 baseline。较早一轮门禁在重组期间读取到不同版本分类快照而失败，未记作通过；冻结后完整重跑的日志为 `.local-debug/script-contract-reduction/make-check-final.log`。

未改变生产 API、ECS、持久格式、模块边界或跨域生命周期；无架构规则例外。本次验证范围为脚本/原生组合、共享初始化的隔离、原生编译及源码门禁，不报告其他平台 CI 成功。
