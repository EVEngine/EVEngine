// 背包玩法域契约测试：真实 Inventory 模块 + Bag + EquipmentSet 走共享玩法协议，
// 覆盖发现、动作词表、权限档位、修订号账本、事件游标与 JSON 门面路由。
//
// 注意：zeroerr 的 CHECK_EQ(lhs, rhs) 会把 lhs 求值两次（一次判等、一次打印），
// 因此有副作用的调用必须先赋给局部变量，再断言变量。

#include "common/Capability.h"
#include "common/GameplayControl.h"
#include "common/GameplayControlJson.h"
#include "common/GameplayInstanceCatalog.h"
#include "common/Module.h"
#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "inventory/Inventory.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>

namespace {

constexpr const char* kInstanceId          = "00000000-0000-7000-8000-000000000701";
constexpr const char* kOwnerId             = "00000000-0000-7000-8000-000000000702";
constexpr const char* kStrangerId          = "00000000-0000-7000-8000-000000000703";
constexpr const char* kCompanionInstanceId = "00000000-0000-7000-8000-000000000704";
constexpr const char* kCompanionOwnerId    = "00000000-0000-7000-8000-000000000705";

constexpr const char* kItemDefinitions = R"([
    {"id":"mcp.sword","displayName":"MCP Sword","maxStack":1,"weight":3.0,
     "category":"weapon","tags":["weapon"],"equipSlot":"weapon"},
    {"id":"mcp.potion","displayName":"MCP Potion","maxStack":10,"weight":0.5,
     "category":"consumable","tags":["potion"]}
])";

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

/** 已发布实例的收据/事件里读取机械可读的生效数量。 */
std::int64_t appliedQuantity(const eve::Value& details) {
    const eve::Value* quantity = member(details, "quantity");
    return quantity == nullptr ? -1 : quantity->asInt();
}

/** 真实模块 + 容器 + 装备栏，并把该实例发布到共享玩法协议。 */
struct InventoryFixture {
    eve::inventory::Inventory*                 inventory = nullptr;
    eve::inventory::Bag*                       bag       = nullptr;
    eve::inventory::EquipmentSet*              equipment = nullptr;
    eve::SubjectRef                            instance;
    eve::SubjectRef                            owner;
    std::vector<eve::inventory::Bag*>          ownedBags;
    std::vector<eve::inventory::EquipmentSet*> ownedEquipment;

    InventoryFixture() {
        inventory = eve::inventory::Inventory::create();
        REQUIRE(inventory != nullptr);
        inventory->clearGameplayControls();
        inventory->clearItemDefinitions();
        const int registered = inventory->registerItemsFromJson(kItemDefinitions);
        REQUIRE_EQ(registered, 2);
        bag = inventory->newBag(6);
        REQUIRE(bag != nullptr);
        ownedBags.push_back(bag);
        bag->setId("mcp.player.bag");
        equipment = inventory->newEquipmentSet();
        REQUIRE(equipment != nullptr);
        ownedEquipment.push_back(equipment);
        equipment->defineSlot("weapon");
        equipment->addSlotAllowedTag("weapon", "weapon");
        instance             = subject(kInstanceId);
        owner                = subject(kOwnerId);
        const auto published = inventory->publishGameplay(kInstanceId, kOwnerId, bag, equipment);
        REQUIRE(published.ok());
    }

    ~InventoryFixture() {
        inventory->clearGameplayControls();
        for (auto* set : ownedEquipment)
            if (set != nullptr) set->destroy();
        for (auto* container : ownedBags)
            if (container != nullptr) container->destroy();
    }

    InventoryFixture(const InventoryFixture&)            = delete;
    InventoryFixture& operator=(const InventoryFixture&) = delete;

    /** 通过能力注册表取回适配器，和 JSON 路由走同一条查找路径。 */
    eve::IGameplayControlProvider* provider() const {
        eve::IGameplayControlProvider* found = nullptr;
        eve::cap::forEach<eve::IGameplayControlProvider>([&](auto* candidate) {
            if (candidate != nullptr && candidate->gameplayDomain() == "inventory") found = candidate;
        });
        return found;
    }

