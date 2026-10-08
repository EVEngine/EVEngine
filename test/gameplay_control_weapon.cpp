#include "common/Capability.h"
#include "common/GameplayControlJson.h"
#include "common/GameplayInstanceCatalog.h"
#include "inventory/Bag.h"
#include "inventory/InventoryResourceAccount.h"
#include "inventory/Item.h"
#include "transaction/Transaction.h"
#include "weapon/WeaponControl.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {

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

class Effect final : public eve::transaction::ITransactionParticipant {
public:
    [[nodiscard]] eve::Result<void> prepare(const eve::transaction::TransactionContext&) override {
        prepared = true;
        return eve::Result<void>::success();
    }
    [[nodiscard]] eve::Result<void> commit(const eve::transaction::TransactionContext&) override {
        committed = true;
        return eve::Result<void>::success();
    }
    [[nodiscard]] eve::Result<void> rollback(const eve::transaction::TransactionContext&) override {
        prepared = false;
        return eve::Result<void>::success();
    }
    [[nodiscard]] eve::Result<void> compensate(const eve::transaction::TransactionContext&) override {
        committed = false;
        prepared = false;
        return eve::Result<void>::success();
    }
    bool prepared = false;
    bool committed = false;
};

eve::GameplayCommand fireCommand(const char* commandId, eve::SubjectRef wielder,
                                 const eve::GameplayObservation& observed) {
    eve::GameplayCommand command;
    command.id = commandId;
    command.action = action("weapon:fire");
    command.subject = wielder;
    command.observedTick = observed.tick;
    command.expectedRevision = observed.revision;
    command.parameters = eve::Value(eve::Value::Object{{"shooterId", eve::Value(7)},
                                                        {"targetX", eve::Value(4.0)},
                                                        {"targetY", eve::Value(5.0)},
                                                        {"targetZ", eve::Value(6.0)}});
    return command;
}

}  // namespace

TEST_CASE("gameplay.control.weaponUsesCanonicalActionAndAtomicAmmoPayment") {
    eve::inventory::ItemRegistry::clear();
    eve::inventory::ItemDefinition ammo;
    ammo.id = "ammo";
    ammo.maxStack = 20;
    eve::inventory::ItemRegistry::registerItem(ammo);
    eve::inventory::Bag bag(1);
    // 先存到局部变量再断言：REQUIRE_EQ/CHECK_EQ 会求值两次，直接写 `REQUIRE_EQ(bag.addItem(...), n)`
    // 会真的加两次弹药，后面的扣减断言便永远不成立。
    const int stocked = bag.addItem("ammo", 2);
    REQUIRE_EQ(stocked, 2);
    eve::inventory::InventoryResourceAccount account(bag);
    Effect effect;
    eve::weapon::WeaponDefinition definition;
    definition.id = "player-rifle";
    definition.resource.kind = eve::weapon::ResourceKind::Ammo;
    definition.resource.cost = 1.0f;
    const auto instance = subject("00000000-0000-7000-8000-000000000801");
    const auto wielder = subject("00000000-0000-7000-8000-000000000802");
    eve::weapon::WeaponControl control(instance, wielder, definition, account, effect);
    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {wielder}};
    eve::GameplaySession automation{"automation", eve::GameplayAccess::TestDriver, {wielder}};

    bool discovered = false;
    eve::cap::forEach<eve::IGameplayControlProvider>([&](auto* provider) {
        if (provider == &control && provider->gameplayDomain() == "weapon") discovered = true;
    });
    CHECK(discovered);
    auto playerActions = control.availableGameplayActions(player, instance, wielder);
    auto automationActions = control.availableGameplayActions(automation, instance, wielder);
    REQUIRE(playerActions.ok());
    REQUIRE(automationActions.ok());
    CHECK_EQ(playerActions.value().front().id, automationActions.value().front().id);

    auto observed = control.observeGameplay(player, instance);
    REQUIRE(observed.ok());
    const auto before = std::move(observed).takeValue();
    auto fired = control.submitGameplay(player, instance, fireCommand("weapon-fire-1", wielder, before));
    REQUIRE(fired.ok());
    CHECK_EQ(bag.countItem("ammo"), 1);
    CHECK(effect.committed);

    // 目录能力：该适配器服务的就是这一个实例，`instances` 因此能列出它。
    const auto catalogInstances = control.gameplayInstances();
    REQUIRE_EQ(catalogInstances.size(), std::size_t{1});
    CHECK_EQ(catalogInstances[0].format(), instance.format());
    eve::IGameplayInstanceCatalog* catalog = nullptr;
    eve::cap::forEach<eve::IGameplayInstanceCatalog>([&](auto* candidate) {
        if (candidate != nullptr && candidate->gameplayDomain() == "weapon") catalog = candidate;
    });
    REQUIRE(catalog != nullptr);
    CHECK_EQ(catalog->gameplayInstances().size(), std::size_t{1});

    auto scoped = eve::executeGameplayControlJson(
        R"({"schemaId":"evengine.gameplay-control-request","schemaVersion":1,"op":"instances","domain":"weapon"})");
    REQUIRE(scoped.ok());
    CHECK(scoped.value().find(instance.format()) != std::string::npos);

    auto stale = control.submitGameplay(player, instance, fireCommand("weapon-fire-stale", wielder, before));
    CHECK(!stale.ok());
    CHECK_EQ(stale.code(), eve::StatusCode::Conflict);
    CHECK_EQ(bag.countItem("ammo"), 1);
    auto events = control.gameplayEvents(player, instance, 0);
    REQUIRE(events.ok());
    REQUIRE_EQ(events.value().size(), std::size_t{1});
    CHECK_EQ(events.value().front().causationCommandId, std::string("weapon-fire-1"));
    eve::inventory::ItemRegistry::clear();
}

