#include <cstdint>
#include <initializer_list>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <utility>
#include <vector>
#include "common/Capability.h"
#include "common/GameplayControl.h"
#include "common/GameplayControlJson.h"
#include "common/GameplayInstanceCatalog.h"
#include "common/Module.h"
#include "common/ServiceInterfaces.h"
#include "dialogue/ConversationAuthoring.h"
#include "dialogue/ConversationCompiler.h"
#include "dialogue/ConversationImporter.h"
#include "dialogue/Dialogue.h"
#include "dialogue/DialogueFlow.h"
#include "dialogue/DialogueSequence.h"
#include "dialogue/DnutParser.h"
#include "dnut_interpreter/DnutCompiler.h"
#include "dnut_interpreter/SequenceRuntime.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// dialogue_authoring.cpp
using namespace eve::dialogue;

TEST_CASE("dialogueAuthoring.documentGraphEditingAndDiagnostics") {
    ConversationDocument document("quest.intro");
    REQUIRE(document.addParameter("player"));
    REQUIRE(document.addNode("start", "line"));
    REQUIRE(document.setEntry("start"));
    REQUIRE(document.setField("start", "speaker", "guide"));
    REQUIRE(document.setField("start", "text", "Welcome, {player.name}."));
    REQUIRE(document.setField("start", "next", "decision"));
    REQUIRE(document.addNode("decision", "choice"));
    REQUIRE(document.addRoute("decision", "accept", "end"));

    CHECK_EQ(document.getNodeCount(), 3);
    CHECK_EQ(document.getFieldKind("start", 2), std::string("multiline"));
    CHECK_EQ(document.getRouteLabel("decision", 0), std::string("accept"));
    CHECK(document.validate());

    REQUIRE(document.renameNode("decision", "offer"));
    CHECK_EQ(document.getField("start", "next"), std::string("offer"));
    REQUIRE(document.removeNode("end"));
    CHECK(!document.validate());
    CHECK(document.getDiagnosticCount() > 0);
    CHECK_EQ(document.getDiagnosticSeverity(0), std::string("error"));
    CHECK(!document.getDiagnosticMessage(0).empty());
}


// dialogue_compiler.cpp
using namespace eve::dialogue;

TEST_CASE("dialogueCompiler.singleParserMixedDocumentAndStrictSchema") {
    const std::string                   source = R"(
schema "eve.dnut"
version 1
pool greeting {
  guide: "A // inside string is not a comment" id="greeting.line"
}
story forest.arrival {
  dialogue id=story.forest.arrival
  wait 0.35
}
conversation greeting.scene entry=end {
  parameter count int optional default=2
  node end end
}
)";
    std::vector<ConversationDiagnostic> diagnostics;
    auto                                parsed = parseDnutDocument(source, "mixed.dnut", diagnostics);
    REQUIRE(parsed.ok());
    DnutDocument document = std::move(parsed).takeValue();
    CHECK_EQ(document.conversations.size(), size_t(1));
    CHECK_EQ(document.conversations[0].parameters[0].defaultValue.asInt(), 2);
    REQUIRE(document.poolRoot.find("pools") != nullptr);

    diagnostics.clear();
    CHECK(!parseDnutDocument("schema \"eve.dnut\"\nversion 1\nunknown thing\n", "bad.dnut", diagnostics));
    REQUIRE(!diagnostics.empty());
    CHECK_EQ(diagnostics[0].code, std::string("DnutParseError"));
}

