#include "dialogue/ConversationToolchain.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::dialogue;

TEST_CASE("dialogueToolchain.crossFileCallsAndStableRenames") {
    ConversationAsset caller;
    caller.id    = "quest";
    caller.entry = "call";
    caller.nodes = {{"call", ConversationAsset::Node::Kind::Call, {}, {}, {}, {}, {}, {}, {}, "greeting", "end"},
                    {"end", ConversationAsset::Node::Kind::End}};
    ConversationAsset greeting;
    greeting.id    = "greeting";
    greeting.entry = "line";
    greeting.nodes = {{"line", ConversationAsset::Node::Kind::Line, "end"},
                      {"end", ConversationAsset::Node::Kind::End}};
    std::vector<ConversationAsset>      assets{caller, greeting};
    std::vector<ConversationDiagnostic> diagnostics;
    CHECK(lintConversationWorkspace(assets, "workspace", diagnostics));
    std::string error;
    CHECK(renameConversationAsset(assets, "greeting", "common.greeting", &error));
    CHECK(assets[0].findNode("call")->target == "common.greeting");
    CHECK(renameConversationNode(assets, "common.greeting", "line", "welcome", &error));
    CHECK(assets[1].entry == "welcome");
    CHECK(assets[1].version == 3);
    assets.erase(assets.begin() + 1);
    diagnostics.clear();
    CHECK(!lintConversationWorkspace(assets, "workspace", diagnostics));
    CHECK(diagnostics.back().message.find("missing conversation") != std::string::npos);
}
