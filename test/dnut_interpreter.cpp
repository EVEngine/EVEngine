#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "dnut_interpreter/DnutCompiler.h"
#include "dnut_interpreter/SequenceRuntime.h"
#include "dnut_interpreter/StepKindRegistry.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace eve::dnut;

StepField requiredField(std::string name, StepFieldType type) { return StepField{std::move(name), type, true}; }
StepField optionalField(std::string name, StepFieldType type) { return StepField{std::move(name), type, false}; }

/** @brief Registry with one `mark` instant step and two host-presented steps. */
StepKindRegistry makeRegistry(std::vector<std::string>* marks = nullptr) {
    StepKindRegistry registry;
    registry
        .registerStep({"mark", "Mark", "test", StepShape::Instant,
                       {requiredField("id", StepFieldType::String)}})
        .expect("mark descriptor");
    registry
        .registerHandler("mark", [marks](const SequenceNode& node, const StepContext&) -> StepOutcome {
            if (marks) {
                const eve::Value* id = node.payload.find("id");
                marks->push_back(id && id->isString() ? id->asString() : std::string{});
            }
            return StepOutcome{};
        })
        .expect("mark handler");
    registry.registerStep({"hold", "Hold", "test", StepShape::Await, {}}).expect("hold descriptor");
    registry
        .registerStep({"move", "Move", "test", StepShape::Await,
                       {requiredField("actor", StepFieldType::String), requiredField("x", StepFieldType::Number),
                        requiredField("y", StepFieldType::Number), optionalField("duration", StepFieldType::Number)}})
        .expect("move descriptor");
    registry
        .registerStep({"message", "Message", "test", StepShape::Await,
                       {requiredField("text", StepFieldType::String)}})
        .expect("message descriptor");
    return registry;
}

const SequenceNode* findNode(const SequenceAsset& asset, const std::string& id) { return asset.findNode(id); }

}  // namespace

TEST_CASE("dnut.language.compilesControlFlow") {
    StepKindRegistry     registry = makeRegistry();
    const std::string    source   = R"(
// leading comment
story forest.arrival repeatable version=2 {
    move actor=hero x=12 y=8 duration=0.8
    if switch.gate == true {
        mark id=inside
    } else {
        mark id=outside
    }
    choice {
        option "Accept" when variable.gold >= 10 {
            mark id=accepted
        }
        option "Decline" {
            mark id=declined
        }
    }
    call forest.epilogue
    wait 0.5
    end
}

pool village {
    elder: "欢迎"
}
)";
    DnutCompileOutput output = compileDnut(source, "story.dnut", registry);
    REQUIRE(!output.hasErrors());
    REQUIRE_EQ(output.assets.size(), 1u);

    const SequenceAsset& asset = output.assets.front();
    CHECK_EQ(asset.id, std::string("forest.arrival"));
    CHECK_EQ(asset.version, 2);
    CHECK(asset.repeatable);
    REQUIRE(asset.validate().ok());

    const SequenceNode* entry = findNode(asset, asset.entry);
    REQUIRE(entry != nullptr);
    CHECK_EQ(entry->type, std::string("move"));
    const eve::Value* actor = entry->payload.find("actor");
    REQUIRE(actor != nullptr);
    CHECK_EQ(actor->asString(), std::string("hero"));

    const SequenceNode* branch = asset.findNode(entry->next);
    REQUIRE(branch != nullptr);
    CHECK_EQ(branch->type, std::string("branch"));
    REQUIRE_EQ(branch->routes.size(), 1u);
    CHECK(!branch->routes.front().condition.isNull());

    // `if` lowers to a branch whose route is the then-arm and whose `next` is the
    // else-arm; both arms continue at the statement after the `if`.
    const SequenceNode* thenArm = findNode(asset, branch->routes.front().target);
    const SequenceNode* elseArm = findNode(asset, branch->next);
    REQUIRE(thenArm != nullptr);
    REQUIRE(elseArm != nullptr);
    CHECK_EQ(thenArm->type, std::string("mark"));
    CHECK_EQ(elseArm->type, std::string("mark"));
    CHECK_EQ(thenArm->payload.find("id")->asString(), std::string("inside"));
    CHECK_EQ(elseArm->payload.find("id")->asString(), std::string("outside"));
    CHECK_EQ(thenArm->next, elseArm->next);

    const SequenceNode* choice = asset.findNode(thenArm->next);
    REQUIRE(choice != nullptr);
    CHECK_EQ(choice->type, std::string("choice"));
    REQUIRE_EQ(choice->routes.size(), 2u);
    CHECK_EQ(choice->routes[0].label, std::string("Accept"));
    CHECK_EQ(choice->routes[1].label, std::string("Decline"));
    CHECK(!choice->routes[0].condition.isNull());
    CHECK(choice->routes[1].condition.isNull());

    // Each choice arm is its own node chain; both arms rejoin at the next statement.
    const SequenceNode* acceptArm  = findNode(asset, choice->routes[0].target);
    const SequenceNode* declineArm = findNode(asset, choice->routes[1].target);
    REQUIRE(acceptArm != nullptr);
    REQUIRE(declineArm != nullptr);
    CHECK_EQ(acceptArm->payload.find("id")->asString(), std::string("accepted"));
    CHECK_EQ(declineArm->payload.find("id")->asString(), std::string("declined"));
    CHECK_EQ(acceptArm->next, choice->next);
    CHECK_EQ(declineArm->next, choice->next);

    const SequenceNode* call = findNode(asset, choice->next);
    REQUIRE(call != nullptr);
    CHECK_EQ(call->type, std::string("call"));
    const eve::Value* target = call->payload.find("target");
    REQUIRE(target != nullptr);
    CHECK_EQ(target->asString(), std::string("forest.epilogue"));

    const SequenceNode* waitNode = findNode(asset, call->next);
    REQUIRE(waitNode != nullptr);
    CHECK_EQ(waitNode->type, std::string("wait"));
    CHECK_EQ(waitNode->payload.find("duration")->asDouble(), 0.5);

    const SequenceNode* endNode = findNode(asset, waitNode->next);
    REQUIRE(endNode != nullptr);
    CHECK_EQ(endNode->type, std::string("end"));
}

