// 对话玩法域契约测试：真实 DialogueFlow 运行器走共享玩法协议，
// 覆盖发现、节点观察、路由选择、权限档位、修订号账本、事件与 JSON 门面路由。
//
// 注意：zeroerr 的 CHECK_EQ(lhs, rhs) 会把 lhs 求值两次（一次判等、一次打印），
// 因此有副作用的调用必须先赋给局部变量，再断言变量。

#include "common/Capability.h"
#include "common/GameplayControl.h"
#include "common/GameplayControlJson.h"
#include "common/GameplayInstanceCatalog.h"
#include "common/Module.h"
#include "dialogue/DialogueFlow.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>

namespace {

constexpr const char* kInstanceId = "00000000-0000-7000-8000-000000000901";
constexpr const char* kOwnerId    = "00000000-0000-7000-8000-000000000902";
constexpr const char* kStrangerId = "00000000-0000-7000-8000-000000000903";

/** 一个带选项的对话：ask(choice) -> shop(line) -> end。 */
constexpr const char* kConversation = R"(
conversation greeting entry=ask
node ask choice speaker=elder
option buy -> shop
option leave -> end
node shop line speaker=elder text="here you go" next=end
node end end
endconversation
)";

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

/** 观察状态里的字符串字段（缺失返回空串）。 */
std::string stateText(const eve::Value& state, const char* name) {
    const eve::Value* field = member(state, name);
    return field == nullptr || !field->isString() ? std::string{} : field->asString();
}

/** 真实模块 + 已加载的对话，并把运行器发布到共享玩法协议。 */
struct DialogueFixture {
    eve::dialogue::DialogueFlow* flow = nullptr;
    eve::SubjectRef              instance;
    eve::SubjectRef              owner;

    DialogueFixture() {
        flow = eve::dialogue::DialogueFlow::create();
        REQUIRE(flow != nullptr);
        flow->clearGameplayControls();
        const auto loaded = flow->loadDnutChecked(kConversation, "greeting.dnut");
        REQUIRE(loaded.ok());
        REQUIRE_EQ(loaded.value(), 1);
        instance             = subject(kInstanceId);
        owner                = subject(kOwnerId);
        const auto published = flow->publishGameplay(kInstanceId, kOwnerId);
        REQUIRE(published.ok());
    }

    ~DialogueFixture() {
        flow->clearGameplayControls();
        flow->clear();
    }

    DialogueFixture(const DialogueFixture&)            = delete;
    DialogueFixture& operator=(const DialogueFixture&) = delete;

    /** 通过能力注册表取回适配器，和 JSON 路由走同一条查找路径。 */
    eve::IGameplayControlProvider* provider() const {
        eve::IGameplayControlProvider* found = nullptr;
        eve::cap::forEach<eve::IGameplayControlProvider>([&](auto* candidate) {
            if (candidate != nullptr && candidate->gameplayDomain() == "dialogue") found = candidate;
        });
        return found;
    }
};

