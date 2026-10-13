# 资源经济：账本、消费与采集

**入口：** `eve.Economy()`，配置槽位 `economy`。玩家账本由 EconomySystem 拥有；
HUD、建筑和生产只能查询/消费该账本，不能各自复制一份余额。金额是整数，玩家参数是本域
整数 ID，不是跨系统 UUID。模块 facade 不等于彼此隔离的账户数据库。

## 任务一：发放资源并购买物品

以下放在游戏初始化阶段，配置需包含 `economy`。`credit` 返回实际入账数量，`debit`
返回是否成功，不是 Result 表。注册资源类型必须先于入账；此例的类别是项目自定义字符串。

```squirrel
local wallet = eve.Economy();
if (!wallet.registerResourceType("tutorial_gold", "currency", 100, "finite"))
    throw "cannot register gold";
local received = wallet.credit(1, "tutorial_gold", 80);
assert(received == 80);
if (!wallet.debit(1, "tutorial_gold", 30)) throw "not enough gold";
assert(wallet.get(1, "tutorial_gold") == 50);
assert(!wallet.debit(1, "tutorial_gold", 60));
assert(wallet.get(1, "tutorial_gold") == 50);
```

只有扣款成功后才能给物品。正式购买若还包含背包容量、库存等可能失败的参与者，必须用
业务域的事务入口；“先扣钱，再 try 加物品”不自动原子，见[事务](transaction.md)及
[商店集成测试](../../../test/rpg_shop_transaction.cpp)。

## 任务二：处理满仓与采集

```squirrel
local accepted = wallet.credit(1, "tutorial_gold", 80);
assert(accepted == 50); // 从 50 到上限 100
assert(wallet.getWasted(1, "tutorial_gold") == 30);
for (local i = 0; i < wallet.getEventCount(); ++i)
    print(wallet.getEventAction(i) + ": " + wallet.getEventAmount(i) + "\n");
```

`getIncome`/`getExpense` 和 waste 事件用于解释账本变化。事件消费完再由指定所有者清理，
多个 UI/AI 消费者不能互相抢先 `clearEvents`。

采集使用 `eve.GatherNode(type, capacity, slots, regen)` 与
`eve.Collector(carryCapacity, rate, travel)`：先注册类型 → 创建节点/采集者 →
检查 `assign(node)` → 固定 tick 调用 `collector.tick(player)` → 读取 cargo、stateName、
trips 与账本。清理前调用 `clearAssignment`，停止 tick，再按对象接口释放节点和采集者。
`tick` 不是 `dt` 秒；不能每个渲染帧随意推进。完整行为及满仓案例见
[economy.cpp](../../../test/economy.cpp)。

## 与 RTS、Agent 组合

RTS 的阵营与资源命令走 RTS 域接口，不直接把其 UUID 当 Economy 的整数玩家参数。
`publishGameplay(instanceId, ownerId, player)` 把账本接到玩法观察/动作协议，需规范 UUID
并检查返回 `ok`；普通玩家不能借该入口随意 credit。采集仍走真实领域 tick，
不是通过调试器改余额。退出前取消发布，避免留下过期控制实例。

## 检查与证据

测试余额不足不扣款、满仓浪费、多个采集者、定义缺失、事件顺序与停止后的对象寿命。
源码：[Economy.cpp](../../../src/modules/economy/Economy.cpp)、
[Economy.h](../../../src/modules/economy/Economy.h)；玩法协议测试：
[economy_gameplay_control.cpp](../../../test/economy_gameplay_control.cpp)。
按类参数参考见本章末尾，不从 C++ 默认实参推断脚本参数可省略。

## 补充绑定参考

参数与接收者见 [源码生成参考](economy/api-reference.md)，使用前先确认本章的前置条件与生命周期。