TEST_CASE("dialogueCompiler.parameterizedExpressionsAndLocalization") {
    const std::string           source = R"DNUT(
schema "eve.dnut"
version 1
conversation common.greeting version=3 entry=decide {
parameter speaker string required
parameter listener string required
parameter location string required
node decide branch {
route friendly when="score(speaker, listener, location) >= threshold(speaker.personality)" target=friendly
route formal target=formal
}
node friendly line speaker=speaker pool=greeting.friendly i18n=dialogue.greeting.friendly voice=voice.greeting.friendly next=end
node formal line speaker=speaker text="Good day, {listener.name}." i18n=dialogue.greeting.formal next=end
node unused command kind=operation target="quest.accept" result="accepted" payment(money=2) mutation(subject="player", key="quest.accepted", kind="set", value=true, persistent=true) next=end
node end end
}
)DNUT";
    eve::dnut::StepKindRegistry registry;
    registerDialogueSequenceSteps(registry).expect("dialogue sequence vocabulary");
    auto compiled = eve::dnut::compileDnutConversations(source, "greeting.dnut", registry);
    REQUIRE(!compiled.hasErrors());
    auto assets = std::move(compiled.assets);
    CHECK(assets.size() == 1);
    CHECK(assets[0].version == 3);
    CHECK(assets[0].findNode("decide")->routes[0].label == "friendly");
    REQUIRE(assets[0].findNode("unused") != nullptr);
    auto mutations = decodeSequenceStateMutations(assets[0].findNode("unused")->payload);
    REQUIRE(mutations.ok());
    REQUIRE(mutations.value().size() == 1);
    CHECK(mutations.value()[0].persistent);
    auto payment = decodeSequencePayment(assets[0].findNode("unused")->payload);
    REQUIRE(payment.ok());
    CHECK_EQ(payment.value().money, 2);

    std::vector<ConversationDiagnostic> diagnostics;
    CHECK(lintConversations(assets, "greeting.dnut", diagnostics).ok());
    CHECK(diagnostics.size() == 1);
    CHECK(static_cast<int>(diagnostics[0].severity) == static_cast<int>(ConversationDiagnostic::Severity::Warning));
    const std::string csv = exportConversationLocalizationCsv(assets);
    CHECK(csv.find("dialogue.greeting.friendly") != std::string::npos);
    CHECK(csv.find("Good day, {listener.name}.") != std::string::npos);
}

TEST_CASE("dialogueCompiler.domainValidatorsRejectInvalidPaymentPlacementAndValues") {
    eve::dnut::StepKindRegistry registry;
    registerDialogueSequenceSteps(registry).expect("dialogue sequence vocabulary");

    auto validChoice = eve::dnut::compileDnutConversations(
        "schema \"eve.dnut\"\nversion 1\n"
        "conversation valid entry=pick {\n"
        "node pick choice {\n"
        "route buy target=end text=\"Buy\" i18n=choice.buy payment(money=1) "
        "mutation(subject=\"player\", key=\"bought\", kind=\"set\", value=true)\n"
        "}\nnode end end\n}\n",
        "valid-choice.dnut", registry);
    REQUIRE(!validChoice.hasErrors());
    const auto& route = validChoice.assets.front().findNode("pick")->routes.front();
    CHECK_EQ(sequenceRoutePayloadString(route, "text"), std::string("Buy"));
    auto routePayment = decodeSequencePayment(route.payload);
    REQUIRE(routePayment.ok());
    CHECK_EQ(routePayment.value().money, 1);
    auto routeMutations = decodeSequenceStateMutations(route.payload);
    REQUIRE(routeMutations.ok());
    CHECK_EQ(routeMutations.value().size(), 1u);

    auto badChoice = eve::dnut::compileDnutConversations(
        "schema \"eve.dnut\"\nversion 1\n"
        "conversation invalid entry=pick {\n"
        "node pick choice {\nroute buy target=end payment(money=0)\n}\n"
        "node end end\n}\n",
        "bad-choice.dnut", registry);
    CHECK(badChoice.hasErrors());

    auto badLine = eve::dnut::compileDnutConversations(
        "schema \"eve.dnut\"\nversion 1\n"
        "conversation invalid entry=line {\n"
        "node line line text=\"hello\" payment(money=1) next=end\n"
        "node end end\n}\n",
        "bad-line.dnut", registry);
    CHECK(badLine.hasErrors());
}

// dialogue_conversation.cpp
using namespace eve;
using namespace eve::dialogue;
using namespace eve::dnut;