eve::GameplayCommand command(const DialogueFixture& fixture, const char* id, const char* actionText,
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
           operation + "\",\"domain\":\"dialogue\",\"instance\":\"" + kInstanceId +
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

TEST_CASE("gameplay.control.dialoguePublishesNodeAndRouteVocabulary") {
    DialogueFixture fixture;
    auto*           provider = fixture.provider();
    REQUIRE(provider != nullptr);
    CHECK_EQ(fixture.flow->gameplayControlCount(), 1);

    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {fixture.owner}};
    eve::GameplaySession automation{"automation", eve::GameplayAccess::TestDriver, {fixture.owner}};

    auto observed = provider->observeGameplay(player, fixture.instance);
    REQUIRE(observed.ok());
    const auto idle = std::move(observed).takeValue();
    CHECK_EQ(idle.domain.format(), std::string("gameplay:dialogue"));
    CHECK_EQ(idle.revision, std::uint64_t{0});
    CHECK_EQ(member(idle.state, "active")->asBool(), false);
    CHECK_EQ(member(idle.state, "blocked")->asBool(), false);
    CHECK_EQ(stateText(idle.state, "node"), std::string());
    CHECK_EQ(stateText(idle.state, "bindings"), std::string("empty"));

    // 三个动作对玩家与自动化档位一致：对话没有"发放"这类特权动作。
    auto playerActions     = provider->availableGameplayActions(player, fixture.instance, fixture.owner);
    auto automationActions = provider->availableGameplayActions(automation, fixture.instance, fixture.owner);
    REQUIRE(playerActions.ok());
    REQUIRE(automationActions.ok());
    CHECK_EQ(static_cast<int>(playerActions.value().size()), 3);
    CHECK_EQ(playerActions.value()[0].id.format(), std::string("dialogue:start"));
    CHECK_EQ(playerActions.value()[1].id.format(), std::string("dialogue:advance"));
    CHECK_EQ(playerActions.value()[2].id.format(), std::string("dialogue:select"));
    CHECK_EQ(playerActions.value()[2].id.format(), automationActions.value()[2].id.format());

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

TEST_CASE("gameplay.control.dialoguePlaysConversationThroughItsOwnOperations") {
    DialogueFixture fixture;
    auto*           provider = fixture.provider();
    REQUIRE(provider != nullptr);
    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {fixture.owner}};

    // 未开始时不能推进，也不能选择。
    auto earlyAdvance = provider->submitGameplay(player, fixture.instance,
                                                 command(fixture, "advance-early", "dialogue:advance", 0, object({})));
    CHECK(!earlyAdvance.ok());
    CHECK_EQ(earlyAdvance.code(), eve::StatusCode::Rejected);

    auto started = provider->submitGameplay(
        player, fixture.instance,
        command(fixture, "start-1", "dialogue:start", 0, object({{"conversation", eve::Value("greeting")}})));
    REQUIRE(started.ok());
    CHECK_EQ(started.value().resultingRevision, std::uint64_t{1});
    CHECK_EQ(member(started.value().details, "active")->asBool(), true);
    CHECK_EQ(member(started.value().details, "node")->asString(), std::string("ask"));
    CHECK_EQ(member(started.value().details, "nodeKind")->asString(), std::string("choice"));

    // 选项节点把路由词表投影给调用方：agent 不必猜 route id。
    auto observed = provider->observeGameplay(player, fixture.instance);
    REQUIRE(observed.ok());
    const auto asking = std::move(observed).takeValue();
    CHECK_EQ(member(asking.state, "active")->asBool(), true);
    CHECK_EQ(stateText(asking.state, "conversation"), std::string("greeting"));
    CHECK_EQ(stateText(asking.state, "node"), std::string("ask"));
    CHECK_EQ(stateText(asking.state, "nodeKind"), std::string("choice"));
    CHECK_EQ(stateText(asking.state, "speaker"), std::string("elder"));
    const eve::Value* routes = member(asking.state, "routes");
    REQUIRE(routes != nullptr);
    const auto* routeArray = routes->getIf<eve::Value::Array>();
    REQUIRE(routeArray != nullptr);
    REQUIRE_EQ(static_cast<int>(routeArray->size()), 2);
    CHECK_EQ(member((*routeArray)[0], "route")->asString(), std::string("buy"));
    CHECK_EQ(member((*routeArray)[1], "route")->asString(), std::string("leave"));

    // 选项节点上直接 advance 会被运行器拒绝（必须 select）。
    auto blocked = provider->submitGameplay(player, fixture.instance,
                                            command(fixture, "advance-blocked", "dialogue:advance", 1, object({})));
    CHECK(!blocked.ok());
    CHECK_EQ(blocked.code(), eve::StatusCode::Failed);

    auto selected = provider->submitGameplay(
        player, fixture.instance,
        command(fixture, "select-buy", "dialogue:select", 1, object({{"route", eve::Value("buy")}})));
    REQUIRE(selected.ok());
    CHECK_EQ(selected.value().resultingRevision, std::uint64_t{2});
    CHECK_EQ(member(selected.value().details, "node")->asString(), std::string("shop"));
    CHECK_EQ(member(selected.value().details, "nodeKind")->asString(), std::string("line"));

    auto atShop = provider->observeGameplay(player, fixture.instance);
    REQUIRE(atShop.ok());
    CHECK_EQ(stateText(atShop.value().state, "text"), std::string("here you go"));

    auto advanced = provider->submitGameplay(player, fixture.instance,
                                             command(fixture, "advance-1", "dialogue:advance", 2, object({})));
    REQUIRE(advanced.ok());
    CHECK_EQ(advanced.value().resultingRevision, std::uint64_t{3});
    CHECK_EQ(member(advanced.value().details, "active")->asBool(), false);

    auto finished = provider->observeGameplay(player, fixture.instance);
    REQUIRE(finished.ok());
    CHECK_EQ(member(finished.value().state, "active")->asBool(), false);

    // 事件账本按命令给出因果链与结果节点。
    auto events = provider->gameplayEvents(player, fixture.instance, 0);
    REQUIRE(events.ok());
    const auto list = std::move(events).takeValue();
    REQUIRE_EQ(static_cast<int>(list.size()), 3);
    CHECK_EQ(list[0].type, std::string("dialogue.started"));
    CHECK_EQ(list[1].type, std::string("dialogue.selected"));
    CHECK_EQ(list[2].type, std::string("dialogue.finished"));
    CHECK_EQ(member(list[1].payload, "node")->asString(), std::string("shop"));
    CHECK_EQ(list[2].causationCommandId, std::string("advance-1"));
    CHECK_EQ(list[2].subject.format(), std::string(kOwnerId));

    auto drained = provider->gameplayEvents(player, fixture.instance, 3);
    REQUIRE(drained.ok());
    CHECK_EQ(drained.code(), eve::StatusCode::NoOp);

    // 未知对话在未激活时报告 NotFound，而不是先撞上"已在对话中"的前置条件。
    auto unknownConversation = provider->submitGameplay(
        player, fixture.instance,
        command(fixture, "start-unknown", "dialogue:start", 3, object({{"conversation", eve::Value("nope")}})));
    CHECK(!unknownConversation.ok());
    CHECK_EQ(unknownConversation.code(), eve::StatusCode::NotFound);

    auto restarted = provider->submitGameplay(
        player, fixture.instance,
        command(fixture, "start-2", "dialogue:start", 3, object({{"conversation", eve::Value("greeting")}})));
    REQUIRE(restarted.ok());
    CHECK_EQ(restarted.value().resultingRevision, std::uint64_t{4});

    auto stale = provider->submitGameplay(
        player, fixture.instance,
        command(fixture, "select-stale", "dialogue:select", 0, object({{"route", eve::Value("buy")}})));
    CHECK(!stale.ok());
    CHECK_EQ(stale.code(), eve::StatusCode::Conflict);

    auto unsupported =
        provider->submitGameplay(player, fixture.instance, command(fixture, "shrug", "dialogue:shrug", 4, object({})));
    CHECK(!unsupported.ok());
    CHECK_EQ(unsupported.code(), eve::StatusCode::Unsupported);

    auto malformed = provider->submitGameplay(player, fixture.instance,
                                              command(fixture, "select-empty", "dialogue:select", 4, object({})));
    CHECK(!malformed.ok());
    CHECK_EQ(malformed.code(), eve::StatusCode::Rejected);

    auto advanced2 = provider->advanceGameplay(player, fixture.instance,
                                               {eve::SimulationTick(1), eve::Duration::fromNanoseconds(16666667)});
    REQUIRE(advanced2.ok());
    auto rewound = provider->advanceGameplay(player, fixture.instance,
                                             {eve::SimulationTick(1), eve::Duration::fromNanoseconds(1)});
    CHECK(!rewound.ok());
    CHECK_EQ(rewound.code(), eve::StatusCode::Conflict);
}

