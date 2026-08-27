#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "attributes/AttributeSetResourceAccount.h"
#include "card/CardAttributes.h"
#include "card/Card.h"
#include "card/CardContainers.h"
#include "common/ECS.h"
#include "economy/EconomyLedger.h"
#include "rts/RTS.h"
#include "rts/RTSEconomy.h"

#include <cstdint>
#include <utility>

namespace {

eve::SubjectRef subject(const char *text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

eve::SimulationStep step(std::uint64_t tick, double seconds) {
    auto delta = eve::Duration::fromSeconds(seconds);
    REQUIRE(delta.ok());
    return {eve::SimulationTick(tick), std::move(delta).takeValue()};
}

eve::effects::EffectPolicy oneShotPolicy() {
    eve::effects::EffectPolicy policy;
    policy.stackMode = eve::effects::StackMode::NewInstance;
    policy.stackCount = eve::effects::StackCountPolicy::Keep;
    policy.maxStacks = 1;
    return policy;
}

}  // namespace

TEST_CASE("production.cardFacadeRoutesAttributesEffectsAndPlay") {
    eve::card::Card cards;
    REQUIRE_EQ(cards.registerCardsFromJson(
                    R"JSON([{"id":"soldier","kind":"creature","cost":2,"attack":4,"health":10}])JSON"),
                1);
    auto *deck = cards.newDeck();
    auto *hand = cards.newHand(cards.newConfig());
    auto *card = cards.newCard("soldier");
    REQUIRE(deck != nullptr);
    REQUIRE(hand != nullptr);
    REQUIRE(card != nullptr);
    deck->push(card);

    auto base = cards.setCardAttribute(
        *card, eve::card::CardAttributeAdapter::attackAttribute, 7.0);
    REQUIRE(base.ok());
    auto attack = cards.getCardAttribute(
        *card, eve::card::CardAttributeAdapter::attackAttribute);
    REQUIRE(attack.ok());
    CHECK_EQ(attack.value(), 7.0);

    eve::card::CardEffectDefinition damage;
    damage.id = "poison";
    damage.source = "card.facade.test";
    damage.period = 1.0;
    damage.duration = 2.0;
    damage.magnitude = 2.0;
    damage.policy = oneShotPolicy();
    auto effect = cards.applyEffect(*card, damage, subject("00000000-0000-7000-8000-000000000101"));
    REQUIRE(effect.ok());
    auto effectHandle = std::move(effect).takeValue();
    CHECK_EQ(card->effects()->values.count(), 1u);

    auto advanced = cards.step(step(1, 1.0));
    REQUIRE(advanced.ok());
    const auto advancedCount = std::move(advanced).takeValue();
    CHECK_EQ(advancedCount, 1u);
    CHECK_EQ(card->effects()->values.target().health, 8);
    auto health = cards.getCardAttribute(
        *card, eve::card::CardAttributeAdapter::healthAttribute);
    REQUIRE(health.ok());
    CHECK_EQ(health.value(), 8.0);
    REQUIRE(card->effects()->values.resolve(effectHandle).ok());

    eve::attributes::AttributeSet playerAttributes;
    playerAttributes.setBase("mana", 10.0);
    eve::attributes::AttributeSetResourceAccount account(playerAttributes);
    eve::card::CardContainerAdapter source(
        eve::container::ContainerId("card:facade-deck"),
        eve::card::CardContainerKind::Deck, deck);
    eve::card::CardContainerAdapter destination(
        eve::container::ContainerId("card:facade-hand"),
        eve::card::CardContainerKind::Hand, nullptr, hand);
    eve::card::CardPlayComposition composition;
    composition.source = &source;
    composition.destination = &destination;
    composition.sourceSlot = eve::container::SlotIndex(0);
    composition.destinationSlot = eve::container::SlotIndex(0);

    auto played = cards.play(*card, account, std::move(composition), "card.facade.play");
    REQUIRE(played.ok());
    std::move(played).takeValue();
    CHECK_EQ(deck->count(), 0);
    CHECK_EQ(hand->count(), 1);
    CHECK_EQ(playerAttributes.getBase("mana"), 8.0);
    CHECK_EQ(static_cast<int>(card->state()->phase),
             static_cast<int>(eve::card::CardState::Played));
}

TEST_CASE("production.rtsFacadeRoutesCanonicalAttributesEffectsAndBuild") {
    eve::rts::RTS rts;
    auto unitResult = rts.newUnit(
        subject("00000000-0000-7000-8000-000000000102"));
    REQUIRE(unitResult.ok());
    auto *unit = std::move(unitResult).takeValue();
    auto buildingResult = rts.newBuilding(
        subject("00000000-0000-7000-8000-000000000103"));
    REQUIRE(buildingResult.ok());
    auto *building = std::move(buildingResult).takeValue();

    auto setAttack = rts.setUnitAttribute(
        *unit, eve::rts::RTSUnitAttributeAdapter::attackAttribute, 12.0);
    REQUIRE(setAttack.ok());
    auto attack = rts.readUnitAttribute(
        *unit, eve::rts::RTSUnitAttributeAdapter::attackAttribute);
    REQUIRE(attack.ok());
    CHECK_EQ(attack.value(), 12.0);

    eve::rts::RTSEffectDefinition suppression;
    suppression.id = "suppression";
    suppression.source = "rts.facade.test";
    suppression.period = 1.0;
    suppression.duration = 2.0;
    suppression.magnitude = 1.0;
    suppression.kind = eve::rts::RTSEffectKind::Suppression;
    suppression.policy = oneShotPolicy();
    auto effect = rts.applyEffect(*unit, suppression);
    REQUIRE(effect.ok());
    auto effectHandle = std::move(effect).takeValue();
    CHECK_EQ(unit->effects()->values.count(), 1u);

    eve::action::ActionRuntime action;
    eve::rts::ActionAdapter actionAdapter(action);
    auto advanced = rts.step(step(1, 1.0), actionAdapter);
    REQUIRE(advanced.ok());
    std::move(advanced).takeValue();
    CHECK_EQ(unit->effects()->values.target().suppression, 1u);
    REQUIRE(unit->effects()->values.target().commandInterrupts == 0u);
    REQUIRE(unit->effects()->values.count() == 1u);
    REQUIRE(unit->effects()->values.remove(effectHandle).ok());

    eve::economy::EconomyLedger ledger;
    auto credited = ledger.credit("gold", 10);
    REQUIRE(credited == 10);
    eve::rts::RTSEconomyAdapter economy(ledger);
    auto cost = eve::resource::CostSpec::single("gold", 3);
    REQUIRE(cost.ok());
    auto duration = eve::Duration::fromSeconds(1.0);
    REQUIRE(duration.ok());
    auto built = rts.build(*building, action, economy.account(),
                           std::move(cost).takeValue(), "worker",
                           std::move(duration).takeValue());
    REQUIRE(built.ok());
    std::move(built).takeValue();
    CHECK_EQ(ledger.get("gold"), 7);
    CHECK_EQ(building->production()->values.taskCount(), 1u);
    CHECK_EQ(building->orders()->values.orderCount(), 1u);
    CHECK_EQ(action.executionCount(), 1u);

    auto insufficientCost = eve::resource::CostSpec::single("gold", 20);
    REQUIRE(insufficientCost.ok());
    auto failed = rts.build(*building, action, economy.account(),
                            std::move(insufficientCost).takeValue(), "tank",
                            eve::Duration::fromSeconds(1.0).expect("test build duration"));
    CHECK(!failed.ok());
    CHECK_EQ(ledger.get("gold"), 7);
    CHECK_EQ(building->production()->values.taskCount(), 1u);
    CHECK_EQ(building->orders()->values.orderCount(), 1u);
}
