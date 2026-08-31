#include "attributes/AttributeResourceAccount.h"
#include "attributes/AttributeSetResourceAccount.h"
#include "card/Card.h"
#include "card/CardContainers.h"
#include "card/CardPlay.h"
#include "economy/EconomyLedger.h"
#include "inventory/InventoryResourceAccount.h"
#include "inventory/Item.h"
#include "orders/CommandQueue.h"
#include "production/Production.h"
#include "rts/RTSEconomy.h"
#include "rts/RTSProductionAction.h"
#include "transaction/AtomicResourcePayment.h"
#include "weapon/WeaponAction.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace {

eve::resource::CostSpec makeCost(std::string_view resource, std::int64_t amount) {
    auto result = eve::resource::CostSpec::single(resource, amount);
    REQUIRE(result.ok());
    return std::move(result).takeValue();
}

eve::Status injectedFailure(std::string_view path) {
    return eve::Status::failure(eve::StatusCode::Rejected,
                                eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                                       "failure injected by resource adapter test", std::string(path)));
}

class TestEffect final : public eve::transaction::ITransactionParticipant {
public:
    explicit TestEffect(int& visibleValue) : visibleValue_(visibleValue) {}

    std::string_view name() const noexcept override { return "test-effect"; }

    [[nodiscard]] eve::Result<void> prepare(const eve::transaction::TransactionContext&) override {
        if (failPrepare) return eve::Result<void>::failure(injectedFailure("effect.prepare"));
        if (prepared || committed) return eve::Result<void>::failure(injectedFailure("effect.reentry"));
        prepared = true;
        return eve::Result<void>::success();
    }

    [[nodiscard]] eve::Result<void> commit(const eve::transaction::TransactionContext&) override {
        if (!prepared || committed) return eve::Result<void>::failure(injectedFailure("effect.commit.state"));
        if (failCommit) return eve::Result<void>::failure(injectedFailure("effect.commit"));
        ++visibleValue_;
        committed = true;
        return eve::Result<void>::success();
    }

    [[nodiscard]] eve::Result<void> rollback(const eve::transaction::TransactionContext&) override {
        if (committed) return eve::Result<void>::failure(injectedFailure("effect.rollback.committed"));
        prepared = false;
        return eve::Result<void>::success();
    }

    [[nodiscard]] eve::Result<void> compensate(const eve::transaction::TransactionContext&) override {
        if (!committed) return eve::Result<void>::success();
        --visibleValue_;
        committed = false;
        prepared  = false;
        return eve::Result<void>::success();
    }

    bool failPrepare = false;
    bool failCommit  = false;
    bool prepared    = false;
    bool committed   = false;

private:
    int& visibleValue_;
};

}  // namespace

TEST_CASE("resource.gameplay.attributeViewsReuseOneCanonicalAccount") {
    eve::attributes::AttributeSet attributes;
    attributes.setBase("mana", 10.0);
    attributes.setBase("stamina", 7.0);
    eve::attributes::AttributeSetResourceAccount base(attributes);
    eve::attributes::ManaAccountAdapter          mana(base, eve::attributes::AttributeResourceKind::Mana);
    eve::attributes::StaminaAccountAdapter       stamina(base, eve::attributes::AttributeResourceKind::Stamina);

    const auto manaCost        = makeCost("mana", 3);
    const auto staminaCost     = makeCost("stamina", 2);
    auto       manaReservation = mana.reserve(manaCost);
    REQUIRE(manaReservation.ok());
    auto committed = mana.commit(manaReservation.value());
    REQUIRE(committed.ok());
    CHECK_EQ(attributes.getBase("mana"), 7.0);
    CHECK_EQ(attributes.getBase("stamina"), 7.0);

    auto wrongView = mana.reserve(staminaCost);
    CHECK(!wrongView.ok());
    auto staminaReservation = stamina.reserve(staminaCost);
    REQUIRE(staminaReservation.ok());
    auto staminaRollback = stamina.rollback(staminaReservation.value());
    REQUIRE(staminaRollback.ok());
}

TEST_CASE("resource.gameplay.cardManaPaymentIsAtomicAndUsesPlayerAccount") {
    ecs::Table           world;
    ecs::ScopedTable     guard(world);
    eve::card::CardData* card = eve::card::CardData::createCard();
    REQUIRE(card != nullptr);
    card->state()->phase = eve::card::CardState::Hand;

    eve::card::CardDefinition definition;
    definition.id   = "firebolt";
    definition.cost = 3;
    eve::attributes::AttributeSet playerAttributes;
    playerAttributes.setBase("mana", 10.0);
    eve::attributes::AttributeSetResourceAccount playerAccount(playerAttributes);

    auto paid = eve::card::CardPlayPaymentAdapter::play(*card, definition, playerAccount, "card.firebolt.1");
    REQUIRE(paid.ok());
    CHECK_EQ(static_cast<int>(card->state()->phase), static_cast<int>(eve::card::CardState::Played));
    CHECK_EQ(playerAttributes.getBase("mana"), 7.0);

    card->release();
}

