// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "dialogue_authoring.cpp",
    "dialogue_text.cpp",
    "dialogue.cpp"
  ]
}
*/
// clang-format on

#include "avatar/Avatar.h"
#include "common/Capability.h"
#include "common/GameplayControl.h"
#include "common/GameplayControlJson.h"
#include "common/ServiceInterfaces.h"
#include "dialogue/ConversationAuthoring.h"
#include "dialogue/ConversationCompiler.h"
#include "dialogue/ConversationImporter.h"
#include "dialogue/ConversationText.h"
#include "dialogue/ConversationToolchain.h"
#include "dialogue/Dialogue.h"
#include "dialogue/DialogueFlow.h"
#include "dialogue/DialogueSequence.h"
#include "dialogue/DialogueUX.h"
#include "dialogue/DialogueVoice.h"
#include "dialogue/DnutParser.h"
#include "dnut_interpreter/DnutCompiler.h"
#include "dnut_interpreter/SequenceRuntime.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using namespace eve::dialogue;
using namespace eve::dnut;

namespace {
SequenceNode node(std::string id, std::string type, std::string next = {}) {
    SequenceNode result;
    result.id   = std::move(id);
    result.type = std::move(type);
    result.next = std::move(next);
    return result;
}
SequenceRoute route(std::string label, std::string target, eve::Value condition = {}) {
    SequenceRoute result;
    result.label     = std::move(label);
    result.target    = std::move(target);
    result.condition = std::move(condition);
    return result;
}
StepKindRegistry registry() {
    StepKindRegistry result;
    registerDialogueSequenceSteps(result).expect("dialogue sequence vocabulary");
    return result;
}
SequenceAsset greeting() {
    SequenceAsset asset;
    asset.id         = "common.greeting";
    asset.entry      = "choice";
    asset.parameters = {SequenceParameter{"speaker"}, SequenceParameter{"listener"}};
    auto choice      = node("choice", "choice");
    choice.routes    = {route("welcome", "line"), route("leave", "end")};
    auto line        = node("line", "line", "end");
    line.payload.set("text", eve::Value("Welcome"));
    asset.nodes = {choice, line, node("end", "end")};
    return asset;
}

constexpr auto  instanceId = "00000000-0000-7000-8000-000000000901";
constexpr auto  ownerId    = "00000000-0000-7000-8000-000000000902";
constexpr auto  strangerId = "00000000-0000-7000-8000-000000000903";
constexpr auto  flowText   = R"(
conversation greeting entry=ask
node ask choice speaker=elder
option buy -> shop
option leave -> end
node shop line speaker=elder text="here you go" next=end
node end end
endconversation
)";
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
std::string textField(const eve::Value& value, const char* key) {
    auto* field = member(value, key);
    return field && field->isString() ? field->asString() : std::string{};
}
eve::GameplayCommand command(eve::SubjectRef owner, const char* id, const char* verb, std::uint64_t revision,
                             eve::Value payload = eve::Value(eve::Value::Object{})) {
    eve::GameplayCommand result;
    result.id               = id;
    result.action           = action(verb);
    result.subject          = owner;
    result.observedTick     = eve::SimulationTick::zero();
    result.expectedRevision = revision;
    result.parameters       = std::move(payload);
    return result;
}
eve::Value stringParameter(const char* key, const char* value) {
    eve::Value::Object result;
    result.emplace(key, eve::Value(value));
    return eve::Value(std::move(result));
}
struct PublishedFlow {
    DialogueFlow*        flow     = DialogueFlow::create();
    eve::SubjectRef      instance = subject(instanceId);
    eve::SubjectRef      owner    = subject(ownerId);
    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {owner}};
    eve::GameplaySession driver{"driver", eve::GameplayAccess::TestDriver, {owner}};
    PublishedFlow() {
        REQUIRE(flow != nullptr);
        flow->clearGameplayControls();
        auto loaded = flow->loadDnutChecked(flowText, "greeting.dnut");
        REQUIRE(loaded.ok());
        REQUIRE(loaded.value() == 1);
        REQUIRE(flow->publishGameplay(instanceId, ownerId).ok());
    }
    ~PublishedFlow() {
        flow->clearGameplayControls();
        flow->clear();
    }
    eve::IGameplayControlProvider& provider() {
        eve::IGameplayControlProvider* found = nullptr;
        eve::cap::forEach<eve::IGameplayControlProvider>([&](auto* candidate) {
            if (candidate && candidate->gameplayDomain() == "dialogue") found = candidate;
        });
        REQUIRE(found != nullptr);
        return *found;
    }
};
std::string request(const char* operation, const char* access, const std::string& extra = {}) {
    return std::string("{\"schemaId\":\"evengine.gameplay-control-request\",\"schemaVersion\":1,\"op\":\"") +
           operation + "\",\"domain\":\"dialogue\",\"instance\":\"" + instanceId +
           "\",\"session\":{\"id\":\"test\",\"access\":\"" + access + "\",\"controlledSubjects\":[\"" + ownerId +
           "\"]}" + extra + "}";
}
std::string commandJson(const char* id, const char* verb, int revision, const char* payload) {
    return std::string(",\"command\":{\"id\":\"") + id + "\",\"action\":\"" + verb + "\",\"subject\":\"" + ownerId +
           "\",\"observedTick\":0,\"expectedRevision\":" + std::to_string(revision) + ",\"parameters\":" + payload +
           "}";
}
}  // namespace

