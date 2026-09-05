#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Diagnostic.h"
#include "common/Capability.h"
#include "common/GameplayControl.h"
#include "common/GameplayControlJson.h"
#include "common/Value.h"
#include "devtools/AgentDevelopmentSession.hpp"
#include "devtools/PlayHost.h"
#include "devtools/PlayTrace.h"

#include <string>
#include <vector>

using namespace eve;
using namespace eve::dev;

namespace {

class FakePlayHostRuntime final : public IPlayHostRuntime {
public:
    bool paused() const override { return paused_; }
    std::uint64_t hostFrame() const override { return hostFrame_; }
    void pause() override { paused_ = true; }
    void play() override { paused_ = false; }

    Result<void> stepFrames(std::int64_t count) override {
        hostFrame_ += static_cast<std::uint64_t>(count);
        paused_ = true;
        return Result<void>::success();
    }

    Result<Value> loadContract() const override { return Result<Value>::success(contract); }

    Result<Value> observeScriptRoot(std::string_view root,
                                    const std::vector<std::string>& fields) const override {
        const auto* object = roots.getIf<Value::Object>();
        REQUIRE(object);
        const auto found = object->find(std::string(root));
        REQUIRE(found != object->end());
        if (fields.empty()) return Result<Value>::success(found->second);
        Value::Object projected;
        for (const auto& field : fields) {
            const auto* node = found->second.getIf<Value::Object>();
            REQUIRE(node);
            auto child = node->find(field);
            REQUIRE(child != node->end());
            projected.emplace(field, child->second);
        }
        return Result<Value>::success(Value(std::move(projected)));
    }

    Result<Value> capturePng(std::string path) override {
        return Result<Value>::success(Value(Value::Object{{"height", Value(std::int64_t{2})},
                                                          {"path", Value(std::move(path))},
                                                          {"width", Value(std::int64_t{3})}}));
    }

    Result<std::string> captureCheckpoint() override { return Result<std::string>::success(checkpoint); }

    Result<void> restoreCheckpoint(std::string_view json) override {
        auto parsed = Value::fromJson(json);
        REQUIRE(parsed.ok());
        const auto* object = parsed.value().getIf<Value::Object>();
        REQUIRE(object);
        const auto found = object->find("roots");
        REQUIRE(found != object->end());
        roots = found->second;
        return Result<void>::success();
    }

    std::vector<std::string> gameplayDomains() const override { return {"json-fixture"}; }

    Result<Value> invokeScriptAction(std::string_view id, std::string_view source) override {
        lastAction = std::string(id);
        lastScript = std::string(source);
        if (id == "reset") {
            auto* object = roots.getIf<Value::Object>();
            if (object) {
                auto gameState = object->find("gameState");
                if (gameState != object->end()) {
                    auto* state = gameState->second.getIf<Value::Object>();
                    if (state) {
                        Value::Object copy = *state;
                        copy["tick"]       = Value(std::int64_t{0});
                        (*object)["gameState"] = Value(std::move(copy));
                    }
                }
            }
        }
        return Result<Value>::success(Value(Value::Object{{"action", Value(std::string(id))}}));
    }

    Value       contract;
    Value       roots;
    std::string checkpoint;
    std::string lastAction;
    std::string lastScript;

private:
    bool          paused_    = false;
    std::uint64_t hostFrame_ = 0;
};

Value parse(std::string_view json) {
    auto value = Value::fromJson(json);
    REQUIRE(value.ok());
    return std::move(value).takeValue();
}

SubjectRef subject(const char* text) {
    const auto id = PersistentId::parse(text);
    REQUIRE(id.has_value());
    return SubjectRef::fromPersistentId(*id);
}

LogicalId logical(const char* text) {
    const auto id = LogicalId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

class JsonProvider final : public IGameplayControlProvider {
public:
    explicit JsonProvider(SubjectRef instance) : instance_(instance) {
        cap::addListener<IGameplayControlProvider>(this);
    }
    ~JsonProvider() override { cap::removeListener<IGameplayControlProvider>(this); }

    std::string_view gameplayDomain() const noexcept override { return "json-fixture"; }
    [[nodiscard]] Result<GameplayObservation> observeGameplay(const GameplaySession&,
                                                              SubjectRef instance) const override {
        if (instance != instance_)
            return Result<GameplayObservation>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "fixture not found", "instance"));
        GameplayObservation result;
        result.domain   = logical("gameplay:json-fixture");
        result.instance = instance_;
        result.tick     = tick_;
        result.revision = revision_;
        result.state    = Value(Value::Object{{"count", Value(count_)}});
        return Result<GameplayObservation>::success(std::move(result));
    }
    [[nodiscard]] Result<std::vector<GameplayActionDescriptor>> availableGameplayActions(
        const GameplaySession&, SubjectRef, SubjectRef) const override {
        return Result<std::vector<GameplayActionDescriptor>>::success(
            {{logical("fixture:increment"), Value(Value::Object{})}});
    }
    [[nodiscard]] Result<GameplayCommandReceipt> submitGameplay(const GameplaySession&, SubjectRef,
                                                                const GameplayCommand& command) override {
        ++count_;
        ++revision_;
        GameplayCommandReceipt result;
        result.commandId          = command.id;
        result.executionId        = "fixture-execution";
        result.acceptedTick       = tick_;
        result.resultingRevision  = revision_;
        result.details            = Value(Value::Object{{"count", Value(count_)}});
        return Result<GameplayCommandReceipt>::success(std::move(result));
    }
    [[nodiscard]] Result<GameplayObservation> advanceGameplay(const GameplaySession& session, SubjectRef instance,
                                                              const SimulationStep& step) override {
        tick_ = step.tick;
        return observeGameplay(session, instance);
    }
    [[nodiscard]] Result<std::vector<GameplayEvent>> gameplayEvents(const GameplaySession&, SubjectRef,
                                                                    std::uint64_t) const override {
        return Result<std::vector<GameplayEvent>>::success({});
    }

private:
    SubjectRef      instance_;
    SimulationTick  tick_     = SimulationTick::zero();
    std::uint64_t   revision_ = 1;
    int             count_    = 0;
};

