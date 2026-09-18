// 卡牌玩法域契约测试：真实 Card 模块 + 手牌/牌库走共享玩法协议，
// 覆盖发现、手牌观察、抽取、出牌支付边界、作弊档位、事件与 JSON 门面路由。
//
// 注意：zeroerr 的 CHECK_EQ(lhs, rhs) 会把 lhs 求值两次（一次判等、一次打印），
// 因此有副作用的调用必须先赋给局部变量，再断言变量。

#include "card/Card.h"
#include "card/CardTypes.h"
#include "common/Capability.h"
#include "common/GameplayControl.h"
#include "common/GameplayControlJson.h"
#include "common/GameplayInstanceCatalog.h"
#include "common/Module.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>

namespace {

constexpr const char* kInstanceId = "00000000-0000-7000-8000-000000000a01";
constexpr const char* kOwnerId    = "00000000-0000-7000-8000-000000000a02";
constexpr const char* kStrangerId = "00000000-0000-7000-8000-000000000a03";

constexpr const char* kDefinitions = R"JSON(
[{"id":"free","name":"Free","kind":"spell","cost":0},
 {"id":"bolt","name":"Bolt","kind":"spell","cost":3}]
)JSON";

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

eve::LogicalId action(const char* text) {
    const auto id = eve::LogicalId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

eve::Value object(std::initializer_list<std::pair<const std::string, eve::Value>> entries) {
    return eve::Value(eve::Value::Object(entries));
}

const eve::Value* member(const eve::Value& value, const char* name) {
    const auto* entries = value.getIf<eve::Value::Object>();
    if (entries == nullptr) return nullptr;
    const auto found = entries->find(name);
    return found == entries->end() ? nullptr : &found->second;
}

const eve::Value::Array* handCards(const eve::Value& state) {
    const eve::Value* hand = member(state, "hand");
    return hand == nullptr ? nullptr : hand->getIf<eve::Value::Array>();
}

/** 手牌里第一张指定定义的卡牌实例 id；找不到返回空串。 */
std::string firstCardOf(const eve::Value& state, const char* definition) {
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

eve::GameplayCommand command(const CardFixture& fixture, const char* id, const char* actionText,
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

std::string envelope(const char* operation, const char* access, const std::string& extra = {}) {
    return std::string("{\"schemaId\":\"evengine.gameplay-control-request\",\"schemaVersion\":1,\"op\":\"") +
           operation + "\",\"domain\":\"card\",\"instance\":\"" + kInstanceId +
           "\",\"session\":{\"id\":\"mcp\",\"access\":\"" + access + "\",\"controlledSubjects\":[\"" + kOwnerId +
           "\"]}" + extra + "}";
}

std::string jsonCommand(const char* id, const char* actionText, std::uint64_t expectedRevision,
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

TEST_CASE("gameplay.control.cardScriptPublishesItsHand") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"SQ(
        local card = eve.Card();
        card.clearGameplayControls();
        card.clearCardDefinitions();
        card.registerCardsFromJson("[{\"id\":\"free\",\"name\":\"Free\",\"kind\":\"spell\",\"cost\":0}]");
        local hand = card.newHand(card.newConfig());
        scriptPublished <- card.publishGameplay(
            "00000000-0000-7000-8000-000000000e01",
            "00000000-0000-7000-8000-000000000e02", hand).ok;
        scriptInstances <- card.getGameplayControlCount();
        local duplicate = card.publishGameplay(
            "00000000-0000-7000-8000-000000000e01",
            "00000000-0000-7000-8000-000000000e02", hand);
        scriptDuplicateOk <- duplicate.ok;
        scriptDuplicateMessage <- duplicate.message;
        local malformed = card.publishGameplay("not-a-uuid",
            "00000000-0000-7000-8000-000000000e02", hand);
        scriptMalformedOk <- malformed.ok;
        scriptUnpublished <- card.unpublishGameplay("00000000-0000-7000-8000-000000000e01").ok;
        scriptRemaining <- card.getGameplayControlCount();
    )SQ"));

    CHECK(vm.find("scriptPublished").toBool());
    CHECK_EQ(static_cast<int>(vm.find("scriptInstances").toInt()), 1);
    CHECK(!vm.find("scriptDuplicateOk").toBool());
    CHECK(vm.find("scriptDuplicateMessage").toString().find("already owns") != std::string::npos);
    CHECK(!vm.find("scriptMalformedOk").toBool());
    CHECK(vm.find("scriptUnpublished").toBool());
    CHECK_EQ(static_cast<int>(vm.find("scriptRemaining").toInt()), 0);
}
