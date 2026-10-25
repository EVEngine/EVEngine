#include "card/Card.h"
#include "card/CardPlay.h"
#include "card/CardState.h"
#include "common/Capability.h"
#include "common/ConditionKeys.h"
#include "common/ECS.h"
#include "common/GameplayControl.h"
#include "common/GameplayControlJson.h"
#include "common/StateValue.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace eve::card;

namespace {
constexpr auto definitions = R"([{"id":"flame","name":"Flame","kind":"creature","cost":2,"attack":3,"health":2},
                                  {"id":"bolt","name":"Bolt","kind":"spell","cost":3}])";
constexpr auto controlDefinitions = R"([{"id":"free","name":"Free","kind":"spell","cost":0},
                                         {"id":"bolt","name":"Bolt","kind":"spell","cost":3}])";
constexpr auto instanceId         = "00000000-0000-7000-8000-000000000a01";
constexpr auto ownerId            = "00000000-0000-7000-8000-000000000a02";
constexpr auto strangerId         = "00000000-0000-7000-8000-000000000a03";

eve::SubjectRef subject(const char* text) {
    auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

eve::LogicalId action(const char* text) {
    auto id = eve::LogicalId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

const eve::Value* member(const eve::Value& value, const char* key) {
    auto* object = value.getIf<eve::Value::Object>();
    if (!object) return nullptr;
    auto it = object->find(key);
    return it == object->end() ? nullptr : &it->second;
}

std::string cardOf(const eve::Value& state, const char* definition) {
    auto* hand = member(state, "hand");
    if (!hand) return {};
    auto* cards = hand->getIf<eve::Value::Array>();
    if (!cards) return {};
    for (const auto& card : *cards) {
        auto* id  = member(card, "card");
        auto* def = member(card, "definition");
        if (id && def && def->asString() == definition) return id->asString();
    }
    return {};
}

eve::GameplayCommand command(eve::SubjectRef owner, const char* id, const char* verb, std::uint64_t revision,
                             eve::Value parameters = eve::Value(eve::Value::Object{})) {
    eve::GameplayCommand result;
    result.id               = id;
    result.action           = action(verb);
    result.subject          = owner;
    result.observedTick     = eve::SimulationTick::zero();
    result.expectedRevision = revision;
    result.parameters       = std::move(parameters);
    return result;
}

eve::Value cardParameter(std::string id) {
    eve::Value::Object object;
    object.emplace("card", eve::Value(std::move(id)));
    return eve::Value(std::move(object));
}

struct PublishedHand {
    Card*                module   = Card::create();
    Hand*                hand     = nullptr;
    eve::SubjectRef      instance = subject(instanceId);
    eve::SubjectRef      owner    = subject(ownerId);
    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {owner}};
    eve::GameplaySession driver{"driver", eve::GameplayAccess::TestDriver, {owner}};

    PublishedHand() {
        REQUIRE(module != nullptr);
        module->clearGameplayControls();
        module->clearCardDefinitions();
        REQUIRE(module->registerCardsFromJson(controlDefinitions) == 2);
        auto* config = module->newConfig();
        REQUIRE(config != nullptr);
        module->setConfig(config);
        hand = module->newHand(config);
        REQUIRE(hand != nullptr);
        hand->meta()->owner = "player";
        auto* deck          = module->newDeck();
        REQUIRE(deck != nullptr);
        deck->push(module->newCard("free"));
        deck->push(module->newCard("bolt"));
        hand->addCard(module->newCard("free"));
        REQUIRE(module->publishGameplay(instanceId, ownerId, hand).ok());
    }
    ~PublishedHand() {
        module->clearGameplayControls();
        module->clearCardDefinitions();
    }
    eve::IGameplayControlProvider& provider() {
        eve::IGameplayControlProvider* found = nullptr;
        eve::cap::forEach<eve::IGameplayControlProvider>([&](auto* candidate) {
            if (candidate && candidate->gameplayDomain() == "card") found = candidate;
        });
        REQUIRE(found != nullptr);
        return *found;
    }
};

std::string request(const char* operation, const char* access, const std::string& extra = {}) {
    return std::string("{\"schemaId\":\"evengine.gameplay-control-request\",\"schemaVersion\":1,\"op\":\"") +
           operation + "\",\"domain\":\"card\",\"instance\":\"" + instanceId +
           "\",\"session\":{\"id\":\"test\",\"access\":\"" + access + "\",\"controlledSubjects\":[\"" + ownerId +
           "\"]}" + extra + "}";
}

std::string drawJson() {
    return std::string(",\"command\":{\"id\":\"draw\",\"action\":\"card:draw\",\"subject\":\"") + ownerId +
           "\",\"observedTick\":0,\"expectedRevision\":0,\"parameters\":{}}";
}

CardData& newCard(Card& module, const char* id) {
    auto* result = module.newCard(id);
    REQUIRE(result != nullptr);
    return *result;
}
}  // namespace

