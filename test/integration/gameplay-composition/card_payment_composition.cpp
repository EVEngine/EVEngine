#include "card/Card.h"
#include "card/CardPlay.h"
#include "economy/EconomyLedger.h"
#include "rts/RTSEconomy.h"
#include "rts/RTSProductionAction.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <array>
#include <limits>

namespace {
class Effect final : public eve::transaction::ITransactionParticipant {
public:
    int               value = 0;
    bool              fail  = false;
    std::string_view  name() const noexcept override { return "composition-effect"; }
    eve::Result<void> prepare(const eve::transaction::TransactionContext&) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> commit(const eve::transaction::TransactionContext&) override {
        if (fail)
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected failure", "test"));
        ++value;
        return eve::Result<void>::success();
    }
    eve::Result<void> rollback(const eve::transaction::TransactionContext&) override {
        return eve::Result<void>::success();
    }
    eve::Result<void> compensate(const eve::transaction::TransactionContext&) override {
        --value;
        return eve::Result<void>::success();
    }
};
}  // namespace

TEST_CASE("card.customResourceCostComposesWithRtsAccountAndEffects") {
    eve::card::Card cards;
    REQUIRE_EQ(cards.registerCardsFromJson(R"([{"id":"deploy","cost":99}])"), 1);
    auto* card = cards.newCard("deploy");
    REQUIRE(card != nullptr);
    eve::economy::EconomyLedger ledger;
    CHECK_EQ(ledger.credit("composition_ore", 10), 10);
    CHECK_EQ(ledger.credit("composition_supply", 4), 4);
    eve::rts::RTSEconomyAdapter    economy(ledger);
    Effect                         first, second;
    eve::card::CardPlayComposition composition;
    composition.paymentCost =
        eve::resource::CostSpec::from({{"composition_ore", 3}, {"composition_supply", 2}}).expect("valid cost");
    composition.effect            = &first;
    composition.additionalEffects = {&second};
    second.fail                   = true;
    const auto previous           = card->state()->phase;
    auto       rejected           = cards.play(*card, economy.account(), composition);
    CHECK(!rejected.ok());
    CHECK_EQ(first.value, 0);
    CHECK_EQ(ledger.get("composition_ore"), 10);
    CHECK_EQ(static_cast<int>(card->state()->phase), static_cast<int>(previous));
    second.fail = false;
    auto played = cards.play(*card, economy.account(), composition);
    REQUIRE(played.ok());
    CHECK_EQ(first.value, 1);
    CHECK_EQ(second.value, 1);
    CHECK_EQ(ledger.get("composition_ore"), 7);
    CHECK_EQ(ledger.get("composition_supply"), 2);
    CHECK_EQ(ledger.get("mana"), 0);
}

TEST_CASE("card.rtsPaymentRejectsDuplicateEffectsBeforeMutation") {
    eve::economy::EconomyLedger ledger;
    CHECK_EQ(ledger.credit("composition_ore", 10), 10);
    eve::rts::RTSEconomyAdapter economy(ledger);
    auto                        cost = eve::resource::CostSpec::single("composition_ore", 3).expect("valid cost");
    Effect                      effect;
    std::array<eve::transaction::ITransactionParticipant*, 2> effects{&effect, &effect};
    auto                                                      rejected = economy.pay(cost, effects);
    CHECK(!rejected.ok());
    CHECK_EQ(effect.value, 0);
    CHECK_EQ(ledger.get("composition_ore"), 10);
    Effect second;
    effects[1] = &second;
    auto paid  = economy.pay(cost, effects);
    REQUIRE(paid.ok());
    CHECK_EQ(effect.value, 1);
    CHECK_EQ(second.value, 1);
    CHECK_EQ(ledger.get("composition_ore"), 7);
}

