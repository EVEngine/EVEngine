#include "economy/Economy.h"
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <utility>
#include <vector>
#include "common/Capability.h"
#include "common/GameplayControl.h"
#include "common/GameplayControlJson.h"
#include "common/GameplayInstanceCatalog.h"
#include "common/IEconomy.h"
#include "common/Module.h"
#include "economy/Collector.h"
#include "economy/EconomySystem.h"
#include "economy/GatherNode.h"
#include "economy/ResourceType.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// economy.cpp
using namespace eve::economy;

namespace {

ResourceTypeDef makeType(const std::string& id, int stockMax, const std::string& category = "stock") {
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
    Reset           reset;
    ResourceTypeDef empty;
    CHECK(!ResourceTypeRegistry::registerType(empty));
    CHECK_EQ(ResourceTypeRegistry::count(), 0);
}

TEST_CASE("economy.ledger.creditRespectsCapAndTracksWaste") {
    Reset reset;
    ResourceTypeRegistry::registerType(makeType("minerals", 10));

    EconomyLedger ledger;
    int           accepted = ledger.credit("minerals", 6);
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
                   workerA.stateName().c_str(), workerA.cargo(), workerB.stateName().c_str(), workerB.cargo(),
                   mine.amount(), EconomySystem::get(player, "minerals"));
        }
        while (EconomySystem::eventCount() > lastEvents) {
            const auto& ev = EconomySystem::eventAt(lastEvents);
            printf("      -> %s %s x%d (balance=%d)\n", ev.action.c_str(), ev.type.c_str(), ev.amount,
                   EconomySystem::get(player, "minerals"));
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
    printf("    玩家%d %s 余额 %d / 上限 %d, 浪费 %d\n", player, "minerals", EconomySystem::get(player, "minerals"),
           EconomySystem::getCap(player, "minerals"), EconomySystem::getWasted(player, "minerals"));
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
            printf("      -> %s %s x%d (balance=%d)\n", ev.action.c_str(), ev.type.c_str(), ev.amount,
                   EconomySystem::get(player2, "gold"));
            ++lastEvents;
        }
        if (miner.isIdle()) {
            doneTick = t;
            break;
        }
    }

    printf("\n    采空于 t=%d\n", doneTick);
    printf("    玩家%d 黄金余额 %d / 上限 %d, 浪费 %d\n", player2, EconomySystem::get(player2, "gold"),
           EconomySystem::getCap(player2, "gold"), EconomySystem::getWasted(player2, "gold"));
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

// economy_gameplay_control.cpp
// 经济玩法域契约测试：真实 Economy 模块 + EconomySystem 账本走共享玩法协议，
// 覆盖发现、账本观察、权限档位、上限浪费、修订号账本、事件游标与 JSON 门面路由。
//
// 注意：zeroerr 的 CHECK_EQ(lhs, rhs) 会把 lhs 求值两次（一次判等、一次打印），
// 因此有副作用的调用必须先赋给局部变量，再断言变量。


