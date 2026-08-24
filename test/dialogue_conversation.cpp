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

TEST_CASE("dialogueConversation.callStackStateRoundtrip") {
    ConversationAsset child;
    child.id = "common.child";
    child.entry = "line";
    ConversationAsset::Node childLine;
    childLine.id = "line";
    childLine.kind = ConversationAsset::Node::Kind::Line;
    childLine.text = "hello";
    childLine.next = "end";
    ConversationAsset::Node childEnd;
    childEnd.id = "end";
    childEnd.kind = ConversationAsset::Node::Kind::End;
    child.nodes = {childLine, childEnd};

    ConversationAsset parent;
    parent.id = "scene.parent";
    parent.entry = "call";
    ConversationAsset::Node call;
    call.id = "call";
    call.kind = ConversationAsset::Node::Kind::Call;
    call.target = child.id;
    call.next = "after";
    ConversationAsset::Node after;
    after.id = "after";
    after.kind = ConversationAsset::Node::Kind::Line;
    after.text = "returned";
    after.next = "end";
    ConversationAsset::Node end;
    end.id = "end";
    end.kind = ConversationAsset::Node::Kind::End;
    parent.nodes = {call, after, end};

    const auto resolve = [&](const std::string& id) -> const ConversationAsset* {
        if (id == parent.id) return &parent;
        if (id == child.id) return &child;
        return nullptr;
    };
    ConversationRunner original;
    original.setAssetResolver(resolve);
    std::string error;
    CHECK(original.start(&parent, StateValue::object(), &error));
    CHECK(original.currentNodeId() == "line");
    original.locals().set("calculatedPrice", StateValue::integer(42));
    StateValue saved;
    CHECK(original.captureState(saved));

    ConversationRunner restored;
    restored.setAssetResolver(resolve);
    CHECK(restored.restoreState(saved, &error));
    CHECK(restored.currentNodeId() == "line");
    CHECK(restored.locals().find("calculatedPrice")->asInt() == 42);
    CHECK(restored.advance(&error));
    CHECK(restored.currentNodeId() == "after");
}

TEST_CASE("dialogueConversation.commandsAndEvents") {
    ConversationAsset asset;
    asset.id = "scene.command";
    asset.entry = "calculate";
    ConversationAsset::Node command;
    command.id = "calculate";
    command.kind = ConversationAsset::Node::Kind::Command;
    command.target = "economy.quote";
    command.expression = "quote";
    command.next = "line";
    ConversationAsset::Node line;
    line.id = "line";
    line.kind = ConversationAsset::Node::Kind::Line;
    line.next = "end";
    ConversationAsset::Node end;
    end.id = "end";
    end.kind = ConversationAsset::Node::Kind::End;
    asset.nodes = {command, line, end};

    std::vector<ConversationRunner::Event::Kind> events;
    ConversationRunner runner;
    runner.setEventSink(
        [&](const ConversationRunner::Event& event) { events.push_back(event.kind); });
    runner.registerCommand(
        "economy.quote", [](const StateValue&, const StateValue&, const StateValue&) {
            ConversationRunner::CommandResult result;
            result.value = StateValue::integer(125);
            return result;
        });
    std::string error;
    CHECK(runner.start(&asset, StateValue::object(), &error));
    CHECK(runner.currentNodeId() == "line");
    CHECK(runner.locals().find("quote")->asInt() == 125);
    CHECK(events.size() >= 5);
}

TEST_CASE("dialogueConversation.asyncCommand") {
    ConversationAsset asset;
    asset.id = "scene.wait-command";
    asset.entry = "animate";
    ConversationAsset::Node command;
    command.id = "animate";
    command.kind = ConversationAsset::Node::Kind::Command;
    command.target = "animation.play";
    command.expression = "animationResult";
    command.next = "end";
    ConversationAsset::Node end;
    end.id = "end";
    end.kind = ConversationAsset::Node::Kind::End;
    asset.nodes = {command, end};
    ConversationRunner runner;
    runner.registerCommand(
        "animation.play", [](const StateValue&, const StateValue&, const StateValue&) {
            ConversationRunner::CommandResult result;
            result.status = ConversationRunner::CommandResult::Status::Blocked;
            return result;
        });
    std::string error;
    CHECK(runner.start(&asset, StateValue::object(), &error));
    CHECK(runner.isBlocked());
    CHECK(runner.resumeCommand(StateValue::string("finished"), &error));
    CHECK(!runner.isActive());
}