    /** 再发布一个没有装备栏的同伴背包（多实例共存）。 */
    eve::Result<void> publishCompanion() {
        auto* companionBag = inventory->newBag(4);
        REQUIRE(companionBag != nullptr);
        ownedBags.push_back(companionBag);
        companionBag->setId("mcp.companion.bag");
        return inventory->publishGameplay(kCompanionInstanceId, kCompanionOwnerId, companionBag, nullptr);
    }

    /** 直接写入容器（测试夹具自己的准备步骤，不走适配器）。 */
    void stockBag() {
        const int swords  = bag->addItem("mcp.sword", 1);
        const int potions = bag->addItem("mcp.potion", 3);
        REQUIRE_EQ(swords, 1);
        REQUIRE_EQ(potions, 3);
    }
};

eve::GameplayCommand command(const InventoryFixture& fixture, const char* id, const char* actionText,
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

eve::GameplayCommand grant(const InventoryFixture& fixture, const char* id, std::uint64_t expectedRevision,
                           std::int64_t quantity) {
    return command(fixture, id, "inventory:add-item", expectedRevision,
                   object({{"itemId", eve::Value("mcp.potion")}, {"quantity", eve::Value(quantity)}}));
}

eve::GameplayCommand remove(const InventoryFixture& fixture, const char* id, std::uint64_t expectedRevision,
                            std::int64_t quantity) {
    return command(fixture, id, "inventory:remove-item", expectedRevision,
                   object({{"itemId", eve::Value("mcp.potion")}, {"quantity", eve::Value(quantity)}}));
}

std::string envelope(const char* operation, const char* access, const std::string& extra = {}) {
    return std::string("{\"schemaId\":\"evengine.gameplay-control-request\",\"schemaVersion\":1,\"op\":\"") +
           operation + "\",\"domain\":\"inventory\",\"instance\":\"" + kInstanceId +
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

TEST_CASE("gameplay.control.inventoryPublishesOneVocabularyForPlayerAndAutomation") {
    InventoryFixture fixture;
    auto*            provider = fixture.provider();
    REQUIRE(provider != nullptr);
    CHECK_EQ(fixture.inventory->gameplayControlCount(), 1);
    fixture.stockBag();
    CHECK_EQ(fixture.bag->countItem("mcp.potion"), 3);
    CHECK_EQ(fixture.bag->getUsedSlotCount(), 2);

    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {fixture.owner}};
    eve::GameplaySession automation{"automation", eve::GameplayAccess::TestDriver, {fixture.owner}};

    auto observed = provider->observeGameplay(player, fixture.instance);
    REQUIRE(observed.ok());
    const auto observation = std::move(observed).takeValue();
    CHECK_EQ(observation.domain.format(), std::string("gameplay:inventory"));
    CHECK_EQ(observation.instance.format(), std::string(kInstanceId));
    CHECK_EQ(observation.tick.value(), std::uint64_t{0});
    CHECK_EQ(observation.revision, std::uint64_t{0});

    const eve::Value* bagId = member(observation.state, "bagId");
    REQUIRE(bagId != nullptr);
    CHECK_EQ(bagId->asString(), std::string("mcp.player.bag"));
    const eve::Value* slotCount = member(observation.state, "slotCount");
    REQUIRE(slotCount != nullptr);
    CHECK_EQ(slotCount->asInt(), std::int64_t{6});
    const eve::Value* slots = member(observation.state, "slots");
    REQUIRE(slots != nullptr);
    const auto* slotArray = slots->getIf<eve::Value::Array>();
    REQUIRE(slotArray != nullptr);
    CHECK_EQ(static_cast<int>(slotArray->size()), 2);
    const eve::Value* equipment = member(observation.state, "equipment");
    REQUIRE(equipment != nullptr);
    const auto* equipmentArray = equipment->getIf<eve::Value::Array>();
    REQUIRE(equipmentArray != nullptr);
    CHECK_EQ(static_cast<int>(equipmentArray->size()), 1);

    auto playerActions     = provider->availableGameplayActions(player, fixture.instance, fixture.owner);
    auto automationActions = provider->availableGameplayActions(automation, fixture.instance, fixture.owner);
    REQUIRE(playerActions.ok());
    REQUIRE(automationActions.ok());
    const auto playerList     = std::move(playerActions).takeValue();
    const auto automationList = std::move(automationActions).takeValue();
    CHECK_EQ(static_cast<int>(playerList.size()), 4);
    CHECK_EQ(static_cast<int>(automationList.size()), 5);

    bool playerMayGrant     = false;
    bool automationMayGrant = false;
    for (const auto& descriptor : playerList) {
        if (descriptor.id.format() == "inventory:add-item") playerMayGrant = true;
    }
    for (const auto& descriptor : automationList) {
        if (descriptor.id.format() == "inventory:add-item") automationMayGrant = true;
    }
    CHECK(!playerMayGrant);
    CHECK(automationMayGrant);

    auto wrongSubject = provider->availableGameplayActions(player, fixture.instance, subject(kStrangerId));
    CHECK(!wrongSubject.ok());
    CHECK_EQ(wrongSubject.code(), eve::StatusCode::Rejected);
}

TEST_CASE("gameplay.control.inventoryServesSeveralPublishedInstancesFromOneDomain") {
    InventoryFixture fixture;
    auto*            provider = fixture.provider();
    REQUIRE(provider != nullptr);

    const auto companion = fixture.publishCompanion();
    REQUIRE(companion.ok());
    CHECK_EQ(fixture.inventory->gameplayControlCount(), 2);
    const auto published = fixture.inventory->gameplayInstances();
    REQUIRE_EQ(static_cast<int>(published.size()), 2);
    CHECK_EQ(published[0], std::string(kInstanceId));
    CHECK_EQ(published[1], std::string(kCompanionInstanceId));

    // 共享路由器要求每个领域唯一：第二个实例不能再注册成第二个 provider。
    eve::IGameplayControlProvider* matches = nullptr;
    int                            count   = 0;
    eve::cap::forEach<eve::IGameplayControlProvider>([&](auto* candidate) {
        if (candidate != nullptr && candidate->gameplayDomain() == "inventory") {
            matches = candidate;
            ++count;
        }
    });
    CHECK_EQ(count, 1);
    CHECK(matches == provider);

    const auto           companionInstance = subject(kCompanionInstanceId);
    const auto           companionOwner    = subject(kCompanionOwnerId);
    eve::GameplaySession companionSession{"companion-player", eve::GameplayAccess::PlayerEquivalent, {companionOwner}};
    auto                 companionObserved = provider->observeGameplay(companionSession, companionInstance);
    REQUIRE(companionObserved.ok());
    CHECK_EQ(companionObserved.value().state.getIf<eve::Value::Object>()->at("bagId").asString(),
             std::string("mcp.companion.bag"));

    // 没有装备栏，也（对玩家档位）不能凭空发放物品：动作词表只有 2 条。
    auto companionActions = provider->availableGameplayActions(companionSession, companionInstance, companionOwner);
    REQUIRE(companionActions.ok());
    CHECK_EQ(static_cast<int>(companionActions.value().size()), 2);

    // 另一个玩家档位既不能观察、也不能操作同伴的背包。
    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {fixture.owner}};
    auto                 denied = provider->observeGameplay(player, companionInstance);
    CHECK(!denied.ok());
    CHECK_EQ(denied.code(), eve::StatusCode::Rejected);

    // 同一实例重复发布被拒绝，且不影响既有发布。
    auto duplicate = fixture.inventory->publishGameplay(kInstanceId, kOwnerId, fixture.bag, fixture.equipment);
    CHECK(!duplicate.ok());
    CHECK_EQ(duplicate.code(), eve::StatusCode::Conflict);
    CHECK_EQ(fixture.inventory->gameplayControlCount(), 2);

    auto legacy = provider->observeGameplay(player, fixture.instance);
    REQUIRE(legacy.ok());

    // 取消发布只影响目标实例。
    const auto unpublished = fixture.inventory->unpublishGameplay(kCompanionInstanceId);
    CHECK(unpublished.ok());
    const auto again = fixture.inventory->unpublishGameplay(kCompanionInstanceId);
    CHECK(!again.ok());
    CHECK_EQ(again.code(), eve::StatusCode::NotFound);
    CHECK_EQ(fixture.inventory->gameplayControlCount(), 1);
    auto gone = provider->observeGameplay(companionSession, companionInstance);
    CHECK(!gone.ok());
    CHECK_EQ(gone.code(), eve::StatusCode::NotFound);
    auto stillThere = provider->observeGameplay(player, fixture.instance);
    REQUIRE(stillThere.ok());

    // MCP 发现路径：`op:instances` 报告可枚举的领域与实例，并单独列出不可枚举的领域。
    auto catalogs = eve::executeGameplayControlJson(
        R"({"schemaId":"evengine.gameplay-control-request","schemaVersion":1,"op":"instances"})");
    REQUIRE(catalogs.ok());
    CHECK(catalogs.value().find("\"domain\":\"inventory\"") != std::string::npos);
    CHECK(catalogs.value().find(kInstanceId) != std::string::npos);
    CHECK(catalogs.value().find(kCompanionInstanceId) == std::string::npos);

    auto scoped = eve::executeGameplayControlJson(
        R"({"schemaId":"evengine.gameplay-control-request","schemaVersion":1,"op":"instances","domain":"inventory"})");
    REQUIRE(scoped.ok());
    CHECK(scoped.value().find(kInstanceId) != std::string::npos);

    auto unknownDomain = eve::executeGameplayControlJson(
        R"({"schemaId":"evengine.gameplay-control-request","schemaVersion":1,"op":"instances","domain":"rpg.battle"})");
    CHECK(!unknownDomain.ok());
    CHECK_EQ(unknownDomain.code(), eve::StatusCode::Unsupported);
}