TEST_CASE("gameplay.control.routerDispatchesAmongTwoWeaponInstances") {
    eve::inventory::ItemRegistry::clear();
    eve::inventory::ItemDefinition ammo;
    ammo.id       = "ammo";
    ammo.maxStack = 20;
    eve::inventory::ItemRegistry::registerItem(ammo);
    eve::inventory::Bag firstBag(1);
    eve::inventory::Bag secondBag(1);
    const int           firstStock  = firstBag.addItem("ammo", 2);
    const int           secondStock = secondBag.addItem("ammo", 2);
    REQUIRE_EQ(firstStock, 2);
    REQUIRE_EQ(secondStock, 2);
    eve::inventory::InventoryResourceAccount firstAccount(firstBag);
    eve::inventory::InventoryResourceAccount secondAccount(secondBag);
    Effect                                   firstEffect;
    Effect                                   secondEffect;
    eve::weapon::WeaponDefinition            firstDefinition;
    firstDefinition.id                             = "rifle-alpha";
    firstDefinition.resource.kind                  = eve::weapon::ResourceKind::Ammo;
    firstDefinition.resource.cost                  = 1.0f;
    eve::weapon::WeaponDefinition secondDefinition = firstDefinition;
    secondDefinition.id                            = "rifle-beta";
    const auto                 firstInstance       = subject("00000000-0000-7000-8000-000000000831");
    const auto                 firstWielder        = subject("00000000-0000-7000-8000-000000000832");
    const auto                 secondInstance      = subject("00000000-0000-7000-8000-000000000833");
    const auto                 secondWielder       = subject("00000000-0000-7000-8000-000000000834");
    eve::weapon::WeaponControl first(firstInstance, firstWielder, firstDefinition, firstAccount, firstEffect);
    eve::weapon::WeaponControl second(secondInstance, secondWielder, secondDefinition, secondAccount, secondEffect);

    // 两把武器 = 两个同域 provider：路由器按实例目录分派，两边都能观察。
    auto observeFirst = eve::executeGameplayControlJson(
        "{\"schemaId\":\"evengine.gameplay-control-request\",\"schemaVersion\":1,\"op\":\"observe\","
        "\"domain\":\"weapon\",\"instance\":\"" +
        firstInstance.format() + "\",\"session\":{\"id\":\"probe\",\"access\":\"player\",\"controlledSubjects\":[\"" +
        firstWielder.format() + "\"]}}");
    REQUIRE(observeFirst.ok());
    CHECK(observeFirst.value().find("rifle-alpha") != std::string::npos);

    auto observeSecond = eve::executeGameplayControlJson(
        "{\"schemaId\":\"evengine.gameplay-control-request\",\"schemaVersion\":1,\"op\":\"observe\","
        "\"domain\":\"weapon\",\"instance\":\"" +
        secondInstance.format() + "\",\"session\":{\"id\":\"probe\",\"access\":\"player\",\"controlledSubjects\":[\"" +
        secondWielder.format() + "\"]}}");
    REQUIRE(observeSecond.ok());
    CHECK(observeSecond.value().find("rifle-beta") != std::string::npos);

    // 开火只落到被路由到的第二把武器：只有它的弹药被扣。
    auto fireSecond = eve::executeGameplayControlJson(
        "{\"schemaId\":\"evengine.gameplay-control-request\",\"schemaVersion\":1,\"op\":\"submit\","
        "\"domain\":\"weapon\",\"instance\":\"" +
        secondInstance.format() + "\",\"session\":{\"id\":\"probe\",\"access\":\"player\",\"controlledSubjects\":[\"" +
        secondWielder.format() + "\"]},\"command\":{\"id\":\"routed-fire\",\"action\":\"weapon:fire\",\"subject\":\"" +
        secondWielder.format() +
        "\",\"observedTick\":0,\"expectedRevision\":1,\"parameters\":{\"shooterId\":7,\"targetX\":1.0,"
        "\"targetY\":0.0,\"targetZ\":0.0}}}");
    REQUIRE(fireSecond.ok());
    CHECK_EQ(secondBag.countItem("ammo"), 1);
    CHECK_EQ(firstBag.countItem("ammo"), 2);
    CHECK(secondEffect.committed);
    CHECK(!firstEffect.committed);
    eve::inventory::ItemRegistry::clear();
}

TEST_CASE("gameplay.control.weaponRejectsUnauthorizedWielderWithoutCharging") {
    eve::inventory::ItemRegistry::clear();
    eve::inventory::ItemDefinition ammo;
    ammo.id = "ammo";
    ammo.maxStack = 20;
    eve::inventory::ItemRegistry::registerItem(ammo);
    eve::inventory::Bag bag(1);
    const int           stockedGuard = bag.addItem("ammo", 1);
    REQUIRE_EQ(stockedGuard, 1);
    eve::inventory::InventoryResourceAccount account(bag);
    Effect effect;
    eve::weapon::WeaponDefinition definition;
    definition.id = "guard-rifle";
    definition.resource.kind = eve::weapon::ResourceKind::Ammo;
    definition.resource.cost = 1.0f;
    const auto instance = subject("00000000-0000-7000-8000-000000000811");
    const auto wielder = subject("00000000-0000-7000-8000-000000000812");
    const auto stranger = subject("00000000-0000-7000-8000-000000000813");
    eve::weapon::WeaponControl control(instance, wielder, definition, account, effect);
    eve::GameplaySession unauthorized{"other", eve::GameplayAccess::PlayerEquivalent, {stranger}};
    auto observed = control.observeGameplay(unauthorized, instance);
    CHECK(!observed.ok());
    CHECK_EQ(bag.countItem("ammo"), 1);
    CHECK(!effect.committed);
    eve::inventory::ItemRegistry::clear();
}
