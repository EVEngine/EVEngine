# 自动数值平衡：BalanceSearch

`eve.BalanceSearch()` 是 agent 模块提供的原生 CPU 数值搜索组件。
用户声明允许调整的参数、期望指标和模拟预算，再提供自己的评估器。
组件返回可审查的参数提案及训练/验证证据；配置表、技能、装备和角色仍由游戏拥有。

## 最小用法

```squirrel
local search = eve.BalanceSearch();
local setup = search.configure({
    schema="evengine.balance.problem", version=1,
    parameters=[{name="fireball.damage", initial=30.0, min=20.0, max=80.0}],
    targets=[{name="dps", min=20.0, max=24.0, scale=4.0}],
    candidates=64, steps=300, dt=0.1
});
if (!setup.ok) throw setup.code;

// 演示接口形状的极简评估器；完整战斗示例见 examples/rpg-balance。
local result = search.run({
    evaluate=function(parameters, seed, dt, steps) {
        // 正式接入：重置隔离场景和 RNG，执行 steps 个 dt，返回实测指标。
        return {dps=parameters["fireball.damage"] / 2.0};
    }
});
if (!result.ok) throw result.code;
print(result.value.status + "\n");
local candidate = result.value.parameters;
```

`configure` 是便宜的验证/复制阶段，失败保持上一次有效配置。
`run` 是显式昂贵的同步作业，应在离线测试、加载界面或独立测试宿主运行。
不要每帧调用。run/configure 在回调中的重入返回 Conflict。

## 三条研究路线如何组合