namespace {

[[maybe_unused]] SequenceNode node(std::string id, std::string type, std::string next = {}) {
    SequenceNode value;
    value.id   = std::move(id);
    value.type = std::move(type);
    value.next = std::move(next);
    return value;
}

[[maybe_unused]] SequenceRoute route(std::string label, std::string target, eve::Value condition = {}) {
    SequenceRoute value;
    value.label     = std::move(label);
    value.target    = std::move(target);
    value.condition = std::move(condition);
    return value;
}

[[maybe_unused]] StepKindRegistry dialogueRegistry() {
    StepKindRegistry registry;
    registerDialogueSequenceSteps(registry).expect("dialogue sequence steps");
    return registry;
}

[[maybe_unused]] SequenceAsset makeGreeting() {
    SequenceAsset asset;
    asset.id            = "common.greeting";
    asset.entry         = "decide";
    asset.parameters    = {SequenceParameter{"speaker"}, SequenceParameter{"listener"}, SequenceParameter{"location"}};
    SequenceNode decide = node("decide", "branch");
    decide.routes = {route("friendly", "friendly", eve::Value("speaker.mood == happy")), route("formal", "formal")};
    SequenceNode friendly = node("friendly", "line", "end");
    friendly.payload.set("speaker", eve::Value("speaker"));
    friendly.payload.set("pool", eve::Value("greeting.friendly"));
    SequenceNode formal = friendly;
    formal.id           = "formal";
    formal.payload.set("pool", eve::Value("greeting.formal"));
    asset.nodes = {decide, friendly, formal, node("end", "end")};
    return asset;
}

}  // namespace

TEST_CASE("dialogueConversation.typedDefaultsAndExcessBindings") {
    SequenceAsset asset;
    asset.id    = "typed";
    asset.entry = "line";
    SequenceParameter count;
    count.name         = "count";
    count.type         = SequenceParameterType::Integer;
    count.required     = false;
    count.defaultValue = eve::Value::integer(3);
    asset.parameters   = {count};
    asset.nodes        = {node("line", "line", "end"), node("end", "end")};

    StepKindRegistry registry = dialogueRegistry();
    SequenceRuntime  runtime;
    runtime.setStepRegistry(&registry);
    REQUIRE(runtime.start(&asset).ok());
    REQUIRE(runtime.bindings().find("count") != nullptr);
    CHECK_EQ(runtime.bindings().find("count")->asInt(), 3);
    runtime.stop();

    eve::Value wrongType = eve::Value::Object{};
    wrongType.set("count", eve::Value("bad"));
    CHECK(!runtime.start(&asset, std::move(wrongType)).ok());
    eve::Value excess = eve::Value::Object{};
    excess.set("extra", eve::Value::integer(1));
    CHECK(!runtime.start(&asset, std::move(excess)).ok());
}

TEST_CASE("dialogueConversation.parameterizedRuntime") {
    SequenceAsset    asset    = makeGreeting();
    StepKindRegistry registry = dialogueRegistry();
    SequenceRuntime  runtime;
    runtime.setStepRegistry(&registry);
    runtime.setConditionEvaluator([&runtime](const eve::Value& expression) {
        const eve::Value* speaker = runtime.bindings().find("speaker");
        const eve::Value* mood    = speaker ? speaker->find("mood") : nullptr;
        const bool        passed  = expression.isString() && expression.asString() == "speaker.mood == happy" && mood &&
                            mood->isString() && mood->asString() == "happy";
        return SequenceConditionOutcome{passed, passed ? "" : "false", {}};
    });
    eve::Value bindings = eve::Value::Object{};
    bindings.set("speaker", eve::Value::Object{{"mood", eve::Value("happy")}});
    bindings.set("listener", eve::Value::Object{{"id", eve::Value("player")}});
    bindings.set("location", eve::Value("village"));
    CHECK(runtime.start(&asset, std::move(bindings)).ok());
    CHECK(runtime.isBlocked());
    CHECK(runtime.currentNodeId() == "friendly");
    CHECK(sequencePayloadString(*runtime.currentNode(), "pool") == "greeting.friendly");
    CHECK(runtime.advance().ok());
    CHECK(!runtime.isActive());
}