// Authoring edits rewrite references and validation reports dangling routes.
TEST_CASE("dialogue.authoring.documentGraphRenameAndDiagnostics") {
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
    CHECK(document.validate());
    REQUIRE(document.renameNode("decision", "offer"));
    CHECK_EQ(document.getField("start", "next"), std::string("offer"));
    REQUIRE(document.removeNode("end"));
    CHECK(!document.validate());
    CHECK_GT(document.getDiagnosticCount(), 0);
    CHECK_EQ(document.getDiagnosticSeverity(0), std::string("error"));
}

// Parser accepts the declared subset, records source diagnostics and compiler decodes typed payloads.
TEST_CASE("dialogue.compiler.mixedDocumentAndUnknownSyntax") {
    const std::string                   source = R"(schema "eve.dnut"
version 1
pool greeting { guide: "A // inside string" id="greeting.line" }
story arrival { dialogue id=arrival wait 0.35 }
conversation scene entry=end {
 parameter count int optional default=2
 node end end
})";
    std::vector<ConversationDiagnostic> diagnostics;
    auto                                parsed = parseDnutDocument(source, "mixed.dnut", diagnostics);
    REQUIRE(parsed.ok());
    CHECK_EQ(parsed.value().conversations.size(), size_t{1});
    CHECK_EQ(parsed.value().conversations[0].parameters[0].defaultValue.asInt(), 2);
    CHECK(parsed.value().poolRoot.find("pools") != nullptr);
    diagnostics.clear();
    CHECK(!parseDnutDocument("schema \"eve.dnut\"\nversion 1\nunknown thing\n", "bad.dnut", diagnostics).ok());
    REQUIRE(!diagnostics.empty());
    CHECK_EQ(diagnostics[0].code, std::string("DnutParseError"));
}

TEST_CASE("dialogue.compiler.routePaymentAndMutationHaveTypedPayloads") {
    auto steps    = registry();
    auto compiled = compileDnutConversations(
        "schema \"eve.dnut\"\nversion 1\n"
        "conversation shop entry=pick {\n"
        "node pick choice {\n"
        "route buy target=end text=\"Buy\" i18n=choice.buy payment(money=1) "
        "mutation(subject=\"player\", key=\"bought\", kind=\"set\", value=true)\n"
        "}\nnode end end\n}\n",
        "shop.dnut", steps);
    REQUIRE(!compiled.hasErrors());
    const auto& route = compiled.assets.front().findNode("pick")->routes.front();
    CHECK_EQ(sequenceRoutePayloadString(route, "text"), std::string("Buy"));
    auto payment = decodeSequencePayment(route.payload);
    REQUIRE(payment.ok());
    CHECK_EQ(payment.value().money, 1);
    auto mutations = decodeSequenceStateMutations(route.payload);
    REQUIRE(mutations.ok());
    CHECK_EQ(mutations.value().size(), size_t{1});
    auto invalid = compileDnutConversations(
        "schema \"eve.dnut\"\nversion 1\nconversation bad entry=line {\n"
        "node line line text=\"hello\" payment(money=1) next=end\nnode end end\n}\n",
        "bad.dnut", steps);
    CHECK(invalid.hasErrors());
}

