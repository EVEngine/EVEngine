#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_card_gameplay_control) || \
    defined(EVE_TEST_ORIGIN_card_reload)
#include "PrivateTestScope.h"
#include <cstdint>
#include <initializer_list>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <utility>
#include "card/Card.h"
#include "card/CardState.h"
#include "card/CardTypes.h"
#include "common/Capability.h"
#include "common/GameplayControl.h"
#include "common/GameplayControlJson.h"
#include "common/GameplayInstanceCatalog.h"
#include "common/Module.h"
#include "common/StateValue.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// Responsibility: card_hand_rules
namespace test_card_hand_rules {
// card_gameplay_control.cpp
namespace {
namespace card_gameplay_control {
// 卡牌玩法域契约测试：真实 Card 模块 + 手牌/牌库走共享玩法协议，
// 覆盖发现、手牌观察、抽取、出牌支付边界、作弊档位、事件与 JSON 门面路由。
//
// 注意：zeroerr 的 CHECK_EQ(lhs, rhs) 会把 lhs 求值两次（一次判等、一次打印），
// 因此有副作用的调用必须先赋给局部变量，再断言变量。

namespace {

constexpr const char* kInstanceId = "00000000-0000-7000-8000-000000000a01";
constexpr const char* kOwnerId    = "00000000-0000-7000-8000-000000000a02";
constexpr const char* kStrangerId = "00000000-0000-7000-8000-000000000a03";

constexpr const char* kDefinitions = R"JSON(
[{"id":"free","name":"Free","kind":"spell","cost":0},
 {"id":"bolt","name":"Bolt","kind":"spell","cost":3}]
)JSON";

[[maybe_unused]] eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

[[maybe_unused]] eve::LogicalId action(const char* text) {
    const auto id = eve::LogicalId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

[[maybe_unused]] eve::Value object(std::initializer_list<std::pair<const std::string, eve::Value>> entries) {
    return eve::Value(eve::Value::Object(entries));
}

[[maybe_unused]] const eve::Value* member(const eve::Value& value, const char* name) {
    const auto* entries = value.getIf<eve::Value::Object>();
    if (entries == nullptr) return nullptr;
    const auto found = entries->find(name);
    return found == entries->end() ? nullptr : &found->second;
}

[[maybe_unused]] const eve::Value::Array* handCards(const eve::Value& state) {
    const eve::Value* hand = member(state, "hand");
    return hand == nullptr ? nullptr : hand->getIf<eve::Value::Array>();
}

/** 手牌里第一张指定定义的卡牌实例 id；找不到返回空串。 */
[[maybe_unused]] std::string firstCardOf(const eve::Value& state, const char* definition) {
    const auto* cards = handCards(state);
    if (cards == nullptr) return {};
    for (const auto& card : *cards) {
        const eve::Value* def = member(card, "definition");
        const eve::Value* id  = member(card, "card");
        if (def != nullptr && id != nullptr && def->asString() == definition) return id->asString();
    }
    return {};
}

/** 真实模块 + 手牌 + 牌库，并把该手牌发布到共享玩法协议（未绑定支付账户）。 */
struct CardFixture {
    eve::card::Card*         module = nullptr;
    eve::card::Hand*         hand   = nullptr;
    eve::card::LayoutConfig* config = nullptr;
    eve::SubjectRef          instance;
    eve::SubjectRef          owner;

    CardFixture() {
        module = eve::card::Card::create();
        REQUIRE(module != nullptr);
        module->clearGameplayControls();
        module->clearCardDefinitions();
        const int registered = module->registerCardsFromJson(kDefinitions);
        REQUIRE_EQ(registered, 2);
        config = module->newConfig();
        REQUIRE(config != nullptr);
        module->setConfig(config);
        hand = module->newHand(config);
        REQUIRE(hand != nullptr);
        hand->meta()->owner   = "player";
        eve::card::Deck* deck = module->newDeck();
        REQUIRE(deck != nullptr);
        deck->push(module->newCard("free"));
        deck->push(module->newCard("bolt"));
        eve::card::CardData* inHand = module->newCard("free");
        REQUIRE(inHand != nullptr);
        hand->addCard(inHand);
        instance             = subject(kInstanceId);
        owner                = subject(kOwnerId);
        const auto published = module->publishGameplay(kInstanceId, kOwnerId, hand);
        REQUIRE(published.ok());
    }

    ~CardFixture() {
        module->clearGameplayControls();
        module->clearCardDefinitions();
    }

    CardFixture(const CardFixture&)            = delete;
    CardFixture& operator=(const CardFixture&) = delete;

    /** 通过能力注册表取回适配器，和 JSON 路由走同一条查找路径。 */
    eve::IGameplayControlProvider* provider() const {
        eve::IGameplayControlProvider* found = nullptr;
        eve::cap::forEach<eve::IGameplayControlProvider>([&](auto* candidate) {
            if (candidate != nullptr && candidate->gameplayDomain() == "card") found = candidate;
        });
        return found;
    }
};

[[maybe_unused]] eve::GameplayCommand command(const CardFixture& fixture, const char* id, const char* actionText,
                                              std::uint64_t expectedRevision, eve::Value parameters) {
    eve::GameplayCommand result;
    result.id               = id;
    result.action           = action(actionText);
    result.subject          = fixture.owner;
    result.observedTick     = eve::SimulationTick::zero();
    result.expectedRevision = expectedRevision;
    result.parameters       = std::move(parameters);
    return result;
}

[[maybe_unused]] std::string envelope(const char* operation, const char* access, const std::string& extra = {}) {
    return std::string("{\"schemaId\":\"evengine.gameplay-control-request\",\"schemaVersion\":1,\"op\":\"") +
           operation + "\",\"domain\":\"card\",\"instance\":\"" + kInstanceId +
           "\",\"session\":{\"id\":\"mcp\",\"access\":\"" + access + "\",\"controlledSubjects\":[\"" + kOwnerId +
           "\"]}" + extra + "}";
}

[[maybe_unused]] std::string jsonCommand(const char* id, const char* actionText, std::uint64_t expectedRevision,
                                         const char* parameters) {
    return std::string(",\"command\":{\"id\":\"") + id + "\",\"action\":\"" + actionText + "\",\"subject\":\"" +
           kOwnerId + "\",\"observedTick\":0,\"expectedRevision\":" + std::to_string(expectedRevision) +
           ",\"parameters\":" + parameters + "}";
}

}  // namespace

TEST_CASE("gameplay.control.cardPublishesHandAndPaymentBoundary") {
    CardFixture fixture;
    auto*       provider = fixture.provider();
    REQUIRE(provider != nullptr);
    CHECK_EQ(fixture.module->gameplayControlCount(), 1);

    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {fixture.owner}};
    eve::GameplaySession automation{"automation", eve::GameplayAccess::TestDriver, {fixture.owner}};