// Definitions and instances: id lookup and enumeration are stable, while each factory card has a distinct identity.
TEST_CASE("card.definition.registrationMutationAndInvalidId") {
    Card module;
    REQUIRE(module.registerCardsFromJson(definitions) == 2);
    CHECK_EQ(module.getCardDefinitionId(0), std::string("bolt"));
    CHECK_EQ(module.getCardDefinitionId(1), std::string("flame"));
    REQUIRE(module.setCardDefinition("archer", "Archer", "creature", 4, 5, 3, 1.2f, -0.2f, 0.5f));
    CHECK_EQ(module.getCardDefinitionId(0), std::string("archer"));
    CHECK_EQ(module.getCardDefinitionAttack("archer"), 5);
    CHECK_EQ(module.getCardDefinitionTintR("archer"), 1.f);
    CHECK_EQ(module.getCardDefinitionTintG("archer"), 0.f);
    CHECK(!module.setCardDefinition("", "Invalid", "", 0, 0, 0, 0.f, 0.f, 0.f));
    REQUIRE(module.removeCardDefinition("bolt"));
    CHECK(!module.hasCardDefinition("bolt"));
    CHECK(!module.removeCardDefinition("bolt"));
}

TEST_CASE("card.instance.uniqueIdsDescriptionAndPresentationSnapshot") {
    Card module;
    REQUIRE(module.registerCardsFromJson(definitions) == 2);
    auto& first  = newCard(module, "flame");
    auto& second = newCard(module, "flame");
    CHECK_EQ(first.identity()->definitionId, std::string("flame"));
    CHECK(first.identity()->id != second.identity()->id);
    CHECK(first.describe().find("Flame") != std::string::npos);
    CHECK(first.describe().find("3/2") != std::string::npos);
    first.layout()->x     = 42.f;
    first.layout()->angle = 12.f;
    CHECK_EQ(module.capturePresentation(), 2);
    auto* snapshot = module.getPresentation(0);
    REQUIRE(snapshot != nullptr);
    CHECK_EQ(snapshot->instanceId, first.identity()->id);
    CHECK_EQ(snapshot->x, 42.f);
    CHECK_EQ(snapshot->angle, 12.f);
    CHECK(module.getPresentation(2) == nullptr);
}

TEST_CASE("card.presentation.layoutRayMappingAndParallelMiss") {
    CardPlaneMapper mapper;
    mapper.setLogicalRect(0.f, 0.f, 1280.f, 700.f);
    mapper.setPlane(-6.4f, 0.6f, -7.f, 12.8f, 0.f, 0.f, 0.f, 0.f, 14.f);
    REQUIRE(mapper.mapLayout(640.f, 350.f));
    CHECK_LT(std::fabs(mapper.getWorldX()), 0.0001f);
    CHECK_LT(std::fabs(mapper.getWorldY() - 0.6f), 0.0001f);
    REQUIRE(mapper.mapRay(0.f, 10.f, 0.f, 0.f, -1.f, 0.f));
    CHECK_LT(std::fabs(mapper.getLogicalX() - 640.f), 0.0001f);
    CHECK_LT(std::fabs(mapper.getLogicalY() - 350.f), 0.0001f);
    CHECK(!mapper.mapRay(0.f, 10.f, 0.f, 1.f, 0.f, 0.f));
}