TEST_CASE("dnut.language.reportsUnknownStepsAndBadPayloads") {
    StepKindRegistry registry = makeRegistry();
    DnutCompileOutput output =
        compileDnut("story broken {\n    teleport actor=hero\n}\n", "broken.dnut", registry);
    CHECK(output.hasErrors());
    CHECK(output.assets.empty());
    REQUIRE(!output.diagnostics.empty());
    CHECK(output.diagnostics.front().path == std::string("broken.dnut"));
    CHECK(output.diagnostics.front().line == 2);
    CHECK(output.diagnostics.front().message.find("teleport") != std::string::npos);

    DnutCompileOutput missing =
        compileDnut("story broken {\n    mark\n}\n", "broken.dnut", registry);
    CHECK(missing.hasErrors());
    CHECK(missing.diagnostics.front().message.find("requires field 'id'") != std::string::npos);

    DnutCompileOutput unknownField =
        compileDnut("story broken {\n    mark id=ok extra=1\n}\n", "broken.dnut", registry);
    CHECK(unknownField.hasErrors());
    CHECK(unknownField.diagnostics.front().message.find("does not accept field 'extra'") != std::string::npos);

    DnutCompileOutput instantWithoutHandler =
        compileDnut("story broken {\n    hold id=1\n}\n", "broken.dnut", registry);
    CHECK(instantWithoutHandler.hasErrors());
}

TEST_CASE("dnut.language.recoversAfterABadStatement") {
    StepKindRegistry  registry = makeRegistry();
    DnutCompileOutput output =
        compileDnut("story mixed {\n    teleport\n    mark id=ok\n}\n", "mixed.dnut", registry);
    CHECK(output.hasErrors());
    // Recovery keeps the block parseable, so the following valid statement still
    // resolves; the asset itself is withheld because the document has an error.
    CHECK(output.assets.empty());
    CHECK_EQ(output.diagnostics.size(), 1u);
}

