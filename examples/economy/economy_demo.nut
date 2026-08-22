// EVEngine economy 模块 MVP 演示（eve run -r <本文件>）
//
// 演示内容：
//   1) SC2 式矿物采集：一个双槽矿点 + 两个采集者，各自完成
//      "接近 -> 采集 -> 满载 -> 返回 -> 卸货入账" 的 WorkerTrip 循环；
//   2) 满仓浪费：玩家持有上限 10，节点储量 25，超限部分记 waste 事件。
//
// 运行方式（无需窗口/Vulkan）：
//   build/win32-debug/src/engine/eve.exe run -r examples/economy/economy_demo.nut

local eco = eve.Economy();

function pad(s, w) {
    while (s.len() < w) s += " ";
    return s;
}

function dump_events(from) {
    while (eco.getEventCount() > from) {
        local i = from;
        local action = pad(eco.getEventAction(i), 6);
        print(format("      [event] %s %-10s x%d (player %d)\n",
                     action, eco.getEventType(i), eco.getEventAmount(i),
                     eco.getEventPlayer(i)));
        from++;
    }
    return from;
}

print("====================================================================\n");
print(" EVEngine economy MVP demo\n");
print("====================================================================\n");

// ---------------------------------------------------------------------------
// 场景 1：双工人矿物采集（SC2 式 WorkerTrip）
// ---------------------------------------------------------------------------
print("\n[1] 矿物采集：矿点 30，双槽位，工人 A(载荷5, 速率1, 单程2t) + 工人 B(载荷3, 速率2, 单程3t)\n");

eco.registerResourceType("minerals", "stock", 100, "finite");
eco.clearEvents();

local mine = eve.GatherNode("minerals", 30, 2, 0);
local workerA = eve.Collector(5, 1, 2);
local workerB = eve.Collector(3, 2, 3);

print("  槽位: " + mine.freeSlots() + " 空闲\n");
workerA.assign(mine);
print("  A 进入矿点, 槽位: " + mine.freeSlots() + " 空闲\n");
workerB.assign(mine);
print("  B 进入矿点, 槽位: " + mine.freeSlots() + " 空闲\n");

local events = 0;
local doneTick = -1;
for (local t = 0; t < 300; ++t) {
    workerA.tick(1);
    workerB.tick(1);
    events = dump_events(events);
    if (workerA.isIdle() && workerB.isIdle()) {
        doneTick = t;
        break;
    }
}

print("\n  矿点采空于 t=" + doneTick + "\n");
print("  A: 往返 " + workerA.trips() + " 次, 累计入账 " + workerA.totalGathered() + "\n");
print("  B: 往返 " + workerB.trips() + " 次, 累计入账 " + workerB.totalGathered() + "\n");
print("  玩家1 矿物余额: " + eco.get(1, "minerals") + " / 上限 " + eco.getCap(1, "minerals") +
      ", 浪费 " + eco.getWasted(1, "minerals") + "\n");
print("  矿点剩余: " + mine.amount() + "\n");

workerA.destroy();
workerB.destroy();
mine.destroy();

// ---------------------------------------------------------------------------
// 场景 2：持有上限导致的浪费
// ---------------------------------------------------------------------------
print("\n[2] 满仓浪费：玩家持有上限 10，节点 25，载荷 5（第 3 次起全部浪费）\n");

eco.registerResourceType("gold", "stock", 10, "finite");
eco.clearEvents();

local goldNode = eve.GatherNode("gold", 25, 1, 0);
local miner = eve.Collector(5, 1, 1);
miner.assign(goldNode);

events = 0;
doneTick = -1;
for (local t = 0; t < 300; ++t) {
    miner.tick(2);
    events = dump_events(events);
    if (miner.isIdle()) {
        doneTick = t;
        break;
    }
}

print("\n  采空于 t=" + doneTick + "\n");
print("  玩家2 黄金余额: " + eco.get(2, "gold") + " / 上限 " + eco.getCap(2, "gold") +
      ", 浪费 " + eco.getWasted(2, "gold") + "\n");
print("  矿工往返 " + miner.trips() + " 次, 入账 " + miner.totalGathered() + "\n");

miner.destroy();
goldNode.destroy();

print("\n====================================================================\n");
print(" 演示结束\n");
print("====================================================================\n");