TEST_CASE("card.instance.hitBoundsScaleAndZeroWidth") {
    Card module;
    REQUIRE(module.registerCardsFromJson(definitions) == 2);
    auto& card           = newCard(module, "flame");
    card.layout()->x     = 100.f;
    card.layout()->y     = 100.f;
    card.layout()->w     = 50.f;
    card.layout()->h     = 70.f;
    card.layout()->scale = 1.f;
    CHECK(card.hit(100.f, 100.f));
    CHECK(card.hit(125.f, 135.f));
    CHECK(!card.hit(126.f, 100.f));
    card.layout()->scale = 2.f;
    CHECK(card.hit(140.f, 100.f));
    card.layout()->w = 0.f;
    CHECK(!card.hit(100.f, 100.f));
}

// Containers: a deck is LIFO; draw transfers ownership to the named hand.
TEST_CASE("card.deck.emptyBoundsLifoShuffleAndClear") {
    Card module;
    REQUIRE(module.registerCardsFromJson(definitions) == 2);
    auto* deck = module.newDeck();
    REQUIRE(deck != nullptr);
    CHECK(deck->isEmpty());
    CHECK(deck->draw() == nullptr);
    CHECK(deck->get(-1) == nullptr);
    auto& flame = newCard(module, "flame");
    auto& bolt  = newCard(module, "bolt");
    deck->push(&flame);
    deck->push(&bolt);
    CHECK(deck->peek() == &bolt);
    CHECK(deck->get(0) == &flame);
    CHECK(deck->get(2) == nullptr);
    deck->shuffle();
    CHECK_EQ(deck->count(), 2);
    CHECK(deck->get(0) != deck->get(1));
    deck->clear();
    CHECK(deck->isEmpty());
    deck->push(&flame);
    deck->push(&bolt);
    CHECK(deck->draw() == &bolt);
    CHECK(deck->draw() == &flame);
    CHECK(deck->draw() == nullptr);
}

TEST_CASE("card.hand.addRemoveFindAndDraw") {
    Card module;
    REQUIRE(module.registerCardsFromJson(definitions) == 2);
    auto* config = module.newConfig();
    REQUIRE(config != nullptr);
    auto* hand = module.newHand(config);
    REQUIRE(hand != nullptr);
    hand->meta()->owner = "player";
    auto* deck          = module.newDeck();
    REQUIRE(deck != nullptr);
    auto& flame = newCard(module, "flame");
    auto& bolt  = newCard(module, "bolt");
    hand->addCard(&flame);
    CHECK(hand->get(0) == &flame);
    CHECK(hand->find(flame.identity()->id) == &flame);
    CHECK(module.findHand("player") == hand);
    CHECK(static_cast<int>(flame.state()->phase) == static_cast<int>(CardState::Hand));
    CHECK_EQ(flame.layout()->w, config->cardW);
    CHECK(hand->get(5) == nullptr);
    deck->push(&bolt);
    CHECK(module.drawCard("player") == &bolt);
    CHECK(deck->isEmpty());
    CHECK_EQ(hand->count(), 2);
    CHECK(!hand->removeCard(&newCard(module, "flame")));
    REQUIRE(hand->removeCard(&flame));
    CHECK_EQ(hand->count(), 1);
    hand->clear();
    CHECK_EQ(hand->count(), 0);
}

