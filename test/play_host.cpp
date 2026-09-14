#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Diagnostic.h"
#include "common/Value.h"
#include "devtools/Debugger.hpp"
#include "devtools/PlayHost.h"

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

    Result<Value> loadContract() const override {
        if (contractMissing)
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::NotFound, "missing contract", "contract"));
        return Result<Value>::success(contract);
    }

    Result<Value> observeScriptRoot(std::string_view root,
                                    const std::vector<std::string>& fields) const override {
        const auto* object = roots.getIf<Value::Object>();
        if (!object)
            return Result<Value>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "no roots", "observation"));
        const auto found = object->find(std::string(root));
        if (found == object->end())
            return Result<Value>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "root missing", "observation.path"));
        if (fields.empty()) return Result<Value>::success(found->second);
        Value::Object projected;
        for (const auto& field : fields) {
            Value current = found->second;
            std::string token;
            bool ok = true;
            for (char ch : field + std::string(".")) {
                if (ch != '.') {
                    token.push_back(ch);
                    continue;
                }
                const auto* node = current.getIf<Value::Object>();
                if (!node) {
                    ok = false;
                    break;
                }
                auto child = node->find(token);
                if (child == node->end()) {
                    ok = false;
                    break;
                }
                Value next = child->second;
                current = std::move(next);
                token.clear();
            }
            if (!ok)
                return Result<Value>::failure(
                    Diagnostic::error(DiagnosticCode::NotFound, "field missing", "observation.fields"));
            projected.emplace(field, std::move(current));
        }
        return Result<Value>::success(Value(std::move(projected)));
    }

    Result<Value> capturePng(std::string path) override {
        if (!capture)
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::Unsupported, "no capture", "capture"));
        lastCapturePath = path;
        return Result<Value>::success(Value(Value::Object{{"height", Value(std::int64_t{2})},
                                                          {"path", Value(std::move(path))},
                                                          {"width", Value(std::int64_t{3})}}));
    }

    Result<std::string> captureCheckpoint() override { return Result<std::string>::success(checkpoint); }

    Result<void> restoreCheckpoint(std::string_view json) override {
        restored = std::string(json);
        auto parsed = Value::fromJson(json);
        if (parsed) {
            const auto* object = parsed.value().getIf<Value::Object>();
            if (object) {
                const auto found = object->find("roots");
                if (found != object->end()) roots = found->second;
            }
        }
        return Result<void>::success();
    }

    std::vector<std::string> gameplayDomains() const override { return domains; }

    Result<Value> invokeScriptAction(std::string_view id, std::string_view source) override {
        lastAction     = std::string(id);
        lastActionScript = std::string(source);
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

    Value                     contract;
    Value                     roots;
    std::string               checkpoint = R"({"version":2,"roots":{"gameState":{"tick":1}}})";
    std::string               restored;
    std::string               lastCapturePath;
    std::string               lastAction;
    std::string               lastActionScript;
    std::vector<std::string>  domains{"tactics"};
    bool                      contractMissing = false;
    bool                      capture         = true;

private:
    bool          paused_    = false;
    std::uint64_t hostFrame_ = 0;
};

Value parse(std::string_view json) {
    auto value = Value::fromJson(json);
    REQUIRE(value.ok());
    return std::move(value).takeValue();
}

const char* kContract = R"json({
  "schemaId": "evengine.game-agent-contract",
  "schemaVersion": 1,
  "id": "examples/ai-game",
  "entry": "main.nut",
  "smoke": {"scene": "default", "seed": 42, "maxHostFrames": 180, "mustReach": ["combat-alive"]},
  "clock": {"defaultStep": "frame"},
  "observations": [
    {"id": "combat-alive", "kind": "script-root", "path": "gameState", "fields": ["tick", "enemy.hp"]}
  ],
  "actions": {"source": "script-map", "map": [{"id": "reset", "script": "game.reset()"}]},
  "capture": {"requiredFor": ["visual"], "backend": "engine-readback"},
  "tolerances": {"screenshot": "human-or-vlm", "numericAbs": 0.0001}
})json";

Value play(FakePlayHostRuntime& runtime, std::string_view json) {
    auto result = executePlayRequest(parse(json), runtime);
    REQUIRE(result.ok());
    return std::move(result).takeValue();
}

}  // namespace

