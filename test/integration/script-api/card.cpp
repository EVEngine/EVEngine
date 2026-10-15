#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ScriptTest.h"
#include "card/Card.h"
#include "common/ECS.h"

#include <cmath>

using namespace eve::card;

namespace {

[[maybe_unused]] int countCardView() {
    int n = 0;
    auto view = ecs::View<CardData, CardData::Identity>();
    for (auto it = view.begin(); it != view.end(); ++it) ++n;
    return n;
}

[[maybe_unused]] bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

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


static const char *kCardViewScript = R"SQ(
[[maybe_unused]] function testCardCppView() {
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


namespace {

[[maybe_unused]] void settleHand(Hand *h, const std::vector<Zone *> &zones, std::vector<CardEvent> &out) {
    for (int i = 0; i < 120; ++i) h->update(1.f / 60.f, 0.f, 0.f, false, zones, out);
    out.clear();
}

}  // namespace
