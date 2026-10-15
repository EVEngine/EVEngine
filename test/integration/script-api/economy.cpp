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

[[maybe_unused]] ResourceTypeDef makeType(const std::string& id, int stockMax, const std::string& category = "stock") {
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
[[maybe_unused]] int runUntilIdle(Collector& worker, int player, int maxTicks = 1000) {
    int ticks = 0;
    while (!worker.isIdle() && ticks < maxTicks) {
        worker.tick(player);
        ++ticks;
    }
    return ticks;
}

}  // namespace


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

[[maybe_unused]] eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

[[maybe_unused]] eve::LogicalId action(const char* text) {
    const auto id = eve::LogicalId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

[[maybe_unused]] eve::Value object(std::initializer_list<std::pair<const std::string, eve::Value>> entries) {
    return eve::Value(eve::Value::Object(entries));
}

[[maybe_unused]] const eve::Value* member(const eve::Value& value, const char* name) {
    const auto* entries = value.getIf<eve::Value::Object>();
    if (entries == nullptr) return nullptr;
    const auto found = entries->find(name);
    return found == entries->end() ? nullptr : &found->second;
}

/** 收据/事件里机械可读的生效数量。 */
[[maybe_unused]] std::int64_t appliedQuantity(const eve::Value& details) {
    const eve::Value* quantity = member(details, "quantity");
    return quantity == nullptr ? -1 : quantity->asInt();
}

/** 已发布实例账本里某个资源类型的当前余额；未列出返回 -1。 */
[[maybe_unused]] std::int64_t balanceOf(const eve::Value& state, const char* type) {
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

[[maybe_unused]] eve::GameplayCommand command(const EconomyFixture& fixture, const char* id, const char* actionText,
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

[[maybe_unused]] eve::GameplayCommand move(const EconomyFixture& fixture, const char* id, const char* actionText,
                          std::uint64_t expectedRevision, const char* type, std::int64_t amount) {
    return command(fixture, id, actionText, expectedRevision,
                   object({{"type", eve::Value(type)}, {"amount", eve::Value(amount)}}));
}

[[maybe_unused]] std::string envelope(const char* operation, const char* access, const std::string& extra = {}) {
    return std::string("{\"schemaId\":\"evengine.gameplay-control-request\",\"schemaVersion\":1,\"op\":\"") +
           operation + "\",\"domain\":\"economy\",\"instance\":\"" + kInstanceId +
           "\",\"session\":{\"id\":\"mcp\",\"access\":\"" + access + "\",\"controlledSubjects\":[\"" + kOwnerId +
           "\"]}" + extra + "}";
}

[[maybe_unused]] std::string jsonCommand(const char* id, const char* actionText, std::uint64_t expectedRevision,
                        const char* parameters) {
    return std::string(",\"command\":{\"id\":\"") + id + "\",\"action\":\"" + actionText + "\",\"subject\":\"" +
           kOwnerId + "\",\"observedTick\":0,\"expectedRevision\":" + std::to_string(expectedRevision) +
           ",\"parameters\":" + parameters + "}";
}

}  // namespace


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