TEST_CASE("dialogueConversation.rejectsMissingAndUndeclaredBindings") {
    SequenceAsset    asset    = makeGreeting();
    StepKindRegistry registry = dialogueRegistry();
    SequenceRuntime  runtime;
    runtime.setStepRegistry(&registry);
    eve::Value bindings = eve::Value::Object{};
    bindings.set("speaker", eve::Value::Object{});
    bindings.set("listener", eve::Value::Object{});
    auto missing = runtime.start(&asset, bindings);
    CHECK(!missing.ok());
    CHECK(missing.status().describe().find("missing required binding 'location'") != std::string::npos);

    bindings.set("location", eve::Value("village"));
    bindings.set("unexpected", eve::Value(true));
    auto excess = runtime.start(&asset, bindings);
    CHECK(!excess.ok());
    CHECK(excess.status().describe().find("undeclared binding 'unexpected'") != std::string::npos);
}

TEST_CASE("dialogueConversation.validation") {
    SequenceAsset asset   = makeGreeting();
    asset.nodes.back().id = "friendly";
    auto invalid          = asset.validate();
    CHECK(!invalid.ok());
    CHECK(invalid.status().describe().find("duplicate sequence node id") != std::string::npos);
}

TEST_CASE("dialogueConversation.expressionFailureDoesNotSelectElse") {
    SequenceAsset    asset    = makeGreeting();
    StepKindRegistry registry = dialogueRegistry();
    SequenceRuntime  runtime;
    runtime.setStepRegistry(&registry);
    runtime.setConditionEvaluator(
        [](const eve::Value&) { return SequenceConditionOutcome{false, "failed", "expression failed"}; });
    eve::Value bindings = eve::Value::Object{};
    bindings.set("speaker", eve::Value::Object{});
    bindings.set("listener", eve::Value::Object{});
    bindings.set("location", eve::Value("village"));
    auto failed = runtime.start(&asset, std::move(bindings));
    CHECK(!failed.ok());
    CHECK(failed.status().describe().find("expression failed") != std::string::npos);
}

TEST_CASE("dialogueConversation.callStackStateRoundtripUsesPayloadReturn") {
    SequenceAsset child;
    child.id               = "common.child";
    child.entry            = "line";
    SequenceNode childLine = node("line", "line", "end");
    childLine.payload.set("text", eve::Value("hello"));
    child.nodes = {childLine, node("end", "end")};

    SequenceAsset parent;
    parent.id         = "scene.parent";
    parent.entry      = "call";
    SequenceNode call = node("call", "call");
    call.payload.set("target", eve::Value(child.id));
    call.payload.set("return", eve::Value("after"));
    SequenceNode after = node("after", "line", "end");
    after.payload.set("text", eve::Value("returned"));
    parent.nodes = {call, after, node("end", "end")};

    const auto resolve = [&](const std::string& id) -> const SequenceAsset* {
        if (id == parent.id) return &parent;
        if (id == child.id) return &child;
        return nullptr;
    };
    StepKindRegistry registry = dialogueRegistry();
    SequenceRuntime  original;
    original.setStepRegistry(&registry);
    original.setAssetResolver(resolve);
    CHECK(original.start(&parent).ok());
    CHECK(original.currentNodeId() == "line");
    original.locals().set("calculatedPrice", eve::Value::integer(42));
    eve::Value saved;
    REQUIRE(original.captureState(saved).ok());

    SequenceRuntime restored;
    restored.setStepRegistry(&registry);
    restored.setAssetResolver(resolve);
    CHECK(restored.restoreState(saved).ok());
    CHECK(restored.currentNodeId() == "line");
    CHECK(restored.locals().find("calculatedPrice")->asInt() == 42);
    CHECK(restored.advance().ok());
    CHECK(restored.currentNodeId() == "after");
}