const char* kScriptContract = R"json({
  "schemaId": "evengine.game-agent-contract",
  "schemaVersion": 1,
  "id": "examples/ai-game",
  "entry": "main.nut",
  "clock": {"defaultStep": "frame"},
  "observations": [
    {"id": "combat-alive", "kind": "script-root", "path": "gameState", "fields": ["tick"]}
  ],
  "actions": {"source": "script-map", "map": [{"id": "reset", "script": "game.reset()"}]},
  "capture": {"requiredFor": ["visual"], "backend": "engine-readback"}
})json";

const char* kGameplayContract = R"json({
  "schemaId": "evengine.game-agent-contract",
  "schemaVersion": 1,
  "id": "json-fixture-play",
  "entry": "main.nut",
  "clock": {"defaultStep": "frame"},
  "observations": [
    {"id": "combat-alive", "kind": "script-root", "path": "gameState", "fields": ["tick"]}
  ],
  "actions": {
    "source": "gameplay-domain",
    "domain": "json-fixture",
    "map": [{"id": "increment", "action": "fixture:increment"}]
  }
})json";

}  // namespace

TEST_CASE("devtools.playTrace.rejectsUnknownFieldsAndReplaysStableDigest") {
    PlayTraceBuffer::instance().clear();
    FakePlayHostRuntime runtime;
    runtime.contract = parse(kScriptContract);
    runtime.roots    = parse(R"({"gameState":{"tick":4}})");
    runtime.checkpoint =
        R"({"version":2,"roots":{"gameState":{"tick":4}}})";

    auto unknown = parsePlayTrace(parse(R"({"schemaId":"evengine.play-trace","schemaVersion":1,"steps":[],"extra":true})"));
    CHECK(!unknown.ok());
    CHECK_EQ(unknown.status().diagnostics().front().code(), DiagnosticCode::ParseError);

    auto observed = executePlayRequest(
        parse(R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"observe","observation":"combat-alive","trace":"append"})"),
        runtime);
    REQUIRE(observed.ok());
    auto acted = executePlayRequest(
        parse(R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"act","action":"reset","trace":"append"})"),
        runtime);
    REQUIRE(acted.ok());
    CHECK_EQ(runtime.lastAction, std::string("reset"));
    CHECK_EQ(runtime.lastScript, std::string("game.reset()"));
    auto afterReset = executePlayRequest(
        parse(R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"observe","observation":"combat-alive","trace":"append"})"),
        runtime);
    REQUIRE(afterReset.ok());
    CHECK_EQ(afterReset.value().getIf<Value::Object>()->at("state").getIf<Value::Object>()->at("tick").asInt(),
             std::int64_t{0});

    auto exported = PlayTraceBuffer::instance().exportTrace();
    const auto* traceObject = exported.getIf<Value::Object>();
    REQUIRE(traceObject);
    Value recording = exported;
    recording.set("startCheckpoint", Value(std::string(R"({"version":2,"roots":{"gameState":{"tick":4}}})")));

    runtime.roots = parse(R"({"gameState":{"tick":99}})");
    auto first    = replayPlayTrace(recording, runtime);
    REQUIRE(first.ok());
    auto digestA = playObservationDigest(runtime.roots.getIf<Value::Object>()->at("gameState"));
    REQUIRE(digestA.ok());

    runtime.roots = parse(R"({"gameState":{"tick":77}})");
    auto second   = replayPlayTrace(recording, runtime);
    REQUIRE(second.ok());
    auto digestB = playObservationDigest(runtime.roots.getIf<Value::Object>()->at("gameState"));
    REQUIRE(digestB.ok());
    CHECK_EQ(digestA.value(), digestB.value());

    PlayTraceBuffer::instance().clear();
    runtime.roots = parse(R"({"gameState":{"tick":4}})");
    REQUIRE(executePlayRequest(
                parse(R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"observe","observation":"combat-alive","trace":"append"})"),
                runtime)
                .ok());
    auto passOne = PlayTraceBuffer::instance().exportTrace();
    PlayTraceBuffer::instance().clear();
    runtime.roots = parse(R"({"gameState":{"tick":4}})");
    REQUIRE(executePlayRequest(
                parse(R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"observe","observation":"combat-alive","trace":"append"})"),
                runtime)
                .ok());
    auto passTwo = PlayTraceBuffer::instance().exportTrace();
    const std::string firstDigest =
        passOne.getIf<Value::Object>()->at("steps").getIf<Value::Array>()->at(0)
            .getIf<Value::Object>()
            ->at("observationDigest")
            .asString();
    const std::string secondDigest =
        passTwo.getIf<Value::Object>()->at("steps").getIf<Value::Array>()->at(0)
            .getIf<Value::Object>()
            ->at("observationDigest")
            .asString();
    CHECK_EQ(firstDigest, secondDigest);
}