// Runtime bindings, route selection, commands and save/restore form a state machine.
TEST_CASE("dialogue.runtime.requiredBindingsAndUnknownBindingsAreRejected") {
    auto            asset = greeting();
    auto            steps = registry();
    SequenceRuntime runtime;
    runtime.setStepRegistry(&steps);
    eve::Value missing = eve::Value::Object{{"speaker", eve::Value("guide")}};
    CHECK(!runtime.start(&asset, missing).ok());
    eve::Value excess = eve::Value::Object{
        {"speaker", eve::Value("guide")}, {"listener", eve::Value("hero")}, {"extra", eve::Value(true)}};
    CHECK(!runtime.start(&asset, excess).ok());
    REQUIRE(!runtime.isActive());
}

TEST_CASE("dialogue.runtime.choiceRequiresRouteAndAdvancesToItsTarget") {
    auto            asset = greeting();
    auto            steps = registry();
    SequenceRuntime runtime;
    runtime.setStepRegistry(&steps);
    eve::Value bindings = eve::Value::Object{{"speaker", eve::Value("guide")}, {"listener", eve::Value("hero")}};
    REQUIRE(runtime.start(&asset, bindings).ok());
    CHECK(runtime.isBlocked());
    CHECK_EQ(runtime.currentNodeId(), std::string("choice"));
    CHECK(!runtime.advance().ok());
    CHECK(!runtime.selectRouteForTransaction("unknown").ok());
    CHECK_EQ(runtime.currentNodeId(), std::string("choice"));
    REQUIRE(runtime.selectRouteForTransaction("welcome").ok());
    CHECK_EQ(runtime.currentNodeId(), std::string("line"));
    REQUIRE(runtime.advance().ok());
    CHECK(!runtime.isActive());
}

TEST_CASE("dialogue.runtime.failedRouteRestoresSelectionPoint") {
    SequenceAsset asset;
    asset.id    = "scene";
    asset.entry = "choice";
    auto choice = node("choice", "choice");
    choice.routes.push_back(route("broken", "command"));
    auto call = node("command", "command", "end");
    call.payload.set("name", eve::Value("missing.handler"));
    asset.nodes           = {choice, call, node("end", "end")};
    auto            steps = registry();
    SequenceRuntime runtime;
    runtime.setStepRegistry(&steps);
    REQUIRE(runtime.start(&asset).ok());
    CHECK(!runtime.selectRouteForTransaction("broken").ok());
    CHECK(runtime.isBlocked());
    CHECK_EQ(runtime.currentNodeId(), std::string("choice"));
}

TEST_CASE("dialogue.runtime.blockedCommandRequestSurvivesSaveAndRequiresMatchingResume") {
    SequenceAsset asset;
    asset.id    = "scene.wait";
    asset.entry = "command";
    auto call   = node("command", "command", "end");
    call.payload.set("name", eve::Value("animation.play"));
    call.payload.set("resultLocal", eve::Value("result"));
    asset.nodes           = {call, node("end", "end")};
    auto            steps = registry();
    SequenceRuntime runtime;
    runtime.setStepRegistry(&steps);
    runtime.registerCommand("animation.play", [](const SequenceCommandRequest&) {
        SequenceCommandResponse response;
        response.status = SequenceCommandResponse::Status::Blocked;
        return response;
    });
    REQUIRE(runtime.start(&asset).ok());
    CHECK(runtime.isWaitingCommand());
    CHECK(!runtime.advance().ok());
    auto requestId = runtime.pendingCommandRequestId();
    REQUIRE(!requestId.empty());
    eve::Value saved;
    REQUIRE(runtime.captureState(saved).ok());
    SequenceRuntime restored;
    restored.setStepRegistry(&steps);
    restored.setAssetResolver([&](const std::string& id) { return id == asset.id ? &asset : nullptr; });
    REQUIRE(restored.restoreState(saved).ok());
    REQUIRE(restored.lastCommandRequest() != nullptr);
    CHECK_EQ(restored.lastCommandRequest()->requestId, requestId);
    CHECK(!runtime.resumeCommand("stale", eve::Value("ignored")).ok());
    REQUIRE(runtime.resumeCommand(requestId, eve::Value("finished")).ok());
    CHECK(!runtime.isActive());
}

TEST_CASE("dialogue.runtime.expressionFailureDoesNotSelectFallback") {
    SequenceAsset asset;
    asset.id              = "conditional";
    asset.entry           = "branch";
    auto branch           = node("branch", "branch");
    branch.routes         = {route("yes", "line", eve::Value("bad expression")), route("else", "end")};
    asset.nodes           = {branch, node("line", "line", "end"), node("end", "end")};
    auto            steps = registry();
    SequenceRuntime runtime;
    runtime.setStepRegistry(&steps);
    runtime.setConditionEvaluator(
        [](const eve::Value&) { return SequenceConditionOutcome{false, "failed", "expression failed"}; });
    auto result = runtime.start(&asset);
    CHECK(!result.ok());
    REQUIRE(result.status().describe().find("expression failed") != std::string::npos);
}