TEST_CASE("dialogueConversation.commandsAndEvents") {
    SequenceAsset asset;
    asset.id             = "scene.command";
    asset.entry          = "calculate";
    SequenceNode command = node("calculate", "command", "line");
    command.payload.set("name", eve::Value("economy.quote"));
    command.payload.set("resultLocal", eve::Value("quote"));
    asset.nodes = {command, node("line", "line", "end"), node("end", "end")};

    std::vector<SequenceRuntime::EventKind> events;
    StepKindRegistry                        registry = dialogueRegistry();
    SequenceRuntime                         runtime;
    runtime.setStepRegistry(&registry);
    runtime.setEventSink([&](const SequenceRuntime::Event& event) { events.push_back(event.kind); });
    runtime.registerCommand("economy.quote", [](const SequenceCommandRequest&) {
        SequenceCommandResponse response;
        response.value = eve::Value::integer(125);
        return response;
    });
    CHECK(runtime.start(&asset).ok());
    CHECK(runtime.currentNodeId() == "line");
    CHECK(runtime.locals().find("quote")->asInt() == 125);
    CHECK(events.size() >= 5);
}

TEST_CASE("dialogueConversation.asyncCommandDualResumeAndRestore") {
    SequenceAsset asset;
    asset.id             = "scene.wait-command";
    asset.entry          = "animate";
    SequenceNode command = node("animate", "command", "end");
    command.payload.set("name", eve::Value("animation.play"));
    command.payload.set("resultLocal", eve::Value("animationResult"));
    asset.nodes = {command, node("end", "end")};

    StepKindRegistry registry = dialogueRegistry();
    SequenceRuntime  runtime;
    runtime.setStepRegistry(&registry);
    runtime.registerCommand("animation.play", [](const SequenceCommandRequest&) {
        SequenceCommandResponse response;
        response.status = SequenceCommandResponse::Status::Blocked;
        return response;
    });
    CHECK(runtime.start(&asset).ok());
    CHECK(runtime.isBlocked());
    CHECK(runtime.isWaitingCommand());
    CHECK(!runtime.advance().ok());
    const std::string requestId = runtime.pendingCommandRequestId();
    CHECK(!requestId.empty());
    eve::Value saved;
    REQUIRE(runtime.captureState(saved).ok());

    SequenceRuntime restored;
    restored.setStepRegistry(&registry);
    restored.setAssetResolver([&](const std::string& id) { return id == asset.id ? &asset : nullptr; });
    REQUIRE(restored.restoreState(saved).ok());
    REQUIRE(restored.lastCommandRequest() != nullptr);
    CHECK_EQ(restored.lastCommandRequest()->requestId, requestId);
    CHECK(!runtime.resumeCommand("stale", StateValue::string("ignored")).ok());
    REQUIRE(runtime.resumeCommand(requestId, StateValue::string("finished")).ok());
    CHECK(!runtime.isActive());

    REQUIRE(runtime.start(&asset).ok());
    const std::string canonicalRequestId = runtime.pendingCommandRequestId();
    REQUIRE(runtime.resumeCommand(canonicalRequestId, eve::Value("canonical")).ok());
    CHECK(!runtime.isActive());
}

TEST_CASE("dialogueConversation.transactionalSelectionStableDiagnostics") {
    SequenceAsset asset;
    asset.id            = "scene.choice";
    asset.entry         = "choice";
    SequenceNode choice = node("choice", "choice");
    choice.routes.push_back(route("yes", "end"));
    asset.nodes = {choice, node("end", "end")};

    StepKindRegistry registry = dialogueRegistry();
    SequenceRuntime  runtime;
    runtime.setStepRegistry(&registry);
    REQUIRE(runtime.start(&asset).ok());
    auto missing = runtime.selectRouteForTransaction("missing");
    REQUIRE(!missing.ok());
    CHECK(runtime.currentNodeId() == "choice");
    REQUIRE(runtime.selectRouteForTransaction("yes").ok());
    CHECK(!runtime.isActive());
    auto notCommand = runtime.resumeCommand("missing", eve::Value("ignored"));
    REQUIRE(!notCommand.ok());
    CHECK_EQ(static_cast<int>(notCommand.error()->code()),
             static_cast<int>(eve::DiagnosticCode::DialogueNotWaitingForCommand));
}

