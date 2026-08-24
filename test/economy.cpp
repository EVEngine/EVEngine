#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Capability.h"
#include "common/IEconomy.h"
#include "economy/Collector.h"
#include "economy/Economy.h"
#include "economy/EconomySystem.h"
#include "economy/GatherNode.h"
#include "economy/ResourceType.h"

#include <cstdio>
#include <string>

using namespace eve::economy;

namespace {

ResourceTypeDef makeType(const std::string& id, int stockMax,
                         const std::string& category = "stock") {
    ResourceTypeDef def;
    def.id          = id;
    def.category    = category;
    def.stockMax    = stockMax;
    def.depletion   = DepletionModel::Finite;
    def.displayName = id;
    return def;
}

/** 每个用例从干净状态开始（进程内单测）。 */
struct Reset {
    Reset() {
        ResourceTypeRegistry::clear();
        EconomySystem::clear();
    }
    ~Reset() {
        ResourceTypeRegistry::clear();
        EconomySystem::clear();
    }
};

/** 推进 worker 直到待命；返回消耗的 tick 数（超时返回 maxTicks）。 */
int runUntilIdle(Collector& worker, int player, int maxTicks = 1000) {
    int ticks = 0;
    while (!worker.isIdle() && ticks < maxTicks) {
        worker.tick(player);
        ++ticks;
    }
    return ticks;
}

}  // namespace

TEST_CASE("economy.registry.registerFindClear") {
    Reset reset;
    CHECK_EQ(ResourceTypeRegistry::count(), 0);
    CHECK(ResourceTypeRegistry::find("minerals") == nullptr);

    CHECK(ResourceTypeRegistry::registerType(makeType("minerals", 200)));
    CHECK_EQ(ResourceTypeRegistry::count(), 1);
    REQUIRE(ResourceTypeRegistry::find("minerals") != nullptr);
    CHECK_EQ(ResourceTypeRegistry::find("minerals")->stockMax, 200);
    CHECK_EQ(ResourceTypeRegistry::find("minerals")->category, "stock");

    // 重复注册替换旧定义
    CHECK(ResourceTypeRegistry::registerType(makeType("minerals", 500)));
    CHECK_EQ(ResourceTypeRegistry::count(), 1);
    CHECK_EQ(ResourceTypeRegistry::find("minerals")->stockMax, 500);

    ResourceTypeRegistry::clear();
    CHECK_EQ(ResourceTypeRegistry::count(), 0);
}

TEST_CASE("economy.registry.rejectsEmptyId") {
    Reset reset;
    ResourceTypeDef empty;
    CHECK(!ResourceTypeRegistry::registerType(empty));
    CHECK_EQ(ResourceTypeRegistry::count(), 0);
}

TEST_CASE("economy.ledger.creditRespectsCapAndTracksWaste") {
    Reset reset;
    ResourceTypeRegistry::registerType(makeType("minerals", 10));

    EconomyLedger ledger;
    int accepted = ledger.credit("minerals", 6);
    CHECK_EQ(accepted, 6);
    CHECK_EQ(ledger.get("minerals"), 6);
    CHECK_EQ(ledger.getWasted("minerals"), 0);

    accepted = ledger.credit("minerals", 6);
    CHECK_EQ(accepted, 4);  // 只收 4，超限 2 记为浪费
    CHECK_EQ(ledger.get("minerals"), 10);
    CHECK_EQ(ledger.getWasted("minerals"), 2);

    // 未注册类型视为无上限
    CHECK_EQ(ledger.credit("unregistered", 5), 5);
    CHECK_EQ(ledger.get("unregistered"), 5);
}

TEST_CASE("economy.ledger.debitNeverGoesNegative") {
    Reset reset;
    ResourceTypeRegistry::registerType(makeType("gold", 100));

    EconomyLedger ledger;
    ledger.credit("gold", 5);

    CHECK(ledger.canAfford("gold", 5));
    CHECK(!ledger.canAfford("gold", 6));
    CHECK(ledger.debit("gold", 3));
    CHECK_EQ(ledger.get("gold"), 2);
    CHECK(!ledger.debit("gold", 3));
    CHECK_EQ(ledger.get("gold"), 2);  // 失败不扣款
    CHECK_EQ(ledger.getIncome("gold"), 5);
    CHECK_EQ(ledger.getExpense("gold"), 3);
}