// Published gameplay offers the same route vocabulary to a player and a test driver.
TEST_CASE("dialogue.gameplay.idleObservationAndActionsRespectOwner") {
    PublishedFlow fixture;
    auto&         provider = fixture.provider();
    auto          observed = provider.observeGameplay(fixture.player, fixture.instance);
    REQUIRE(observed.ok());
    CHECK_EQ(observed.value().domain.format(), std::string("gameplay:dialogue"));
    CHECK_EQ(observed.value().revision, std::uint64_t{0});
    CHECK(!member(observed.value().state, "active")->asBool());
    CHECK_EQ(textField(observed.value().state, "node"), std::string{});
    auto actions = provider.availableGameplayActions(fixture.player, fixture.instance, fixture.owner);
    auto driver  = provider.availableGameplayActions(fixture.driver, fixture.instance, fixture.owner);
    REQUIRE(actions.ok());
    REQUIRE(driver.ok());
    CHECK_EQ(actions.value().size(), size_t{3});
    CHECK_EQ(actions.value()[0].id.format(), std::string("dialogue:start"));
    CHECK_EQ(actions.value()[1].id.format(), std::string("dialogue:advance"));
    CHECK_EQ(actions.value()[2].id.format(), std::string("dialogue:select"));
    CHECK_EQ(driver.value().size(), actions.value().size());
    eve::GameplaySession intruder{"intruder", eve::GameplayAccess::PlayerEquivalent, {subject(strangerId)}};
    CHECK(!provider.observeGameplay(intruder, fixture.instance).ok());
    CHECK(!provider.availableGameplayActions(fixture.player, fixture.instance, subject(strangerId)).ok());
}

TEST_CASE("dialogue.gameplay.startSelectAdvanceAndEventSequence") {
    PublishedFlow fixture;
    auto&         provider = fixture.provider();
    CHECK(!provider
               .submitGameplay(fixture.player, fixture.instance, command(fixture.owner, "early", "dialogue:advance", 0))
               .ok());
    auto start = provider.submitGameplay(
        fixture.player, fixture.instance,
        command(fixture.owner, "start", "dialogue:start", 0, stringParameter("conversation", "greeting")));
    REQUIRE(start.ok());
    CHECK_EQ(start.value().resultingRevision, std::uint64_t{1});
    CHECK_EQ(textField(start.value().details, "node"), std::string("ask"));
    auto observed = provider.observeGameplay(fixture.player, fixture.instance);
    REQUIRE(observed.ok());
    auto* routes = member(observed.value().state, "routes")->getIf<eve::Value::Array>();
    REQUIRE(routes != nullptr);
    REQUIRE(routes->size() == size_t{2});
    CHECK_EQ(textField((*routes)[0], "route"), std::string("buy"));
    CHECK(
        !provider
             .submitGameplay(fixture.player, fixture.instance, command(fixture.owner, "blocked", "dialogue:advance", 1))
             .ok());
    auto selected = provider.submitGameplay(
        fixture.player, fixture.instance,
        command(fixture.owner, "select", "dialogue:select", 1, stringParameter("route", "buy")));
    REQUIRE(selected.ok());
    CHECK_EQ(selected.value().resultingRevision, std::uint64_t{2});
    CHECK_EQ(textField(selected.value().details, "node"), std::string("shop"));
    auto finish = provider.submitGameplay(fixture.player, fixture.instance,
                                          command(fixture.owner, "finish", "dialogue:advance", 2));
    REQUIRE(finish.ok());
    CHECK_EQ(finish.value().resultingRevision, std::uint64_t{3});
    CHECK(!member(finish.value().details, "active")->asBool());
    auto events = provider.gameplayEvents(fixture.player, fixture.instance, 0);
    REQUIRE(events.ok());
    REQUIRE(events.value().size() == size_t{3});
    CHECK_EQ(events.value()[0].type, std::string("dialogue.started"));
    CHECK_EQ(events.value()[1].type, std::string("dialogue.selected"));
    CHECK_EQ(events.value()[2].type, std::string("dialogue.finished"));
}