TEST_CASE("devtools.playHost.actMatchesGameplayControlSubmit") {
    const auto instance = subject("00000000-0000-7000-8000-000000000a01");
    const std::string instanceText = instance.format();
    FakePlayHostRuntime runtime;
    runtime.contract = parse(kGameplayContract);
    runtime.roots    = parse(R"({"gameState":{"tick":1}})");

    Value::Object command{{"id", Value("json-command-1")},
                          {"action", Value("fixture:increment")},
                          {"subject", Value(instanceText)},
                          {"observedTick", Value(std::int64_t{0})},
                          {"expectedRevision", Value(std::int64_t{1})},
                          {"parameters", Value(Value::Object{})}};
    Value session{Value::Object{{"id", Value("test")},
                                {"access", Value("player")},
                                {"controlledSubjects", Value(Value::Array{Value(instanceText)})}}};

    std::int64_t playRevision = 0;
    {
        JsonProvider provider(instance);
        Value playRequest{Value::Object{
            {"schemaId", Value("evengine.play-request")},
            {"schemaVersion", Value(std::int64_t{1})},
            {"op", Value("act")},
            {"action", Value("increment")},
            {"instance", Value(instanceText)},
            {"session", session},
            {"command", Value(command)},
        }};
        auto viaPlay = executePlayRequest(playRequest, runtime);
        REQUIRE(viaPlay.ok());
        playRevision =
            viaPlay.value().getIf<Value::Object>()->at("gameplay").getIf<Value::Object>()->at("receipt")
                .getIf<Value::Object>()
                ->at("resultingRevision")
                .asInt();
    }
    std::int64_t jsonRevision = 0;
    {
        JsonProvider provider(instance);
        Value gameplayRequest{Value::Object{
            {"schemaId", Value("evengine.gameplay-control-request")},
            {"schemaVersion", Value(std::int64_t{1})},
            {"op", Value("submit")},
            {"domain", Value("json-fixture")},
            {"instance", Value(instanceText)},
            {"session", session},
            {"command", Value(command)},
        }};
        auto viaJson = executeGameplayControlRequest(gameplayRequest);
        REQUIRE(viaJson.ok());
        jsonRevision = viaJson.value().getIf<Value::Object>()->at("receipt").getIf<Value::Object>()->at("resultingRevision").asInt();
    }
    CHECK_EQ(playRevision, jsonRevision);
    CHECK_EQ(playRevision, std::int64_t{2});
}

TEST_CASE("devtools.playHost.sessionAutoRecordsEvidenceWithoutEval") {
    PlayTraceBuffer::instance().clear();
    AgentDevelopmentSession::instance().reset();
    FakePlayHostRuntime runtime;
    runtime.contract = parse(kScriptContract);
    runtime.roots    = parse(R"({"gameState":{"tick":4}})");

    auto& session = AgentDevelopmentSession::instance();
    REQUIRE(session
                .start("Play a mapped action",
                       {{"combat-alive", "Combat state is observable", true, false},
                        {"visual", "Frame is captured", true, false}})
                .isAccepted());
    const std::string id = session.sessionId();
    REQUIRE(session.advance(id, AgentDevelopmentPhase::Modify).isAccepted());
    REQUIRE(session.advance(id, AgentDevelopmentPhase::Run).isAccepted());
    REQUIRE(session.advance(id, AgentDevelopmentPhase::Observe).isAccepted());

    REQUIRE(executePlayRequest(
                parse(R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"observe","observation":"combat-alive"})"),
                runtime)
                .ok());
    REQUIRE(executePlayRequest(
                parse(R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"capture","path":"frame.png"})"),
                runtime)
                .ok());
    bool sawObserve = false;
    bool sawShot    = false;
    for (const auto& evidence : session.evidence()) {
        if (evidence.kind == "runtime-observation" && evidence.criterionId == "combat-alive") sawObserve = true;
        if (evidence.kind == "screenshot" && evidence.criterionId == "visual") sawShot = true;
        CHECK_NE(evidence.kind, std::string("eval"));
    }
    CHECK(sawObserve);
    CHECK(sawShot);
    REQUIRE(session.advance(id, AgentDevelopmentPhase::Verify).isAccepted());
    REQUIRE(session.complete(id, "play evidence closed the session").isAccepted());
    session.reset();
    PlayTraceBuffer::instance().clear();
}