TEST_CASE("card.zone.rectangleKindFilterAndDisabledState") {
    Card module;
    REQUIRE(module.registerCardsFromJson(definitions) == 2);
    auto* zone = module.newZone("board", "Board", 10.f, 20.f, 100.f, 60.f);
    REQUIRE(zone != nullptr);
    auto& creature = newCard(module, "flame");
    auto& spell    = newCard(module, "bolt");
    CHECK(zone->contains(10.f, 20.f));
    CHECK(zone->contains(109.f, 79.f));
    CHECK(!zone->contains(9.f, 20.f));
    CHECK(zone->accepts(&creature));
    CHECK(zone->accepts(&spell));
    CHECK(!zone->accepts(nullptr));
    zone->filter()->acceptKinds = {"spell"};
    CHECK(!zone->accepts(&creature));
    CHECK(zone->accepts(&spell));
    zone->rect()->enabled = false;
    CHECK(!zone->accepts(&spell));
}

TEST_CASE("card.hand.pickTopmostAndSymmetricFanSlots") {
    Card module;
    REQUIRE(module.registerCardsFromJson(definitions) == 2);
    auto* config = module.newConfig();
    auto* hand   = module.newHand(config);
    auto& first  = newCard(module, "flame");
    auto& last   = newCard(module, "bolt");
    hand->addCard(&first);
    hand->addCard(&last);
    for (auto* card : {&first, &last}) {
        card->layout()->x     = 100.f;
        card->layout()->y     = 100.f;
        card->layout()->w     = 50.f;
        card->layout()->h     = 70.f;
        card->layout()->scale = 1.f;
    }
    CHECK(hand->pick(100.f, 100.f) == &last);
    CHECK(hand->pick(300.f, 300.f) == nullptr);
    float x = 0.f, y = 0.f, angle = 0.f;
    hand->slotTransform(0, 3, x, y, angle);
    CHECK_LT(angle, 0.f);
    hand->slotTransform(1, 3, x, y, angle);
    CHECK_LT(std::fabs(angle), 0.0001f);
    hand->slotTransform(2, 3, x, y, angle);
    CHECK_GT(angle, 0.f);
}

// Persistence: durable phase restores; transient pointer interaction is dropped.
TEST_CASE("card.state.restoreDurablePhasesAndClearTransientDrag") {
    Card module;
    REQUIRE(module.registerCardsFromJson(definitions) == 2);
    auto& flame            = newCard(module, "flame");
    auto& bolt             = newCard(module, "bolt");
    flame.state()->phase   = CardState::Played;
    bolt.state()->phase    = CardState::Disabled;
    bolt.state()->dragging = true;
    eve::StateValue snapshot;
    REQUIRE(captureCardState(snapshot));
    flame.state()->phase = CardState::Deck;
    bolt.state()->phase  = CardState::Hand;
    std::string error;
    REQUIRE(restoreCardState(snapshot, &error));
    CHECK(error.empty());
    CHECK(flame.state()->phase == CardState::Played);
    CHECK(bolt.state()->phase == CardState::Disabled);
    CHECK(!bolt.state()->dragging);
    REQUIRE(resetCardState());
    CHECK(flame.state()->phase == CardState::Deck);
    CHECK(bolt.state()->phase == CardState::Deck);
    flame.release();
    bolt.release();
}

TEST_CASE("card.state.malformedRestoreReturnsError") {
    std::string error;
    CHECK(!restoreCardState(eve::StateValue::object(), &error));
    REQUIRE(!error.empty());
}

