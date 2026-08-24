#include "dialogue/ConversationImporter.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::dialogue;

TEST_CASE("dialogueImporter.yarnSpinner") {
    const std::string                   source = R"(title: Start
---
Guide: Welcome, {player.name}.
[[Continue|Next]]
===
title: Next
---
Guide: Let us begin.
===)";
    std::vector<ConversationAsset>      assets;
    std::vector<ConversationDiagnostic> diagnostics;
    CHECK(importYarnConversation(source, "intro.yarn", assets, diagnostics));
    CHECK(assets.size() == 1);
    CHECK(assets[0].id == "intro");
    CHECK(assets[0].findNode("Start")->speaker == "Guide");
    CHECK(assets[0].findNode("Start.1")->routes[0].second == "Next");
}

TEST_CASE("dialogueImporter.twineTwee3") {
    const std::string                   source = R"(:: StoryData
{"ifid":"test"}
:: Start [intro]
Narrator: Choose a destination.
[[Market->Market]]
[[Harbor<-Harbor]]
:: Market
Merchant: Fresh fruit!
:: Harbor
Sailor: Fair winds.)";
    std::vector<ConversationAsset>      assets;
    std::vector<ConversationDiagnostic> diagnostics;
    CHECK(importTweeConversation(source, "travel.twee", assets, diagnostics));
    CHECK(assets.size() == 1);
    CHECK(assets[0].findNode("Start.1")->routes.size() == 2);
    CHECK(assets[0].findNode("Market")->text == "Fresh fruit!");
}

TEST_CASE("dialogueImporter.yarnCommandsTagsAndShortcutOptions") {
    const std::string                   source = R"(title: Start
---
Guide: Welcome. #line:intro.welcome #voice:intro_001
<<set $met_guide = true>>
<<wait 0.25>>
-> Continue
    <<jump Next>>
===
title: Next
---
<<stop>>
===)";
    std::vector<ConversationAsset>      assets;
    std::vector<ConversationDiagnostic> diagnostics;
    CHECK(importYarnConversation(source, "intro.yarn", assets, diagnostics));
    CHECK(assets[0].findNode("Start")->i18nKey == "intro.welcome");
    CHECK(assets[0].findNode("Start")->voice == "intro_001");
    CHECK(assets[0].findNode("Start.1")->target == "set");
    CHECK(assets[0].findNode("Start.2")->expression == "0.25");
    CHECK(assets[0].findNode("Start.3")->routes[0].second == "Next");
    CHECK(static_cast<int>(assets[0].findNode("Next")->kind) == static_cast<int>(ConversationAsset::Node::Kind::End));
}
