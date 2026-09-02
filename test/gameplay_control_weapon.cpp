#include "common/Capability.h"
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
    REQUIRE_EQ(bag.addItem("ammo", 2), 2);
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

TEST_CASE("gameplay.control.weaponRejectsUnauthorizedWielderWithoutCharging") {
    eve::inventory::ItemRegistry::clear();
    eve::inventory::ItemDefinition ammo;
    ammo.id = "ammo";
    ammo.maxStack = 20;
    eve::inventory::ItemRegistry::registerItem(ammo);
    eve::inventory::Bag bag(1);
    REQUIRE_EQ(bag.addItem("ammo", 1), 1);
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