TEST_CASE("dialogueConversation.transactionalSelectionRestoresAfterRouteFailure") {
    SequenceAsset asset;
    asset.id            = "scene.choice-rollback";
    asset.entry         = "choice";
    SequenceNode choice = node("choice", "choice");
    choice.routes.push_back(route("broken", "command"));
    SequenceNode command = node("command", "command", "end");
    command.payload.set("name", eve::Value("missing.handler"));
    asset.nodes = {choice, command, node("end", "end")};

    StepKindRegistry registry = dialogueRegistry();
    SequenceRuntime  runtime;
    runtime.setStepRegistry(&registry);
    REQUIRE(runtime.start(&asset).ok());
    auto failed = runtime.selectRouteForTransaction("broken");
    REQUIRE(!failed.ok());
    CHECK(runtime.isBlocked());
    CHECK_EQ(runtime.currentNodeId(), std::string("choice"));
}

// dialogue_gameplay_control.cpp
// 对话玩法域契约测试：真实 DialogueFlow 运行器走共享玩法协议，
// 覆盖发现、节点观察、路由选择、权限档位、修订号账本、事件与 JSON 门面路由。
//
// 注意：zeroerr 的 CHECK_EQ(lhs, rhs) 会把 lhs 求值两次（一次判等、一次打印），
// 因此有副作用的调用必须先赋给局部变量，再断言变量。


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

/** 观察状态里的字符串字段（缺失返回空串）。 */
[[maybe_unused]] std::string stateText(const eve::Value& state, const char* name) {
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

[[maybe_unused]] eve::GameplayCommand command(const DialogueFixture& fixture, const char* id, const char* actionText,
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
           operation + "\",\"domain\":\"dialogue\",\"instance\":\"" + kInstanceId +
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


// dialogue_hot_reload.cpp
using namespace eve::dialogue;

namespace {
class DialogueMemoryFileSystem final : public eve::service::IFileSystem {
public:
    std::string content;
    bool        readFile(const std::string&, std::vector<std::uint8_t>& out) override {
        out.assign(content.begin(), content.end());
        return true;
    }
    bool writeFile(const std::string&, const void*, size_t) override { return false; }
    bool fileExists(const std::string&) override { return true; }
};
}  // namespace

TEST_CASE("dialogueHotReload.fileLoadingUsesInjectedCapability") {
    eve::cap::detail::clearAllRaw();
    DialogueFlow flow;
    CHECK(!flow.loadDnutFileChecked("missing.dnut").ok());
    DialogueMemoryFileSystem filesystem;
    filesystem.content = "schema \"eve.dnut\"\nversion 1\nconversation loaded entry=end {\nnode end end\n}\n";
    eve::cap::provide<eve::service::IFileSystem>(&filesystem);
    REQUIRE(flow.loadDnutFileChecked("memory.dnut").ok());
    CHECK(flow.hasConversation("loaded"));
    eve::cap::revoke<eve::service::IFileSystem>(&filesystem);
}

TEST_CASE("dialogueHotReload.transactionalCrossFileValidation") {
    DialogueFlow      flow;
    const std::string greeting = R"(
schema "eve.dnut"
version 1
conversation greeting entry=line {
node line line text="hello" next=end
node end end
}
)";
    REQUIRE(flow.loadDnutChecked(greeting, "greeting.dnut").ok());
    CHECK(flow.hasConversation("greeting"));

    CHECK(!flow.reloadDnutChecked("not a conversation", "greeting.dnut").ok());
    CHECK(flow.hasConversation("greeting"));
    CHECK(!flow.getLastLoadChanged());

    const std::string brokenReference = R"(
schema "eve.dnut"
version 1
conversation greeting entry=call {
node call call target=missing next=end
node end end
}
)";
    CHECK(!flow.reloadDnutChecked(brokenReference, "greeting.dnut").ok());
    CHECK(flow.hasConversation("greeting"));
    CHECK(flow.getDiagnosticMessage(0).find("missing conversation") != std::string::npos);

    const std::string shared = R"(
schema "eve.dnut"
version 1
conversation shared entry=end {
node end end
}
)";
    REQUIRE(flow.loadDnutChecked(shared, "shared.dnut").ok());
    const std::string validReference = R"(