TEST_CASE("dialogue.gameplay.invalidConversationRevisionRouteAndOperationDoNotAdvance") {
    PublishedFlow fixture;
    auto&         provider = fixture.provider();
    CHECK(!provider
               .submitGameplay(
                   fixture.player, fixture.instance,
                   command(fixture.owner, "unknown", "dialogue:start", 0, stringParameter("conversation", "missing")))
               .ok());
    REQUIRE(provider
                .submitGameplay(
                    fixture.player, fixture.instance,
                    command(fixture.owner, "start", "dialogue:start", 0, stringParameter("conversation", "greeting")))
                .ok());
    CHECK(!provider
               .submitGameplay(fixture.player, fixture.instance,
                               command(fixture.owner, "stale", "dialogue:select", 0, stringParameter("route", "buy")))
               .ok());
    CHECK(!provider
               .submitGameplay(fixture.player, fixture.instance, command(fixture.owner, "empty", "dialogue:select", 1))
               .ok());
    CHECK(!provider
               .submitGameplay(fixture.player, fixture.instance, command(fixture.owner, "unknown", "dialogue:shrug", 1))
               .ok());
    auto observed = provider.observeGameplay(fixture.player, fixture.instance);
    REQUIRE(observed.ok());
    CHECK_EQ(observed.value().revision, std::uint64_t{1});
    CHECK_EQ(textField(observed.value().state, "node"), std::string("ask"));
}

TEST_CASE("dialogue.gameplay.singleRunnerPublicationAndUnpublication") {
    PublishedFlow fixture;
    CHECK_EQ(fixture.flow->gameplayControlCount(), 1);
    CHECK(!fixture.flow->publishGameplay("00000000-0000-7000-8000-000000000904", "00000000-0000-7000-8000-000000000905")
               .ok());
    REQUIRE(fixture.flow->unpublishGameplay(instanceId).ok());
    CHECK_EQ(fixture.flow->gameplayControlCount(), 0);
    CHECK(!fixture.flow->unpublishGameplay(instanceId).ok());
    REQUIRE(
        fixture.flow->publishGameplay("00000000-0000-7000-8000-000000000904", "00000000-0000-7000-8000-000000000905")
            .ok());
    CHECK_EQ(fixture.flow->gameplayControlCount(), 1);
}

TEST_CASE("dialogue.gameplay.jsonFacadeUsesSameStartAndRouteContract") {
    PublishedFlow fixture;
    auto          observed = eve::executeGameplayControlJson(request("observe", "player"));
    REQUIRE(observed.ok());
    CHECK(observed.value().find("gameplay:dialogue") != std::string::npos);
    auto started = eve::executeGameplayControlJson(
        request("submit", "player", commandJson("start", "dialogue:start", 0, R"({"conversation":"greeting"})")));
    REQUIRE(started.ok());
    CHECK(started.value().find("\"node\":\"ask\"") != std::string::npos);
    auto selected = eve::executeGameplayControlJson(
        request("submit", "player", commandJson("select", "dialogue:select", 1, R"({"route":"buy"})")));
    REQUIRE(selected.ok());
    CHECK(selected.value().find("\"node\":\"shop\"") != std::string::npos);
    auto events = eve::executeGameplayControlJson(request("events", "player", ",\"afterSequence\":0"));
    REQUIRE(events.ok());
    CHECK(events.value().find("dialogue.started") != std::string::npos);
    CHECK(events.value().find("dialogue.selected") != std::string::npos);
}

// Reload owns whole sources and commits pool/conversation changes only after workspace validation.
TEST_CASE("dialogue.reload.malformedAndCrossFileFailurePreservePriorSource") {
    DialogueFlow      flow;
    const std::string original = R"(schema "eve.dnut"
version 1
conversation greeting entry=line {
node line line text="hello" next=end
node end end
})";
    REQUIRE(flow.loadDnutChecked(original, "greeting.dnut").ok());
    CHECK(!flow.reloadDnutChecked("not a conversation", "greeting.dnut").ok());
    CHECK(flow.hasConversation("greeting"));
    CHECK(!flow.getLastLoadChanged());
    const std::string broken = R"(schema "eve.dnut"
version 1
conversation greeting entry=call {
node call call target=missing next=end
node end end
})";
    CHECK(!flow.reloadDnutChecked(broken, "greeting.dnut").ok());
    CHECK(flow.hasConversation("greeting"));
    CHECK(flow.getDiagnosticMessage(0).find("missing conversation") != std::string::npos);
}