TEST_CASE("devtools.playHost.rejectsUnknownFieldsAndRequiresContractForObserve") {
    FakePlayHostRuntime runtime;
    runtime.contract = parse(kContract);
    auto unknown = executePlayRequest(parse(R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"status","extra":true})"),
                                      runtime);
    CHECK(!unknown.ok());
    CHECK_EQ(unknown.code(), StatusCode::Failed);
    CHECK_EQ(unknown.status().diagnostics().front().code(), DiagnosticCode::ParseError);

    runtime.contractMissing = true;
    auto observe = executePlayRequest(
        parse(R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"observe","observation":"combat-alive"})"),
        runtime);
    CHECK(!observe.ok());
    CHECK_EQ(observe.status().diagnostics().front().code(), DiagnosticCode::Unsupported);

    auto status = play(runtime, R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"status"})");
    CHECK_EQ(status.getIf<Value::Object>()->at("contractLoaded").asBool(), false);
}

TEST_CASE("devtools.playHost.pausesStepsObservesCapturesAndRestores") {
    FakePlayHostRuntime runtime;
    runtime.contract = parse(kContract);
    runtime.roots    = parse(R"({"gameState":{"tick":4,"enemy":{"hp":20.0},"player":{"hp":100.0}}})");

    auto clock = play(runtime, R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"clock","mode":"pause"})");
    CHECK(clock.getIf<Value::Object>()->at("paused").asBool());

    auto stepped =
        play(runtime,
             R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"step","clock":"frame","count":10})");
    CHECK_EQ(stepped.getIf<Value::Object>()->at("count").asInt(), std::int64_t{10});
    CHECK_EQ(runtime.hostFrame(), std::uint64_t{10});

    auto observed = play(
        runtime,
        R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"observe","observation":"combat-alive"})");
    const auto* state = observed.getIf<Value::Object>()->at("state").getIf<Value::Object>();
    REQUIRE(state);
    CHECK_EQ(state->at("tick").asInt(), std::int64_t{4});
    CHECK_EQ(state->at("enemy.hp").asDouble(), 20.0);

    auto captured =
        play(runtime, R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"capture","path":"frame.png"})");
    CHECK_EQ(captured.getIf<Value::Object>()->at("path").asString(), std::string("frame.png"));
    CHECK_EQ(captured.getIf<Value::Object>()->at("backend").asString(), std::string("engine-readback"));

    runtime.capture = false;
    auto missingCapture =
        executePlayRequest(parse(R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"capture"})"), runtime);
    CHECK(!missingCapture.ok());
    CHECK_EQ(missingCapture.status().diagnostics().front().code(), DiagnosticCode::Unsupported);

    auto checkpoint = play(
        runtime, R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"checkpoint","mode":"capture"})");
    const std::string json = checkpoint.getIf<Value::Object>()->at("json").asString();
    Value restoreRequest{Value::Object{
        {"schemaId", Value("evengine.play-request")},
        {"schemaVersion", Value(std::int64_t{1})},
        {"op", Value("checkpoint")},
        {"mode", Value("restore")},
        {"json", Value(json)},
    }};
    auto restored = executePlayRequest(restoreRequest, runtime);
    REQUIRE(restored.ok());
    CHECK_EQ(runtime.restored, json);

    auto batched = play(runtime, R"json({
      "schemaId": "evengine.play-request",
      "schemaVersion": 1,
      "op": "batch",
      "requests": [
        {"schemaId":"evengine.play-request","schemaVersion":1,"op":"clock","mode":"play"},
        {"schemaId":"evengine.play-request","schemaVersion":1,"op":"status"}
      ]
    })json");
    CHECK_EQ(batched.getIf<Value::Object>()->at("count").asInt(), std::int64_t{2});
    CHECK(!runtime.paused());

    auto nested = executePlayRequest(parse(R"json({
      "schemaId": "evengine.play-request",
      "schemaVersion": 1,
      "op": "batch",
      "requests": [
        {"schemaId":"evengine.play-request","schemaVersion":1,"op":"batch","requests":[]}
      ]
    })json"),
                                     runtime);
    CHECK(!nested.ok());
    CHECK_EQ(nested.status().diagnostics().front().code(), DiagnosticCode::Unsupported);

    auto gameplayStep = executePlayRequest(
        parse(R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"step","clock":"gameplay","count":1})"),
        runtime);
    CHECK(!gameplayStep.ok());
    CHECK_EQ(gameplayStep.status().diagnostics().front().code(), DiagnosticCode::Unsupported);
}

TEST_CASE("devtools.debugger.stepFramesReArmsUntilCountConsumed") {
    Debugger& debugger = Debugger::instance();
    debugger.detach();
    debugger.pause(PauseReason::PauseKey);
    debugger.stepFrames(3);
    CHECK(debugger.shouldRunUpdate());
    debugger.notifyFrameDone();
    CHECK(debugger.shouldRunUpdate());
    debugger.notifyFrameDone();
    CHECK(debugger.shouldRunUpdate());
    debugger.notifyFrameDone();
    CHECK(debugger.isPaused());
    debugger.resume();
}