TEST_CASE("dnut.runtime.executesAndBlocks") {
    std::vector<std::string> marks;
    StepKindRegistry         registry = makeRegistry(&marks);
    DnutCompileOutput output = compileDnut(
        "story run {\n    mark id=first\n    hold\n    mark id=second\n    message text=hi\n}\n",
        "run.dnut", registry);
    REQUIRE(!output.hasErrors());
    REQUIRE_EQ(output.assets.size(), 1u);

    const SequenceAsset* asset = &output.assets.front();
    SequenceRuntime      runtime;
    runtime.setStepRegistry(&registry);
    REQUIRE(runtime.start(asset).ok());
    REQUIRE_EQ(marks.size(), 1u);
    CHECK_EQ(marks.front(), std::string("first"));
    CHECK(runtime.isBlocked());
    CHECK_EQ(runtime.currentNode()->type, std::string("hold"));

    REQUIRE(runtime.advance().ok());
    REQUIRE_EQ(marks.size(), 2u);
    CHECK_EQ(marks.back(), std::string("second"));
    CHECK(runtime.isBlocked());
    CHECK_EQ(runtime.currentNode()->type, std::string("message"));

    REQUIRE(runtime.advance().ok());
    CHECK(!runtime.isActive());
}

TEST_CASE("dnut.runtime.choiceConditionsAndRejection") {
    StepKindRegistry         registry = makeRegistry();
    DnutCompileOutput output = compileDnut(
        "story pick {\n    choice {\n        option \"Yes\" when switch.ok == true {\n            mark id=yes\n        }\n"
        "        option \"No\" {\n            mark id=no\n        }\n    }\n}\n",
        "pick.dnut", registry);
    REQUIRE(!output.hasErrors());
    const SequenceAsset* asset = &output.assets.front();

    SequenceRuntime runtime;
    runtime.setStepRegistry(&registry);
    runtime.setConditionEvaluator([](const eve::Value&) { return SequenceConditionOutcome{false, "value-mismatch"}; });
    REQUIRE(runtime.start(asset).ok());
    CHECK(runtime.isBlocked());

    // A rejected conditional route keeps the cursor on the choice.
    auto rejected = runtime.select("Yes");
    CHECK(!rejected.ok());
    CHECK(runtime.isBlocked());
    CHECK_EQ(runtime.currentNode()->type, std::string("choice"));
    const auto* rejectedDiagnostic = rejected.error();
    REQUIRE(rejectedDiagnostic != nullptr);
    CHECK(rejectedDiagnostic->message().find("choice condition rejected") != std::string::npos);

    // An unknown label is a distinct, stable failure.
    auto unknown = runtime.select("Nothing");
    CHECK(!unknown.ok());
    const auto* unknownDiagnostic = unknown.error();
    REQUIRE(unknownDiagnostic != nullptr);
    CHECK(unknownDiagnostic->message().find("unknown choice route") != std::string::npos);

    // The unconditional route always works.
    REQUIRE(runtime.select("No").ok());
    CHECK(!runtime.isActive());
}

TEST_CASE("dnut.runtime.advanceRejectsChoice") {
    StepKindRegistry  registry = makeRegistry();
    DnutCompileOutput output =
        compileDnut("story pick {\n    choice {\n        option \"Only\" {\n            mark id=x\n        }\n    }\n}\n",
                    "pick.dnut", registry);
    REQUIRE(!output.hasErrors());
    SequenceRuntime runtime;
    runtime.setStepRegistry(&registry);
    REQUIRE(runtime.start(&output.assets.front()).ok());
    CHECK(!runtime.advance().ok());
    CHECK(runtime.isBlocked());
}