namespace {

constexpr const char* kInstanceId       = "00000000-0000-7000-8000-000000000801";
constexpr const char* kOwnerId          = "00000000-0000-7000-8000-000000000802";
constexpr const char* kStrangerId       = "00000000-0000-7000-8000-000000000803";
constexpr const char* kSecondInstanceId = "00000000-0000-7000-8000-000000000804";
constexpr const char* kSecondOwnerId    = "00000000-0000-7000-8000-000000000805";
constexpr int         kPlayer           = 7;
constexpr int         kSecondPlayer     = 9;

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

eve::LogicalId action(const char* text) {
    const auto id = eve::LogicalId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

eve::Value object(std::initializer_list<std::pair<const std::string, eve::Value>> entries) {
    return eve::Value(eve::Value::Object(entries));
}

const eve::Value* member(const eve::Value& value, const char* name) {
    const auto* entries = value.getIf<eve::Value::Object>();
    if (entries == nullptr) return nullptr;
    const auto found = entries->find(name);
    return found == entries->end() ? nullptr : &found->second;
}

/** 收据/事件里机械可读的生效数量。 */
std::int64_t appliedQuantity(const eve::Value& details) {
    const eve::Value* quantity = member(details, "quantity");
    return quantity == nullptr ? -1 : quantity->asInt();
}

/** 已发布实例账本里某个资源类型的当前余额；未列出返回 -1。 */
std::int64_t balanceOf(const eve::Value& state, const char* type) {
    const eve::Value* resources = member(state, "resources");
    if (resources == nullptr) return -1;
    const auto* array = resources->getIf<eve::Value::Array>();
    if (array == nullptr) return -1;
    for (const auto& entry : *array) {
        const eve::Value* id     = member(entry, "type");
        const eve::Value* amount = member(entry, "amount");
        if (id != nullptr && amount != nullptr && id->asString() == type) return amount->asInt();
    }
    return -1;
}

/** 真实模块 + 账本，并把玩家账本发布到共享玩法协议。 */
struct EconomyFixture {
    eve::economy::Economy* economy = nullptr;
    eve::SubjectRef        instance;
    eve::SubjectRef        owner;

    EconomyFixture() {
        economy = eve::economy::Economy::create();
        REQUIRE(economy != nullptr);
        eve::economy::EconomySystem::clear();
        eve::economy::Economy::clearTypes();
        eve::economy::Economy::clearEvents();
        REQUIRE(eve::economy::Economy::registerResourceType("gold", "stock", 100, "finite"));
        REQUIRE(eve::economy::Economy::registerResourceType("food", "stock", 0, "renewable"));
        REQUIRE(eve::economy::Economy::registerResourceType("crystal", "special", 10, "growing"));
        instance             = subject(kInstanceId);
        owner                = subject(kOwnerId);
        const auto published = economy->publishGameplay(kInstanceId, kOwnerId, kPlayer);
        REQUIRE(published.ok());
    }

    ~EconomyFixture() {
        economy->clearGameplayControls();
        eve::economy::EconomySystem::clear();
        eve::economy::Economy::clearTypes();
        eve::economy::Economy::clearEvents();
    }

    EconomyFixture(const EconomyFixture&)            = delete;
    EconomyFixture& operator=(const EconomyFixture&) = delete;

    /** 通过能力注册表取回适配器，和 JSON 路由走同一条查找路径。 */
    eve::IGameplayControlProvider* provider() const {
        eve::IGameplayControlProvider* found = nullptr;
        eve::cap::forEach<eve::IGameplayControlProvider>([&](auto* candidate) {
            if (candidate != nullptr && candidate->gameplayDomain() == "economy") found = candidate;
        });
        return found;
    }

    /** 再发布第二个玩家的账本（多实例共存）。 */
    eve::Result<void> publishSecondPlayer() {
        return economy->publishGameplay(kSecondInstanceId, kSecondOwnerId, kSecondPlayer);
    }
};

eve::GameplayCommand command(const EconomyFixture& fixture, const char* id, const char* actionText,
                             std::uint64_t expectedRevision, eve::Value parameters) {
    eve::GameplayCommand result;
    result.id               = id;
    result.action           = action(actionText);
    result.subject          = fixture.owner;
    result.observedTick     = eve::SimulationTick::zero();
    result.expectedRevision = expectedRevision;
    result.parameters       = std::move(parameters);
    return result;
}

eve::GameplayCommand move(const EconomyFixture& fixture, const char* id, const char* actionText,
                          std::uint64_t expectedRevision, const char* type, std::int64_t amount) {
    return command(fixture, id, actionText, expectedRevision,
                   object({{"type", eve::Value(type)}, {"amount", eve::Value(amount)}}));
}

std::string envelope(const char* operation, const char* access, const std::string& extra = {}) {
    return std::string("{\"schemaId\":\"evengine.gameplay-control-request\",\"schemaVersion\":1,\"op\":\"") +
           operation + "\",\"domain\":\"economy\",\"instance\":\"" + kInstanceId +
           "\",\"session\":{\"id\":\"mcp\",\"access\":\"" + access + "\",\"controlledSubjects\":[\"" + kOwnerId +
           "\"]}" + extra + "}";
}

std::string jsonCommand(const char* id, const char* actionText, std::uint64_t expectedRevision,
                        const char* parameters) {
    return std::string(",\"command\":{\"id\":\"") + id + "\",\"action\":\"" + actionText + "\",\"subject\":\"" +
           kOwnerId + "\",\"observedTick\":0,\"expectedRevision\":" + std::to_string(expectedRevision) +
           ",\"parameters\":" + parameters + "}";
}

}  // namespace

TEST_CASE("gameplay.control.economyPublishesOneVocabularyForPlayerAndAutomation") {
    EconomyFixture fixture;
    auto*          provider = fixture.provider();
    REQUIRE(provider != nullptr);
    CHECK_EQ(fixture.economy->gameplayControlCount(), 1);

    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {fixture.owner}};
    eve::GameplaySession automation{"automation", eve::GameplayAccess::TestDriver, {fixture.owner}};

