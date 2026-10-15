#include "dialogue/ConversationText.h"
#include "dialogue/ConversationToolchain.h"
#include "dialogue/DialogueSequence.h"
#include "dialogue/DialogueUX.h"
#include "dialogue/DialogueVoice.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// dialogue_text.cpp
using namespace eve;
using namespace eve::dialogue;

TEST_CASE("dialogueText.parametersFallbackAndTransforms") {
    Value                    bindings = Value::Object{{"speaker", Value::Object{{"name", Value("mara")}}},
                                                      {"listener", Value::Object{{"name", Value("Iris")}}},
                                                      {"location", Value::Object{{"name", Value("harbor")}}}};
    Value                    locals   = Value::Object{{"speaker", Value::Object{{"name", Value("local mara")}}}};
    ConversationTextRenderer renderer;
    CHECK(renderer.render("{speaker.name|capitalize} greets {listener.name} at {location.name|upper}.", bindings,
                          locals) == "Local mara greets Iris at HARBOR.");
    CHECK(renderer.render("Welcome, {listener.title??traveler}.", bindings, locals) == "Welcome, traveler.");
    CHECK(renderer.render("Keep {unknown.value} visible.", bindings, locals) == "Keep {unknown.value} visible.");
}

TEST_CASE("dialogueText.stateDrivenCharacterTone") {
    ConversationTextRenderer renderer;
    renderer.addToneRule("speaker.tired", "... ", " ...", "Good", "Fine");
    renderer.addToneRule("speaker.formal", "Captain, ", {}, {}, {});
    Value values = Value::Object{};
    CHECK(renderer.render("Good morning.", values, values, [](const std::string& expression) {
        return expression == "speaker.tired";
    }) == "... Fine morning. ...");
    CHECK(renderer.render("Good morning.", values, values, [](const std::string& expression) {
        return expression == "speaker.formal";
    }) == "Captain, Good morning.");
}

// dialogue_toolchain.cpp
using namespace eve::dialogue;

TEST_CASE("dialogueToolchain.crossFileCallsAndStableRenames") {
    eve::dnut::SequenceAsset caller;
    caller.id    = "quest";
    caller.entry = "call";
    eve::dnut::SequenceNode call;
    call.id   = "call";
    call.type = "call";
    call.next = "end";
    call.payload.set("target", eve::Value("greeting"));
    caller.nodes = {call, {"end", "end"}};
    eve::dnut::SequenceAsset greeting;
    greeting.id    = "greeting";
    greeting.entry = "line";
    greeting.nodes = {{"line", "line", "end"}, {"end", "end"}};
    std::vector<eve::dnut::SequenceAsset> assets{caller, greeting};
    std::vector<ConversationDiagnostic>   diagnostics;
    CHECK(lintConversationWorkspace(assets, "workspace", diagnostics).ok());
    CHECK(renameConversationAsset(assets, "greeting", "common.greeting").ok());
    CHECK(sequencePayloadString(*assets[0].findNode("call"), "target") == "common.greeting");
    CHECK(renameConversationNode(assets, "common.greeting", "line", "welcome").ok());
    CHECK(assets[1].entry == "welcome");
    CHECK(assets[1].version == 3);
    assets.erase(assets.begin() + 1);
    diagnostics.clear();
    CHECK(!lintConversationWorkspace(assets, "workspace", diagnostics).ok());
    CHECK(diagnostics.back().message.find("missing conversation") != std::string::npos);
}

TEST_CASE("dialogueToolchain.largeWorkspaceStress") {
    constexpr int                         assetCount    = 160;
    constexpr int                         nodesPerAsset = 100;
    std::vector<eve::dnut::SequenceAsset> assets;
    assets.reserve(assetCount);
    for (int assetIndex = 0; assetIndex < assetCount; ++assetIndex) {
        eve::dnut::SequenceAsset asset;
        asset.id    = "stress." + std::to_string(assetIndex);
        asset.entry = "node.0";
        asset.nodes.reserve(nodesPerAsset);
        for (int nodeIndex = 0; nodeIndex < nodesPerAsset - 1; ++nodeIndex) {
            eve::dnut::SequenceNode node;
            node.id   = "node." + std::to_string(nodeIndex);
            node.type = "line";
            node.payload.set("text", eve::Value("Line {speaker.name} " + std::to_string(nodeIndex)));
            node.next = "node." + std::to_string(nodeIndex + 1);
            asset.nodes.push_back(std::move(node));
        }
        asset.nodes.push_back({"node.99", "end"});
        assets.push_back(std::move(asset));
    }
    std::vector<ConversationDiagnostic> diagnostics;
    CHECK(lintConversationWorkspace(assets, "stress-workspace", diagnostics).ok());
    CHECK(diagnostics.empty());
    CHECK(assets.size() == assetCount);
    CHECK(assets.back().nodes.size() == nodesPerAsset);
}

// dialogue_ux.cpp
using namespace eve::dialogue;

TEST_CASE("dialogueUX.historyRichTextAndReadState") {
    DialogueUX* ux = DialogueUX::create();
    ux->clearHistory();
    ux->setSkipMode("off");
    ux->record("intro.1", "Alice", "Hello [pause=0.2][color=red]world[/color]!");
    CHECK(ux->getHistoryCount() == 1);
    CHECK(ux->getHistoryText(0) == "Hello world!");
    CHECK(ux->getTextActionCount("[speed=20]Hi[shake]!") == 2);
    CHECK(ux->isRead("intro.1"));
    CHECK(ux->setSkipMode("read"));
    CHECK(ux->shouldSkip("intro.1"));
    CHECK(!ux->shouldSkip("intro.2"));
}

TEST_CASE("dialogueUX.autoAdvanceWaitsForVoice") {
    DialogueUX* ux = DialogueUX::create();
    ux->setAutoMode(true);
    ux->setAutoDelay(0.5f);
    ux->resetAutoTimer();
    CHECK(!ux->updateAuto(1.f, true));
    CHECK(!ux->updateAuto(0.25f, false));
    CHECK(ux->updateAuto(0.25f, false));
}

// dialogue_voice.cpp
using namespace eve::dialogue;

TEST_CASE("dialogueVoice.localizedDurationAndEnvelope") {
    DialogueVoice* voice = DialogueVoice::create();
    voice->clear();
    voice->setEnvelopeRate(4.f);
    CHECK(voice->registerClip("intro.1", "en", 1.f, "0,0.25,0.75,1"));
    CHECK(voice->registerClip("intro.1", "", 0.5f, "0.1,0.2"));
    CHECK(voice->play("intro.1", "en"));
    voice->update(0.5f);
    CHECK(voice->getAmplitude() == 0.75f);
    CHECK(voice->isPlaying());
    voice->update(0.5f);
    CHECK(!voice->isPlaying());
    CHECK(voice->shouldAutoAdvance());
    CHECK(voice->play("intro.1", "zh"));
    CHECK(voice->getDuration() == 0.5f);
}