// Play conditions combine card state with explicitly supplied external capabilities.
TEST_CASE("card.playCondition.localStateAndExternalQueries") {
    using namespace eve::condition;
    Card module;
    REQUIRE(module.registerCardsFromJson(R"([{"id":"fire","kind":"spell","cost":3,"tags":["fire"]}])") == 1);
    auto& card          = newCard(module, "fire");
    card.state()->phase = CardState::Hand;
    CardDefinition definition;
    definition.id   = "fire";
    definition.kind = "spell";
    definition.cost = 3;
    definition.tags = {"fire"};
    CardPlayConditionQueries queries;
    queries.resource = [](std::string_view key) -> std::optional<eve::Value> {
        return key == "mana" ? std::optional<eve::Value>(eve::Value(5)) : std::nullopt;
    };
    queries.authority = [](std::string_view scope) -> std::optional<bool> { return scope == "play"; };
    int calls         = 0;
    queries.policy    = [&calls](std::string_view, const eve::Value&) -> std::optional<ConditionResult> {
        ++calls;
        return ConditionResult::success();
    };
    auto condition = Condition::all({tagPresent("fire"), Condition::compare("card.cost", CompareOperator::LessEqual, 3),
                                     resourceExists("mana"), stateEquals("card.phase", "hand"), authorityAllow("play"),
                                     Condition::policyCall("safe")});
    CHECK(CardPlayConditionAdapter::evaluate(&card, definition, condition, queries).passed());
    CHECK_EQ(calls, 1);
    card.state()->phase = CardState::Played;
    auto rejected = CardPlayConditionAdapter::evaluate(&card, definition, stateEquals("card.phase", "hand"), queries);
    CHECK(!rejected.passed());
    CHECK(static_cast<int>(rejected.reasonCode()) == static_cast<int>(ConditionReasonCode::ValueMismatch));
}

TEST_CASE("card.playCondition.definitionStoredRuleUsesInjectedResource") {
    using namespace eve::condition;
    Card module;
    REQUIRE(module.registerCardsFromJson(R"([{"id":"ward","kind":"spell","cost":2,"tags":["ward"]}])") == 1);
    auto& card          = newCard(module, "ward");
    card.state()->phase = CardState::Hand;
    REQUIRE(module.setCardPlayCondition("ward", tagPresent("missing")).ok());
    CHECK(!module.evaluatePlay(&card).passed());
    REQUIRE(module.setCardPlayCondition("ward", resourceExists("mana")).ok());
    CardPlayConditionQueries queries;
    queries.resource = [](std::string_view key) -> std::optional<eve::Value> {
        return key == "mana" ? std::optional<eve::Value>(eve::Value(1)) : std::nullopt;
    };
    CHECK(module.evaluatePlay(&card, queries).passed());
}

// Published hands expose state and actions with owner/access/revision checks.
TEST_CASE("card.gameplay.observeAndActionVocabularyRespectAccess") {
    PublishedHand fixture;
    auto&         provider = fixture.provider();
    auto          observed = provider.observeGameplay(fixture.player, fixture.instance);
    REQUIRE(observed.ok());
    CHECK_EQ(member(observed.value().state, "owner")->asString(), std::string("player"));
    CHECK_EQ(member(observed.value().state, "handSize")->asInt(), std::int64_t{1});
    CHECK_EQ(member(observed.value().state, "deck")->asInt(), std::int64_t{2});
    CHECK_EQ(member(observed.value().state, "payment")->asString(), std::string("unbound"));
    auto player = provider.availableGameplayActions(fixture.player, fixture.instance, fixture.owner);
    auto driver = provider.availableGameplayActions(fixture.driver, fixture.instance, fixture.owner);
    REQUIRE(player.ok());
    REQUIRE(driver.ok());
    CHECK_EQ(player.value().size(), size_t{2});
    CHECK_EQ(driver.value().size(), size_t{3});
    CHECK_EQ(player.value()[0].id.format(), std::string("card:draw"));
    CHECK_EQ(player.value()[1].id.format(), std::string("card:play"));
    eve::GameplaySession stranger{"stranger", eve::GameplayAccess::PlayerEquivalent, {subject(strangerId)}};
    CHECK(!provider.observeGameplay(stranger, fixture.instance).ok());
    CHECK(!provider.availableGameplayActions(fixture.player, fixture.instance, subject(strangerId)).ok());
}