TEST_CASE("gameplay.control.inventoryGrantsOnlyThroughNonPlayerProfiles") {
    InventoryFixture fixture;
    auto*            provider = fixture.provider();
    REQUIRE(provider != nullptr);
    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {fixture.owner}};
    eve::GameplaySession automation{"automation", eve::GameplayAccess::TestDriver, {fixture.owner}};

    auto refused = provider->submitGameplay(player, fixture.instance, grant(fixture, "grant-refused", 0, 2));
    CHECK(!refused.ok());
    CHECK_EQ(refused.code(), eve::StatusCode::Rejected);
    CHECK_EQ(fixture.bag->countItem("mcp.potion"), 0);

    auto granted = provider->submitGameplay(automation, fixture.instance, grant(fixture, "grant-accepted", 0, 2));
    REQUIRE(granted.ok());
    CHECK_EQ(granted.value().resultingRevision, std::uint64_t{1});
    CHECK_EQ(appliedQuantity(granted.value().details), std::int64_t{2});
    CHECK_EQ(fixture.bag->countItem("mcp.potion"), 2);

    auto events = provider->gameplayEvents(automation, fixture.instance, 0);
    REQUIRE(events.ok());
    const auto list = std::move(events).takeValue();
    REQUIRE_EQ(static_cast<int>(list.size()), 1);
    CHECK_EQ(list[0].type, std::string("inventory.item-added"));
    CHECK_EQ(list[0].causationCommandId, std::string("grant-accepted"));
    CHECK_EQ(list[0].subject.format(), std::string(kOwnerId));
    CHECK_EQ(appliedQuantity(list[0].payload), std::int64_t{2});

    auto stale = provider->submitGameplay(automation, fixture.instance, grant(fixture, "grant-stale", 0, 1));
    CHECK(!stale.ok());
    CHECK_EQ(stale.code(), eve::StatusCode::Conflict);
    CHECK_EQ(fixture.bag->countItem("mcp.potion"), 2);

    eve::GameplaySession intruder{"intruder", eve::GameplayAccess::PlayerEquivalent, {subject(kStrangerId)}};
    auto                 denied = provider->observeGameplay(intruder, fixture.instance);
    CHECK(!denied.ok());
    CHECK_EQ(denied.code(), eve::StatusCode::Rejected);

    auto missing = provider->observeGameplay(automation, subject(kStrangerId));
    CHECK(!missing.ok());
    CHECK_EQ(missing.code(), eve::StatusCode::NotFound);
}

