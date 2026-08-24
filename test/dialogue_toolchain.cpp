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

TEST_CASE("dialogueToolchain.largeWorkspaceStress") {
    constexpr int                  assetCount    = 160;
    constexpr int                  nodesPerAsset = 100;
    std::vector<ConversationAsset> assets;
    assets.reserve(assetCount);
    for (int assetIndex = 0; assetIndex < assetCount; ++assetIndex) {
        ConversationAsset asset;
        asset.id    = "stress." + std::to_string(assetIndex);
        asset.entry = "node.0";
        asset.nodes.reserve(nodesPerAsset);
        for (int nodeIndex = 0; nodeIndex < nodesPerAsset - 1; ++nodeIndex) {
            ConversationAsset::Node node;
            node.id   = "node." + std::to_string(nodeIndex);
            node.kind = ConversationAsset::Node::Kind::Line;
            node.text = "Line {speaker.name} " + std::to_string(nodeIndex);
            node.next = "node." + std::to_string(nodeIndex + 1);
            asset.nodes.push_back(std::move(node));
        }
        asset.nodes.push_back({"node.99", ConversationAsset::Node::Kind::End});
        assets.push_back(std::move(asset));
    }
    std::vector<ConversationDiagnostic> diagnostics;
    CHECK(lintConversationWorkspace(assets, "stress-workspace", diagnostics));
    CHECK(diagnostics.empty());
    CHECK(assets.size() == assetCount);
    CHECK(assets.back().nodes.size() == nodesPerAsset);
}
