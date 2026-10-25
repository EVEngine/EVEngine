#include "dnut_interpreter/DnutBlockScanner.h"
#include "dnut_interpreter/DnutCompiler.h"
#include "dnut_interpreter/DnutLexer.h"
#include "dnut_interpreter/SequenceRuntime.h"
#include "dnut_interpreter/StepKindRegistry.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace eve::dnut;

namespace {

StepKindRegistry vocabulary(std::vector<std::string>* observed = nullptr) {
    StepKindRegistry registry;
    registry.registerStep({"mark", "Mark", "test", StepShape::Instant, {StepField{"id", StepFieldType::String, true}}})
        .expect("mark descriptor");
    registry
        .registerHandler("mark",
                         [observed](const SequenceNode& node, const StepContext&) -> StepOutcome {
                             if (observed) observed->push_back(node.payload.find("id")->asString());
                             return {};
                         })
        .expect("mark handler");
    registry
        .registerStep(
            {"message", "Message", "test", StepShape::Await, {StepField{"text", StepFieldType::String, true}}})
        .expect("message descriptor");
    registry
        .registerStep(
            {"move",
             "Move",
             "test",
             StepShape::Await,
             {StepField{"x", StepFieldType::Number, true}, StepField{"duration", StepFieldType::Number, false}}})
        .expect("move descriptor");
    return registry;
}

const SequenceNode& node(const SequenceAsset& asset, const std::string& id) {
    auto* found = asset.findNode(id);
    REQUIRE(found != nullptr);
    return *found;
}

}  // namespace

// Source -> token stream -> block inventory: delimiters inside quoted text remain data.
TEST_CASE("dnut.language.scansMultipleDialectsAndQuotedBraces") {
    const std::string source =
        "schema \"eve.dnut\"\nversion 1\n"
        "pool greetings { guide: \"brace { is text\" }\n"
        "conversation intro entry=end { node end end }\n"
        "story arrival { end }\n";
    auto tokens = lexDnut(source, "mixed.dnut");
    REQUIRE(tokens.ok());
    auto blocks = scanDnutBlocks(tokens.value(), "mixed.dnut");
    REQUIRE(blocks.ok());
    REQUIRE_EQ(blocks.value().size(), std::size_t{3});
    CHECK_EQ(blocks.value()[0].kind, std::string("pool"));
    CHECK_EQ(blocks.value()[1].kind, std::string("conversation"));
    CHECK_EQ(blocks.value()[2].kind, std::string("story"));
}

// A compiled asset is a graph; assert the graph shape and payload, not generated node ids.
TEST_CASE("dnut.compiler.lowersSequenceBranchChoiceCallWaitAndEnd") {
    auto registry = vocabulary();
    auto output   = compileDnut(R"(
story forest.arrival repeatable version=2 {
    mark id=entered
    if switch.gate == true {
        mark id=open
    } else {
        mark id=closed
    }
    choice {
        option "Accept" when variable.gold >= 10 { mark id=accepted }
        option "Decline" { mark id=declined }
    }
    call forest.end
    wait 0.5
    end
}
)",
                                "story.dnut", registry);
    REQUIRE(!output.hasErrors());
    REQUIRE_EQ(output.assets.size(), std::size_t{1});
    const auto& asset = output.assets.front();
    REQUIRE(asset.validate().ok());
    CHECK_EQ(asset.id, std::string("forest.arrival"));
    CHECK_EQ(asset.version, 2);
    CHECK(asset.repeatable);
    const auto& entry = node(asset, asset.entry);
    CHECK_EQ(entry.type, std::string("mark"));
    CHECK_EQ(entry.payload.find("id")->asString(), std::string("entered"));
    const auto& branch = node(asset, entry.next);
    REQUIRE_EQ(branch.routes.size(), std::size_t{1});
    CHECK_EQ(branch.type, std::string("branch"));
    CHECK(!branch.routes[0].condition.isNull());
    const auto& open   = node(asset, branch.routes[0].target);
    const auto& closed = node(asset, branch.next);
    CHECK_EQ(open.payload.find("id")->asString(), std::string("open"));
    CHECK_EQ(closed.payload.find("id")->asString(), std::string("closed"));
    CHECK_EQ(open.next, closed.next);
    const auto& choice = node(asset, open.next);
    REQUIRE_EQ(choice.routes.size(), std::size_t{2});
    CHECK_EQ(choice.routes[0].label, std::string("Accept"));
    CHECK(!choice.routes[0].condition.isNull());
    CHECK_EQ(choice.routes[1].label, std::string("Decline"));
    CHECK(choice.routes[1].condition.isNull());
    CHECK_EQ(node(asset, choice.routes[0].target).next, choice.next);
    CHECK_EQ(node(asset, choice.routes[1].target).next, choice.next);
    const auto& call = node(asset, choice.next);
    CHECK_EQ(call.type, std::string("call"));
    CHECK_EQ(call.payload.find("target")->asString(), std::string("forest.end"));
    const auto& wait = node(asset, call.next);
    CHECK_EQ(wait.type, std::string("wait"));
    CHECK_EQ(wait.payload.find("duration")->asDouble(), 0.5);
    CHECK_EQ(node(asset, wait.next).type, std::string("end"));
}