    auto observed = provider->observeGameplay(player, fixture.instance);
    REQUIRE(observed.ok());
    const auto observation = std::move(observed).takeValue();
    CHECK_EQ(observation.domain.format(), std::string("gameplay:economy"));
    CHECK_EQ(observation.instance.format(), std::string(kInstanceId));
    CHECK_EQ(observation.revision, std::uint64_t{0});
    CHECK_EQ(member(observation.state, "player")->asInt(), std::int64_t{kPlayer});
    CHECK_EQ(balanceOf(observation.state, "gold"), std::int64_t{0});
    CHECK_EQ(balanceOf(observation.state, "crystal"), std::int64_t{0});

    const eve::Value* resources = member(observation.state, "resources");
    REQUIRE(resources != nullptr);
    const auto* array = resources->getIf<eve::Value::Array>();
    REQUIRE(array != nullptr);
    CHECK_EQ(static_cast<int>(array->size()), 3);
    // 注册顺序无关：类型按 id 字典序出现，crystal/food/gold 都能被逐个检查。
    CHECK_EQ(member((*array)[0], "type")->asString(), std::string("crystal"));
    CHECK_EQ(member((*array)[0], "cap")->asInt(), std::int64_t{10});
    CHECK_EQ(member((*array)[0], "depletion")->asString(), std::string("growing"));
    CHECK_EQ(member((*array)[1], "type")->asString(), std::string("food"));
    CHECK_EQ(member((*array)[1], "cap")->asInt(), std::int64_t{0});
    CHECK_EQ(member((*array)[2], "type")->asString(), std::string("gold"));
    CHECK_EQ(member((*array)[2], "category")->asString(), std::string("stock"));

    auto playerActions     = provider->availableGameplayActions(player, fixture.instance, fixture.owner);
    auto automationActions = provider->availableGameplayActions(automation, fixture.instance, fixture.owner);
    REQUIRE(playerActions.ok());
    REQUIRE(automationActions.ok());
    CHECK_EQ(static_cast<int>(playerActions.value().size()), 1);
    CHECK_EQ(static_cast<int>(automationActions.value().size()), 2);
    CHECK_EQ(playerActions.value()[0].id.format(), std::string("economy:debit"));
    bool automationMayGrant = false;
    for (const auto& descriptor : automationActions.value()) {
        if (descriptor.id.format() == "economy:credit") automationMayGrant = true;
    }
    CHECK(automationMayGrant);

    auto wrongSubject = provider->availableGameplayActions(player, fixture.instance, subject(kStrangerId));
    CHECK(!wrongSubject.ok());
    CHECK_EQ(wrongSubject.code(), eve::StatusCode::Rejected);

    eve::GameplaySession intruder{"intruder", eve::GameplayAccess::PlayerEquivalent, {subject(kStrangerId)}};
    auto                 denied = provider->observeGameplay(intruder, fixture.instance);
    CHECK(!denied.ok());
    CHECK_EQ(denied.code(), eve::StatusCode::Rejected);

    auto missing = provider->observeGameplay(automation, subject(kStrangerId));
    CHECK(!missing.ok());
    CHECK_EQ(missing.code(), eve::StatusCode::NotFound);
}

