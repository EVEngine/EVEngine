#include "dialogue/ConversationImporter.h"
#include "dialogue/DialogueSequence.h"
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
    std::vector<ConversationDiagnostic> diagnostics;
    auto imported = importYarnConversation(source, "intro.yarn", diagnostics);
    REQUIRE(imported.ok());
    auto assets = std::move(imported).takeValue();
    CHECK(assets.size() == 1);
    CHECK(assets[0].id == "intro");
    CHECK(sequencePayloadString(*assets[0].findNode("Start"), "speaker") == "Guide");
    CHECK(assets[0].findNode("Start.1")->routes[0].target == "Next");
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
    std::vector<ConversationDiagnostic> diagnostics;
    auto imported = importTweeConversation(source, "travel.twee", diagnostics);
    REQUIRE(imported.ok());
    auto assets = std::move(imported).takeValue();
    CHECK(assets.size() == 1);
    CHECK(assets[0].findNode("Start.1")->routes.size() == 2);
    CHECK(sequencePayloadString(*assets[0].findNode("Market"), "text") == "Fresh fruit!");
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
    std::vector<ConversationDiagnostic> diagnostics;
    auto imported = importYarnConversation(source, "intro.yarn", diagnostics);
    REQUIRE(imported.ok());
    auto assets = std::move(imported).takeValue();
    CHECK(sequencePayloadString(*assets[0].findNode("Start"), "i18n") == "intro.welcome");
    CHECK(sequencePayloadString(*assets[0].findNode("Start"), "voice") == "intro_001");
    CHECK(sequencePayloadString(*assets[0].findNode("Start.1"), "name") == "set");
    CHECK(sequencePayloadString(*assets[0].findNode("Start.2"), "duration") == "0.25");
    CHECK(assets[0].findNode("Start.3")->routes[0].target == "Next");
    CHECK(assets[0].findNode("Next")->type == "end");
}
