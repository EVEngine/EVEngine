#include "dialogue/Conversation.h"
#include "zeroerr/unittest.h"

using namespace eve;
using namespace eve::dialogue;

namespace {

ConversationAsset makeGreeting() {
    ConversationAsset asset;
    asset.id = "common.greeting";
    asset.entry = "decide";
    asset.parameters = {"speaker", "listener", "location"};
    ConversationAsset::Node decide;
    decide.id = "decide";
    decide.kind = ConversationAsset::Node::Kind::Branch;
    decide.routes = {{"speaker.mood == happy", "friendly"}, {"else", "formal"}};
    ConversationAsset::Node friendly;
    friendly.id = "friendly";
    friendly.kind = ConversationAsset::Node::Kind::Line;
    friendly.speaker = "speaker";
    friendly.pool = "greeting.friendly";
    friendly.next = "end";
    ConversationAsset::Node formal = friendly;
    formal.id = "formal";
    formal.pool = "greeting.formal";
    ConversationAsset::Node end;
    end.id = "end";
    end.kind = ConversationAsset::Node::Kind::End;
    asset.nodes = {decide, friendly, formal, end};
    return asset;
}

}  // namespace

TEST_CASE("dialogueConversation.parameterizedRunner") {
    ConversationAsset asset = makeGreeting();
    ConversationRunner runner;
    runner.setExpressionEvaluator([](const std::string& expression, const StateValue& bindings,
                                     const StateValue&) {
        CHECK(expression == "speaker.mood == happy");
        const StateValue* mood = bindings.get("speaker.mood");
        return StateValue::boolean(mood && mood->isString() && mood->asString() == "happy");
    });
    StateValue bindings = StateValue::object();
    CHECK(bindings.setPath("speaker.mood", StateValue::string("happy")));
    CHECK(bindings.setPath("listener.id", StateValue::string("player")));
    std::string error;
    CHECK(runner.start(&asset, std::move(bindings), &error));
    CHECK(error.empty());
    CHECK(runner.isBlocked());
    CHECK(runner.currentNodeId() == "friendly");
    CHECK(runner.currentNode()->pool == "greeting.friendly");
    CHECK(runner.advance(&error));
    CHECK(!runner.isActive());
}

TEST_CASE("dialogueConversation.validation") {
    ConversationAsset asset = makeGreeting();
    asset.nodes.back().id = "friendly";
    std::string error;
    CHECK(!asset.validate(&error));
    CHECK(error.find("duplicate node id") != std::string::npos);
}