TEST_CASE("gameplay.control.economyGrantsOnlyThroughNonPlayerProfiles") {
    EconomyFixture fixture;
    auto*          provider = fixture.provider();
    REQUIRE(provider != nullptr);
    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {fixture.owner}};
    eve::GameplaySession automation{"automation", eve::GameplayAccess::TestDriver, {fixture.owner}};

    auto refused = provider->submitGameplay(player, fixture.instance,
                                            move(fixture, "grant-refused", "economy:credit", 0, "gold", 50));
    CHECK(!refused.ok());
    CHECK_EQ(refused.code(), eve::StatusCode::Rejected);
    CHECK_EQ(eve::economy::EconomySystem::get(kPlayer, "gold"), 0);

    // 上限 100：发放 150 只入账 100，其余 50 以 waste 事件披露。
    auto capped = provider->submitGameplay(automation, fixture.instance,
                                           move(fixture, "grant-capped", "economy:credit", 0, "gold", 150));
    REQUIRE(capped.ok());
    CHECK_EQ(appliedQuantity(capped.value().details), std::int64_t{100});
    CHECK_EQ(eve::economy::EconomySystem::get(kPlayer, "gold"), 100);
    CHECK_EQ(eve::economy::EconomySystem::getWasted(kPlayer, "gold"), 50);

    // 上限 0 = 不限：food 可自由入账。
    auto unlimited = provider->submitGameplay(automation, fixture.instance,
                                              move(fixture, "grant-unlimited", "economy:credit", 1, "food", 25));
    REQUIRE(unlimited.ok());
    CHECK_EQ(appliedQuantity(unlimited.value().details), std::int64_t{25});

    auto events = provider->gameplayEvents(automation, fixture.instance, 0);
    REQUIRE(events.ok());
    const auto list = std::move(events).takeValue();
    REQUIRE_EQ(static_cast<int>(list.size()), 3);
    CHECK_EQ(list[0].type, std::string("economy.credited"));
    CHECK_EQ(list[1].type, std::string("economy.wasted"));
    CHECK_EQ(appliedQuantity(list[1].payload), std::int64_t{50});
    CHECK_EQ(list[2].type, std::string("economy.credited"));
    CHECK_EQ(list[2].causationCommandId, std::string("grant-unlimited"));
    CHECK_EQ(list[2].subject.format(), std::string(kOwnerId));

    // 消费：玩家档位可用，余额不足即拒绝且不改变账本。
    auto tooExpensive = provider->submitGameplay(player, fixture.instance,
                                                 move(fixture, "spend-too-much", "economy:debit", 2, "gold", 500));
    CHECK(!tooExpensive.ok());
    CHECK_EQ(tooExpensive.code(), eve::StatusCode::Rejected);
    CHECK_EQ(eve::economy::EconomySystem::get(kPlayer, "gold"), 100);

    auto spent =
        provider->submitGameplay(player, fixture.instance, move(fixture, "spend-1", "economy:debit", 2, "gold", 40));
    REQUIRE(spent.ok());
    CHECK_EQ(appliedQuantity(spent.value().details), std::int64_t{40});
    CHECK_EQ(eve::economy::EconomySystem::get(kPlayer, "gold"), 60);
    CHECK_EQ(eve::economy::EconomySystem::getExpense(kPlayer, "gold"), 40);

    // 未知类型：账本与注册表都不认识，报 NotFound 而不是"余额不足"。
    auto unknown = provider->submitGameplay(player, fixture.instance,
                                            move(fixture, "spend-unknown", "economy:debit", 3, "unobtanium", 1));
    CHECK(!unknown.ok());
    CHECK_EQ(unknown.code(), eve::StatusCode::NotFound);

    auto unsupported = provider->submitGameplay(player, fixture.instance,
                                                move(fixture, "spend-weird", "economy:teleport", 3, "gold", 1));
    CHECK(!unsupported.ok());
    CHECK_EQ(unsupported.code(), eve::StatusCode::Unsupported);

    auto stale =
        provider->submitGameplay(player, fixture.instance, move(fixture, "spend-stale", "economy:debit", 0, "gold", 1));
    CHECK(!stale.ok());
    CHECK_EQ(stale.code(), eve::StatusCode::Conflict);

    // 负数/零数量在任何档位都被拒绝。
    auto negative = provider->submitGameplay(automation, fixture.instance,
                                             move(fixture, "grant-negative", "economy:credit", 3, "gold", -5));
    CHECK(!negative.ok());
    CHECK_EQ(negative.code(), eve::StatusCode::Rejected);

    // 事件游标只看新增部分：消费事件排在三个发放事件之后。
    auto later = provider->gameplayEvents(player, fixture.instance, 3);
    REQUIRE(later.ok());
    const auto tail = std::move(later).takeValue();
    REQUIRE_EQ(static_cast<int>(tail.size()), 1);
    CHECK_EQ(tail[0].type, std::string("economy.debited"));
    CHECK_EQ(tail[0].causationCommandId, std::string("spend-1"));
    CHECK_EQ(appliedQuantity(tail[0].payload), std::int64_t{40});

    auto drained = provider->gameplayEvents(player, fixture.instance, 4);
    REQUIRE(drained.ok());
    CHECK_EQ(drained.code(), eve::StatusCode::NoOp);

    auto advanced = provider->advanceGameplay(player, fixture.instance,
                                              {eve::SimulationTick(1), eve::Duration::fromNanoseconds(16666667)});
    REQUIRE(advanced.ok());
    CHECK_EQ(advanced.value().tick.value(), std::uint64_t{1});
    auto rewound = provider->advanceGameplay(player, fixture.instance,
                                             {eve::SimulationTick(1), eve::Duration::fromNanoseconds(1)});
    CHECK(!rewound.ok());
    CHECK_EQ(rewound.code(), eve::StatusCode::Conflict);
}

