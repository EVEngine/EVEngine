#include "card/Card.h"
#include "card/CardPlay.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <optional>

TEST_CASE("card.playConditionAdapterReadsCardAndExternalCapabilities") {
    using namespace eve::card;
    using namespace eve::decision;

    Card mod;
    REQUIRE_EQ(mod.registerCardsFromJson(
                   R"JSON([{"id":"firebolt","kind":"spell","cost":3,"attack":0,"health":0,"tags":["fire"]}])JSON"),
               1);
    CardData* card = mod.newCard("firebolt");
    REQUIRE(card != nullptr);
    card->state()->phase = CardState::Hand;

    CardDefinition local;
    local.id                             = "firebolt";
    local.kind                           = "spell";
    local.cost                           = 3;
    local.tags                           = {"fire"};
    int                      policyCalls = 0;
    CardPlayConditionQueries queries;
    queries.resource = [](std::string_view key) -> std::optional<eve::Value> {
        return key == "mana" ? std::optional<eve::Value>(eve::Value(5)) : std::nullopt;
    };
    queries.authority = [](std::string_view scope) -> std::optional<bool> { return scope == "play"; };
    queries.policy    = [&policyCalls](std::string_view name, const eve::Value&) -> std::optional<ConditionResult> {
        ++policyCalls;
        return name == "card.safe" ? std::optional<ConditionResult>(ConditionResult::success()) : std::nullopt;
    };

    const auto condition = Condition::all({
        Condition::hasTag("fire"),
        Condition::compare("card.cost", CompareOperator::LessEqual, 3),
        Condition::hasResource("mana"),
        Condition::stateEquals("card.phase", "hand"),
        Condition::authorityCheck("play"),
        Condition::policyCall("card.safe"),
    });
    const auto result    = CardPlayConditionAdapter::evaluate(card, local, condition, queries);
    REQUIRE(result.passed());
    CHECK_EQ(policyCalls, 1);

    card->state()->phase = CardState::Played;
    const auto blocked =
        CardPlayConditionAdapter::evaluate(card, local, Condition::stateEquals("card.phase", "hand"), queries);
    REQUIRE(!blocked.passed());
    CHECK_EQ(static_cast<int>(blocked.reasonCode()), static_cast<int>(ConditionReasonCode::StateMismatch));
}

TEST_CASE("card.playConditionIsStoredOnDefinitionAndEvaluatedByCardFacade") {
    using namespace eve::card;
    using namespace eve::decision;

    Card mod;
    REQUIRE_EQ(mod.registerCardsFromJson(R"JSON([{"id":"ward","kind":"spell","cost":2,"tags":["ward"]}])JSON"), 1);
    CardData* card = mod.newCard("ward");
    REQUIRE(card != nullptr);
    card->state()->phase = CardState::Hand;
    auto stored          = mod.setCardPlayCondition("ward", Condition::hasTag("missing"));
    REQUIRE(stored.ok());
    const auto rejected = mod.evaluatePlay(card);
    REQUIRE(!rejected.passed());
    CHECK_EQ(static_cast<int>(rejected.reasonCode()), static_cast<int>(ConditionReasonCode::TagMissing));

    auto resourceStored = mod.setCardPlayCondition("ward", Condition::hasResource("mana"));
    REQUIRE(resourceStored.ok());
    CardPlayConditionQueries queries;
    queries.resource = [](std::string_view key) -> std::optional<eve::Value> {
        return key == "mana" ? std::optional<eve::Value>(eve::Value(1)) : std::nullopt;
    };
    const auto accepted = mod.evaluatePlay(card, queries);
    REQUIRE(accepted.passed());
}