schema "eve.dnut"
version 1
conversation greeting version=2 entry=call {
node call call target=shared next=end
node end end
}
)";
    CHECK(flow.reloadDnutChecked(validReference, "greeting.dnut").ok());
    CHECK(flow.getLastLoadChanged());
    CHECK(flow.lintAllChecked().ok());
}

TEST_CASE("dialogueHotReload.rejectsDuplicateCrossSourceOwnership") {
    DialogueFlow      flow;
    const std::string source = R"(
schema "eve.dnut"
version 1
conversation shared.id entry=end {
node end end
}
)";
    REQUIRE(flow.loadDnutChecked(source, "first.dnut").ok());
    CHECK(!flow.loadDnutChecked(source, "second.dnut").ok());
    CHECK(flow.hasConversation("shared.id"));
    CHECK(flow.removeSourceChecked("first.dnut").ok());
    CHECK(!flow.hasConversation("shared.id"));
}

TEST_CASE("dialogueHotReload.loadReleasesDroppedConversationOwnership") {
    DialogueFlow      flow;
    const std::string first = R"(
schema "eve.dnut"
version 1
conversation keep.me entry=end { node end end }
conversation drop.me entry=end { node end end }
)";
    REQUIRE(flow.loadDnutChecked(first, "owner.dnut").ok());
    CHECK(flow.hasConversation("keep.me"));
    CHECK(flow.hasConversation("drop.me"));

    const std::string second = R"(
schema "eve.dnut"
version 1
conversation keep.me entry=end { node end end }
)";
    REQUIRE(flow.loadDnutChecked(second, "owner.dnut").ok());
    CHECK(flow.hasConversation("keep.me"));
    CHECK(!flow.hasConversation("drop.me"));

    const std::string reclaim = R"(
schema "eve.dnut"
version 1
conversation drop.me entry=end { node end end }
)";
    REQUIRE(flow.loadDnutChecked(reclaim, "other.dnut").ok());
    CHECK(flow.hasConversation("drop.me"));
}

TEST_CASE("dialogueHotReload.commitsPoolsAndConversationsAsOneWorkspace") {
    DialogueFlow      flow;
    Dialogue*         dialogue = Dialogue::create();
    const std::string source   = R"(
schema "eve.dnut"
version 1
pool greeting { guide: "hello" }
conversation greeting.scene entry=end { node end end }
)";
    REQUIRE(flow.loadDnutChecked(source, "mixed.dnut").ok());
    CHECK(dialogue->hasPool("greeting"));
    CHECK(flow.hasConversation("greeting.scene"));

    const std::string duplicate = R"(
schema "eve.dnut"
version 1
pool greeting { guide: "duplicate" }
conversation other.scene entry=end { node end end }
)";
    CHECK(!flow.loadDnutChecked(duplicate, "other.dnut").ok());
    CHECK(!flow.hasConversation("other.scene"));
    CHECK(dialogue->hasPool("greeting"));
    CHECK(flow.removeSourceChecked("mixed.dnut").ok());
    CHECK(!dialogue->hasPool("greeting"));
}

TEST_CASE("dialogueHotReload.exposesStableRuntimeLintDiagnostics") {
    DialogueFlow      flow;
    const std::string source = R"(
schema "eve.dnut"
version 1
conversation diagnostics entry=speak {
node speak line speaker=guide text="Hello" next=invoke
node invoke command kind=operation target="world.open" next=end
node end end
}
)";
    REQUIRE(flow.loadDnutChecked(source, "diagnostics.dnut").ok());
    CHECK(!flow.lintAllChecked().ok());
    bool sawHandler      = false;
    bool sawLocalization = false;
    bool sawVoice        = false;
    for (int index = 0; index < flow.getDiagnosticCount(); ++index) {
        CHECK(flow.getDiagnosticLine(index) > 0);
        CHECK(flow.getDiagnosticColumn(index) > 0);
        if (flow.getDiagnosticCode(index) == "MissingCommandHandler") {
            sawHandler = true;
            CHECK(flow.getDiagnosticAssetPath(index) == "diagnostics/invoke");
        } else if (flow.getDiagnosticCode(index) == "MissingLocalizationReference") {
            sawLocalization = true;
            CHECK(flow.getDiagnosticAssetPath(index) == "diagnostics/speak");
        } else if (flow.getDiagnosticCode(index) == "MissingVoiceReference") {
            sawVoice = true;
            CHECK(flow.getDiagnosticAssetPath(index) == "diagnostics/speak");
        }
    }
    CHECK(sawHandler);
    CHECK(sawLocalization);
    CHECK(sawVoice);
    flow.setManualCommandMode(true);
    CHECK(flow.lintAllChecked().ok());
}