TEST_CASE("dialogue.reload.crossSourceOwnershipAndRemoval") {
    DialogueFlow      flow;
    const std::string first = R"(schema "eve.dnut"
version 1
conversation shared entry=end { node end end }
conversation dropped entry=end { node end end })";
    REQUIRE(flow.loadDnutChecked(first, "first.dnut").ok());
    CHECK(!flow.loadDnutChecked(first, "other.dnut").ok());
    const std::string revised = R"(schema "eve.dnut"
version 1
conversation shared entry=end { node end end })";
    REQUIRE(flow.loadDnutChecked(revised, "first.dnut").ok());
    CHECK(!flow.hasConversation("dropped"));
    const std::string reclaimed = R"(schema "eve.dnut"
version 1
conversation dropped entry=end { node end end })";
    REQUIRE(flow.loadDnutChecked(reclaimed, "other.dnut").ok());
    CHECK(flow.hasConversation("dropped"));
    REQUIRE(flow.removeSourceChecked("first.dnut").ok());
    CHECK(!flow.hasConversation("shared"));
    CHECK(flow.hasConversation("dropped"));
}

TEST_CASE("dialogue.reload.poolAndConversationCommitTogether") {
    DialogueFlow      flow;
    auto*             dialogue = Dialogue::create();
    const std::string source   = R"(schema "eve.dnut"
version 1
pool greeting { guide: "hello" }
conversation scene entry=end { node end end })";
    REQUIRE(flow.loadDnutChecked(source, "one.dnut").ok());
    CHECK(dialogue->hasPool("greeting"));
    CHECK(flow.hasConversation("scene"));
    const std::string conflict = R"(schema "eve.dnut"
version 1
pool greeting { guide: "duplicate" }
conversation other entry=end { node end end })";
    CHECK(!flow.loadDnutChecked(conflict, "two.dnut").ok());
    CHECK(!flow.hasConversation("other"));
    CHECK(dialogue->hasPool("greeting"));
    REQUIRE(flow.removeSourceChecked("one.dnut").ok());
    CHECK(!dialogue->hasPool("greeting"));
}

TEST_CASE("dialogue.reload.unsupportedSchemaVersionHasLocatedDiagnostic") {
    DialogueFlow flow;
    CHECK(!flow.loadDnutChecked("schema \"eve.dnut\"\nversion 999\n", "future.dnut").ok());
    REQUIRE(flow.getDiagnosticCount() == 1);
    CHECK_EQ(flow.getDiagnosticCode(0), std::string("UnsupportedSchemaVersion"));
    CHECK_GT(flow.getDiagnosticLine(0), 0);
    CHECK_GT(flow.getDiagnosticColumn(0), 0);
}

// Importers normalize Yarn and Twee into the same nodes/routes the runtime consumes.
TEST_CASE("dialogue.import.yarnLinesChoicesCommandsAndTags") {
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
    auto                                result = importYarnConversation(source, "intro.yarn", diagnostics);
    REQUIRE(result.ok());
    auto& asset = result.value()[0];
    CHECK_EQ(sequencePayloadString(*asset.findNode("Start"), "i18n"), std::string("intro.welcome"));
    CHECK_EQ(sequencePayloadString(*asset.findNode("Start"), "voice"), std::string("intro_001"));
    CHECK_EQ(sequencePayloadString(*asset.findNode("Start.1"), "name"), std::string("set"));
    CHECK_EQ(sequencePayloadString(*asset.findNode("Start.2"), "duration"), std::string("0.25"));
    CHECK_EQ(asset.findNode("Start.3")->routes[0].target, std::string("Next"));
}

TEST_CASE("dialogue.import.tweeLinksProduceChoiceTargets") {
    const std::string                   source = R"(:: StoryData
{"ifid":"test"}
:: Start [intro]
Narrator: Choose.
[[Market->Market]]
[[Harbor<-Harbor]]
:: Market
Merchant: Fruit!
:: Harbor
Sailor: Winds.)";
    std::vector<ConversationDiagnostic> diagnostics;
    auto                                result = importTweeConversation(source, "travel.twee", diagnostics);
    REQUIRE(result.ok());
    auto& asset = result.value()[0];
    REQUIRE(asset.findNode("Start.1") != nullptr);
    CHECK_EQ(asset.findNode("Start.1")->routes.size(), size_t{2});
    CHECK_EQ(sequencePayloadString(*asset.findNode("Market"), "text"), std::string("Fruit!"));
}