    auto observed = provider->observeGameplay(player, fixture.instance);
    REQUIRE(observed.ok());
    const auto state = std::move(observed).takeValue().state;
    CHECK_EQ(member(state, "owner")->asString(), std::string("player"));
    CHECK_EQ(member(state, "handSize")->asInt(), std::int64_t{1});
    CHECK_EQ(member(state, "deck")->asInt(), std::int64_t{2});
    // 未绑定支付账户是可见的，而不是让 card:play 静默消失。
    CHECK_EQ(member(state, "payment")->asString(), std::string("unbound"));
    const auto* cards = handCards(state);
    REQUIRE(cards != nullptr);
    REQUIRE_EQ(static_cast<int>(cards->size()), 1);
    CHECK_EQ(member((*cards)[0], "definition")->asString(), std::string("free"));
    CHECK_EQ(member((*cards)[0], "cost")->asInt(), std::int64_t{0});
    CHECK_EQ(member((*cards)[0], "state")->asString(), std::string("hand"));

    auto playerActions     = provider->availableGameplayActions(player, fixture.instance, fixture.owner);
    auto automationActions = provider->availableGameplayActions(automation, fixture.instance, fixture.owner);
    REQUIRE(playerActions.ok());
    REQUIRE(automationActions.ok());
    CHECK_EQ(static_cast<int>(playerActions.value().size()), 2);
    CHECK_EQ(playerActions.value()[0].id.format(), std::string("card:draw"));
    CHECK_EQ(playerActions.value()[1].id.format(), std::string("card:play"));
    CHECK_EQ(static_cast<int>(automationActions.value().size()), 3);
    bool automationMayRewrite = false;
    for (const auto& descriptor : automationActions.value()) {
        if (descriptor.id.format() == "card:set-attribute") automationMayRewrite = true;
    }
    CHECK(automationMayRewrite);

