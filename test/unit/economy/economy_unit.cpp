#include "common/Capability.h"
#include "common/GameplayControl.h"
#include "common/GameplayControlJson.h"
#include "common/IEconomy.h"
#include "economy/Collector.h"
#include "economy/Economy.h"
#include "economy/EconomySystem.h"
#include "economy/GatherNode.h"
#include "economy/ResourceType.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cstdint>
#include <string>
#include <utility>

using namespace eve::economy;

namespace {
struct CleanEconomy {
    CleanEconomy() {
        ResourceTypeRegistry::clear();
        EconomySystem::clear();
    }
    ~CleanEconomy() {
        ResourceTypeRegistry::clear();
        EconomySystem::clear();
    }
};

ResourceTypeDef resource(std::string id, int cap) {
    ResourceTypeDef result;
    result.id        = std::move(id);
    result.category  = "stock";
    result.stockMax  = cap;
    result.depletion = DepletionModel::Finite;
    return result;
}

int runUntilIdle(Collector& worker, int player) {
    int ticks = 0;
    while (!worker.isIdle() && ticks < 1000) {
        worker.tick(player);
        ++ticks;
    }
    return ticks;
}

constexpr auto instanceId    = "00000000-0000-7000-8000-000000000801";
constexpr auto ownerId       = "00000000-0000-7000-8000-000000000802";
constexpr auto strangerId    = "00000000-0000-7000-8000-000000000803";
constexpr auto secondId      = "00000000-0000-7000-8000-000000000804";
constexpr auto secondOwnerId = "00000000-0000-7000-8000-000000000805";

eve::SubjectRef subject(const char* text) {
    auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

eve::LogicalId action(const char* text) {
    auto id = eve::LogicalId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

const eve::Value* member(const eve::Value& value, const char* key) {
    auto* object = value.getIf<eve::Value::Object>();
    if (!object) return nullptr;
    auto it = object->find(key);
    return it == object->end() ? nullptr : &it->second;
}

std::int64_t balance(const eve::Value& state, const char* type) {
    auto* resources = member(state, "resources");
    if (!resources) return -1;
    auto* array = resources->getIf<eve::Value::Array>();
    if (!array) return -1;
    for (const auto& item : *array) {
        auto* name   = member(item, "type");
        auto* amount = member(item, "amount");
        if (name && amount && name->asString() == type) return amount->asInt();
    }
    return -1;
}

struct PublishedEconomy {
    Economy*             module;
    eve::SubjectRef      instance = subject(instanceId);
    eve::SubjectRef      owner    = subject(ownerId);
    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {owner}};
    eve::GameplaySession driver{"driver", eve::GameplayAccess::TestDriver, {owner}};

    PublishedEconomy() : module(Economy::create()) {
        REQUIRE(module != nullptr);
        EconomySystem::clear();
        Economy::clearTypes();
        Economy::clearEvents();
        REQUIRE(Economy::registerResourceType("gold", "stock", 100, "finite"));
        REQUIRE(Economy::registerResourceType("food", "stock", 0, "renewable"));
        REQUIRE(module->publishGameplay(instanceId, ownerId, 7).ok());
    }
    ~PublishedEconomy() {
        module->clearGameplayControls();
        EconomySystem::clear();
        Economy::clearTypes();
        Economy::clearEvents();
    }
    eve::IGameplayControlProvider& provider() {
        eve::IGameplayControlProvider* found = nullptr;
        eve::cap::forEach<eve::IGameplayControlProvider>([&](auto* candidate) {
            if (candidate && candidate->gameplayDomain() == "economy") found = candidate;
        });
        REQUIRE(found != nullptr);
        return *found;
    }
    eve::GameplayCommand command(const char* id, const char* verb, std::uint64_t revision, const char* type,
                                 std::int64_t amount) const {
        eve::GameplayCommand result;
        result.id               = id;
        result.action           = action(verb);
        result.subject          = owner;
        result.observedTick     = eve::SimulationTick::zero();
        result.expectedRevision = revision;
        eve::Value::Object parameters;
        parameters.emplace("type", eve::Value(type));
        parameters.emplace("amount", eve::Value(amount));
        result.parameters = eve::Value(std::move(parameters));
        return result;
    }
};

std::string request(const char* op, const char* access, std::string extra = {}) {
    return std::string("{\"schemaId\":\"evengine.gameplay-control-request\",\"schemaVersion\":1,\"op\":\"") + op +
           "\",\"domain\":\"economy\",\"instance\":\"" + instanceId + "\",\"session\":{\"id\":\"test\",\"access\":\"" +
           access + "\",\"controlledSubjects\":[\"" + ownerId + "\"]}" + extra + "}";
}

std::string commandJson(const char* id, const char* verb, int revision, int amount) {
    return std::string(",\"command\":{\"id\":\"") + id + "\",\"action\":\"" + verb + "\",\"subject\":\"" + ownerId +
           "\",\"observedTick\":0,\"expectedRevision\":" + std::to_string(revision) +
           ",\"parameters\":{\"type\":\"gold\",\"amount\":" + std::to_string(amount) + "}}";
}
}  // namespace

// Resource definitions and the ledger expose caps, accepted quantities and cumulative accounting.
TEST_CASE("economy.registry.registrationReplacementAndInvalidIdentity") {
    CleanEconomy clean;
    CHECK_EQ(ResourceTypeRegistry::count(), 0);
    CHECK(ResourceTypeRegistry::find("gold") == nullptr);
    REQUIRE(ResourceTypeRegistry::registerType(resource("gold", 100)));
    REQUIRE(ResourceTypeRegistry::registerType(resource("gold", 200)));
    CHECK_EQ(ResourceTypeRegistry::count(), 1);
    REQUIRE(ResourceTypeRegistry::find("gold") != nullptr);
    CHECK_EQ(ResourceTypeRegistry::find("gold")->stockMax, 200);
    CHECK(!ResourceTypeRegistry::registerType(resource("", 10)));
    CHECK_EQ(ResourceTypeRegistry::count(), 1);
    ResourceTypeRegistry::clear();
    CHECK_EQ(ResourceTypeRegistry::count(), 0);
}

TEST_CASE("economy.ledger.creditCapsTracksWasteAndUnknownTypeIsUnlimited") {
    CleanEconomy clean;
    REQUIRE(ResourceTypeRegistry::registerType(resource("gold", 10)));
    EconomyLedger ledger;
    const int     first  = ledger.credit("gold", 6);
    const int     second = ledger.credit("gold", 6);
    CHECK_EQ(first, 6);
    CHECK_EQ(second, 4);
    CHECK_EQ(ledger.get("gold"), 10);
    CHECK_EQ(ledger.getIncome("gold"), 10);
    CHECK_EQ(ledger.getWasted("gold"), 2);
    const int uncapped = ledger.credit("unregistered", 20);
    CHECK_EQ(uncapped, 20);
    CHECK_EQ(ledger.get("unregistered"), 20);
}

TEST_CASE("economy.ledger.debitRejectsInsufficientFundsWithoutMutation") {
    CleanEconomy  clean;
    EconomyLedger ledger;
    const int     credited = ledger.credit("gold", 5);
    CHECK_EQ(credited, 5);
    CHECK(ledger.canAfford("gold", 5));
    CHECK(!ledger.canAfford("gold", 6));
    REQUIRE(ledger.debit("gold", 3));
    CHECK_EQ(ledger.get("gold"), 2);
    CHECK(!ledger.debit("gold", 3));
    CHECK_EQ(ledger.get("gold"), 2);
    CHECK_EQ(ledger.getExpense("gold"), 3);
}

TEST_CASE("economy.system.eventsDescribeAcceptedCreditDebitAndWaste") {
    CleanEconomy clean;
    REQUIRE(ResourceTypeRegistry::registerType(resource("wood", 20)));
    int callbacks = 0;
    EconomySystem::registerHook("test", [&](const EconomyEvent&) { ++callbacks; });
    const int first = EconomySystem::credit(1, "wood", 5);
    CHECK_EQ(first, 5);
    REQUIRE(EconomySystem::debit(1, "wood", 2));
    const int capped = EconomySystem::credit(1, "wood", 19);
    CHECK_EQ(capped, 17);
    CHECK_EQ(EconomySystem::get(1, "wood"), 20);
    CHECK_EQ(EconomySystem::getWasted(1, "wood"), 2);
    CHECK_EQ(EconomySystem::eventCount(), 4);
    CHECK_EQ(EconomySystem::eventAt(0).action, std::string("credit"));
    CHECK_EQ(EconomySystem::eventAt(1).action, std::string("debit"));
    CHECK_EQ(EconomySystem::eventAt(2).amount, 17);
    CHECK_EQ(EconomySystem::eventAt(3).action, std::string("waste"));
    CHECK_EQ(EconomySystem::eventAt(3).amount, 2);
    CHECK_EQ(callbacks, 4);
}

// Worker trips reserve node slots and deposit full or partial cargo until depletion.
TEST_CASE("economy.collector.fullLoadsAndFinalPartialLoadReachLedger") {
    CleanEconomy clean;
    REQUIRE(ResourceTypeRegistry::registerType(resource("ore", 100)));
    GatherNode node("ore", 13, 2);
    Collector  worker(5, 1, 2);
    REQUIRE(worker.assign(&node));
    CHECK_LT(runUntilIdle(worker, 7), 1000);
    CHECK(node.depleted());
    CHECK(worker.isIdle());
    CHECK_EQ(worker.cargo(), 0);
    CHECK_EQ(worker.trips(), 3);
    CHECK_EQ(worker.totalGathered(), 13);
    CHECK_EQ(EconomySystem::get(7, "ore"), 13);
}

TEST_CASE("economy.collector.fullCapReportsWasteButStillDrainsNode") {
    CleanEconomy clean;
    REQUIRE(ResourceTypeRegistry::registerType(resource("ore", 10)));
    GatherNode node("ore", 25, 1);
    Collector  worker(5, 1, 1);
    REQUIRE(worker.assign(&node));
    CHECK_LT(runUntilIdle(worker, 7), 1000);
    CHECK(node.depleted());
    CHECK_EQ(worker.trips(), 5);
    CHECK_EQ(worker.totalGathered(), 10);
    CHECK_EQ(EconomySystem::get(7, "ore"), 10);
    CHECK_EQ(EconomySystem::getWasted(7, "ore"), 15);
}

TEST_CASE("economy.collector.slotContentionAndRelease") {
    CleanEconomy clean;
    REQUIRE(ResourceTypeRegistry::registerType(resource("ore", 100)));
    GatherNode node("ore", 10, 1);
    Collector  first(5, 1, 1), second(5, 1, 1);
    REQUIRE(first.assign(&node));
    CHECK_EQ(node.freeSlots(), 0);
    CHECK(!second.assign(&node));
    CHECK(second.isIdle());
    first.clearAssignment();
    CHECK_EQ(node.freeSlots(), 1);
    REQUIRE(second.assign(&node));
    CHECK_LT(runUntilIdle(second, 7), 1000);
    CHECK_EQ(node.freeSlots(), 1);
    CHECK_EQ(EconomySystem::get(7, "ore"), 10);
}

TEST_CASE("economy.capability.registeredIEconomyMatchesLedgerAccounting") {
    CleanEconomy clean;
    REQUIRE(Economy::create() != nullptr);
    auto* service = eve::cap::query<eve::economy::IEconomy>();
    REQUIRE(service != nullptr);
    REQUIRE(Economy::registerResourceType("gold", "stock", 50, "finite"));
    const int accepted = service->credit(3, "gold", 60);
    CHECK_EQ(accepted, 50);
    CHECK_EQ(service->get(3, "gold"), 50);
    CHECK_EQ(service->getWasted(3, "gold"), 10);
    REQUIRE(service->debit(3, "gold", 20));
    CHECK_EQ(service->get(3, "gold"), 30);
    CHECK_EQ(service->getCap(3, "gold"), 50);
    CHECK_EQ(service->getIncome(3, "gold"), 50);
    CHECK_EQ(service->getExpense(3, "gold"), 20);
}

// Gameplay control: observers, access levels, revision CAS, events and instance isolation.
TEST_CASE("economy.gameplay.observationAndActionsRespectOwnerAndAccess") {
    PublishedEconomy fixture;
    auto&            provider = fixture.provider();
    auto             observed = provider.observeGameplay(fixture.player, fixture.instance);
    REQUIRE(observed.ok());
    CHECK_EQ(observed.value().domain.format(), std::string("gameplay:economy"));
    CHECK_EQ(observed.value().revision, std::uint64_t{0});
    CHECK_EQ(member(observed.value().state, "player")->asInt(), std::int64_t{7});
    CHECK_EQ(balance(observed.value().state, "gold"), std::int64_t{0});
    auto playerActions = provider.availableGameplayActions(fixture.player, fixture.instance, fixture.owner);
    auto driverActions = provider.availableGameplayActions(fixture.driver, fixture.instance, fixture.owner);
    REQUIRE(playerActions.ok());
    REQUIRE(driverActions.ok());
    CHECK_EQ(playerActions.value().size(), size_t{1});
    CHECK_EQ(playerActions.value()[0].id.format(), std::string("economy:debit"));
    CHECK_EQ(driverActions.value().size(), size_t{2});
    eve::GameplaySession intruder{"intruder", eve::GameplayAccess::PlayerEquivalent, {subject(strangerId)}};
    CHECK(!provider.observeGameplay(intruder, fixture.instance).ok());
    CHECK(!provider.availableGameplayActions(fixture.player, fixture.instance, subject(strangerId)).ok());
    CHECK(!provider.observeGameplay(fixture.driver, subject(strangerId)).ok());
}

TEST_CASE("economy.gameplay.creditDebitCapAndRevisionOutcomes") {
    PublishedEconomy fixture;
    auto&            provider = fixture.provider();
    auto             denied   = provider.submitGameplay(fixture.player, fixture.instance,
                                                        fixture.command("denied", "economy:credit", 0, "gold", 50));
    CHECK(!denied.ok());
    CHECK_EQ(EconomySystem::get(7, "gold"), 0);
    auto capped = provider.submitGameplay(fixture.driver, fixture.instance,
                                          fixture.command("grant", "economy:credit", 0, "gold", 150));
    REQUIRE(capped.ok());
    CHECK_EQ(EconomySystem::get(7, "gold"), 100);
    CHECK_EQ(EconomySystem::getWasted(7, "gold"), 50);
    auto unlimited = provider.submitGameplay(fixture.driver, fixture.instance,
                                             fixture.command("food", "economy:credit", 1, "food", 25));
    REQUIRE(unlimited.ok());
    CHECK_EQ(EconomySystem::get(7, "food"), 25);
    CHECK(!provider
               .submitGameplay(fixture.player, fixture.instance,
                               fixture.command("too-much", "economy:debit", 2, "gold", 500))
               .ok());
    auto spent = provider.submitGameplay(fixture.player, fixture.instance,
                                         fixture.command("spend", "economy:debit", 2, "gold", 40));
    REQUIRE(spent.ok());
    CHECK_EQ(EconomySystem::get(7, "gold"), 60);
    CHECK_EQ(EconomySystem::getExpense(7, "gold"), 40);
    auto stale = provider.submitGameplay(fixture.player, fixture.instance,
                                         fixture.command("stale", "economy:debit", 0, "gold", 1));
    CHECK(!stale.ok());
    CHECK(static_cast<int>(stale.code()) == static_cast<int>(eve::StatusCode::Conflict));
    CHECK(!provider
               .submitGameplay(fixture.driver, fixture.instance,
                               fixture.command("negative", "economy:credit", 3, "gold", -1))
               .ok());
    CHECK(!provider
               .submitGameplay(fixture.player, fixture.instance,
                               fixture.command("unknown", "economy:debit", 3, "unknown", 1))
               .ok());
    CHECK_EQ(EconomySystem::get(7, "gold"), 60);
}

TEST_CASE("economy.gameplay.eventCursorAndSimulationTime") {
    PublishedEconomy fixture;
    auto&            provider = fixture.provider();
    REQUIRE(provider
                .submitGameplay(fixture.driver, fixture.instance,
                                fixture.command("grant", "economy:credit", 0, "gold", 150))
                .ok());
    auto all = provider.gameplayEvents(fixture.driver, fixture.instance, 0);
    REQUIRE(all.ok());
    REQUIRE(all.value().size() == size_t{2});
    CHECK_EQ(all.value()[0].type, std::string("economy.credited"));
    CHECK_EQ(all.value()[1].type, std::string("economy.wasted"));
    auto empty = provider.gameplayEvents(fixture.driver, fixture.instance, 2);
    REQUIRE(empty.ok());
    CHECK(static_cast<int>(empty.code()) == static_cast<int>(eve::StatusCode::NoOp));
    auto advanced = provider.advanceGameplay(fixture.player, fixture.instance,
                                             {eve::SimulationTick(1), eve::Duration::fromNanoseconds(16666667)});
    REQUIRE(advanced.ok());
    CHECK_EQ(advanced.value().tick.value(), std::uint64_t{1});
    CHECK(!provider
               .advanceGameplay(fixture.player, fixture.instance,
                                {eve::SimulationTick(1), eve::Duration::fromNanoseconds(1)})
               .ok());
}

TEST_CASE("economy.gameplay.multiplePublishedLedgersStayIndependent") {
    PublishedEconomy fixture;
    auto&            provider = fixture.provider();
    REQUIRE(fixture.module->publishGameplay(secondId, secondOwnerId, 9).ok());
    CHECK_EQ(fixture.module->gameplayControlCount(), 2);
    REQUIRE(
        provider
            .submitGameplay(fixture.driver, fixture.instance, fixture.command("grant", "economy:credit", 0, "gold", 30))
            .ok());
    CHECK_EQ(EconomySystem::get(7, "gold"), 30);
    CHECK_EQ(EconomySystem::get(9, "gold"), 0);
    eve::GameplaySession second{"second", eve::GameplayAccess::PlayerEquivalent, {subject(secondOwnerId)}};
    auto                 observed = provider.observeGameplay(second, subject(secondId));
    REQUIRE(observed.ok());
    CHECK_EQ(balance(observed.value().state, "gold"), std::int64_t{0});
    CHECK(!provider.observeGameplay(fixture.player, subject(secondId)).ok());
    CHECK(!fixture.module->publishGameplay(instanceId, ownerId, 7).ok());
    REQUIRE(fixture.module->unpublishGameplay(secondId).ok());
    CHECK(!fixture.module->unpublishGameplay(secondId).ok());
    CHECK_EQ(fixture.module->gameplayControlCount(), 1);
}

TEST_CASE("economy.gameplay.jsonFacadeRoutesObserveSubmitAndEvents") {
    PublishedEconomy fixture;
    auto domains = eve::executeGameplayControlJson(
        R"({"schemaId":"evengine.gameplay-control-request","schemaVersion":1,"op":"domains"})");
    REQUIRE(domains.ok());
    CHECK(domains.value().find("economy") != std::string::npos);
    auto observed = eve::executeGameplayControlJson(request("observe", "player"));
    REQUIRE(observed.ok());
    CHECK(observed.value().find("\"player\":7") != std::string::npos);
    auto denied =
        eve::executeGameplayControlJson(request("submit", "player", commandJson("denied", "economy:credit", 0, 5)));
    CHECK(!denied.ok());
    auto grant = eve::executeGameplayControlJson(
        request("submit", "test-driver", commandJson("grant", "economy:credit", 0, 20)));
    REQUIRE(grant.ok());
    CHECK(grant.value().find("\"resultingRevision\":1") != std::string::npos);
    auto spend =
        eve::executeGameplayControlJson(request("submit", "player", commandJson("spend", "economy:debit", 1, 5)));
    REQUIRE(spend.ok());
    CHECK_EQ(EconomySystem::get(7, "gold"), 15);
    auto events = eve::executeGameplayControlJson(request("events", "player", ",\"afterSequence\":0"));
    REQUIRE(events.ok());
    CHECK(events.value().find("economy.credited") != std::string::npos);
    CHECK(events.value().find("economy.debited") != std::string::npos);
}
