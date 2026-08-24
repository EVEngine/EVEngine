#include "dialogue/ConversationPersistence.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve;
using namespace eve::dialogue;

namespace {

ConversationAsset makeAsset(std::string id, int version, std::string line) {
    ConversationAsset asset;
    asset.id      = std::move(id);
    asset.version = version;
    asset.entry   = line;
    asset.nodes   = {{line, ConversationAsset::Node::Kind::Line, "end"}, {"end", ConversationAsset::Node::Kind::End}};
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
    std::string error;
    const auto  json = conversationStateToJson(state, &error);
    REQUIRE(!json.empty());
    StateValue restored;
    CHECK(conversationStateFromJson(json, restored, &error));
    CHECK(restored == state);
}

TEST_CASE("dialoguePersistence.migratesCurrentAndCallFrames") {
    ConversationAsset currentParent = makeAsset("scene.parent", 4, "after-renamed");
    ConversationAsset currentChild  = makeAsset("common.child.v2", 3, "line-renamed");
    ConversationAsset oldParent     = makeAsset("scene.parent", 1, "call");
    ConversationAsset oldChild      = makeAsset("common.child", 1, "line");
    oldParent.nodes[0].kind         = ConversationAsset::Node::Kind::Call;
    oldParent.nodes[0].target       = oldChild.id;
    oldParent.nodes[0].next         = "after";
    oldParent.nodes.push_back({"after", ConversationAsset::Node::Kind::Line, "end"});
    ConversationRunner runner;
    runner.setAssetResolver([&](const std::string& id) -> const ConversationAsset* {
        if (id == oldParent.id) return &oldParent;
        if (id == oldChild.id) return &oldChild;
        return nullptr;
    });
    std::string error;
    REQUIRE(runner.start(&oldParent, StateValue::object(), &error));
    StateValue saved;
    REQUIRE(runner.captureState(saved));

    ConversationSaveMigrations migrations;
    CHECK(migrations.registerMigration("common.child", 1, "common.child.v2", "line:line-renamed", &error));
    CHECK(migrations.registerMigration("scene.parent", 1, "scene.parent", "after:after-renamed", &error));
    const auto resolveCurrent = [&](const std::string& id) -> const ConversationAsset* {
        if (id == currentParent.id) return &currentParent;
        if (id == currentChild.id) return &currentChild;
        return nullptr;
    };
    CHECK(migrations.migrate(saved, resolveCurrent, &error));
    ConversationRunner restored;
    restored.setAssetResolver(resolveCurrent);
    CHECK(restored.restoreState(saved, &error));
    CHECK(restored.currentNodeId() == "line-renamed");
    CHECK(restored.advance(&error));
    CHECK(restored.currentNodeId() == "after-renamed");
}
