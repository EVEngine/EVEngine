#include <cstdint>
#include <initializer_list>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <utility>
#include "common/Capability.h"
#include "common/GameplayControl.h"
#include "common/GameplayControlJson.h"
#include "common/GameplayInstanceCatalog.h"
#include "common/Module.h"
#include "common/Value.h"
#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "inventory/Inventory.h"
#include "inventory/InventorySaveSession.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// inventory_gameplay_control.cpp
// 背包玩法域契约测试：真实 Inventory 模块 + Bag + EquipmentSet 走共享玩法协议，
// 覆盖发现、动作词表、权限档位、修订号账本、事件游标与 JSON 门面路由。
//
// 注意：zeroerr 的 CHECK_EQ(lhs, rhs) 会把 lhs 求值两次（一次判等、一次打印），
// 因此有副作用的调用必须先赋给局部变量，再断言变量。


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

/** 已发布实例的收据/事件里读取机械可读的生效数量。 */
[[maybe_unused]] std::int64_t appliedQuantity(const eve::Value& details) {
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

[[maybe_unused]] eve::GameplayCommand command(const InventoryFixture& fixture, const char* id, const char* actionText,
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

[[maybe_unused]] eve::GameplayCommand grant(const InventoryFixture& fixture, const char* id, std::uint64_t expectedRevision,
                           std::int64_t quantity) {
    return command(fixture, id, "inventory:add-item", expectedRevision,
                   object({{"itemId", eve::Value("mcp.potion")}, {"quantity", eve::Value(quantity)}}));
}

[[maybe_unused]] eve::GameplayCommand remove(const InventoryFixture& fixture, const char* id, std::uint64_t expectedRevision,
                            std::int64_t quantity) {
    return command(fixture, id, "inventory:remove-item", expectedRevision,
                   object({{"itemId", eve::Value("mcp.potion")}, {"quantity", eve::Value(quantity)}}));
}

[[maybe_unused]] std::string envelope(const char* operation, const char* access, const std::string& extra = {}) {
    return std::string("{\"schemaId\":\"evengine.gameplay-control-request\",\"schemaVersion\":1,\"op\":\"") +
           operation + "\",\"domain\":\"inventory\",\"instance\":\"" + kInstanceId +
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

// inventory_save_session.cpp
using namespace eve::inventory;