TEST_CASE("card.gameplay.drawFreePlayAndPaidBoundary") {
    PublishedHand fixture;
    auto&         provider = fixture.provider();
    auto          drawn =
        provider.submitGameplay(fixture.player, fixture.instance, command(fixture.owner, "draw", "card:draw", 0));
    REQUIRE(drawn.ok());
    CHECK_EQ(drawn.value().resultingRevision, std::uint64_t{1});
    CHECK_EQ(fixture.hand->count(), 2);
    auto state = provider.observeGameplay(fixture.player, fixture.instance);
    REQUIRE(state.ok());
    CHECK_EQ(member(state.value().state, "deck")->asInt(), std::int64_t{1});
    auto freeId = cardOf(state.value().state, "free");
    REQUIRE(!freeId.empty());
    auto played = provider.submitGameplay(fixture.player, fixture.instance,
                                          command(fixture.owner, "play", "card:play", 1, cardParameter(freeId)));
    REQUIRE(played.ok());
    CHECK_EQ(played.value().resultingRevision, std::uint64_t{2});
    state = provider.observeGameplay(fixture.player, fixture.instance);
    REQUIRE(state.ok());
    auto boltId = cardOf(state.value().state, "bolt");
    if (!boltId.empty()) {
        CHECK(!provider
                   .submitGameplay(fixture.player, fixture.instance,
                                   command(fixture.owner, "paid", "card:play", 2, cardParameter(boltId)))
                   .ok());
    }
    CHECK(!provider
               .submitGameplay(fixture.player, fixture.instance,
                               command(fixture.owner, "missing", "card:play", 2, cardParameter("unknown")))
               .ok());
    CHECK(!provider.submitGameplay(fixture.player, fixture.instance, command(fixture.owner, "stale", "card:draw", 0))
               .ok());
    auto events = provider.gameplayEvents(fixture.player, fixture.instance, 0);
    REQUIRE(events.ok());
    REQUIRE(events.value().size() == size_t{2});
    CHECK_EQ(events.value()[0].type, std::string("card.drawn"));
    CHECK_EQ(events.value()[1].type, std::string("card.played"));
}

TEST_CASE("card.gameplay.attributeRewriteRequiresTestDriver") {
    PublishedHand fixture;
    auto&         provider = fixture.provider();
    auto          state    = provider.observeGameplay(fixture.player, fixture.instance);
    REQUIRE(state.ok());
    auto id = cardOf(state.value().state, "free");
    REQUIRE(!id.empty());
    eve::Value::Object parameters;
    parameters.emplace("card", eve::Value(id));
    parameters.emplace("attribute", eve::Value("attack"));
    parameters.emplace("value", eve::Value(5.0));
    auto value = eve::Value(parameters);
    CHECK(!provider
               .submitGameplay(fixture.player, fixture.instance,
                               command(fixture.owner, "denied", "card:set-attribute", 0, value))
               .ok());
    auto applied = provider.submitGameplay(fixture.driver, fixture.instance,
                                           command(fixture.owner, "edit", "card:set-attribute", 0, value));
    REQUIRE(applied.ok());
    CHECK_EQ(applied.value().resultingRevision, std::uint64_t{1});
    state = provider.observeGameplay(fixture.player, fixture.instance);
    REQUIRE(state.ok());
    auto* hand = member(state.value().state, "hand")->getIf<eve::Value::Array>();
    REQUIRE(hand != nullptr);
    CHECK_EQ(member((*hand)[0], "attack")->asInt(), std::int64_t{5});
}