TEST_CASE("card.customPaymentRejectsInvalidCostAndMissingAccount") {
    eve::card::Card cards;
    REQUIRE_EQ(cards.registerCardsFromJson(R"([{"id":"free","cost":0}])"), 1);
    auto* card = cards.newCard("free");
    REQUIRE(card != nullptr);
    eve::card::CardDefinition definition;
    definition.id = "free";
    eve::card::CardPlayRequest request;
    request.card                    = card;
    request.definition              = &definition;
    request.composition.paymentCost = eve::resource::CostSpec::single("ore", 1).expect("cost");
    auto missing                    = eve::card::CardPlayPaymentAdapter::play(request);
    CHECK(!missing.ok());
    eve::economy::EconomyLedger ledger;
    eve::rts::RTSEconomyAdapter economy(ledger);
    request.playerAccount           = &economy.account();
    request.composition.paymentCost = eve::resource::CostSpec{};
    auto invalid                    = eve::card::CardPlayPaymentAdapter::play(request);
    CHECK(!invalid.ok());
    CHECK_EQ(static_cast<int>(card->state()->phase), static_cast<int>(eve::card::CardState::Deck));
    request.playerAccount = nullptr;
    request.composition.paymentCost.reset();
    auto free = eve::card::CardPlayPaymentAdapter::play(request);
    REQUIRE(free.ok());
    CHECK_EQ(static_cast<int>(card->state()->phase), static_cast<int>(eve::card::CardState::Played));
}

TEST_CASE("card.economyAccountingOverflowRejectsWithoutPublishing") {
    eve::economy::EconomyLedger ledger;
    auto                        snapshot = ledger.snapshot();
    snapshot.current["composition_ore"]  = 10;
    snapshot.income["composition_ore"]   = std::numeric_limits<int>::max();
    snapshot.expense["composition_ore"]  = std::numeric_limits<int>::max();
    ledger.restore(snapshot);
    eve::rts::RTSEconomyAdapter economy(ledger);
    auto                        cost     = eve::resource::CostSpec::single("composition_ore", 1).expect("valid cost");
    auto                        credited = economy.credit(cost);
    CHECK(!credited.ok());
    Effect effect;
    auto   paid = economy.pay(cost, effect);
    CHECK(!paid.ok());
    CHECK_EQ(effect.value, 0);
    CHECK_EQ(ledger.get("composition_ore"), 10);
    CHECK_EQ(ledger.getIncome("composition_ore"), std::numeric_limits<int>::max());
    CHECK_EQ(ledger.getExpense("composition_ore"), std::numeric_limits<int>::max());
}

TEST_CASE("card.productionAndCardConsumptionShareOnePaymentAndRollback") {
    eve::card::Card cards;
    REQUIRE_EQ(cards.registerCardsFromJson(R"([{"id":"reinforcement","cost":0}])"), 1);
    auto* card = cards.newCard("reinforcement");
    REQUIRE(card != nullptr);
    const auto                  original = card->state()->phase;
    eve::economy::EconomyLedger ledger;
    CHECK_EQ(ledger.credit("composition_ore", 10), 10);
    eve::rts::RTSEconomyAdapter economy(ledger);
    eve::production::WorkQueue  production;
    eve::orders::CommandQueue   orders;
    eve::action::ActionRuntime  actions;
    eve::rts::RTSBuildRequest   request;
    request.production     = &production;
    request.orders         = &orders;
    request.action         = &actions;
    request.account        = &economy.account();
    request.cost           = eve::resource::CostSpec::single("composition_ore", 3).expect("valid cost");
    request.owner          = "factory";
    request.product        = "soldier";
    request.productionKind = "unit";
    request.duration       = eve::Duration::fromSeconds(1).expect("duration");
    Effect effect;
    effect.fail = true;
    eve::card::CardPlayParticipant rejectedCard(*card);
    request.additionalEffects = {&rejectedCard, &effect};
    auto rejected             = eve::rts::RTSProductionActionAdapter::build(request);
    CHECK(!rejected.ok());
    CHECK_EQ(production.taskCount(), 0u);
    CHECK_EQ(orders.orderCount(), 0u);
    CHECK_EQ(ledger.get("composition_ore"), 10);
    CHECK_EQ(static_cast<int>(card->state()->phase), static_cast<int>(original));
    effect.fail = false;
    eve::card::CardPlayParticipant acceptedCard(*card);
    request.additionalEffects = {&acceptedCard, &effect};
    auto accepted             = eve::rts::RTSProductionActionAdapter::build(request);
    REQUIRE(accepted.ok());
    CHECK_EQ(production.taskCount(), 1u);
    CHECK_EQ(orders.orderCount(), 1u);
    CHECK_EQ(ledger.get("composition_ore"), 7);
    CHECK_EQ(static_cast<int>(card->state()->phase), static_cast<int>(eve::card::CardState::Played));
}
