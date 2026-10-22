# Sensing 编译式环境查询（三批实施）

依据：UE 5.3.2 公开镜像提交 c865e168d0935b8e5f4bd865ddcc1c733c8ce7cf 的
EnvQueryManager、EnvQueryInstance、EnvQueryTest、EnvQueryTypes；本实现独立编写，
采用本仓 Result、强类型坐标与 consumer-owned LOS 协议。

## 批次与实现边界

1. 候选收集与结果发布拆分；旧 Preset 编译为拥有型计划；新增定义/计划/实例与统一测量协议。
2. 拥有型 Context 快照、Input/Ring/Grid 生成器、多 Context 聚合、固定/相对归一化、评分贡献、
   有序 Option、版本化诊断，以及 SensingWorld/脚本/MCP 组合入口。
3. 可重排测试段内将纯过滤测试按成本提前；评分测试保留累加顺序；Best 模式安全延迟昂贵纯过滤验证。

预算调度、多线程、导航投影、持久化作者资产、随机选取和图形编辑器不属于这三批。
实例保存独立状态但目前同步执行，不能宣称已提供跨帧调度。

## 唯一真源与兼容

SensingWorld 继续拥有主体事实和空间索引。collectCandidates 为 query、旧 Select、
environmentInput 共用的收集实现；临时位置不写入主体库。环境查询的每个指标只测量一次，
过滤、聚合、评分、有效索引与解释由 EnvironmentQuery.cpp 统一执行。

旧 ITargetingTask 是有序、可修改候选的兼容扩展协议，不能假装它具有纯指标协议的交换律。
TargetingPlan 只消除运行时任务名查找、提前截断和中间结果发布，不擅自改写自定义任务。
circle/box/query 的公开排序与数量契约保留。新位置查询返回独立的拥有型结果，不让旧
Candidate.id 承担位置身份，也不让旧 resultAt 缓存混入无主体的位置。

## 生命周期、线程与确定性

- 编译计划是不可变配置快照，以共享所有权支持定义、计划、实例分别销毁；自定义 metric
  必须不可变，不得用可变外部捕获伪造 reorderable。没有全局计划缓存或新 provider 注册表。
- 实例复制请求值，Context 按名字在 admission 阶段解析成索引；运行热循环不做名字查找。
- services 使用 OptionalRef 同步借用，不进入实例。服务必须活到 run 返回，缺席为 Unsupported。
- 不新增 ECS System、Link、实体父类或持久句柄；生成点是查询内数据。
- 不读取 wall clock 或全局 RNG。同一输入、后端输出和平台上顺序确定；几何浮点跨平台为
  tolerance-bounded（距离/圆环测试采用 1e-4 几何容差），不宣称跨平台 bit-exact。
- 固定范围评分允许安全重排；相对范围评分冻结作者顺序。部分验证只适用于 Best，测试屏障
  阻止移动；测试缺席不伪装为通过。
- Context 快照不代表物理世界快照。调用方在使用延迟结果前负责领域有效性再验证。

## 模块边界与机制选择

遵循 R-MECH-1：每次查询、已知单提供者、同步读取、无事务写入，故使用调用方注入的
ILineOfSightQuery 引用；脚本便利入口在组合边界解析 capability，循环中不查 capability。
EnvironmentQuery.cpp 登记为 hot path（R-MECH-2）。无 sensing → physics/navigation/graphics
依赖；common 不添加领域接口（R-FACE-5）。公共成本以 compile/instantiate/run 分阶段并用
@cost 描述（R-COST-1）。模块边界继续由源码门禁维护，不恢复废弃 JSON catalogue。

六面：复用 ISensingQuery 输出和 ILineOfSightQuery 输入；不新增 ECS/Link/事件；脚本增加
queryPositions；MCP 通过既有 eve_sensing_query 的 query.environment 字段读取结果与解释。
未主张新的模块裁剪能力；验证采用现有 minimal profile（没有物理后端）与注入后端契约。

## 输出协议

eve.sensing.lastQuery v1 增加可空 environment，既有字段不变。嵌套 trace/result 分别使用
eve.sensing.environmentTrace v1、eve.sensing.environmentResult v1，读者忽略未知字段。
这里只提供序列化输出，无持久化 definition/import 协议，因而不存在虚构的恢复迁移支持。
详细 trace 有候选上限；生产侧可以关闭。错误状态也发布 trace，正常无候选是成功空集合。

## 验证清单

测试源：test/integration/sensing/environment_query_tests.cpp，加既有 sensing/targeting 契约。

- 计划/定义独立生命周期、实例 Context 快照、重复执行拒绝。
- 坐标、身份、Context、配置与资源上限拒绝。
- Ring/Grid 无主体位置；多 Context All/Any 与指标聚合。
- 贡献、归一化范围、权重、参考值与拒绝原因。
- Option 仅因空结果切换，缺后端或后端失败立即传播。
- 优化/穷举结果与分数一致，昂贵调用数下降；相对评分和自定义屏障禁止不安全优化。
- trace 开关/容量不改变结果；C++/Squirrel/MCP 数据投影共用执行器。
- 第九个合法候选不会被 Select 提前截断；失败不发布中间结果。

## 实施验证记录（2026-10-22）

三批已实现，并使用 GCC 在 `build/cloud-minimal` 完成验证。
本地配置启用 sensing、font、devtools；为链接既有 NPC 测试目标，额外启用 crowd、editing、
crowd_editing、npc_ai_editing。该目标含 crowd/行为图测试，原 minimal 配置缺少这些实现，
首次链接失败；补齐本地 CMake 配置后通过。没有为此修改仓库默认配置。

| 验证 | 结果 |
|---|---|
| `cmake --build build/cloud-minimal --target unit_test_npc_ai eve -j 4` | 成功 |
| `make check CI_BASE=HEAD` | 全部门禁通过，313 项仓库脚本测试通过 |
| 新增 C++ 文件 `clang-format --dry-run --Werror` | 通过 |
| CTest `^sensing\.` | 42 项通过：既有感知 15、环境查询/管线回归 21、脚本 provider 6 |
| CTest `^profile\.capability\.` | present/absent 两项通过 |
| `smoke_examples.sh basic environment-query` | 两个示例均存活 6 秒，无错误标记 |

运行测试和示例时使用 Xvfb、软件 Vulkan 与 null audio。示例验证是启动与持续运行检查，
不作为画面正确性或导航可达性的证明。源码门禁首次执行缺少工具 PATH，加载引擎环境后
完整重跑通过；上述结果来自重建后的二进制，而非环境预留缓存。

```bash
xvfb-run -a ctest --test-dir build/cloud-minimal --output-on-failure --no-tests=error \
  -R '^(sensing\.|profile\.capability\.)'
EVE="$PWD/build/cloud-minimal/src/engine/eve" \
  xvfb-run -a bash scripts/smoke_examples.sh basic environment-query
```

新增用例验证了 Best 延迟验证在前一候选失败后继续尝试，并将该用例的 LOS 调用从 3 次
降到 2 次；同时覆盖多组候选的优化/穷举一致性、评分累加顺序、异常诊断和旧管线最终限量。
这里不把调用次数断言表述成通用性能基准。

合入最新 dev 后，新增测试按现有目录与分类规则迁至 integration/sensing，并登记 test_levels.json。
上述完整构建与运行记录对应合入 dev 前的功能提交；合入后的结果由 PR CI 验证。