TEST_CASE("resource.gameplay.cardInsufficientManaLeavesEffectUnchanged") {
    ecs::Table           world;
    ecs::ScopedTable     guard(world);
    eve::card::CardData* card = eve::card::CardData::createCard();
    REQUIRE(card != nullptr);
    card->state()->phase = eve::card::CardState::Hand;

    eve::card::CardDefinition definition;
    definition.id   = "expensive";
    definition.cost = 5;
    eve::attributes::AttributeSet playerAttributes;
    playerAttributes.setBase("mana", 2.0);
    eve::attributes::AttributeSetResourceAccount playerAccount(playerAttributes);

    auto rejected = eve::card::CardPlayPaymentAdapter::play(*card, definition, playerAccount, "card.expensive.1");
    CHECK(!rejected.ok());
    CHECK_EQ(static_cast<int>(card->state()->phase), static_cast<int>(eve::card::CardState::Hand));
    CHECK_EQ(playerAttributes.getBase("mana"), 2.0);

    card->release();
}

TEST_CASE("resource.gameplay.weaponAmmoUsesInventoryAndPrepareFailureDoesNotCharge") {
    eve::inventory::ItemRegistry::clear();
    eve::inventory::ItemDefinition ammo;
    ammo.id       = "ammo";
    ammo.maxStack = 99;
    eve::inventory::ItemRegistry::registerItem(ammo);
    eve::inventory::Bag bag(1);
    bag.setId("player.inventory");
    const int ammoAdded = bag.addItem("ammo", 5);
    CHECK_EQ(ammoAdded, 5);
    eve::inventory::InventoryResourceAccount account(bag);

    eve::weapon::WeaponDefinition weapon;
    weapon.id              = "rifle";
    weapon.resource.kind   = eve::weapon::ResourceKind::Ammo;
    weapon.resource.cost   = 1.0f;
    int        effectValue = 0;
    TestEffect effect(effectValue);
    auto       fired = eve::weapon::WeaponActionAdapter::fire(weapon, account, effect, "weapon.rifle.1");
    REQUIRE(fired.ok());
    CHECK_EQ(bag.countItem("ammo"), 4);
    CHECK_EQ(effectValue, 1);

    effect.failPrepare = true;
    auto rejected      = eve::weapon::WeaponActionAdapter::fire(weapon, account, effect, "weapon.rifle.2");
    CHECK(!rejected.ok());
    CHECK_EQ(bag.countItem("ammo"), 4);
    CHECK_EQ(effectValue, 1);

    effect.failPrepare  = false;
    effect.failCommit   = true;
    auto commitRejected = eve::weapon::WeaponActionAdapter::fire(weapon, account, effect, "weapon.rifle.3");
    CHECK(!commitRejected.ok());
    CHECK_EQ(bag.countItem("ammo"), 4);
    CHECK_EQ(effectValue, 1);
    eve::inventory::ItemRegistry::clear();
}

TEST_CASE("resource.gameplay.itemCostAndAccountNonceAreNotCrossAccount") {
    eve::inventory::ItemRegistry::clear();
    eve::inventory::ItemDefinition potion;
    potion.id       = "potion";
    potion.maxStack = 20;
    eve::inventory::ItemRegistry::registerItem(potion);
    eve::inventory::Bag firstBag(1);
    eve::inventory::Bag secondBag(1);
    const int           potionAdded = firstBag.addItem("potion", 3);
    CHECK_EQ(potionAdded, 3);
    eve::inventory::InventoryResourceAccount first(firstBag);
    eve::inventory::InventoryResourceAccount second(secondBag);
    auto                                     costResult = eve::inventory::ItemCostAdapter::itemCost("potion", 2);
    REQUIRE(costResult.ok());
    const auto cost        = std::move(costResult).takeValue();
    auto       reservation = first.reserve(cost);
    REQUIRE(reservation.ok());
    auto foreignCommit = second.commit(reservation.value());
    CHECK(!foreignCommit.ok());
    auto rollback = first.rollback(reservation.value());
    REQUIRE(rollback.ok());
    CHECK_EQ(firstBag.countItem("potion"), 3);
    eve::inventory::ItemRegistry::clear();
}