TEST_CASE("card.gameplay.jsonFacadeRoutesObservationAndDraw") {
    PublishedHand fixture;
    auto          observed = eve::executeGameplayControlJson(request("observe", "player"));
    REQUIRE(observed.ok());
    CHECK(observed.value().find("\"payment\":\"unbound\"") != std::string::npos);
    auto actions =
        eve::executeGameplayControlJson(request("actions", "player", std::string(",\"subject\":\"") + ownerId + "\""));
    REQUIRE(actions.ok());
    CHECK(actions.value().find("card:draw") != std::string::npos);
    auto drawn = eve::executeGameplayControlJson(request("submit", "player", drawJson()));
    REQUIRE(drawn.ok());
    CHECK(drawn.value().find("\"resultingRevision\":1") != std::string::npos);
    auto events = eve::executeGameplayControlJson(request("events", "player", ",\"afterSequence\":0"));
    REQUIRE(events.ok());
    CHECK(events.value().find("card.drawn") != std::string::npos);
}

// Pointer interaction distinguishes a click, a rejected zone and an accepted drop.
TEST_CASE("card.hand.pointerClickRejectedDropAndAcceptedDrop") {
    Card module;
    REQUIRE(module.registerCardsFromJson(definitions) == 2);
    auto* config          = module.newConfig();
    config->dragThreshold = 4.f;
    config->cardW         = 50.f;
    config->cardH         = 70.f;
    config->handX         = 100.f;
    config->handY         = 100.f;
    config->spacing       = 20.f;
    config->arcHeight     = 0.f;
    config->rotationAngle = 0.f;
    auto* hand            = module.newHand(config);
    hand->meta()->owner   = "player";
    hand->addCard(&newCard(module, "flame"));
    auto& spell = newCard(module, "bolt");
    hand->addCard(&spell);
    auto* board                  = module.newZone("board", "Board", 100.f, 300.f, 200.f, 60.f);
    board->filter()->acceptKinds = {"creature"};
    auto* magic                  = module.newZone("magic", "Magic", 100.f, 400.f, 200.f, 60.f);
    magic->filter()->acceptKinds = {"spell"};
    std::vector<Zone*>     zones{board, magic};
    std::vector<CardEvent> events;
    for (int i = 0; i < 120; ++i) hand->update(1.f / 60.f, 0.f, 0.f, false, zones, events);
    events.clear();
    CHECK_LT(std::fabs(spell.layout()->x - 135.f), 0.5f);

    hand->update(1.f / 60.f, 135.f, 100.f, true, zones, events);
    hand->update(1.f / 60.f, 136.f, 100.f, false, zones, events);
    REQUIRE(events.size() == size_t{1});
    CHECK_EQ(events[0].type, std::string("click"));
    CHECK_EQ(events[0].hand, std::string("player"));
    events.clear();

    hand->update(1.f / 60.f, 135.f, 100.f, true, zones, events);
    hand->update(1.f / 60.f, 145.f, 100.f, true, zones, events);
    CHECK(spell.state()->dragging);
    hand->update(1.f / 60.f, 145.f, 330.f, true, zones, events);
    hand->update(1.f / 60.f, 145.f, 330.f, false, zones, events);
    REQUIRE(events.size() == size_t{1});
    CHECK_EQ(events[0].type, std::string("dropRejected"));
    CHECK_EQ(events[0].reason, std::string("kind_not_allowed"));
    CHECK(spell.state()->phase == CardState::Returning);
    events.clear();
    for (int i = 0; i < 120; ++i) hand->update(1.f / 60.f, 0.f, 0.f, false, zones, events);
    events.clear();

    hand->update(1.f / 60.f, 135.f, 100.f, true, zones, events);
    hand->update(1.f / 60.f, 145.f, 100.f, true, zones, events);
    hand->update(1.f / 60.f, 145.f, 430.f, true, zones, events);
    hand->update(1.f / 60.f, 145.f, 430.f, false, zones, events);
    REQUIRE(events.size() == size_t{1});
    CHECK_EQ(events[0].type, std::string("drop"));
    CHECK_EQ(events[0].zoneId, std::string("magic"));
}
