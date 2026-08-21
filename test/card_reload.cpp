#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "card/Card.h"
#include "card/CardState.h"
#include "common/StateValue.h"

#include <string>

namespace {

const char* kDefs = R"JSON(
[{"id":"flame","name":"Flame","kind":"creature","cost":2,"attack":3,"health":2},
 {"id":"bolt","name":"Bolt","kind":"spell","cost":3}]
)JSON";

}  // namespace

TEST_CASE("card.state.captureRestorePhases") {
    eve::card::Card mod;
    mod.registerCardsFromJson(kDefs);
    eve::card::CardData* a = mod.newCard("flame");
    eve::card::CardData* b = mod.newCard("bolt");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);

    a->state()->phase    = eve::card::CardState::Played;
    b->state()->phase    = eve::card::CardState::Disabled;
    b->state()->dragging = true;  // transient interaction, dropped on restore

    eve::StateValue captured;
    REQUIRE(eve::card::captureCardState(captured));

    a->state()->phase = eve::card::CardState::Deck;
    b->state()->phase = eve::card::CardState::Hand;
    std::string err;
    CHECK(eve::card::restoreCardState(captured, &err));
    CHECK(err.empty());
    CHECK(static_cast<int>(a->state()->phase) == static_cast<int>(eve::card::CardState::Played));
    CHECK(static_cast<int>(b->state()->phase) == static_cast<int>(eve::card::CardState::Disabled));
    CHECK(!b->state()->dragging);

    // Reset returns everything to the deck.
    CHECK(eve::card::resetCardState());
    CHECK(static_cast<int>(a->state()->phase) == static_cast<int>(eve::card::CardState::Deck));
    CHECK(static_cast<int>(b->state()->phase) == static_cast<int>(eve::card::CardState::Deck));

    a->release();
    b->release();
}

TEST_CASE("card.state.restoreRejectsMalformed") {
    std::string err;
    CHECK(!eve::card::restoreCardState(eve::StateValue::object(), &err));
    CHECK(!err.empty());
}