TEST_CASE("dnut.runtime.callPushesAndPopsFrames") {
    std::vector<std::string> marks;
    StepKindRegistry         registry = makeRegistry(&marks);
    DnutCompileOutput output = compileDnut(
        "story outer {\n    mark id=before\n    call inner\n    mark id=after\n}\n"
        "story inner {\n    mark id=inside\n}\n",
        "call.dnut", registry);
    REQUIRE(!output.hasErrors());
    REQUIRE_EQ(output.assets.size(), 2u);

    std::map<std::string, const SequenceAsset*> byId;
    for (const auto& asset : output.assets) byId.emplace(asset.id, &asset);

    SequenceRuntime runtime;
    runtime.setStepRegistry(&registry);
    runtime.setAssetResolver([&byId](const std::string& id) -> const SequenceAsset* {
        const auto found = byId.find(id);
        return found == byId.end() ? nullptr : found->second;
    });
    REQUIRE(runtime.start(byId.at("outer")).ok());
    REQUIRE_EQ(marks.size(), 3u);
    CHECK_EQ(marks[0], std::string("before"));
    CHECK_EQ(marks[1], std::string("inside"));
    CHECK_EQ(marks[2], std::string("after"));
    CHECK(!runtime.isActive());
}

TEST_CASE("dnut.runtime.enforcesExecutionBudget") {
    StepKindRegistry registry = makeRegistry();
    // A hand-built self-referential branch is the only way to express a cycle;
    // the compiler cannot produce one, but authored content can be hand-edited.
    SequenceAsset asset;
    asset.id      = "loop";
    asset.entry   = "n1";
    SequenceNode branch;
    branch.id   = "n1";
    branch.type = "branch";
    branch.routes.push_back({"", eve::Value(eve::Value::Object{{"var", eve::Value::string("x")},
                                                               {"op", eve::Value::string("eq")},
                                                               {"value", eve::Value::integer(0)}}),
                             "n1"});
    asset.nodes.push_back(std::move(branch));

    SequenceRuntime runtime;
    runtime.setStepRegistry(&registry);
    runtime.setConditionEvaluator([](const eve::Value&) { return SequenceConditionOutcome{true, {}}; });
    auto started = runtime.start(&asset);
    CHECK(!started.ok());
    const auto* diagnostic = started.error();
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->message().find("budget") != std::string::npos);
}

TEST_CASE("dnut.runtime.capturesAndRestoresCursor") {
    std::vector<std::string> marks;
    StepKindRegistry         registry = makeRegistry(&marks);
    DnutCompileOutput output = compileDnut(
        "story save {\n    mark id=first\n    message text=hi\n    mark id=second\n}\n", "save.dnut", registry);
    REQUIRE(!output.hasErrors());
    REQUIRE_EQ(output.assets.size(), 1u);
    const SequenceAsset* asset = &output.assets.front();

    SequenceRuntime runtime;
    runtime.setStepRegistry(&registry);
    REQUIRE(runtime.start(asset).ok());
    CHECK_EQ(runtime.currentNode()->type, std::string("message"));

    eve::Value captured;
    REQUIRE(runtime.captureState(captured).ok());

    SequenceRuntime restored;
    restored.setStepRegistry(&registry);
    restored.setAssetResolver([asset](const std::string& id) -> const SequenceAsset* {
        return id == asset->id ? asset : nullptr;
    });
    REQUIRE(restored.restoreState(captured).ok());
    CHECK(restored.isActive());
    CHECK(restored.isBlocked());
    CHECK_EQ(restored.currentNode()->type, std::string("message"));
    REQUIRE(restored.advance().ok());
    CHECK_EQ(marks.size(), 2u);
    CHECK_EQ(marks.back(), std::string("second"));
}

TEST_CASE("dnut.runtime.rejectsVersionMismatchOnRestore") {
    StepKindRegistry  registry = makeRegistry();
    DnutCompileOutput output =
        compileDnut("story versioned {\n    message text=hi\n}\n", "v.dnut", registry);
    REQUIRE(!output.hasErrors());
    SequenceAsset changed = output.assets.front();
    changed.version       = 7;

    SequenceRuntime runtime;
    runtime.setStepRegistry(&registry);
    REQUIRE(runtime.start(&output.assets.front()).ok());
    eve::Value captured;
    REQUIRE(runtime.captureState(captured).ok());

    SequenceRuntime restored;
    restored.setStepRegistry(&registry);
    restored.setAssetResolver([&changed](const std::string& id) -> const SequenceAsset* {
        return id == changed.id ? &changed : nullptr;
    });
    CHECK(!restored.restoreState(captured).ok());
    CHECK(!restored.isActive());
}