TEST_CASE("resource.gameplay.rtsEconomyUsesLedgerTransaction") {
    eve::economy::EconomyLedger ledger;
    eve::rts::RTSEconomyAdapter economy(ledger);
    const auto                  income   = makeCost("gold", 10);
    auto                        credited = economy.credit(income);
    REQUIRE(credited.ok());
    CHECK_EQ(ledger.get("gold"), 10);

    const auto price       = makeCost("gold", 4);
    int        effectValue = 0;
    TestEffect effect(effectValue);
    auto       paid = economy.pay(price, effect, "rts.build.1");
    REQUIRE(paid.ok());
    CHECK_EQ(ledger.get("gold"), 6);
    CHECK_EQ(effectValue, 1);

    effect.failPrepare = true;
    auto rejected      = economy.pay(price, effect, "rts.build.2");
    CHECK(!rejected.ok());
    CHECK_EQ(ledger.get("gold"), 6);
}

TEST_CASE("resource.gameplay.cardPlayCompositionIsOneAtomicCrossWrite") {
    eve::card::Card cards;
    REQUIRE_EQ(cards.registerCardsFromJson(R"([{"id":"bolt","kind":"spell","cost":3}])"), 1);
    auto* deck = cards.newDeck();
    auto* hand = cards.newHand(cards.newConfig());
    auto* card = cards.newCard("bolt");
    REQUIRE(deck != nullptr);
    REQUIRE(hand != nullptr);
    REQUIRE(card != nullptr);
    deck->push(card);

    eve::card::CardContainerAdapter source(eve::container::ContainerId("card:atomic-deck"),
                                           eve::card::CardContainerKind::Deck, deck);
    eve::card::CardContainerAdapter destination(eve::container::ContainerId("card:atomic-hand"),
                                                eve::card::CardContainerKind::Hand, nullptr, hand, nullptr,
                                                eve::container::Capacity::fixed(2));
    eve::attributes::AttributeSet   attributes;
    attributes.setBase("mana", 10.0);
    eve::attributes::AttributeSetResourceAccount account(attributes);
    int                                          effectValue = 0;
    TestEffect                                   effect(effectValue);
    eve::card::CardDefinition                    definition;
    definition.id   = "bolt";
    definition.kind = "spell";
    definition.cost = 3;

    eve::card::CardPlayRequest request;
    request.card                        = card;
    request.definition                  = &definition;
    request.playerAccount               = &account;
    request.transactionId               = "card.atomic.success";
    request.composition.source          = &source;
    request.composition.destination     = &destination;
    request.composition.sourceSlot      = eve::container::SlotIndex(0);
    request.composition.destinationSlot = eve::container::SlotIndex(0);
    request.composition.effect          = &effect;
    auto played                         = eve::card::CardPlayPaymentAdapter::play(std::move(request));
    REQUIRE(played.ok());
    CHECK_EQ(deck->count(), 0);
    CHECK_EQ(hand->count(), 1);
    CHECK_EQ(attributes.getBase("mana"), 7.0);
    CHECK_EQ(effectValue, 1);

    auto* rejectedCard = cards.newCard("bolt");
    REQUIRE(rejectedCard != nullptr);
    deck->push(rejectedCard);
    TestEffect rejectedEffect(effectValue);
    rejectedEffect.failCommit = true;
    eve::card::CardPlayRequest rejectedRequest;
    rejectedRequest.card                        = rejectedCard;
    rejectedRequest.definition                  = &definition;
    rejectedRequest.playerAccount               = &account;
    rejectedRequest.transactionId               = "card.atomic.effect-failure";
    rejectedRequest.composition.source          = &source;
    rejectedRequest.composition.destination     = &destination;
    rejectedRequest.composition.sourceSlot      = eve::container::SlotIndex(0);
    rejectedRequest.composition.destinationSlot = eve::container::SlotIndex(1);
    rejectedRequest.composition.effect          = &rejectedEffect;
    auto rejected                               = eve::card::CardPlayPaymentAdapter::play(std::move(rejectedRequest));
    CHECK(!rejected.ok());
    CHECK_EQ(deck->count(), 1);
    CHECK_EQ(hand->count(), 1);
    CHECK_EQ(attributes.getBase("mana"), 7.0);
    CHECK_EQ(effectValue, 1);
}