TEST_CASE("dnut.compiler.rejectsUnknownMissingWrongAndExtraStepFields") {
    auto registry = vocabulary();
    const struct {
        const char* statement;
        const char* diagnostic;
    } cases[] = {
        {"teleport actor=hero", "teleport"},
        {"mark", "requires field 'id'"},
        {"mark id=ok extra=1", "does not accept field 'extra'"},
        {"move x=wrong", "x"},
    };
    for (const auto& test : cases) {
        auto output = compileDnut(std::string("story broken {\n") + test.statement + "\n}\n", "broken.dnut", registry);
        CHECK(output.hasErrors());
        CHECK(output.assets.empty());
        REQUIRE(!output.diagnostics.empty());
        CHECK_EQ(output.diagnostics.front().path, std::string("broken.dnut"));
        CHECK(output.diagnostics.front().message.find(test.diagnostic) != std::string::npos);
    }
}

// Runtime actions form a host protocol: instant steps execute, await steps suspend.
TEST_CASE("dnut.runtime.startsExecutesAndAcknowledgesAwaitSteps") {
    std::vector<std::string> marks;
    auto                     registry = vocabulary(&marks);
    auto output = compileDnut("story run {\n mark id=first\n message text=hi\n mark id=second\n move x=2\n}\n",
                              "run.dnut", registry);
    REQUIRE(!output.hasErrors());
    SequenceRuntime runtime;
    runtime.setStepRegistry(&registry);
    REQUIRE(runtime.start(&output.assets.front()).ok());
    REQUIRE_EQ(marks.size(), std::size_t{1});
    CHECK_EQ(marks[0], std::string("first"));
    CHECK(runtime.isActive());
    CHECK(runtime.isBlocked());
    CHECK_EQ(runtime.currentNode()->type, std::string("message"));
    REQUIRE(runtime.advance().ok());
    REQUIRE_EQ(marks.size(), std::size_t{2});
    CHECK_EQ(marks[1], std::string("second"));
    CHECK_EQ(runtime.currentNode()->type, std::string("move"));
    REQUIRE(runtime.advance().ok());
    CHECK(!runtime.isActive());
    CHECK(!runtime.isBlocked());
}

TEST_CASE("dnut.runtime.choiceRejectsUnknownOrFalseRouteWithoutMovingCursor") {
    auto registry = vocabulary();
    auto output   = compileDnut(R"(
story choose {
    choice {
        option "Yes" when switch.ok == true { mark id=yes }
        option "No" { mark id=no }
    }
}
)",
                                "choice.dnut", registry);
    REQUIRE(!output.hasErrors());
    SequenceRuntime runtime;
    runtime.setStepRegistry(&registry);
    runtime.setConditionEvaluator([](const eve::Value&) { return SequenceConditionOutcome{false, "value-mismatch"}; });
    REQUIRE(runtime.start(&output.assets.front()).ok());
    CHECK(runtime.isBlocked());
    CHECK_EQ(runtime.currentNode()->type, std::string("choice"));
    CHECK(!runtime.advance().ok());
    CHECK(!runtime.select("missing").ok());
    CHECK(!runtime.select("Yes").ok());
    CHECK(runtime.isBlocked());
    CHECK_EQ(runtime.currentNode()->type, std::string("choice"));
    REQUIRE(runtime.select("No").ok());
    CHECK(!runtime.isActive());
}

TEST_CASE("dnut.runtime.callUsesResolverAndReturnsToCaller") {
    std::vector<std::string> marks;
    auto                     registry = vocabulary(&marks);
    auto                     output   = compileDnut(
        "story outer {\n mark id=before\n call inner\n mark id=after\n}\n"
                              "story inner {\n mark id=inside\n}\n",
        "call.dnut", registry);
    REQUIRE(!output.hasErrors());
    REQUIRE_EQ(output.assets.size(), std::size_t{2});
    std::map<std::string, const SequenceAsset*> byId;
    for (const auto& asset : output.assets) byId.emplace(asset.id, &asset);
    SequenceRuntime runtime;
    runtime.setStepRegistry(&registry);
    runtime.setAssetResolver([&](const std::string& id) -> const SequenceAsset* {
        auto it = byId.find(id);
        return it == byId.end() ? nullptr : it->second;
    });
    REQUIRE(runtime.start(byId.at("outer")).ok());
    REQUIRE_EQ(marks.size(), std::size_t{3});
    CHECK_EQ(marks[0], std::string("before"));
    CHECK_EQ(marks[1], std::string("inside"));
    CHECK_EQ(marks[2], std::string("after"));
    CHECK(!runtime.isActive());
}

TEST_CASE("dnut.runtime.captureRestoreAndVersionMismatchAreTransactional") {
    auto registry = vocabulary();
    auto output   = compileDnut("story save {\n message text=hi\n mark id=after\n}\n", "save.dnut", registry);
    REQUIRE(!output.hasErrors());
    auto*           asset = &output.assets.front();
    SequenceRuntime source;
    source.setStepRegistry(&registry);
    REQUIRE(source.start(asset).ok());
    eve::Value snapshot;
    REQUIRE(source.captureState(snapshot).ok());
    SequenceRuntime restored;
    restored.setStepRegistry(&registry);
    restored.setAssetResolver(
        [asset](const std::string& id) -> const SequenceAsset* { return id == asset->id ? asset : nullptr; });
    REQUIRE(restored.restoreState(snapshot).ok());
    CHECK(restored.isBlocked());
    CHECK_EQ(restored.currentNode()->type, std::string("message"));
    REQUIRE(restored.advance().ok());
    CHECK(!restored.isActive());

    SequenceAsset changed = *asset;
    changed.version += 1;
    SequenceRuntime rejected;
    rejected.setStepRegistry(&registry);
    rejected.setAssetResolver(
        [&](const std::string& id) -> const SequenceAsset* { return id == changed.id ? &changed : nullptr; });
    CHECK(!rejected.restoreState(snapshot).ok());
    CHECK(!rejected.isActive());
}