TEST_CASE("gameplay.control.economyServesSeveralLedgersFromOneDomain") {
    EconomyFixture fixture;
    auto*          provider = fixture.provider();
    REQUIRE(provider != nullptr);

    const auto second = fixture.publishSecondPlayer();
    REQUIRE(second.ok());
    CHECK_EQ(fixture.economy->gameplayControlCount(), 2);
    const auto published = fixture.economy->gameplayInstances();
    REQUIRE_EQ(static_cast<int>(published.size()), 2);
    CHECK_EQ(published[1], std::string(kSecondInstanceId));

    int matches = 0;
    eve::cap::forEach<eve::IGameplayControlProvider>([&](auto* candidate) {
        if (candidate != nullptr && candidate->gameplayDomain() == "economy") ++matches;
    });
    CHECK_EQ(matches, 1);

    eve::GameplaySession automation{"automation", eve::GameplayAccess::TestDriver, {fixture.owner}};
    auto                 granted = provider->submitGameplay(automation, fixture.instance,
                                                            move(fixture, "grant-primary", "economy:credit", 0, "gold", 30));
    REQUIRE(granted.ok());
    CHECK_EQ(eve::economy::EconomySystem::get(kPlayer, "gold"), 30);
    // 第二个账本完全隔离：第一个玩家的发放不泄漏到它。
    CHECK_EQ(eve::economy::EconomySystem::get(kSecondPlayer, "gold"), 0);

    const auto           secondInstance = subject(kSecondInstanceId);
    const auto           secondOwner    = subject(kSecondOwnerId);
    eve::GameplaySession secondSession{"second-player", eve::GameplayAccess::PlayerEquivalent, {secondOwner}};
    auto                 observed = provider->observeGameplay(secondSession, secondInstance);
    REQUIRE(observed.ok());
    CHECK_EQ(member(observed.value().state, "player")->asInt(), std::int64_t{kSecondPlayer});
    CHECK_EQ(balanceOf(observed.value().state, "gold"), std::int64_t{0});

    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {fixture.owner}};
    auto                 denied = provider->observeGameplay(player, secondInstance);
    CHECK(!denied.ok());
    CHECK_EQ(denied.code(), eve::StatusCode::Rejected);

    auto duplicate = fixture.economy->publishGameplay(kInstanceId, kOwnerId, kPlayer);
    CHECK(!duplicate.ok());
    CHECK_EQ(duplicate.code(), eve::StatusCode::Conflict);

    const auto unpublished = fixture.economy->unpublishGameplay(kSecondInstanceId);
    CHECK(unpublished.ok());
    const auto again = fixture.economy->unpublishGameplay(kSecondInstanceId);
    CHECK(!again.ok());
    CHECK_EQ(again.code(), eve::StatusCode::NotFound);
    CHECK_EQ(fixture.economy->gameplayControlCount(), 1);
}