TEST_CASE("resource.gameplay.rtsBuildUsesSharedActionOrdersProductionPayment") {
    eve::economy::EconomyLedger ledger;
    REQUIRE_EQ(ledger.credit("gold", 20), 20);
    eve::rts::RTSEconomyAdapter economy(ledger);
    eve::production::WorkQueue  production;
    eve::orders::CommandQueue   orders;
    eve::action::ActionRuntime  action;
    auto                        productionSnapshot = [&production]() {
        auto result = production.snapshot();
        return std::move(result).expect("production snapshot must serialize");
    };
    auto duration = eve::Duration::fromSeconds(1.0);
    REQUIRE(duration.ok());
    const auto cost = makeCost("gold", 5);

    eve::rts::RTSBuildRequest request;
    request.production     = &production;
    request.orders         = &orders;
    request.action         = &action;
    request.account        = &economy.account();
    request.cost           = cost;
    request.owner          = "faction:one";
    request.productionKind = "unit";
    request.product        = "worker";
    request.duration       = std::move(duration).takeValue();
    request.transactionId  = "rts.build.worker";
    auto built             = eve::rts::RTSProductionActionAdapter::build(std::move(request));
    REQUIRE(built.ok());
    CHECK_EQ(ledger.get("gold"), 15);
    CHECK_EQ(production.taskCount(), 1);
    CHECK_EQ(orders.orderCount(), 1);
    CHECK_EQ(action.executionCount(), 1u);

    const auto                productionBeforeActionFailure = productionSnapshot();
    const auto                ordersBeforeActionFailure     = orders.orderCount();
    eve::rts::RTSBuildRequest actionFailure;
    actionFailure.production                 = &production;
    actionFailure.orders                     = &orders;
    actionFailure.action                     = &action;
    actionFailure.account                    = &economy.account();
    actionFailure.cost                       = cost;
    actionFailure.owner                      = "faction:one";
    actionFailure.productionKind             = "unit";
    actionFailure.product                    = "tank";
    actionFailure.duration                   = eve::Duration::fromNanoseconds(1000000000);
    actionFailure.transactionId              = "rts.build.action-failure";
    actionFailure.actionDefinition.condition = eve::decision::Condition::hasTag("missing");
    auto failedAction                        = eve::rts::RTSProductionActionAdapter::build(std::move(actionFailure));
    CHECK(!failedAction.ok());
    CHECK_EQ(ledger.get("gold"), 15);
    CHECK_EQ(productionSnapshot(), productionBeforeActionFailure);
    CHECK_EQ(orders.orderCount(), ordersBeforeActionFailure);

    eve::rts::RTSBuildRequest queueFailure;
    queueFailure.production     = &production;
    queueFailure.orders         = &orders;
    queueFailure.action         = &action;
    queueFailure.account        = &economy.account();
    queueFailure.cost           = cost;
    queueFailure.owner          = "faction:one";
    queueFailure.productionKind = "unit";
    queueFailure.product        = "tank";
    queueFailure.duration       = eve::Duration::fromNanoseconds(1000000000);
    queueFailure.orderKind.clear();
    queueFailure.transactionId = "rts.build.queue-failure";
    auto failedQueue           = eve::rts::RTSProductionActionAdapter::build(std::move(queueFailure));
    CHECK(!failedQueue.ok());
    CHECK_EQ(ledger.get("gold"), 15);
    CHECK_EQ(productionSnapshot(), productionBeforeActionFailure);
    CHECK_EQ(orders.orderCount(), ordersBeforeActionFailure);
}