TEST_CASE("gameplay.control.inventoryEquipUnequipAndRemoveShareOneRevisionLedger") {
    InventoryFixture fixture;
    auto*            provider = fixture.provider();
    REQUIRE(provider != nullptr);
    fixture.stockBag();
    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {fixture.owner}};

    auto equipped = provider->submitGameplay(
        player, fixture.instance,
        command(fixture, "equip-1", "inventory:equip", 0,
                object({{"bagSlot", eve::Value(std::int64_t{0})}, {"equipSlot", eve::Value("weapon")}})));
    REQUIRE(equipped.ok());
    CHECK_EQ(equipped.value().resultingRevision, std::uint64_t{1});
    CHECK_EQ(appliedQuantity(equipped.value().details), std::int64_t{1});
    CHECK_EQ(fixture.equipment->getSlotItemId("weapon"), std::string("mcp.sword"));
    CHECK(fixture.bag->isSlotEmpty(0));
    CHECK_EQ(fixture.bag->countItem("mcp.potion"), 3);

    auto unequipped = provider->submitGameplay(
        player, fixture.instance,
        command(fixture, "unequip-1", "inventory:unequip", 1, object({{"equipSlot", eve::Value("weapon")}})));
    REQUIRE(unequipped.ok());
    CHECK_EQ(unequipped.value().resultingRevision, std::uint64_t{2});
    CHECK_EQ(appliedQuantity(unequipped.value().details), std::int64_t{1});
    CHECK(fixture.equipment->isSlotEmpty("weapon"));
    CHECK_EQ(fixture.bag->getSlotItemId(0), std::string("mcp.sword"));

    auto moved = provider->submitGameplay(player, fixture.instance,
                                          command(fixture, "move-1", "inventory:move-slot", 2,
                                                  object({{"fromSlot", eve::Value(std::int64_t{1})},
                                                          {"toSlot", eve::Value(std::int64_t{2})},
                                                          {"quantity", eve::Value(std::int64_t{1})}})));
    REQUIRE(moved.ok());
    CHECK_EQ(appliedQuantity(moved.value().details), std::int64_t{1});
    CHECK_EQ(fixture.bag->getSlotQuantity(2), 1);

    auto removed = provider->submitGameplay(player, fixture.instance, remove(fixture, "remove-1", 3, 2));
    REQUIRE(removed.ok());
    CHECK_EQ(appliedQuantity(removed.value().details), std::int64_t{2});
    CHECK_EQ(fixture.bag->countItem("mcp.potion"), 1);

    // `Bag` 的语义是"最多取 N"：请求超过持有量时按实际生效量应用，
    // 生效量在收据与事件里披露，而不是谎报全额成功。
    auto partial = provider->submitGameplay(player, fixture.instance, remove(fixture, "remove-2", 4, 5));
    REQUIRE(partial.ok());
    CHECK_EQ(appliedQuantity(partial.value().details), std::int64_t{1});
    CHECK_EQ(fixture.bag->countItem("mcp.potion"), 0);

    auto absent = provider->submitGameplay(player, fixture.instance, remove(fixture, "remove-3", 5, 1));
    CHECK(!absent.ok());
    CHECK_EQ(absent.code(), eve::StatusCode::Rejected);

    auto unknown = provider->submitGameplay(player, fixture.instance,
                                            command(fixture, "mystery-1", "inventory:teleport", 5, object({})));
    CHECK(!unknown.ok());
    CHECK_EQ(unknown.code(), eve::StatusCode::Unsupported);

    auto advanced = provider->advanceGameplay(player, fixture.instance,
                                              {eve::SimulationTick(1), eve::Duration::fromNanoseconds(16666667)});
    REQUIRE(advanced.ok());
    CHECK_EQ(advanced.value().tick.value(), std::uint64_t{1});

    auto rewound = provider->advanceGameplay(player, fixture.instance,
                                             {eve::SimulationTick(1), eve::Duration::fromNanoseconds(1)});
    CHECK(!rewound.ok());
    CHECK_EQ(rewound.code(), eve::StatusCode::Conflict);

    auto events = provider->gameplayEvents(player, fixture.instance, 0);
    REQUIRE(events.ok());
    const auto list = std::move(events).takeValue();
    REQUIRE_EQ(static_cast<int>(list.size()), 5);
    CHECK_EQ(list[0].type, std::string("inventory.equipped"));
    CHECK_EQ(list[1].type, std::string("inventory.unequipped"));
    CHECK_EQ(list[2].type, std::string("inventory.slot-moved"));
    CHECK_EQ(appliedQuantity(list[2].payload), std::int64_t{1});
    CHECK_EQ(list[3].type, std::string("inventory.item-removed"));
    CHECK_EQ(appliedQuantity(list[3].payload), std::int64_t{2});
    CHECK_EQ(list[4].type, std::string("inventory.item-removed"));
    CHECK_EQ(appliedQuantity(list[4].payload), std::int64_t{1});
    CHECK_EQ(list[4].tick.value(), std::uint64_t{0});

    auto drained = provider->gameplayEvents(player, fixture.instance, 5);
    REQUIRE(drained.ok());
    CHECK_EQ(drained.code(), eve::StatusCode::NoOp);
}

