#include "dialogue/ConversationPersistence.h"
#include "dialogue/DialogueSequence.h"
#include "dialogue/DialogueState.h"
#include "dnut_interpreter/SequenceRuntime.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve;
using namespace eve::dialogue;

namespace {

eve::dnut::SequenceAsset makeAsset(std::string id, int version, std::string line) {
    eve::dnut::SequenceAsset asset;
    asset.id      = std::move(id);
    asset.version = version;
    asset.entry   = line;
    asset.nodes   = {{line, "line", "end"}, {"end", "end"}};
    return asset;
}

}  // namespace

TEST_CASE("dialoguePersistence.jsonRoundtrip") {
    StateValue state = StateValue::object();
    state.set("text", StateValue::string("你好 \"traveler\"\n"));
    state.set("count", StateValue::integer(9007199254740991LL));
    StateValue array = StateValue::array();
    array.pushBack(StateValue::boolean(true));
    array.pushBack(StateValue::number(2.5));
    state.set("values", std::move(array));
    auto encoded = conversationStateToJson(state);
    REQUIRE(encoded.ok());
    const auto json = std::move(encoded).takeValue();
    auto decoded = conversationStateFromJson(json);
    REQUIRE(decoded.ok());
    StateValue restored = std::move(decoded).takeValue();
    CHECK(restored == state);
}

TEST_CASE("dialoguePersistence.migratesCurrentAndCallFrames") {
    eve::dnut::SequenceAsset currentParent = makeAsset("scene.parent", 4, "after-renamed");
    eve::dnut::SequenceAsset currentChild  = makeAsset("common.child.v2", 3, "line-renamed");
    eve::dnut::SequenceAsset oldParent     = makeAsset("scene.parent", 1, "call");
    eve::dnut::SequenceAsset oldChild      = makeAsset("common.child", 1, "line");
    oldParent.nodes[0].type                = "call";
    oldParent.nodes[0].payload.set("target", Value(oldChild.id));
    oldParent.nodes[0].payload.set("return", Value("after"));
    oldParent.nodes.push_back({"after", "line", "end"});
    eve::dnut::StepKindRegistry registry;
    registerDialogueSequenceSteps(registry).expect("dialogue sequence vocabulary");
    eve::dnut::SequenceRuntime runner;
    runner.setStepRegistry(&registry);
    runner.setAssetResolver([&](const std::string& id) -> const eve::dnut::SequenceAsset* {
        if (id == oldParent.id) return &oldParent;
        if (id == oldChild.id) return &oldChild;
        return nullptr;
    });
    REQUIRE(runner.start(&oldParent).ok());
    Value captured;
    REQUIRE(runner.captureState(captured).ok());
    StateValue saved = toDialogueStateValue(captured);

    ConversationSaveMigrations migrations;
    CHECK(migrations.registerMigration("common.child", 1, "common.child.v2", "line:line-renamed").ok());
    CHECK(migrations.registerMigration("scene.parent", 1, "scene.parent", "after:after-renamed").ok());
    const auto resolveCurrent = [&](const std::string& id) -> const eve::dnut::SequenceAsset* {
        if (id == currentParent.id) return &currentParent;
        if (id == currentChild.id) return &currentChild;
        return nullptr;
    };
    auto migrated = migrations.migrate(saved, resolveCurrent);
    REQUIRE(migrated.ok());
    saved = std::move(migrated).takeValue();
    eve::dnut::SequenceRuntime restored;
    restored.setStepRegistry(&registry);
    restored.setAssetResolver(resolveCurrent);
    CHECK(restored.restoreState(toCanonicalValue(saved)).ok());
    CHECK(restored.currentNodeId() == "line-renamed");
    CHECK(restored.advance().ok());
    CHECK(restored.currentNodeId() == "after-renamed");
}

TEST_CASE("dialoguePersistence.rejectsLegacyUnversionedState") {
    eve::dnut::SequenceRuntime runner;
    Value                      legacy = Value::Object{{"active", Value(false)}};
    auto rejected = runner.restoreState(legacy);
    CHECK(!rejected.ok());
    CHECK_EQ(static_cast<int>(rejected.error()->code()), static_cast<int>(eve::DiagnosticCode::UnknownVersion));
}