TEST_CASE("resource.gameplay.rtsProductionCancellationRefundsAllResourcesAtomically") {
    eve::economy::EconomyLedger ledger;
    REQUIRE_EQ(ledger.credit("gold", 20), 20);
    REQUIRE_EQ(ledger.credit("gas", 10), 10);
    eve::rts::RTSEconomyAdapter economy(ledger);
    eve::production::WorkQueue production;
    eve::orders::CommandQueue orders;
    eve::action::ActionRuntime action;
    auto costResult = eve::resource::CostSpec::from({{"gold", 5}, {"gas", 3}});
    REQUIRE(costResult.ok());
    auto cost = std::move(costResult).takeValue();

    eve::rts::RTSBuildRequest build;
    build.production = &production;
    build.orders = &orders;
    build.action = &action;
    build.account = &economy.account();
    build.cost = cost;
    build.owner = "faction:one";
    build.productionKind = "unit";
    build.product = "tank";
    build.duration = eve::Duration::fromNanoseconds(1000000000);
    auto built = eve::rts::RTSProductionActionAdapter::build(std::move(build));
    REQUIRE(built.ok());
    auto receipt = std::move(built).takeValue();
    CHECK_EQ(ledger.get("gold"), 15);
    CHECK_EQ(ledger.get("gas"), 7);

    eve::rts::RTSCancelProductionRequest invalid;
    invalid.production = &production;
    invalid.orders = &orders;
    invalid.account = &economy.account();
    invalid.refund = cost;
    invalid.productionTaskId = "missing-task";
    invalid.orderId = receipt.orderId;
    const auto productionBefore = production.snapshot().expect("production cancellation snapshot");
    auto failed = eve::rts::RTSProductionActionAdapter::cancel(std::move(invalid));
    CHECK(!failed.ok());
    CHECK_EQ(production.snapshot().expect("unchanged production snapshot"), productionBefore);
    CHECK_EQ(ledger.get("gold"), 15);
    CHECK_EQ(ledger.get("gas"), 7);

    eve::rts::RTSCancelProductionRequest cancel;
    cancel.production = &production;
    cancel.orders = &orders;
    cancel.account = &economy.account();
    cancel.refund = cost;
    cancel.productionTaskId = receipt.productionTaskId;
    cancel.orderId = receipt.orderId;
    auto cancelled = eve::rts::RTSProductionActionAdapter::cancel(std::move(cancel));
    REQUIRE(cancelled.ok());
    CHECK_EQ(ledger.get("gold"), 20);
    CHECK_EQ(ledger.get("gas"), 10);
    auto task = production.find(receipt.productionTaskId);
    auto order = orders.find(receipt.orderId);
    REQUIRE(task.has_value());
    REQUIRE(order.has_value());
    CHECK_EQ(static_cast<int>(task->get().state),
             static_cast<int>(eve::production::TaskState::Cancelled));
    CHECK_EQ(static_cast<int>(order->get().state),
             static_cast<int>(eve::orders::OrderState::Cancelled));
}

TEST_CASE("resource.gameplay.rtsProductionPriorityProtectsCanonicalResourceFloor") {
    eve::economy::EconomyLedger ledger;
    REQUIRE_EQ(ledger.credit("gold", 10), 10);
    eve::rts::RTSEconomyAdapter economy(ledger);
    eve::production::WorkQueue production;
    eve::orders::CommandQueue orders;
    eve::action::ActionRuntime action;
    auto duration = eve::Duration::fromSeconds(1.0).expect("production duration");
    const auto reserveCost = eve::resource::ResourceCost::create("gold", 6).expect("reserve cost");

    const auto makeRequest = [&](int priority, std::string id) {
        eve::rts::RTSBuildRequest request;
        request.production = &production;
        request.orders = &orders;
        request.action = &action;
        request.account = &economy.account();
        request.cost = makeCost("gold", 5);
        request.owner = "faction:one";
        request.productionKind = "unit";
        request.product = "soldier";
        request.duration = duration;
        request.priority = priority;
        request.transactionId = std::move(id);
        request.resourceReserves.push_back({reserveCost, 10});
        return request;
    };

    auto low = eve::rts::RTSProductionActionAdapter::build(makeRequest(9, "rts.floor.low"));
    CHECK(!low.ok());
    CHECK_EQ(ledger.get("gold"), 10);
    CHECK_EQ(production.taskCount(), 0);
    CHECK_EQ(orders.orderCount(), 0);
    CHECK_EQ(action.executionCount(), 0u);

    auto high = eve::rts::RTSProductionActionAdapter::build(makeRequest(10, "rts.floor.high"));
    REQUIRE(high.ok());
    CHECK_EQ(ledger.get("gold"), 5);
    CHECK_EQ(production.taskCount(), 1);
}

TEST_CASE("resource.gameplay.economySnapshotRestoresBalancesAndAccounting") {
    eve::economy::EconomyLedger ledger;
    REQUIRE_EQ(ledger.credit("gold", 50), 50);
    REQUIRE(ledger.debit("gold", 12));
    REQUIRE_EQ(ledger.credit("gas", 9), 9);
    const auto saved = ledger.snapshot();

    REQUIRE(ledger.debit("gold", 20));
    REQUIRE_EQ(ledger.credit("gas", 4), 4);
    ledger.restore(saved);

    CHECK_EQ(ledger.get("gold"), 38);
    CHECK_EQ(ledger.getIncome("gold"), 50);
    CHECK_EQ(ledger.getExpense("gold"), 12);
    CHECK_EQ(ledger.get("gas"), 9);
    CHECK_EQ(ledger.getIncome("gas"), 9);
    CHECK_EQ(ledger.getExpense("gas"), 0);
}
