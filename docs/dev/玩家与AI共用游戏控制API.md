# 玩家与 AI 共用游戏控制 API

本文设计并记录游戏规则层的统一控制边界。目标不是让 AI 调用键鼠，也不是复制一套“AI
玩法逻辑”，而是让本地玩家输入、脚本、测试驱动和外部 Agent 最终提交同一种玩家语义命令。

## 已实现的边界

公共接口位于 `common/GameplayControl.h`，由多个玩法域通过
`IGameplayControlProvider` capability 同时发布。每个 provider 提供五类操作：

1. `observeGameplay`：取得带 `tick + revision` 的拥有型状态快照；
2. `availableGameplayActions`：按当前状态和权限发现合法动作及参数 schema；
3. `submitGameplay`：提交动作并取得命令回执；
4. `advanceGameplay`：使用注入的 `SimulationStep` 推进确定性模拟；
5. `gameplayEvents`：按序号读取带 causation/correlation 的拥有型事件。

`GameplaySession` 明确区分 `PlayerEquivalent`、`TestDriver` 和 `DeveloperCheat`。
默认玩家路径只能控制 `controlledSubjects` 中声明的稳定 `SubjectRef`；调试权限不会被伪装成
普通玩家能力。命令必须携带观察时的 tick/revision，过期命令在修改权威状态前失败。

当前真实接入：

- `tactics`：移动、转向、等待、结束回合，调用 `Tactics`/`BattleSystem` 的规范命令路径；
- `rts`：对玩家选择集下达移动和攻击命令，调用 `CommandFanOutSystem` 和订单执行器；
- `rpg.battle`：普通攻击与技能，调用 `Battle::setActionChecked`，回合结算仍由 `Battle` 完成。
- `rpg.product`：购买、出售、任务奖励和世界掉落，分别调用现有
  `ShopTransaction`、`QuestReward` 和 `WorldInteraction` 原子事务。
- `weapon`：对装备者提交射击命令，调用 `WeaponActionAdapter`，使用同一条
  `ActionRuntime + AtomicResourcePayment` 路径完成弹药扣减和武器效果。
- `climbing`：开始、攀上、下落和取消。`begin-best` 不接受客户端坐标，每次都使用
  服务器侧 `World3D + ClimbingPose` 重新探测并由 `ClimbingRuntime` 提交。

RPG 旧 Actor 没有持久身份，因此没有把裸指针编码到协议中。`BattleControl` 要求宿主显式把
参战者绑定到 `SubjectRef`，且必须先于 Battle/Actor 销毁；Battle 仍是唯一可变权威。

## 玩家端与 AI 端如何共用

```text
键鼠/UI ─┐
脚本     ├─ GameplaySession + GameplayCommand ─ provider ─ 现有权威系统
测试/AI ─┘                                      │
                         Observation/Receipt/Event
```

适配器只负责把宿主输入翻译成 `GameplayCommand`。规则校验、资源扣减、目标合法性、状态变化和
事件生产仍由玩法域权威系统负责。测试会比较玩家和 TestDriver 在相同状态下发现的动作，防止
两条路径逐渐产生不同规则。

## JSON、Squirrel 与 MCP 门面

`common/GameplayControlJson.h` 定义唯一的版本化 JSON 路由。请求的 `schemaId` 为
`evengine.gameplay-control-request`，`schemaVersion` 为 `1`，`op` 可为 `domains`、
`observe`、`actions`、`submit`、`advance` 或 `events`。未知根字段会失败，不会静默忽略。

- Squirrel 调试宿主：`eve.dev.gameplay(requestJson)`；
- Engine MCP：`eve_gameplay({ request = <同一个请求对象> })`；
- C++ 宿主：直接使用 `IGameplayControlProvider`，或调用
  `executeGameplayControlRequest` / `executeGameplayControlJson`。

MCP 的 `test_scenario` 流程使用该门面完成“观察→发现动作→提交→推进→读取事件”。
旧 `ScenarioRecorder` 仍负责键鼠/事件队列的逐帧录制；它不会伪装成原生玩法状态的恢复器。

## 本轮不包含

- 协议不是网络传输层，也不负责 AI 规划、提示词、模型调用或作弊检测；
- 当前 RTS provider 使用模块内规范订单执行器推进；产品游戏若注入自己的执行器，需要让玩家
  和自动化宿主共享同一个实例，不能各自维护模拟状态。

现在可以称为已接入域的 Agent-playable 规则层 API；未实例化 provider 的游戏对象仍会明确返回
`NotFound`，不应把“模块编译进引擎”解读为“所有游戏实例自动可控”。

## 验证要求

每个接入域至少覆盖：provider 出现/撤回、玩家与 TestDriver 动作等价、未授权拒绝、陈旧命令
无副作用、命令回执、事件因果链，以及单调 tick 的确定性推进。跨模块修改还必须通过模块依赖
图、架构合同和相应裁剪构建；图形可玩性证据必须使用引擎自己的 MCP 截图，而不是 Xvfb
framebuffer。
