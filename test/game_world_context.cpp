#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Value.h"
#include "devtools/GameWorldContext.hpp"
#include "devtools/PlayHost.h"

#include <string>
#include <vector>

using namespace eve;
using namespace eve::dev;

namespace {

Value parse(std::string_view json) {
    auto value = Value::fromJson(json);
    REQUIRE(value.ok());
    return std::move(value).takeValue();
}

}  // namespace

TEST_CASE("devtools.gameWorld.parseRejectsUnknownAndEmptyWorld") {
    clearWorldContext();
    auto unknown = parseWorldObject(parse(R"({"title":"X","extra":true})"));
    CHECK(!unknown.ok());
    CHECK_EQ(unknown.status().diagnostics().front().code(), DiagnosticCode::ParseError);

    auto empty = parseWorldObject(parse(R"({})"));
    CHECK(!empty.ok());
    CHECK_EQ(empty.status().diagnostics().front().code(), DiagnosticCode::ParseError);

    auto badDim = parseWorldObject(parse(R"({"title":"X","dimensions":"4d"})"));
    CHECK(!badDim.ok());
}

TEST_CASE("devtools.gameWorld.publishRuntimeBriefAndClear") {
    clearWorldContext();
    CHECK(!hasWorldContext());

    auto published = publishWorldContext(parse(R"({
      "title":"Runtime World",
      "summary":"Published after systems init",
      "systems":["physics","ui"],
      "tags":["runtime"],
      "dimensions":"2d"
    })"));
    REQUIRE(published.ok());
    CHECK(hasWorldContext());

    GameWorldSnapshotInput input;
    input.contractId         = "examples/test";
    input.contractLoaded     = true;
    input.gameRoot           = "/tmp/game";
    input.domains            = {"inventory"};
    input.configuredModules  = {"physics", "particles"};
    input.liveModules        = {"physics", "ui", "win", "gfx"};

    auto snapshot = buildGameWorldSnapshot(input);
    REQUIRE(snapshot.ok());
    const auto* root = snapshot.value().getIf<Value::Object>();
    REQUIRE(root);
    CHECK_EQ(root->at("schemaId").asString(), std::string(kGameWorldSchemaId));
    CHECK_EQ(root->at("source").asString(), std::string("runtime"));
    CHECK_EQ(root->at("contractId").asString(), std::string("examples/test"));
    const auto* world = root->at("world").getIf<Value::Object>();
    REQUIRE(world);
    CHECK_EQ(world->at("title").asString(), std::string("Runtime World"));
    CHECK_EQ(world->at("dimensions").asString(), std::string("2d"));
    const auto* systems = world->at("systems").getIf<Value::Array>();
    REQUIRE(systems);
    CHECK_EQ(systems->size(), std::size_t{2});
    CHECK_EQ(root->at("configuredModules").getIf<Value::Array>()->size(), std::size_t{2});
    CHECK_EQ(root->at("liveModules").getIf<Value::Array>()->size(), std::size_t{4});

    clearWorldContext();
    CHECK(!hasWorldContext());
    auto afterClear = buildGameWorldSnapshot(input);
    REQUIRE(afterClear.ok());
    CHECK_EQ(afterClear.value().getIf<Value::Object>()->at("source").asString(), std::string("none"));
    CHECK(afterClear.value().getIf<Value::Object>()->at("world").isNull());
}

TEST_CASE("devtools.playHost.statusIncludesRuntimeGameWorld") {
    clearWorldContext();
    REQUIRE(publishWorldContext(parse(R"({
      "title":"AI Game",
      "summary":"Minimal duel after systems init",
      "systems":["ui"]
    })"))
                .ok());

    class Runtime final : public IPlayHostRuntime {
    public:
        bool paused() const override { return false; }
        std::uint64_t hostFrame() const override { return 3; }
        void pause() override {}
        void play() override {}
        Result<void> stepFrames(std::int64_t) override { return Result<void>::success(); }
        Result<Value> loadContract() const override { return Result<Value>::success(contract); }
        Result<Value> observeScriptRoot(std::string_view, const std::vector<std::string>&) const override {
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::Unsupported, "unused", "observation"));
        }
        Result<Value> capturePng(std::string) override {
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::Unsupported, "unused", "capture"));
        }
        Result<std::string> captureCheckpoint() override {
            return Result<std::string>::failure(
                Diagnostic::error(DiagnosticCode::Unsupported, "unused", "checkpoint"));
        }
        Result<void> restoreCheckpoint(std::string_view) override {
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, "unused", "checkpoint"));
        }
        std::vector<std::string> gameplayDomains() const override { return {"tactics"}; }
        Result<Value> invokeScriptAction(std::string_view, std::string_view) override {
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::Unsupported, "unused", "action"));
        }
        Value contract = parse(R"json({
          "schemaId": "evengine.game-agent-contract",
          "schemaVersion": 1,
          "id": "examples/ai-game",
          "entry": "main.nut",
          "clock": {"defaultStep": "frame"},
          "observations": [
            {"id": "combat-alive", "kind": "script-root", "path": "gameState", "fields": ["tick"]}
          ]
        })json");
    } runtime;

    auto status = executePlayRequest(
        parse(R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"status"})"), runtime);
    REQUIRE(status.ok());
    const auto* object = status.value().getIf<Value::Object>();
    REQUIRE(object);
    CHECK(object->at("contractLoaded").asBool());
    CHECK_EQ(object->at("contractId").asString(), std::string("examples/ai-game"));
    const auto* gameWorld = object->at("gameWorld").getIf<Value::Object>();
    REQUIRE(gameWorld);
    CHECK_EQ(gameWorld->at("source").asString(), std::string("runtime"));
    CHECK_EQ(gameWorld->at("world").getIf<Value::Object>()->at("title").asString(), std::string("AI Game"));

    // Contract must reject a static world field — agents read files / runtime publish instead.
    runtime.contract = parse(R"json({
      "schemaId": "evengine.game-agent-contract",
      "schemaVersion": 1,
      "id": "examples/ai-game",
      "entry": "main.nut",
      "world": {"title":"should-fail"},
      "observations": [
        {"id": "combat-alive", "kind": "script-root", "path": "gameState"}
      ]
    })json");
    auto rejected = executePlayRequest(
        parse(R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"status"})"), runtime);
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.status().diagnostics().front().code(), DiagnosticCode::ParseError);

    clearWorldContext();
}