// Text/UX/voice: formatting, reading state and audio completion use observable time.
TEST_CASE("dialogue.text.localsFallbackTransformsAndUnknownPlaceholders") {
    eve::Value bindings = eve::Value::Object{{"speaker", eve::Value::Object{{"name", eve::Value("mara")}}},
                                             {"listener", eve::Value::Object{{"name", eve::Value("Iris")}}},
                                             {"location", eve::Value::Object{{"name", eve::Value("harbor")}}}};
    eve::Value locals   = eve::Value::Object{{"speaker", eve::Value::Object{{"name", eve::Value("local mara")}}}};
    ConversationTextRenderer renderer;
    CHECK_EQ(
        renderer.render("{speaker.name|capitalize} greets {listener.name} at {location.name|upper}.", bindings, locals),
        std::string("Local mara greets Iris at HARBOR."));
    CHECK_EQ(renderer.render("Welcome, {listener.title??traveler}.", bindings, locals),
             std::string("Welcome, traveler."));
    REQUIRE_EQ(renderer.render("Keep {unknown.value} visible.", bindings, locals),
               std::string("Keep {unknown.value} visible."));
}

TEST_CASE("dialogue.ux.historyReadSkipAndVoiceWait") {
    auto* ux = DialogueUX::create();
    ux->clearHistory();
    ux->setSkipMode("off");
    ux->record("intro.1", "Alice", "Hello [pause=0.2][color=red]world[/color]!");
    CHECK_EQ(ux->getHistoryCount(), 1);
    CHECK_EQ(ux->getHistoryText(0), std::string("Hello world!"));
    CHECK(ux->isRead("intro.1"));
    REQUIRE(ux->setSkipMode("read"));
    CHECK(ux->shouldSkip("intro.1"));
    CHECK(!ux->shouldSkip("intro.2"));
    ux->setAutoMode(true);
    ux->setAutoDelay(.5f);
    ux->resetAutoTimer();
    CHECK(!ux->updateAuto(1.f, true));
    CHECK(!ux->updateAuto(.25f, false));
    CHECK(ux->updateAuto(.25f, false));
}

TEST_CASE("dialogue.voice.localeFallbackEnvelopeAndAutoAdvance") {
    auto* voice = DialogueVoice::create();
    voice->clear();
    voice->setEnvelopeRate(4.f);
    REQUIRE(voice->registerClip("intro.1", "en", 1.f, "0,0.25,0.75,1"));
    REQUIRE(voice->registerClip("intro.1", "", .5f, "0.1,0.2"));
    REQUIRE(voice->play("intro.1", "en"));
    voice->update(.5f);
    CHECK_EQ(voice->getAmplitude(), .75f);
    CHECK(voice->isPlaying());
    voice->update(.5f);
    CHECK(!voice->isPlaying());
    CHECK(voice->shouldAutoAdvance());
    REQUIRE(voice->play("intro.1", "zh"));
    CHECK_EQ(voice->getDuration(), .5f);
}

// Facade: speaker registration, choices and UTF-8 typewriter transitions.
TEST_CASE("dialogue.facade.speakerTypewriterChoicesAndUtf8") {
    auto* dialogue = Dialogue::create();
    dialogue->reset();
    REQUIRE(dialogue->registerCharacter("hero", "Hero"));
    dialogue->setTypeSpeed(10.f);
    dialogue->say("hero", "Hello");
    CHECK(dialogue->isTyping());
    CHECK_EQ(dialogue->getSpeakerName(), std::string("Hero"));
    dialogue->update(.2f);
    CHECK_EQ(dialogue->getVisibleText(), std::string("He"));
    dialogue->skipTyping();
    CHECK(dialogue->isWaitingAdvance());
    dialogue->clearChoices();
    REQUIRE(dialogue->addChoice("a", "Alpha"));
    REQUIRE(dialogue->addChoice("b", "Beta"));
    dialogue->presentChoices();
    CHECK(dialogue->isWaitingChoice());
    CHECK(!dialogue->selectChoice(99));
    REQUIRE(dialogue->selectChoice(1));
    CHECK_EQ(dialogue->getSelectedChoiceId(), std::string("b"));
    CHECK(dialogue->isIdle());
    dialogue->setTypeSpeed(100.f);
    const std::string two = "\xe4\xbd\xa0\xe5\xa5\xbd";
    const std::string one = "\xe4\xbd\xa0";
    dialogue->narrate(two);
    dialogue->update(.01f);
    CHECK_EQ(dialogue->getVisibleText(), one);
    dialogue->skipTyping();
    CHECK_EQ(dialogue->getVisibleText(), two);
}

