#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ScriptTest.h"
#include "card/Card.h"
#include "common/ECS.h"

#include <cmath>

using namespace eve::card;

namespace {

int countCardView() {
    int n = 0;
    auto view = ecs::View<CardData, CardData::Identity>();
    for (auto it = view.begin(); it != view.end(); ++it) ++n;
    return n;
}

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

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

TEST_CASE("card.authoring.stableDefinitionEnumerationAndMutation") {
    Card mod;
    REQUIRE_EQ(mod.registerCardsFromJson(kDefs), 2);
    CHECK_EQ(mod.getCardDefinitionId(0), std::string("bolt"));
    CHECK_EQ(mod.getCardDefinitionId(1), std::string("flame"));
    REQUIRE(mod.setCardDefinition("archer", "Archer", "creature", 4, 5, 3, 1.2f, -0.2f, 0.5f));
    CHECK_EQ(mod.getCardDefinitionId(0), std::string("archer"));
    CHECK_EQ(mod.getCardDefinitionAttack("archer"), 5);
    CHECK_EQ(mod.getCardDefinitionTintR("archer"), 1.f);
    CHECK_EQ(mod.getCardDefinitionTintG("archer"), 0.f);
    CHECK(!mod.setCardDefinition("", "Invalid", "", 0, 0, 0, 0.f, 0.f, 0.f));
    REQUIRE(mod.removeCardDefinition("bolt"));
    CHECK(!mod.hasCardDefinition("bolt"));
    CHECK(!mod.removeCardDefinition("bolt"));
}

TEST_CASE("card.presentation.stableIdentityAndSnapshot") {
    Card mod;
    CHECK_EQ(mod.registerCardsFromJson(kDefs), 2);
    CardData *first = mod.newCard("flame");
    CardData *second = mod.newCard("flame");
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK_EQ(first->identity()->definitionId, "flame");
    CHECK_EQ(second->identity()->definitionId, "flame");
    CHECK_NE(first->identity()->id, second->identity()->id);

    first->layout()->x = 42.f;
    first->layout()->angle = 12.f;
    CHECK_EQ(mod.capturePresentation(), 2);
    auto *snapshot = mod.getPresentation(0);
    REQUIRE(snapshot != nullptr);
    CHECK_EQ(snapshot->instanceId, first->identity()->id);
    CHECK_EQ(snapshot->definitionId, "flame");
    CHECK_EQ(snapshot->x, 42.f);
    CHECK_EQ(snapshot->angle, 12.f);
    CHECK_EQ(mod.getPresentation(2), nullptr);
}

TEST_CASE("card.presentation.planeMapperRoundTrip") {
    CardPlaneMapper mapper;
    mapper.setLogicalRect(0.f, 0.f, 1280.f, 700.f);
    mapper.setPlane(-6.4f, 0.6f, -7.f, 12.8f, 0.f, 0.f, 0.f, 0.f, 14.f);

    CHECK(mapper.mapLayout(640.f, 350.f));
    CHECK_LT(std::abs(mapper.getWorldX()), 0.0001f);
    CHECK_LT(std::abs(mapper.getWorldY() - 0.6f), 0.0001f);
    CHECK_LT(std::abs(mapper.getWorldZ()), 0.0001f);

    CHECK(mapper.mapRay(0.f, 10.f, 0.f, 0.f, -1.f, 0.f));
    CHECK_LT(std::abs(mapper.getLogicalX() - 640.f), 0.0001f);
    CHECK_LT(std::abs(mapper.getLogicalY() - 350.f), 0.0001f);
    CHECK(!mapper.mapRay(0.f, 10.f, 0.f, 1.f, 0.f, 0.f));
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

TEST_CASE("card.cardStateName") {
    CHECK_EQ(std::string(cardStateName(CardState::Deck)), std::string("deck"));
    CHECK_EQ(std::string(cardStateName(CardState::Hand)), std::string("hand"));
    CHECK_EQ(std::string(cardStateName(CardState::Hovered)), std::string("hovered"));
    CHECK_EQ(std::string(cardStateName(CardState::Dragging)), std::string("dragging"));
    CHECK_EQ(std::string(cardStateName(CardState::Returning)), std::string("returning"));
    CHECK_EQ(std::string(cardStateName(CardState::Played)), std::string("played"));
    CHECK_EQ(std::string(cardStateName(CardState::Discarded)), std::string("discarded"));
    CHECK_EQ(std::string(cardStateName(CardState::Disabled)), std::string("disabled"));
}

TEST_CASE("card.cardData.hitAndDescribe") {
    Card mod;
    mod.registerCardsFromJson(kDefs);
    CardData *c = mod.newCard("flame");
    c->layout()->x = 100.f;
    c->layout()->y = 100.f;
    c->layout()->w = 50.f;
    c->layout()->h = 70.f;
    c->layout()->scale = 1.f;
    CHECK(c->hit(100.f, 100.f));
    CHECK(c->hit(125.f, 135.f));
    CHECK(!c->hit(126.f, 100.f));
    CHECK(!c->hit(100.f, 136.f));
    c->layout()->scale = 2.f;  // enlarged hit box
    CHECK(c->hit(140.f, 100.f));
    c->layout()->w = 0.f;  // zero-size never hits
    CHECK(!c->hit(100.f, 100.f));

    const std::string creature = c->describe();
    CHECK(creature.find("Flame") != std::string::npos);
    CHECK(creature.find("3/2") != std::string::npos);
    CardData *spell = mod.newCard("bolt");
    CHECK(spell->describe().find("\xe6\xb3\x95\xe6\x9c\xaf") != std::string::npos);  // 法术
}

TEST_CASE("card.deck.drawPeekBoundsAndShuffle") {
    Card mod;
    mod.registerCardsFromJson(kDefs);
    Deck *d = mod.newDeck();
    CHECK(d->isEmpty());
    CHECK_EQ(d->draw(), nullptr);
    CHECK_EQ(d->peek(), nullptr);
    CHECK_EQ(d->get(-1), nullptr);
    CHECK_EQ(d->get(0), nullptr);

    CardData *a = mod.newCard("flame");
    CardData *b = mod.newCard("bolt");
    d->push(a);
    d->push(b);
    CHECK_EQ(d->get(0), a);
    CHECK_EQ(d->get(1), b);
    CHECK_EQ(d->get(2), nullptr);

    d->shuffle();  // membership preserved, order may change
    CHECK_EQ(d->count(), 2);
    const bool top0 = d->get(0) == a || d->get(0) == b;
    const bool top1 = d->get(1) == a || d->get(1) == b;
    CHECK(top0);
    CHECK(top1);
    CHECK(d->get(0) != d->get(1));

    d->clear();
    CHECK(d->isEmpty());
}

TEST_CASE("card.zone.containsAndAccepts") {
    Card mod;
    mod.registerCardsFromJson(kDefs);
    Zone *z = mod.newZone("board", "Board", 10.f, 20.f, 100.f, 60.f);
    CHECK(z->contains(10.f, 20.f));
    CHECK(z->contains(109.f, 79.f));
    CHECK(!z->contains(9.f, 20.f));
    CHECK(!z->contains(111.f, 81.f));  // rect bounds are inclusive

    CardData *flame = mod.newCard("flame");
    CardData *bolt = mod.newCard("bolt");
    CHECK(z->accepts(flame));
    CHECK(z->accepts(bolt));
    CHECK(!z->accepts(nullptr));

    z->filter()->acceptKinds = {"spell"};
    CHECK(!z->accepts(flame));
    CHECK(z->accepts(bolt));

    z->rect()->enabled = false;
    CHECK(!z->accepts(bolt));
}

TEST_CASE("card.hand.addCardSetsLayoutAndPhase") {
    Card mod;
    mod.registerCardsFromJson(kDefs);
    LayoutConfig *cfg = mod.newConfig();
    Hand *h = mod.newHand(cfg);

    CardData *flame = mod.newCard("flame");
    h->addCard(flame);
    CHECK(static_cast<int>(flame->state()->phase) == static_cast<int>(CardState::Hand));
    CHECK_EQ(flame->layout()->w, cfg->cardW);
    CHECK(std::fabs(flame->layout()->scale - 0.01f) < 1e-5f);

    CardData *bolt = mod.newCard("bolt");
    bolt->visual()->disabled = true;
    h->addCard(bolt);
    CHECK(static_cast<int>(bolt->state()->phase) == static_cast<int>(CardState::Disabled));
    CHECK(std::fabs(bolt->layout()->alpha - cfg->disabledAlpha) < 1e-5f);

    CardData *other = mod.newCard("flame");
    CHECK(!h->removeCard(other));
    CHECK(h->removeCard(flame));
    CHECK_EQ(h->count(), 1);
    CHECK_EQ(h->find(bolt->identity()->id), bolt);
    CHECK_EQ(h->find("nope"), nullptr);
    CHECK_EQ(h->get(5), nullptr);
    h->clear();
    CHECK_EQ(h->count(), 0);
}

TEST_CASE("card.hand.pickTopmostAndFanSlot") {
    Card mod;
    mod.registerCardsFromJson(kDefs);
    LayoutConfig *cfg = mod.newConfig();
    Hand *h = mod.newHand(cfg);
    CardData *a = mod.newCard("flame");
    CardData *b = mod.newCard("bolt");
    h->addCard(a);
    h->addCard(b);
    a->layout()->x = 100.f;
    a->layout()->y = 100.f;
    a->layout()->w = 50.f;
    a->layout()->h = 70.f;
    a->layout()->scale = 1.f;
    b->layout()->x = 100.f;
    b->layout()->y = 100.f;
    b->layout()->w = 50.f;
    b->layout()->h = 70.f;
    b->layout()->scale = 1.f;
    CHECK_EQ(h->pick(100.f, 100.f), b);  // top-most = last added
    CHECK_EQ(h->pick(300.f, 300.f), nullptr);

    float ox = 0.f, oy = 0.f, ang = 0.f;
    h->slotTransform(0, 1, ox, oy, ang);
    CHECK(std::fabs(ox - cfg->handX) < 1e-3f);
    CHECK(std::fabs(oy - cfg->handY) < 1e-3f);
    CHECK(std::fabs(ang) < 1e-4f);
    h->slotTransform(0, 3, ox, oy, ang);
    CHECK(ang < 0.f);
    h->slotTransform(1, 3, ox, oy, ang);
    CHECK(std::fabs(ang) < 1e-4f);
    h->slotTransform(2, 3, ox, oy, ang);
    CHECK(ang > 0.f);
}

namespace {

void settleHand(Hand *h, const std::vector<Zone *> &zones, std::vector<CardEvent> &out) {
    for (int i = 0; i < 120; ++i) h->update(1.f / 60.f, 0.f, 0.f, false, zones, out);
    out.clear();
}

}  // namespace

TEST_CASE("card.hand.updateClickDragDrop") {
    Card mod;
    mod.registerCardsFromJson(kDefs);
    LayoutConfig *cfg = mod.newConfig();
    cfg->dragThreshold = 4.f;
    cfg->cardW = 50.f;
    cfg->cardH = 70.f;
    cfg->handX = 100.f;
    cfg->handY = 100.f;
    cfg->spacing = 20.f;
    cfg->arcHeight = 0.f;
    cfg->rotationAngle = 0.f;
    Hand *h = mod.newHand(cfg);
    h->meta()->owner = "p1";
    CardData *a = mod.newCard("flame");  // creature
    CardData *b = mod.newCard("bolt");   // spell
    h->addCard(a);
    h->addCard(b);

    Zone *board = mod.newZone("board", "Board", 100.f, 300.f, 200.f, 60.f);
    board->filter()->acceptKinds = {"creature"};
    Zone *spellOnly = mod.newZone("magic", "Magic", 100.f, 400.f, 200.f, 60.f);
    spellOnly->filter()->acceptKinds = {"spell"};
    std::vector<Zone *> zones = {board, spellOnly};
    std::vector<CardEvent> out;
    settleHand(h, zones, out);

    // After settling with two cards: a at (65,100), b at (135,100).
    CHECK(near(a->layout()->x, 65.f, 0.5f));
    CHECK(near(b->layout()->x, 135.f, 0.5f));

    // Click: press + release without moving beyond threshold.
    h->update(1.f / 60.f, 135.f, 100.f, true, zones, out);
    CHECK_EQ(out.size(), 0u);
    h->update(1.f / 60.f, 136.f, 100.f, false, zones, out);
    REQUIRE_EQ(out.size(), 1u);
    CHECK_EQ(out[0].type, std::string("click"));
    CHECK_EQ(out[0].hand, std::string("p1"));
    CHECK(out[0].cardId.find("bolt") == 0);  // instance id is "bolt#<n>"
    out.clear();

    // Drag spell onto the creature-only board -> dropRejected.
    h->update(1.f / 60.f, 135.f, 100.f, true, zones, out);
    h->update(1.f / 60.f, 145.f, 100.f, true, zones, out);
    CHECK_EQ(b->state()->dragging, true);
    h->update(1.f / 60.f, 145.f, 330.f, true, zones, out);
    h->update(1.f / 60.f, 145.f, 330.f, false, zones, out);
    REQUIRE_EQ(out.size(), 1u);
    CHECK_EQ(out[0].type, std::string("dropRejected"));
    CHECK_EQ(out[0].zoneId, std::string("board"));
    CHECK_EQ(out[0].reason, std::string("kind_not_allowed"));
    CHECK(static_cast<int>(b->state()->phase) == static_cast<int>(CardState::Returning));
    out.clear();

    settleHand(h, zones, out);

    // Drag spell onto the spell-only zone -> drop accepted.
    h->update(1.f / 60.f, 135.f, 100.f, true, zones, out);
    h->update(1.f / 60.f, 145.f, 100.f, true, zones, out);
    h->update(1.f / 60.f, 145.f, 430.f, true, zones, out);
    h->update(1.f / 60.f, 145.f, 430.f, false, zones, out);
    REQUIRE_EQ(out.size(), 1u);
    CHECK_EQ(out[0].type, std::string("drop"));
    CHECK_EQ(out[0].zoneId, std::string("magic"));
    out.clear();

    settleHand(h, zones, out);

    // Non-interactive hand: press produces no click/drag events.
    h->meta()->interactive = false;
    h->update(1.f / 60.f, 135.f, 100.f, true, zones, out);
    h->update(1.f / 60.f, 136.f, 100.f, false, zones, out);
    CHECK_EQ(out.size(), 0u);
}