    auto wrongSubject = provider->availableGameplayActions(player, fixture.instance, subject(kStrangerId));
    CHECK(!wrongSubject.ok());
    CHECK_EQ(wrongSubject.code(), eve::StatusCode::Rejected);

    eve::GameplaySession intruder{"intruder", eve::GameplayAccess::PlayerEquivalent, {subject(kStrangerId)}};
    auto                 denied = provider->observeGameplay(intruder, fixture.instance);
    CHECK(!denied.ok());
    CHECK_EQ(denied.code(), eve::StatusCode::Rejected);

    auto missing = provider->observeGameplay(automation, subject(kStrangerId));
    CHECK(!missing.ok());
    CHECK_EQ(missing.code(), eve::StatusCode::NotFound);
}

TEST_CASE("gameplay.control.cardDrawsPlaysAndReportsItsLedger") {
    CardFixture fixture;
    auto*       provider = fixture.provider();
    REQUIRE(provider != nullptr);
    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {fixture.owner}};

    auto drawn =
        provider->submitGameplay(player, fixture.instance, command(fixture, "draw-1", "card:draw", 0, object({})));
    REQUIRE(drawn.ok());
    CHECK_EQ(drawn.value().resultingRevision, std::uint64_t{1});
    CHECK_EQ(fixture.hand->count(), 2);

    const auto afterDraw = provider->observeGameplay(player, fixture.instance);
    REQUIRE(afterDraw.ok());
    CHECK_EQ(member(afterDraw.value().state, "handSize")->asInt(), std::int64_t{2});
    CHECK_EQ(member(afterDraw.value().state, "deck")->asInt(), std::int64_t{1});
    const std::string freeCard = firstCardOf(afterDraw.value().state, "free");
    REQUIRE(!freeCard.empty());

    // 零费卡在没有支付账户时也能出牌：未绑定边界对零费卡是透明的。
    auto played = provider->submitGameplay(
        player, fixture.instance, command(fixture, "play-1", "card:play", 1, object({{"card", eve::Value(freeCard)}})));
    REQUIRE(played.ok());
    CHECK_EQ(played.value().resultingRevision, std::uint64_t{2});
    CHECK_EQ(member(played.value().details, "quantity")->asInt(), std::int64_t{1});

    // 收费卡在没有账户时得到明确诊断，而不是"动作不存在"。
    const auto afterPlay = provider->observeGameplay(player, fixture.instance);
    REQUIRE(afterPlay.ok());
    const std::string boltCard = firstCardOf(afterPlay.value().state, "bolt");
    if (!boltCard.empty()) {
        auto charged = provider->submitGameplay(
            player, fixture.instance,
            command(fixture, "play-bolt", "card:play", 2, object({{"card", eve::Value(boltCard)}})));
        CHECK(!charged.ok());
        CHECK_EQ(charged.code(), eve::StatusCode::Rejected);
    }

    auto unknownCard = provider->submitGameplay(
        player, fixture.instance,
        command(fixture, "play-missing", "card:play", 2, object({{"card", eve::Value("no.such.card")}})));
    CHECK(!unknownCard.ok());
    CHECK_EQ(unknownCard.code(), eve::StatusCode::NotFound);

    auto unsupported =
        provider->submitGameplay(player, fixture.instance, command(fixture, "shrug", "card:shrug", 2, object({})));
    CHECK(!unsupported.ok());
    CHECK_EQ(unsupported.code(), eve::StatusCode::Unsupported);

    auto stale =
        provider->submitGameplay(player, fixture.instance, command(fixture, "draw-stale", "card:draw", 0, object({})));
    CHECK(!stale.ok());
    CHECK_EQ(stale.code(), eve::StatusCode::Conflict);

