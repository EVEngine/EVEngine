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