TEST_CASE("gameplay.control.inventoryRoutesThroughTheSharedJsonFacade") {
    InventoryFixture fixture;
    fixture.stockBag();

    auto domains = eve::executeGameplayControlJson(
        R"({"schemaId":"evengine.gameplay-control-request","schemaVersion":1,"op":"domains"})");
    REQUIRE(domains.ok());
    CHECK(domains.value().find("inventory") != std::string::npos);

    auto observed = eve::executeGameplayControlJson(envelope("observe", "player"));
    REQUIRE(observed.ok());
    CHECK(observed.value().find("gameplay:inventory") != std::string::npos);
    CHECK(observed.value().find("mcp.player.bag") != std::string::npos);
    CHECK(observed.value().find("mcp.sword") != std::string::npos);

    const std::string actor             = std::string(",\"subject\":\"") + kOwnerId + "\"";
    auto              playerActions     = eve::executeGameplayControlJson(envelope("actions", "player", actor));
    auto              automationActions = eve::executeGameplayControlJson(envelope("actions", "test-driver", actor));
    REQUIRE(playerActions.ok());
    REQUIRE(automationActions.ok());
    CHECK(playerActions.value().find("inventory:equip") != std::string::npos);
    CHECK(playerActions.value().find("inventory:add-item") == std::string::npos);
    CHECK(automationActions.value().find("inventory:add-item") != std::string::npos);

    auto submitted = eve::executeGameplayControlJson(
        envelope("submit", "player",
                 jsonCommand("json-equip-1", "inventory:equip", 0, R"({"bagSlot":0,"equipSlot":"weapon"})")));
    REQUIRE(submitted.ok());
    CHECK(submitted.value().find("json-equip-1") != std::string::npos);
    CHECK(submitted.value().find("\"resultingRevision\":1") != std::string::npos);
    CHECK(submitted.value().find("\"quantity\":1") != std::string::npos);
    CHECK_EQ(fixture.equipment->getSlotItemId("weapon"), std::string("mcp.sword"));

    auto denied = eve::executeGameplayControlJson(
        envelope("submit", "player",
                 jsonCommand("json-grant-1", "inventory:add-item", 1, R"({"itemId":"mcp.potion","quantity":1})")));
    CHECK(!denied.ok());
    CHECK_EQ(denied.code(), eve::StatusCode::Rejected);
    CHECK_EQ(fixture.bag->countItem("mcp.potion"), 3);

    auto granted = eve::executeGameplayControlJson(
        envelope("submit", "test-driver",
                 jsonCommand("json-grant-2", "inventory:add-item", 1, R"({"itemId":"mcp.potion","quantity":1})")));
    REQUIRE(granted.ok());
    CHECK(granted.value().find("\"resultingRevision\":2") != std::string::npos);
    CHECK_EQ(fixture.bag->countItem("mcp.potion"), 4);

    auto events = eve::executeGameplayControlJson(envelope("events", "player", ",\"afterSequence\":0"));
    REQUIRE(events.ok());
    CHECK(events.value().find("inventory.equipped") != std::string::npos);
    CHECK(events.value().find("json-grant-2") != std::string::npos);

    auto advanced =
        eve::executeGameplayControlJson(envelope("advance", "player", ",\"tick\":1,\"deltaNanoseconds\":16666667"));
    REQUIRE(advanced.ok());
    CHECK(advanced.value().find("\"tick\":1") != std::string::npos);
}