TEST_CASE("economy.system.emitsEventsAndHooks") {
    Reset reset;
    ResourceTypeRegistry::registerType(makeType("wood", 20));
    EconomySystem::clearEvents();

    int hookCalls = 0;
    EconomySystem::registerHook("test", [&](const EconomyEvent& ev) {
        ++hookCalls;
        if (ev.action == "credit") CHECK_EQ(ev.amount, 5);
    });

    EconomySystem::credit(1, "wood", 5);
    CHECK_EQ(EconomySystem::get(1, "wood"), 5);
    CHECK_EQ(EconomySystem::eventCount(), 1);
    CHECK(EconomySystem::eventAt(0).action == "credit");
    CHECK_EQ(EconomySystem::eventAt(0).player, 1);
    CHECK(EconomySystem::eventAt(0).type == "wood");
    CHECK_EQ(EconomySystem::eventAt(0).amount, 5);
    CHECK_EQ(hookCalls, 1);

    CHECK(EconomySystem::debit(1, "wood", 2));
    CHECK_EQ(EconomySystem::eventCount(), 2);
    CHECK(EconomySystem::eventAt(1).action == "debit");

    // 当前 3，上限 20 → 收 17，浪费 2
    EconomySystem::credit(1, "wood", 19);
    CHECK_EQ(EconomySystem::getWasted(1, "wood"), 2);
    CHECK_EQ(EconomySystem::eventCount(), 4);
    CHECK(EconomySystem::eventAt(2).action == "credit");
    CHECK(EconomySystem::eventAt(3).action == "waste");
    CHECK_EQ(EconomySystem::eventAt(3).amount, 2);
}

TEST_CASE("economy.workerTrip.gathersUntilNodeDepleted") {
    Reset reset;
    ResourceTypeRegistry::registerType(makeType("minerals", 1000));

    GatherNode node("minerals", 30, 2);
    Collector  worker(5, 1, 2);
    const int  player = 1;

    CHECK(worker.assign(&node));
    const int ticks = runUntilIdle(worker, player);
    CHECK(ticks < 1000);
    CHECK(worker.isIdle());
    CHECK(node.depleted());
    CHECK_EQ(worker.cargo(), 0);
    CHECK_EQ(worker.totalGathered(), 30);
    CHECK_EQ(EconomySystem::get(player, "minerals"), 30);
    CHECK_EQ(EconomySystem::getWasted(player, "minerals"), 0);

    // 6 次往返，每次 5 单位
    int deposits = 0;
    for (int i = 0; i < EconomySystem::eventCount(); ++i) {
        if (EconomySystem::eventAt(i).action == "credit") ++deposits;
    }
    CHECK_EQ(deposits, 6);
    CHECK_EQ(worker.trips(), 6);
}

TEST_CASE("economy.workerTrip.wastesWhenCapReached") {
    Reset reset;
    ResourceTypeRegistry::registerType(makeType("minerals", 10));

    GatherNode node("minerals", 100, 2);
    Collector  worker(5, 1, 1);
    const int  player = 2;

    CHECK(worker.assign(&node));
    const int ticks = runUntilIdle(worker, player);
    CHECK(ticks < 1000);
    CHECK_EQ(EconomySystem::get(player, "minerals"), 10);
    CHECK_EQ(EconomySystem::getWasted(player, "minerals"), 90);
    CHECK_EQ(worker.totalGathered(), 10);
    CHECK_EQ(worker.trips(), 20);
}

TEST_CASE("economy.workerTrip.depositsPartialLoadWhenNodeDepletes") {
    Reset reset;
    ResourceTypeRegistry::registerType(makeType("food", 100));

    GatherNode node("food", 3, 2);
    Collector  worker(5, 1, 2);
    const int  player = 3;

    CHECK(worker.assign(&node));
    const int ticks = runUntilIdle(worker, player);
    CHECK(ticks < 1000);
    CHECK_EQ(EconomySystem::get(player, "food"), 3);
    CHECK_EQ(worker.trips(), 1);
    CHECK_EQ(worker.totalGathered(), 3);
}

TEST_CASE("economy.workerTrip.saturationSlotsLimitWorkers") {
    Reset reset;
    ResourceTypeRegistry::registerType(makeType("minerals", 1000));

    GatherNode node("minerals", 30, 1);  // 单槽位
    Collector  a(5, 1, 1);
    Collector  b(5, 1, 1);
    const int  player = 4;

    CHECK(a.assign(&node));
    CHECK_EQ(node.freeSlots(), 0);
    CHECK(!b.assign(&node));  // 槽位已满，保持待命
    CHECK(b.isIdle());

    a.clearAssignment();
    CHECK_EQ(node.freeSlots(), 1);
    CHECK(b.assign(&node));
    CHECK_EQ(node.freeSlots(), 0);

    const int ticks = runUntilIdle(b, player);
    CHECK(ticks < 1000);
    CHECK(node.depleted());
    CHECK_EQ(EconomySystem::get(player, "minerals"), 30);
    CHECK_EQ(node.freeSlots(), 1);
}

TEST_CASE("economy.capability.moduleProvidesIEconomy") {
    Reset reset;
    auto* eco = Economy::create();
    REQUIRE(eco != nullptr);

    auto* q = eve::cap::query<eve::economy::IEconomy>();
    REQUIRE(q != nullptr);

    Economy::registerResourceType("gold", "stock", 50, "finite");
    CHECK_EQ(q->credit(3, "gold", 60), 50);
    CHECK_EQ(q->get(3, "gold"), 50);
    CHECK_EQ(q->getWasted(3, "gold"), 10);
    CHECK(q->debit(3, "gold", 20));
    CHECK_EQ(q->get(3, "gold"), 30);
    CHECK_EQ(q->getCap(3, "gold"), 50);
    CHECK_EQ(q->getIncome(3, "gold"), 50);
    CHECK_EQ(q->getExpense(3, "gold"), 20);
}