    auto events = provider->gameplayEvents(player, fixture.instance, 0);
    REQUIRE(events.ok());
    const auto list = std::move(events).takeValue();
    REQUIRE_EQ(static_cast<int>(list.size()), 2);
    CHECK_EQ(list[0].type, std::string("card.drawn"));
    CHECK_EQ(list[1].type, std::string("card.played"));
    CHECK_EQ(list[1].causationCommandId, std::string("play-1"));
    CHECK_EQ(list[1].subject.format(), std::string(kOwnerId));

    auto drained = provider->gameplayEvents(player, fixture.instance, 2);
    REQUIRE(drained.ok());
    CHECK_EQ(drained.code(), eve::StatusCode::NoOp);

    auto advanced = provider->advanceGameplay(player, fixture.instance,
                                              {eve::SimulationTick(1), eve::Duration::fromNanoseconds(16666667)});
    REQUIRE(advanced.ok());
    auto rewound = provider->advanceGameplay(player, fixture.instance,
                                             {eve::SimulationTick(1), eve::Duration::fromNanoseconds(1)});
    CHECK(!rewound.ok());
    CHECK_EQ(rewound.code(), eve::StatusCode::Conflict);
}

TEST_CASE("gameplay.control.cardAttributeRewriteIsACheatProfileAction") {
    CardFixture fixture;
    auto*       provider = fixture.provider();
    REQUIRE(provider != nullptr);
    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {fixture.owner}};
    eve::GameplaySession automation{"automation", eve::GameplayAccess::TestDriver, {fixture.owner}};

    const auto observed = provider->observeGameplay(player, fixture.instance);
    REQUIRE(observed.ok());
    const std::string card = firstCardOf(observed.value().state, "free");
    REQUIRE(!card.empty());

    auto refused = provider->submitGameplay(
        player, fixture.instance,
        command(fixture, "cheat-refused", "card:set-attribute", 0,
                object({{"card", eve::Value(card)}, {"attribute", eve::Value("attack")}, {"value", eve::Value(5.0)}})));
    CHECK(!refused.ok());
    CHECK_EQ(refused.code(), eve::StatusCode::Rejected);

    auto applied = provider->submitGameplay(
        automation, fixture.instance,
        command(fixture, "cheat-applied", "card:set-attribute", 0,
                object({{"card", eve::Value(card)}, {"attribute", eve::Value("attack")}, {"value", eve::Value(5.0)}})));
    REQUIRE(applied.ok());
    CHECK_EQ(applied.value().resultingRevision, std::uint64_t{1});

    const auto after = provider->observeGameplay(player, fixture.instance);
    REQUIRE(after.ok());
    const auto* cards = handCards(after.value().state);
    REQUIRE(cards != nullptr);
    REQUIRE_EQ(static_cast<int>(cards->size()), 1);
    CHECK_EQ(member((*cards)[0], "attack")->asInt(), std::int64_t{5});

    auto noCard = provider->submitGameplay(automation, fixture.instance,
                                           command(fixture, "cheat-missing", "card:set-attribute", 1,
                                                   object({{"card", eve::Value("no.such.card")},
                                                           {"attribute", eve::Value("attack")},
                                                           {"value", eve::Value(1.0)}})));
    CHECK(!noCard.ok());
    CHECK_EQ(noCard.code(), eve::StatusCode::NotFound);
}