1. [GEEvo](https://github.com/FlorianRupp/GEEvo-game-economies)：借鉴资源/技能循环模拟产生指标的方式。
   评估器可输出累计伤害、耗蓝、资源净流入等；本组件不复制图模拟器，也不维护第二套战斗公式。
2. [GameUnitBalancing](https://github.com/emrek1/GameUnitBalancing)：借鉴对战收益与少改原配置的目标。
   每一对构筑的收益可成为单独目标；也可使用职业平均对战积分。默认不强制所有配对五五开。
3. [Metagame Autobalancing](https://arxiv.org/abs/2006.04419)：借鉴设计者指定目标克制矩阵、代理评估、外层搜索的结构。
   代理可以是人工策略群体，或者已有 Agent 策略；组件不自动训练、更新或冻结代理。

当前优化器为 `boundary-seeded-repair-search`：初始配置占第一个候选；若预算允许，
第二、三个候选先测试所有参数的下界与上界组合，避免局部变异无法触及远端的可行区域。
四项及以上参数的第四个候选还会测试稀疏边界组合：两端参数保留原值，中间参数取下界，
以跨过需要多个参数同时变化的平台区。找到训练集可行解后，接下来的少量候选逐项尝试
恢复原始参数，以减少不必要的改动。
其余候选有 25% 概率全局均匀探索，否则在当前最佳参数周围做范围逐渐缩小的均匀变异，
越界值裁剪回边界。边界组合也必须是合法的游戏配置；评估失败会明确返回错误。
这是独立的小型黑盒搜索实现，不是 GEEvo 的复现，也不是梯度纳什优化、NSGA-II、TPE 或 Regym 集成。
没有下载或打包上述项目的代码和模型，没有额外 Python/PyTorch 运行依赖。

## 参数和目标

参数项：`name`、`initial`、`min`、`max`。必须是有限数字，满足 min <= initial <= max。
等界参数保持锁定；没有列出的游戏参数不属于搜索空间。
当前只支持连续标量；整数、枚举、技能树合法性必须由游戏配置设计，不能把任意连续值静默取整冒充原参数。

目标项：`name`、`min`、`max`、`scale` 和可选 `weight`（默认 1）。
scale 表示设计上有意义的偏差单位，例如 DPS 差 4 或胜率差 0.1；不同单位的目标因此可以组合。
回调必须返回且只返回这些命名指标；未知/遗漏字段、非数值、NaN/Infinity 都导致失败。

每个目标先在 seed 集合上求均值，再计算落在目标区间外的距离：

```text
violation_i = (mean_i - clamp(mean_i, min_i, max_i)) / scale_i
loss = sum(weight_i * violation_i²)
drift = mean(((candidate_j - initial_j) / range_j)²)
score = loss + changeWeight * drift
```

锁定参数的 drift 项为零。候选按训练 score 严格下降才替换；同分保留较早候选。
这意味着 changeWeight 过高时可以保留目标区间外但改动更小的配置，必须检查报告状态。
目标仅约束 seed 平均值，不是逐局硬约束；单位成本、装备词条预算等硬约束应在参数化阶段保证。
失败的模拟直接终止作业，不会当作一个低分候选忽略。

## 配置与预算

- `candidates`：包含初始配置，默认 128，范围 1..4096。
- `steps`：每次模拟的逻辑步数，默认 120，范围 1..100000。
- `dt`：每步秒数，默认 0.1，范围 (0,60]。
- `changeWeight`：默认 0.01，范围 [0,1000000]。
- `searchSeed`：默认 7，仅用于参数搜索。
- `trainingSeeds`：默认 [11,22,33]，每个候选使用同一组，以降低随机噪声。
- `validationSeeds`：默认 [101,202,303]，不参与候选选择。

seed 集合必须各自非空、无重复、彼此不相交。每组最多 128 个。
seed 可为非负整数或完整 uint64 十进制字符串；脚本回调始终收到字符串。
有 64 个参数、128 个目标的上限；名称唯一且不超过 128 字节。
数值幅度最多 1e9，scale 至少 1e-9，weight 为 (0,1e6]。
`candidates * trainingSeeds.len() + 2 * validationSeeds.len()` 不得超过 65536。
预算限制回调次数，无法抢占一个内部卡死的回调；需要超时保护时使用外部测试进程。

## 报告和验证

run 返回统一 Result，成功的 value 包括：

- `parameters`、`changes`：候选值及每项 before/after/delta。
- `baseline`、`tuned`：训练集的前后指标、loss、score。
- `baselineValidation`、`validation`：独立 seed 集上的前后指标。
- 每个 `metrics[name]` 包含 `mean` 和样本标准差 `deviation`；单个 seed 的 deviation 为零。
- `status`：验证均值全部落入目标区间才是 `within_targets`，否则为 `outside_targets`。
- `evaluations`、`optimizer`、完整 `problem`：评估次数、算法名、复现参数。

`ok=true` 只表示执行完成，不保证可行解、全局最优、真人平衡或乐趣。
标准差不是置信区间；不能因为验证均值达标就宣称统计显著。
不要反复利用同一验证集选择目标、参数范围或 searchSeed，否则该集合已经参与调参；最终验收另留测试集。
应覆盖地图、等级、装备档位、敌人类型和策略群体，确保输出指标反映实际目标玩家。

## RPG 示例

[`examples/rpg-balance`](../../../examples/rpg-balance/README.md) 展示三职业、四套构筑、
四个技能/装备参数、两种策略、两种资源场景及完整双向对战矩阵。
技能消耗、冷却、治疗、防御、周期状态与职业特质通过正式 RPG 实现运行。

正式游戏评估器必须调用游戏的权威战斗路径：候选参数只注入隔离副本，
每次从相同定义/关卡/seed 重建世界、句柄、事件队列与 RNG。
生产定义只有在用户审查并通过领域事务提交后才改变；搜索器没有应用参数的第二条通道。
已提供基于正式 RPG 战斗与结算的 [RPGBalanceSuite](rpg-balance.md)，
可直接定义构筑、双方策略、资源场景和有向克制目标，再把指标交给搜索器。

## C++ 接入与 module-interface

头文件 `agent/Balance.h`，命名空间 `eve::agent::balance`。
构造 BalanceSearch，检查 configure(Problem)，实现 IEvaluator，检查 run(evaluator)。
不新增模块或链接依赖；继续由现有 agent manifest 声明派生源码和启动列表。

- E1/E2 能力：没有全局 capability 注册；单个已知 evaluator 通过同步接口直接注入，
  每候选每 seed 一次，单调用原子返回，机制由基数/频率/观察者可知性/原子性决定。
- E3 ECS/Link：无实体、System 或跨帧 Link；领域对象始终由游戏拥有。
- E4 事件：不新增全局事件；模拟事件由 evaluator 重置和处理。
- E5 脚本：Agent 注册阶段公开 VM 拥有的 BalanceSearch；报告为 owning Value 投影。
- E6 工具：不新增 MCP 协议；可从正常游戏脚本或测试宿主调用。

Builder 拥有配置副本；回调参数仅在调用期间借用，脚本收到新的 owning table。
回调表只在同步调用中 root，不保留到下一帧；失败不发布部分报告，builder 可再次使用。
无锁调用未知代码；调用方不得在回调中销毁 builder/evaluator。
本模块不持有运行中游戏的指针，不自动恢复 evaluator 最后一个模拟状态。

同构建、相同输入和确定性 evaluator 可复现；浮点跨平台按容差验收，不保证逐位一致。
`evengine.balance.problem` 与 `evengine.balance.report` 均为版本 1。
problem codec 拒绝未知字段/版本；没有旧发布格式需要迁移，未来升级必须显式迁移。
report 仅是输出证据，无导入为运行状态的 API；应用候选走游戏自身的定义 schema/事务。

组件无渲染、tensor 或第三方优化器依赖。独立 balance 测试工程的 CPU 配置验证 L2 链接和
脚本配置的 L4 可调用性；不据 include 图声称整个引擎所有 profile 已验证。
架构规则适用项为 Result、成本可见、单一状态所有者、时间/RNG 注入、生命周期、schema 与模块边界；
ECS System、Link 双向销毁、可选 provider 和后端一致性无新增对象，因此不适用。无规则例外。

### 独立验证命令

```sh
cmake -S test/balance -B build/balance-test -DCMAKE_BUILD_TYPE=Debug
cmake --build build/balance-test -j 4
ctest --test-dir build/balance-test --output-on-failure
```

Windows 的 configure/build 通过 `cmake/with-msvc.cmd` 包装，并指定 `-G Ninja`。
若依赖源位于别处，传入 `EVE_ZEROERR_SOURCE`、`EVE_THIRD_PARTY_SOURCE`，始终使用依赖的真实源码。
以 `-DEVE_BALANCE_SCRIPT=OFF` 可验证没有 Squirrel/渲染的原生构建。
单元测试位于 `test/unit/agent/agent.cpp`，
脚本与示例组合测试在 `test/integration/balance-search/`。