TEST_CASE("economy.demoScenario") {
    Reset reset;

    printf("====================================================================\n");
    printf(" EVEngine economy MVP demo\n");
    printf("====================================================================\n");

    // ---- 场景 1：双工人矿物采集（SC2 式 WorkerTrip） ----
    ResourceTypeRegistry::registerType(makeType("minerals", 100));
    EconomySystem::clearEvents();

    GatherNode mine("minerals", 30, 2);
    Collector  workerA(5, 1, 2);
    Collector  workerB(3, 2, 3);
    const int  player = 1;

    printf("\n[1] 矿物采集: 矿点 30, 双槽位, A(载荷5/速率1/单程2t) + B(载荷3/速率2/单程3t)\n");
    printf("    矿点槽位: %d 空闲\n", mine.freeSlots());
    CHECK(workerA.assign(&mine));
    printf("    A 进入矿点 -> 槽位: %d 空闲\n", mine.freeSlots());
    CHECK(workerB.assign(&mine));
    printf("    B 进入矿点 -> 槽位: %d 空闲\n", mine.freeSlots());
    printf("\n    采集循环:\n");

    int lastEvents = 0;
    int doneTick   = -1;
    for (int t = 0; t < 300; ++t) {
        workerA.tick(player);
        workerB.tick(player);
        if (t % 12 == 0) {
            printf("      t=%3d  A[%-13s cargo=%d] B[%-13s cargo=%d] node=%2d balance=%d\n", t,
                   workerA.stateName().c_str(), workerA.cargo(), workerB.stateName().c_str(),
                   workerB.cargo(), mine.amount(), EconomySystem::get(player, "minerals"));
        }
        while (EconomySystem::eventCount() > lastEvents) {
            const auto& ev = EconomySystem::eventAt(lastEvents);
            printf("      -> %s %s x%d (balance=%d)\n", ev.action.c_str(), ev.type.c_str(),
                   ev.amount, EconomySystem::get(player, "minerals"));
            ++lastEvents;
        }
        if (workerA.isIdle() && workerB.isIdle()) {
            doneTick = t;
            break;
        }
    }

    printf("\n    矿点采空于 t=%d\n", doneTick);
    printf("    A: 往返 %d 次, 累计入账 %d\n", workerA.trips(), workerA.totalGathered());
    printf("    B: 往返 %d 次, 累计入账 %d\n", workerB.trips(), workerB.totalGathered());
    printf("    玩家%d %s 余额 %d / 上限 %d, 浪费 %d\n", player, "minerals",
           EconomySystem::get(player, "minerals"), EconomySystem::getCap(player, "minerals"),
           EconomySystem::getWasted(player, "minerals"));
    printf("    矿点剩余 %d\n", mine.amount());

    CHECK(doneTick >= 0);
    CHECK(mine.depleted());
    CHECK_EQ(EconomySystem::get(player, "minerals"), 30);
    CHECK_EQ(EconomySystem::getWasted(player, "minerals"), 0);
    CHECK_EQ(mine.freeSlots(), 2);
    CHECK_EQ(workerA.totalGathered() + workerB.totalGathered(), 30);

    // ---- 场景 2：持有上限导致的浪费 ----
    ResourceTypeRegistry::registerType(makeType("gold", 10));
    EconomySystem::clearEvents();

    GatherNode goldNode("gold", 25, 1);
    Collector  miner(5, 1, 1);
    const int  player2 = 2;
    CHECK(miner.assign(&goldNode));

    printf("\n[2] 满仓浪费: 玩家上限 10, 节点 25, 载荷 5 (第 3 次起全部浪费)\n");
    printf("    采集循环:\n");

    lastEvents = 0;
    doneTick   = -1;
    for (int t = 0; t < 300; ++t) {
        miner.tick(player2);
        while (EconomySystem::eventCount() > lastEvents) {
            const auto& ev = EconomySystem::eventAt(lastEvents);
            printf("      -> %s %s x%d (balance=%d)\n", ev.action.c_str(), ev.type.c_str(),
                   ev.amount, EconomySystem::get(player2, "gold"));
            ++lastEvents;
        }
        if (miner.isIdle()) {
            doneTick = t;
            break;
        }
    }

    printf("\n    采空于 t=%d\n", doneTick);
    printf("    玩家%d 黄金余额 %d / 上限 %d, 浪费 %d\n", player2,
           EconomySystem::get(player2, "gold"), EconomySystem::getCap(player2, "gold"),
           EconomySystem::getWasted(player2, "gold"));
    printf("    矿工往返 %d 次, 入账 %d\n", miner.trips(), miner.totalGathered());

    CHECK(doneTick >= 0);
    CHECK_EQ(EconomySystem::get(player2, "gold"), 10);
    CHECK_EQ(EconomySystem::getWasted(player2, "gold"), 15);
    CHECK_EQ(miner.trips(), 5);
    CHECK_EQ(miner.totalGathered(), 10);

    printf("\n====================================================================\n");
    printf(" 演示结束\n");
    printf("====================================================================\n");
}
