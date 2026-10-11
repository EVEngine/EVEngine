#include "dialogue/DialogueSequence.h"
#include "dnut_interpreter/SequenceRuntime.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <string>
#include <vector>

using namespace eve;
using namespace eve::dialogue;
using namespace eve::dnut;

namespace {

SequenceNode node(std::string id, std::string type, std::string next = {}) {
    SequenceNode value;
    value.id   = std::move(id);
    value.type = std::move(type);
    value.next = std::move(next);
    return value;
}

SequenceRoute route(std::string label, std::string target, eve::Value condition = {}) {
    SequenceRoute value;
    value.label     = std::move(label);
    value.target    = std::move(target);
    value.condition = std::move(condition);
    return value;
}

StepKindRegistry dialogueRegistry() {
    StepKindRegistry registry;
    registerDialogueSequenceSteps(registry).expect("dialogue sequence steps");
    return registry;
}

SequenceAsset makeGreeting() {
    SequenceAsset asset;
    asset.id            = "common.greeting";
    asset.entry = "decide";
    asset.parameters    = {SequenceParameter{"speaker"}, SequenceParameter{"listener"}, SequenceParameter{"location"}};
    SequenceNode decide = node("decide", "branch");
    decide.routes = {route("friendly", "friendly", eve::Value("speaker.mood == happy")), route("formal", "formal")};
    SequenceNode friendly = node("friendly", "line", "end");
    friendly.payload.set("speaker", eve::Value("speaker"));
    friendly.payload.set("pool", eve::Value("greeting.friendly"));
    SequenceNode formal = friendly;
    formal.id = "formal";
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
    auto invalid = asset.validate();
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
    child.entry = "line";
    SequenceNode childLine = node("line", "line", "end");
    childLine.payload.set("text", eve::Value("hello"));
    child.nodes = {childLine, node("end", "end")};

    SequenceAsset parent;
    parent.id         = "scene.parent";
    parent.entry = "call";
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
    asset.entry = "calculate";
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
    asset.entry = "animate";
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
    asset.entry = "choice";
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