TEST_CASE("gameplay.control.economyRoutesThroughTheSharedJsonFacade") {
    EconomyFixture fixture;

    auto domains = eve::executeGameplayControlJson(
        R"({"schemaId":"evengine.gameplay-control-request","schemaVersion":1,"op":"domains"})");
    REQUIRE(domains.ok());
    CHECK(domains.value().find("economy") != std::string::npos);

    auto catalogs = eve::executeGameplayControlJson(
        R"({"schemaId":"evengine.gameplay-control-request","schemaVersion":1,"op":"instances"})");
    REQUIRE(catalogs.ok());
    CHECK(catalogs.value().find("\"domain\":\"economy\"") != std::string::npos);
    CHECK(catalogs.value().find(kInstanceId) != std::string::npos);

    auto observed = eve::executeGameplayControlJson(envelope("observe", "player"));
    REQUIRE(observed.ok());
    CHECK(observed.value().find("gameplay:economy") != std::string::npos);
    CHECK(observed.value().find("\"player\":7") != std::string::npos);
    CHECK(observed.value().find("\"type\":\"gold\"") != std::string::npos);

    const std::string actor         = std::string(",\"subject\":\"") + kOwnerId + "\"";
    auto              playerActions = eve::executeGameplayControlJson(envelope("actions", "player", actor));
    auto              driverActions = eve::executeGameplayControlJson(envelope("actions", "test-driver", actor));
    REQUIRE(playerActions.ok());
    REQUIRE(driverActions.ok());
    CHECK(playerActions.value().find("economy:debit") != std::string::npos);
    CHECK(playerActions.value().find("economy:credit") == std::string::npos);
    CHECK(driverActions.value().find("economy:credit") != std::string::npos);

    auto refused = eve::executeGameplayControlJson(envelope(
        "submit", "player", jsonCommand("json-grant-refused", "economy:credit", 0, R"({"type":"gold","amount":5})")));
    CHECK(!refused.ok());
    CHECK_EQ(refused.code(), eve::StatusCode::Rejected);

    auto granted = eve::executeGameplayControlJson(envelope(
        "submit", "test-driver", jsonCommand("json-grant-1", "economy:credit", 0, R"({"type":"gold","amount":20})")));
    REQUIRE(granted.ok());
    CHECK(granted.value().find("\"resultingRevision\":1") != std::string::npos);
    CHECK(granted.value().find("\"quantity\":20") != std::string::npos);

    auto spent = eve::executeGameplayControlJson(
        envelope("submit", "player", jsonCommand("json-spend-1", "economy:debit", 1, R"({"type":"gold","amount":5})")));
    REQUIRE(spent.ok());
    CHECK(spent.value().find("\"resultingRevision\":2") != std::string::npos);
    CHECK_EQ(eve::economy::EconomySystem::get(kPlayer, "gold"), 15);

    auto events = eve::executeGameplayControlJson(envelope("events", "player", ",\"afterSequence\":0"));
    REQUIRE(events.ok());
    CHECK(events.value().find("economy.credited") != std::string::npos);
    CHECK(events.value().find("economy.debited") != std::string::npos);

    auto advanced =
        eve::executeGameplayControlJson(envelope("advance", "player", ",\"tick\":1,\"deltaNanoseconds\":16666667"));
    REQUIRE(advanced.ok());
    CHECK(advanced.value().find("\"tick\":1") != std::string::npos);
}

TEST_CASE("gameplay.control.economyScriptPublishesItsLedger") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"SQ(
        local economy = eve.Economy();
        economy.clearTypes();
        economy.clearGameplayControls();
        economy.registerResourceType("gold", "stock", 100, "finite");
        scriptPublished <- economy.publishGameplay(
            "00000000-0000-7000-8000-000000000c01",
            "00000000-0000-7000-8000-000000000c02", 42).ok;
        scriptInstances <- economy.getGameplayControlCount();
        scriptTypeIds <- economy.getTypeId(0) + "," + economy.getTypeId(1);
        local duplicate = economy.publishGameplay(
            "00000000-0000-7000-8000-000000000c01",
            "00000000-0000-7000-8000-000000000c02", 42);
        scriptDuplicateOk <- duplicate.ok;
        scriptDuplicateMessage <- duplicate.message;
        local malformed = economy.publishGameplay("not-a-uuid",
            "00000000-0000-7000-8000-000000000c02", 42);
        scriptMalformedOk <- malformed.ok;
        scriptUnpublished <- economy.unpublishGameplay("00000000-0000-7000-8000-000000000c01").ok;
        scriptRemaining <- economy.getGameplayControlCount();
        economy.clearTypes();
    )SQ"));

    CHECK(vm.find("scriptPublished").toBool());
    CHECK_EQ(static_cast<int>(vm.find("scriptInstances").toInt()), 1);
    CHECK_EQ(vm.find("scriptTypeIds").toString(), std::string("gold,"));
    CHECK(!vm.find("scriptDuplicateOk").toBool());
    CHECK(vm.find("scriptDuplicateMessage").toString().find("already owns") != std::string::npos);
    CHECK(!vm.find("scriptMalformedOk").toBool());
    CHECK(vm.find("scriptUnpublished").toBool());
    CHECK_EQ(static_cast<int>(vm.find("scriptRemaining").toInt()), 0);
}