TEST_CASE("gameplay.control.cardRoutesThroughTheSharedJsonFacade") {
    CardFixture fixture;

    auto catalogs = eve::executeGameplayControlJson(
        R"({"schemaId":"evengine.gameplay-control-request","schemaVersion":1,"op":"instances"})");
    REQUIRE(catalogs.ok());
    CHECK(catalogs.value().find("\"domain\":\"card\"") != std::string::npos);
    CHECK(catalogs.value().find(kInstanceId) != std::string::npos);

    auto observed = eve::executeGameplayControlJson(envelope("observe", "player"));
    REQUIRE(observed.ok());
    CHECK(observed.value().find("gameplay:card") != std::string::npos);
    CHECK(observed.value().find("\"payment\":\"unbound\"") != std::string::npos);
    CHECK(observed.value().find("\"definition\":\"free\"") != std::string::npos);

    const std::string actor   = std::string(",\"subject\":\"") + kOwnerId + "\"";
    auto              actions = eve::executeGameplayControlJson(envelope("actions", "player", actor));
    REQUIRE(actions.ok());
    CHECK(actions.value().find("card:draw") != std::string::npos);
    CHECK(actions.value().find("card:set-attribute") == std::string::npos);

    auto drawn = eve::executeGameplayControlJson(
        envelope("submit", "player", jsonCommand("json-draw-1", "card:draw", 0, R"({})")));
    REQUIRE(drawn.ok());
    CHECK(drawn.value().find("\"resultingRevision\":1") != std::string::npos);

    auto events = eve::executeGameplayControlJson(envelope("events", "player", ",\"afterSequence\":0"));
    REQUIRE(events.ok());
    CHECK(events.value().find("card.drawn") != std::string::npos);

    auto advanced =
        eve::executeGameplayControlJson(envelope("advance", "player", ",\"tick\":1,\"deltaNanoseconds\":16666667"));
    REQUIRE(advanced.ok());
    CHECK(advanced.value().find("\"tick\":1") != std::string::npos);
}
}  // namespace card_gameplay_control
}  // namespace

// card_reload.cpp
namespace {
namespace card_reload {
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
}  // namespace card_reload
}  // namespace
}  // namespace test_card_hand_rules
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_card_play_condition)
#include "PrivateTestScope.h"
#include <optional>
#include "card/Card.h"
#include "card/CardPlay.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// card_play_condition.cpp
namespace test_card_play_condition {
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
}  // namespace test_card_play_condition
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_card)
#include <cmath>
#include "PrivateTestScope.h"
#include "ScriptTest.h"
#include "card/Card.h"
#include "common/ECS.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// card.cpp
namespace test_card_unit {
using namespace eve;


using namespace eve::card;

namespace {

[[maybe_unused]] int countCardView() {
    int  n    = 0;
    auto view = ecs::View<CardData, CardData::Identity>();
    for (auto it = view.begin(); it != view.end(); ++it) ++n;
    return n;
}

[[maybe_unused]] bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

const char* kDefs = R"JSON(
[{"id":"flame","name":"Flame","kind":"creature","cost":2,"attack":3,"health":2},
 {"id":"bolt","name":"Bolt","kind":"spell","cost":3}]
)JSON";

}  // namespace


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
    CardData* first  = mod.newCard("flame");
    CardData* second = mod.newCard("flame");
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK_EQ(first->identity()->definitionId, "flame");
    CHECK_EQ(second->identity()->definitionId, "flame");
    CHECK_NE(first->identity()->id, second->identity()->id);

    first->layout()->x     = 42.f;
    first->layout()->angle = 12.f;
    CHECK_EQ(mod.capturePresentation(), 2);
    auto* snapshot = mod.getPresentation(0);
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
    LayoutConfig* cfg = mod.newConfig();
    Hand*         h   = mod.newHand(cfg);
    h->meta()->owner  = "player";
    CardData* a       = mod.newCard("flame");
    CardData* b       = mod.newCard("bolt");
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
    Deck*     d = mod.newDeck();
    CardData* a = mod.newCard("flame");
    CardData* b = mod.newCard("bolt");
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


TEST_CASE("card.drawCardMovesDeckToHand") {
    Card mod;
    mod.registerCardsFromJson(kDefs);
    LayoutConfig* cfg = mod.newConfig();
    Hand*         h   = mod.newHand(cfg);
    h->meta()->owner  = "player";
    Deck* d           = mod.newDeck();
    d->push(mod.newCard("flame"));
    CardData* drawn = mod.drawCard("player");
    REQUIRE(drawn != nullptr);
    CHECK_EQ(h->count(), 1);
    CHECK(d->isEmpty());
    CHECK_EQ(h->get(0), drawn);
}

static const char* kCardViewScript = R"SQ(
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
    CardData* c        = mod.newCard("flame");
    c->layout()->x     = 100.f;
    c->layout()->y     = 100.f;
    c->layout()->w     = 50.f;
    c->layout()->h     = 70.f;
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
    CardData* spell = mod.newCard("bolt");
    CHECK(spell->describe().find("\xe6\xb3\x95\xe6\x9c\xaf") != std::string::npos);  // 法术
}