TEST_CASE("gameplay.control.dialoguePublishesAtMostOneRunnerInstance") {
    DialogueFixture fixture;

    const auto duplicate =
        fixture.flow->publishGameplay("00000000-0000-7000-8000-000000000904", "00000000-0000-7000-8000-000000000905");
    CHECK(!duplicate.ok());
    CHECK_EQ(duplicate.code(), eve::StatusCode::Conflict);
    CHECK_EQ(fixture.flow->gameplayControlCount(), 1);

    const auto published = fixture.flow->gameplayInstances();
    REQUIRE_EQ(static_cast<int>(published.size()), 1);
    CHECK_EQ(published[0], std::string(kInstanceId));

    int matches = 0;
    eve::cap::forEach<eve::IGameplayControlProvider>([&](auto* candidate) {
        if (candidate != nullptr && candidate->gameplayDomain() == "dialogue") ++matches;
    });
    CHECK_EQ(matches, 1);

    const auto unpublished = fixture.flow->unpublishGameplay(kInstanceId);
    CHECK(unpublished.ok());
    const auto again = fixture.flow->unpublishGameplay(kInstanceId);
    CHECK(!again.ok());
    CHECK_EQ(again.code(), eve::StatusCode::NotFound);
    CHECK_EQ(fixture.flow->gameplayControlCount(), 0);

    // 取消发布后重新发布一个不同身份必须成功：发布的是运行器本身。
    const auto republished =
        fixture.flow->publishGameplay("00000000-0000-7000-8000-000000000904", "00000000-0000-7000-8000-000000000905");
    REQUIRE(republished.ok());
    CHECK_EQ(fixture.flow->gameplayControlCount(), 1);
    const auto replaced = fixture.flow->gameplayInstances();
    REQUIRE_EQ(static_cast<int>(replaced.size()), 1);
    CHECK_EQ(replaced[0], std::string("00000000-0000-7000-8000-000000000904"));
}