TEST_CASE("gameplay.control.inventoryScriptPublishesAndUnpublishesItsInstance") {
    constexpr const char* kScriptInstanceId = "00000000-0000-7000-8000-000000000b01";
    constexpr const char* kScriptOwnerId    = "00000000-0000-7000-8000-000000000b02";

    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"SQ(
        local inv = eve.Inventory();
        inv.clearGameplayControls();
        scriptBag <- inv.newBag(4);
        scriptBag.setId("script.bag");
        local published = inv.publishGameplay(
            "00000000-0000-7000-8000-000000000b01",
            "00000000-0000-7000-8000-000000000b02",
            scriptBag, null);
        scriptPublished <- published.ok;
        scriptPublishedMessage <- published.message;
        scriptInstances <- inv.getGameplayControlCount();
        local duplicate = inv.publishGameplay(
            "00000000-0000-7000-8000-000000000b01",
            "00000000-0000-7000-8000-000000000b02",
            scriptBag, null);
        scriptDuplicateOk <- duplicate.ok;
        scriptDuplicateMessage <- duplicate.message;
        local malformed = inv.publishGameplay("not-a-uuid",
            "00000000-0000-7000-8000-000000000b02", scriptBag, null);
        scriptMalformedOk <- malformed.ok;
        scriptMalformedMessage <- malformed.message;
        scriptUnpublished <- inv.unpublishGameplay("00000000-0000-7000-8000-000000000b01").ok;
        scriptRemaining <- inv.getGameplayControlCount();
        scriptBag.destroy();
    )SQ"));

    CHECK(vm.find("scriptPublished").toBool());
    CHECK_EQ(vm.find("scriptPublishedMessage").toString(), std::string("published"));
    CHECK_EQ(static_cast<int>(vm.find("scriptInstances").toInt()), 1);
    CHECK(!vm.find("scriptDuplicateOk").toBool());
    CHECK(vm.find("scriptDuplicateMessage").toString().find("already owns") != std::string::npos);
    CHECK(!vm.find("scriptMalformedOk").toBool());
    CHECK_EQ(vm.find("scriptMalformedMessage").toString(),
             std::string("rejected: instance and owner ids must be canonical persistent ids [instanceId]"));
    CHECK(vm.find("scriptUnpublished").toBool());
    CHECK_EQ(static_cast<int>(vm.find("scriptRemaining").toInt()), 0);

    // 脚本发布走的是同一条权威路径：领域与实例都能被 MCP 侧枚举到。
    auto* inventory = eve::inventory::Inventory::create();
    REQUIRE(inventory != nullptr);
    auto* bag = inventory->newBag(2);
    REQUIRE(bag != nullptr);
    const auto republished = inventory->publishGameplay(kScriptInstanceId, kScriptOwnerId, bag, nullptr);
    REQUIRE(republished.ok());
    const auto catalog = eve::executeGameplayControlJson(
        R"({"schemaId":"evengine.gameplay-control-request","schemaVersion":1,"op":"instances","domain":"inventory"})");
    REQUIRE(catalog.ok());
    CHECK(catalog.value().find(kScriptInstanceId) != std::string::npos);
    inventory->clearGameplayControls();
    bag->destroy();
}