TEST_CASE("card.deck.drawPeekBoundsAndShuffle") {
    Card mod;
    mod.registerCardsFromJson(kDefs);
    Deck* d = mod.newDeck();
    CHECK(d->isEmpty());
    CHECK_EQ(d->draw(), nullptr);
    CHECK_EQ(d->peek(), nullptr);
    CHECK_EQ(d->get(-1), nullptr);
    CHECK_EQ(d->get(0), nullptr);

    CardData* a = mod.newCard("flame");
    CardData* b = mod.newCard("bolt");
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
    Zone* z = mod.newZone("board", "Board", 10.f, 20.f, 100.f, 60.f);
    CHECK(z->contains(10.f, 20.f));
    CHECK(z->contains(109.f, 79.f));
    CHECK(!z->contains(9.f, 20.f));
    CHECK(!z->contains(111.f, 81.f));  // rect bounds are inclusive

    CardData* flame = mod.newCard("flame");
    CardData* bolt  = mod.newCard("bolt");
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
    LayoutConfig* cfg = mod.newConfig();
    Hand*         h   = mod.newHand(cfg);

    CardData* flame = mod.newCard("flame");
    h->addCard(flame);
    CHECK(static_cast<int>(flame->state()->phase) == static_cast<int>(CardState::Hand));
    CHECK_EQ(flame->layout()->w, cfg->cardW);
    CHECK(std::fabs(flame->layout()->scale - 0.01f) < 1e-5f);

    CardData* bolt           = mod.newCard("bolt");
    bolt->visual()->disabled = true;
    h->addCard(bolt);
    CHECK(static_cast<int>(bolt->state()->phase) == static_cast<int>(CardState::Disabled));
    CHECK(std::fabs(bolt->layout()->alpha - cfg->disabledAlpha) < 1e-5f);

    CardData* other = mod.newCard("flame");
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
    LayoutConfig* cfg = mod.newConfig();
    Hand*         h   = mod.newHand(cfg);
    CardData*     a   = mod.newCard("flame");
    CardData*     b   = mod.newCard("bolt");
    h->addCard(a);
    h->addCard(b);
    a->layout()->x     = 100.f;
    a->layout()->y     = 100.f;
    a->layout()->w     = 50.f;
    a->layout()->h     = 70.f;
    a->layout()->scale = 1.f;
    b->layout()->x     = 100.f;
    b->layout()->y     = 100.f;
    b->layout()->w     = 50.f;
    b->layout()->h     = 70.f;
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

[[maybe_unused]] void settleHand(Hand* h, const std::vector<Zone*>& zones, std::vector<CardEvent>& out) {
    for (int i = 0; i < 120; ++i) h->update(1.f / 60.f, 0.f, 0.f, false, zones, out);
    out.clear();
}

}  // namespace

TEST_CASE("card.hand.updateClickDragDrop") {
    Card mod;
    mod.registerCardsFromJson(kDefs);
    LayoutConfig* cfg  = mod.newConfig();
    cfg->dragThreshold = 4.f;
    cfg->cardW         = 50.f;
    cfg->cardH         = 70.f;
    cfg->handX         = 100.f;
    cfg->handY         = 100.f;
    cfg->spacing       = 20.f;
    cfg->arcHeight     = 0.f;
    cfg->rotationAngle = 0.f;
    Hand* h            = mod.newHand(cfg);
    h->meta()->owner   = "p1";
    CardData* a        = mod.newCard("flame");  // creature
    CardData* b        = mod.newCard("bolt");   // spell
    h->addCard(a);
    h->addCard(b);

    Zone* board                      = mod.newZone("board", "Board", 100.f, 300.f, 200.f, 60.f);
    board->filter()->acceptKinds     = {"creature"};
    Zone* spellOnly                  = mod.newZone("magic", "Magic", 100.f, 400.f, 200.f, 60.f);
    spellOnly->filter()->acceptKinds = {"spell"};
    std::vector<Zone*>     zones     = {board, spellOnly};
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
}  // namespace test_card_unit
#endif