TEST_CASE("gameplay.control.dialogueRoutesThroughTheSharedJsonFacade") {
    DialogueFixture fixture;

    auto catalogs = eve::executeGameplayControlJson(
        R"({"schemaId":"evengine.gameplay-control-request","schemaVersion":1,"op":"instances"})");
    REQUIRE(catalogs.ok());
    CHECK(catalogs.value().find("\"domain\":\"dialogue\"") != std::string::npos);
    CHECK(catalogs.value().find(kInstanceId) != std::string::npos);

    auto observed = eve::executeGameplayControlJson(envelope("observe", "player"));
    REQUIRE(observed.ok());
    CHECK(observed.value().find("gameplay:dialogue") != std::string::npos);
    CHECK(observed.value().find("\"bindings\":\"empty\"") != std::string::npos);

    const std::string actor   = std::string(",\"subject\":\"") + kOwnerId + "\"";
    auto              actions = eve::executeGameplayControlJson(envelope("actions", "player", actor));
    REQUIRE(actions.ok());
    CHECK(actions.value().find("dialogue:start") != std::string::npos);
    CHECK(actions.value().find("dialogue:select") != std::string::npos);

    auto started = eve::executeGameplayControlJson(envelope(
        "submit", "player", jsonCommand("json-start-1", "dialogue:start", 0, R"({"conversation":"greeting"})")));
    REQUIRE(started.ok());
    CHECK(started.value().find("\"resultingRevision\":1") != std::string::npos);
    CHECK(started.value().find("\"node\":\"ask\"") != std::string::npos);

    auto selected = eve::executeGameplayControlJson(
        envelope("submit", "player", jsonCommand("json-select-1", "dialogue:select", 1, R"({"route":"buy"})")));
    REQUIRE(selected.ok());
    CHECK(selected.value().find("\"node\":\"shop\"") != std::string::npos);

    auto events = eve::executeGameplayControlJson(envelope("events", "player", ",\"afterSequence\":0"));
    REQUIRE(events.ok());
    CHECK(events.value().find("dialogue.started") != std::string::npos);
    CHECK(events.value().find("dialogue.selected") != std::string::npos);

    auto advanced =
        eve::executeGameplayControlJson(envelope("advance", "player", ",\"tick\":1,\"deltaNanoseconds\":16666667"));
    REQUIRE(advanced.ok());
    CHECK(advanced.value().find("\"tick\":1") != std::string::npos);
}

TEST_CASE("gameplay.control.dialogueScriptPublishesItsRunner") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"SQ(
        local flow = eve.DialogueFlow();
        flow.clearGameplayControls();
        scriptPublished <- flow.publishGameplay(
            "00000000-0000-7000-8000-000000000d01",
            "00000000-0000-7000-8000-000000000d02").ok;
        scriptInstances <- flow.getGameplayControlCount();
        local duplicate = flow.publishGameplay(
            "00000000-0000-7000-8000-000000000d01",
            "00000000-0000-7000-8000-000000000d02");
        scriptDuplicateOk <- duplicate.ok;
        scriptDuplicateMessage <- duplicate.message;
        local malformed = flow.publishGameplay("not-a-uuid",
            "00000000-0000-7000-8000-000000000d02");
        scriptMalformedOk <- malformed.ok;
        scriptUnpublished <- flow.unpublishGameplay("00000000-0000-7000-8000-000000000d01").ok;
        scriptRemaining <- flow.getGameplayControlCount();
    )SQ"));

    CHECK(vm.find("scriptPublished").toBool());
    CHECK_EQ(static_cast<int>(vm.find("scriptInstances").toInt()), 1);
    CHECK(!vm.find("scriptDuplicateOk").toBool());
    CHECK(vm.find("scriptDuplicateMessage").toString().find("already published") != std::string::npos);
    CHECK(!vm.find("scriptMalformedOk").toBool());
    CHECK(vm.find("scriptUnpublished").toBool());
    CHECK_EQ(static_cast<int>(vm.find("scriptRemaining").toInt()), 0);
}