TEST_CASE("dialogue.toolchain.crossFileRenameUpdatesCallAndMissingTargetLint") {
    SequenceAsset caller;
    caller.id    = "quest";
    caller.entry = "call";
    auto call    = node("call", "call", "end");
    call.payload.set("target", eve::Value("greeting"));
    caller.nodes = {call, node("end", "end")};
    SequenceAsset callee;
    callee.id    = "greeting";
    callee.entry = "line";
    callee.nodes = {node("line", "line", "end"), node("end", "end")};
    std::vector<SequenceAsset>          assets{caller, callee};
    std::vector<ConversationDiagnostic> diagnostics;
    REQUIRE(lintConversationWorkspace(assets, "workspace", diagnostics).ok());
    REQUIRE(renameConversationAsset(assets, "greeting", "common.greeting").ok());
    CHECK_EQ(sequencePayloadString(*assets[0].findNode("call"), "target"), std::string("common.greeting"));
    REQUIRE(renameConversationNode(assets, "common.greeting", "line", "welcome").ok());
    CHECK_EQ(assets[1].entry, std::string("welcome"));
    assets.erase(assets.begin() + 1);
    diagnostics.clear();
    CHECK(!lintConversationWorkspace(assets, "workspace", diagnostics).ok());
    CHECK(diagnostics.back().message.find("missing conversation") != std::string::npos);
}

TEST_CASE("dialogue.text.toneRulesDependOnNamedState") {
    ConversationTextRenderer renderer;
    renderer.addToneRule("speaker.tired", "... ", " ...", "Good", "Fine");
    renderer.addToneRule("speaker.formal", "Captain, ", {}, {}, {});
    eve::Value empty = eve::Value::Object{};
    CHECK_EQ(
        renderer.render("Good morning.", empty, empty, [](const std::string& key) { return key == "speaker.tired"; }),
        std::string("... Fine morning. ..."));
    REQUIRE_EQ(
        renderer.render("Good morning.", empty, empty, [](const std::string& key) { return key == "speaker.formal"; }),
        std::string("Captain, Good morning."));
}

class MemoryDialogueFiles final : public eve::service::IFileSystem {
public:
    std::string content;
    bool        readFile(const std::string&, std::vector<std::uint8_t>& output) override {
        output.assign(content.begin(), content.end());
        return true;
    }
    bool writeFile(const std::string&, const void*, size_t) override { return false; }
    bool fileExists(const std::string&) override { return true; }
};

TEST_CASE("dialogue.reload.fileLoadingUsesInjectedFilesystemCapability") {
    eve::cap::detail::clearAllRaw();
    DialogueFlow flow;
    CHECK(!flow.loadDnutFileChecked("missing.dnut").ok());
    MemoryDialogueFiles files;
    files.content = "schema \"eve.dnut\"\nversion 1\nconversation loaded entry=end {\nnode end end\n}\n";
    eve::cap::provide<eve::service::IFileSystem>(&files);
    REQUIRE(flow.loadDnutFileChecked("memory.dnut").ok());
    CHECK(flow.hasConversation("loaded"));
    eve::cap::revoke<eve::service::IFileSystem>(&files);
}

TEST_CASE("dialogue.facade.stageProjectionTracksSlotAndVisibility") {
    auto* dialogue = Dialogue::create();
    auto* avatars  = eve::avatar::Avatar::create();
    dialogue->reset();
    REQUIRE(dialogue->registerCharacter("alice", "Alice"));
    auto* portrait = avatars->newImageAvatar();
    REQUIRE(portrait != nullptr);
    portrait->addLayer("body", nullptr, 0);
    portrait->setLayerSize("body", 100.f, 200.f);
    REQUIRE(dialogue->bindAvatar("alice", portrait));
    dialogue->setSlotX("left", .2f);
    REQUIRE(dialogue->show("alice", "left"));
    dialogue->syncStage(800.f, 600.f);
    CHECK_EQ(dialogue->getSlot("alice"), std::string("left"));
    CHECK_EQ(portrait->getX(), 160.f);
    CHECK(portrait->isVisible());
    REQUIRE(dialogue->hide("alice"));
    CHECK(!dialogue->isShown("alice"));
    dialogue->reset();
    portrait->release();
    delete portrait;
}