TEST_CASE("dialogueHotReload.rejectsUnsupportedDnutVersionWithStableDiagnostic") {
    DialogueFlow flow;
    CHECK(!flow.loadDnutChecked("schema \"eve.dnut\"\nversion 999\n", "future.dnut").ok());
    REQUIRE(flow.getDiagnosticCount() == 1);
    CHECK(flow.getDiagnosticCode(0) == "UnsupportedSchemaVersion");
    CHECK(flow.getDiagnosticLine(0) > 0);
    CHECK(flow.getDiagnosticColumn(0) > 0);
}

// dialogue_importers.cpp
using namespace eve::dialogue;

TEST_CASE("dialogueImporter.yarnSpinner") {
    const std::string                   source = R"(title: Start
---
Guide: Welcome, {player.name}.
[[Continue|Next]]
===
title: Next
---
Guide: Let us begin.
===)";
    std::vector<ConversationDiagnostic> diagnostics;
    auto                                imported = importYarnConversation(source, "intro.yarn", diagnostics);
    REQUIRE(imported.ok());
    auto assets = std::move(imported).takeValue();
    CHECK(assets.size() == 1);
    CHECK(assets[0].id == "intro");
    CHECK(sequencePayloadString(*assets[0].findNode("Start"), "speaker") == "Guide");
    CHECK(assets[0].findNode("Start.1")->routes[0].target == "Next");
}

TEST_CASE("dialogueImporter.twineTwee3") {
    const std::string                   source = R"(:: StoryData
{"ifid":"test"}
:: Start [intro]
Narrator: Choose a destination.
[[Market->Market]]
[[Harbor<-Harbor]]
:: Market
Merchant: Fresh fruit!
:: Harbor
Sailor: Fair winds.)";
    std::vector<ConversationDiagnostic> diagnostics;
    auto                                imported = importTweeConversation(source, "travel.twee", diagnostics);
    REQUIRE(imported.ok());
    auto assets = std::move(imported).takeValue();
    CHECK(assets.size() == 1);
    CHECK(assets[0].findNode("Start.1")->routes.size() == 2);
    CHECK(sequencePayloadString(*assets[0].findNode("Market"), "text") == "Fresh fruit!");
}

TEST_CASE("dialogueImporter.yarnCommandsTagsAndShortcutOptions") {
    const std::string                   source = R"(title: Start
---
Guide: Welcome. #line:intro.welcome #voice:intro_001
<<set $met_guide = true>>
<<wait 0.25>>
-> Continue
    <<jump Next>>
===
title: Next
---
<<stop>>
===)";
    std::vector<ConversationDiagnostic> diagnostics;
    auto                                imported = importYarnConversation(source, "intro.yarn", diagnostics);
    REQUIRE(imported.ok());
    auto assets = std::move(imported).takeValue();
    CHECK(sequencePayloadString(*assets[0].findNode("Start"), "i18n") == "intro.welcome");
    CHECK(sequencePayloadString(*assets[0].findNode("Start"), "voice") == "intro_001");
    CHECK(sequencePayloadString(*assets[0].findNode("Start.1"), "name") == "set");
    CHECK(sequencePayloadString(*assets[0].findNode("Start.2"), "duration") == "0.25");
    CHECK(assets[0].findNode("Start.3")->routes[0].target == "Next");
    CHECK(assets[0].findNode("Next")->type == "end");
}
