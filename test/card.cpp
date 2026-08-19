#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ScriptTest.h"
#include "card/Card.h"
#include "common/ECS.h"

using namespace eve::card;

namespace {

int countCardView() {
    int n = 0;
    auto view = ecs::View<CardData, CardData::Identity>();
    for (auto it = view.begin(); it != view.end(); ++it) ++n;
    return n;
}

const char *kDefs = R"JSON(
[{"id":"flame","name":"Flame","kind":"creature","cost":2,"attack":3,"health":2},
 {"id":"bolt","name":"Bolt","kind":"spell","cost":3}]
)JSON";

}  // namespace

TEST_CASE("card.factory.newCardFromJson") {
    Card mod;
    CHECK_EQ(mod.registerCardsFromJson(kDefs), 2);
    CHECK(mod.hasCardDefinition("flame"));
    CardData *c = mod.newCard("flame");
    REQUIRE(c != nullptr);
    CHECK_EQ(c->identity()->name, std::string("Flame"));
    CHECK_EQ(c->stats()->cost, 2);
    CHECK_EQ(c->stats()->attack, 3);
    CHECK_EQ(mod.newCard("missing"), nullptr);
}

TEST_CASE("card.hand.membershipOrder") {
    Card mod;
    mod.registerCardsFromJson(kDefs);
    LayoutConfig *cfg = mod.newConfig();
    Hand *h = mod.newHand(cfg);
    h->meta()->owner = "player";
    CardData *a = mod.newCard("flame");
    CardData *b = mod.newCard("bolt");
    h->addCard(a);
    h->addCard(b);
    CHECK_EQ(h->count(), 2);
    CHECK_EQ(h->get(0), a);
    CHECK_EQ(h->get(1), b);
    CHECK_EQ(mod.findHand("player"), h);
}

TEST_CASE("card.deck.stackOrder") {
    Card mod;
    mod.registerCardsFromJson(kDefs);
    Deck *d = mod.newDeck();
    CardData *a = mod.newCard("flame");
    CardData *b = mod.newCard("bolt");
    d->push(a);
    d->push(b);
    CHECK_EQ(d->count(), 2);
    CHECK_EQ(d->peek(), b);
    CHECK_EQ(d->draw(), b);
    CHECK_EQ(d->draw(), a);
    CHECK(d->isEmpty());
}

TEST_CASE("card.ecs.viewCountsFactoryCards") {
    const int before = countCardView();
    {
        Card mod;
        mod.registerCardsFromJson(kDefs);
        CHECK(mod.newCard("flame") != nullptr);
        CHECK(mod.newCard("bolt") != nullptr);
        CHECK_EQ(countCardView(), before + 2);
    }
    CHECK_EQ(countCardView(), before);
}

TEST_CASE("card.ecs.removeKeepsEntityInView") {
    Card mod;
    mod.registerCardsFromJson(kDefs);
    const int before = countCardView();
    LayoutConfig *cfg = mod.newConfig();
    Hand *h = mod.newHand(cfg);
    CardData *c = mod.newCard("flame");
    h->addCard(c);
    CHECK_EQ(countCardView(), before + 1);
    CHECK(h->removeCard(c));
    CHECK_EQ(h->count(), 0);
    CHECK_EQ(countCardView(), before + 1);
    CHECK_EQ(c->identity()->name, std::string("Flame"));
}

TEST_CASE("card.drawCardMovesDeckToHand") {
    Card mod;
    mod.registerCardsFromJson(kDefs);
    LayoutConfig *cfg = mod.newConfig();
    Hand *h = mod.newHand(cfg);
    h->meta()->owner = "player";
    Deck *d = mod.newDeck();
    d->push(mod.newCard("flame"));
    CardData *drawn = mod.drawCard("player");
    REQUIRE(drawn != nullptr);
    CHECK_EQ(h->count(), 1);
    CHECK(d->isEmpty());
    CHECK_EQ(h->get(0), drawn);
}

static const char *kCardViewScript = R"SQ(
function testCardCppView() {
    local card = eve.Card()
    card.registerCardsFromJson("[{\"id\":\"flame\",\"name\":\"Flame\"}]")
    local c = card.newCard("flame")
    if (c == null) return false
    local all = eve.view(eve.CardData)
    foreach (e in all) {
        if (e.getId() == c.getId()) return true
    }
    return false
}
)SQ";

UnitSciptTest(CardViewScriptTest, kCardViewScript);

TEST_CASE_FIXTURE(CardViewScriptTest, "card.script.viewSeesNewCard") {
    CHECK(vm.callFunc(vm.findFunc("testCardCppView"), vm).toBool());
}
