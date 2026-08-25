#include "dialogue/ConversationCompiler.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::dialogue;

TEST_CASE("dialogueCompiler.parameterizedExpressionsAndLocalization") {
    const std::string source = R"(
conversation common.greeting version=3 entry=decide params=speaker,listener,location
node decide branch
when score(speaker, listener, location) >= threshold(speaker.personality) -> friendly
else -> formal
node friendly line speaker=speaker pool=greeting.friendly i18n=dialogue.greeting.friendly voice=voice.greeting.friendly next=end
node formal line speaker=speaker text="Good day, {listener.name}." i18n=dialogue.greeting.formal next=end
node unused end
node end end
endconversation
)";
    std::vector<ConversationAsset> assets;
    std::vector<ConversationDiagnostic> diagnostics;
    CHECK(compileDnutConversations(source, "greeting.dnut", assets, diagnostics));
    CHECK(assets.size() == 1);
    CHECK(assets[0].version == 3);
    CHECK(assets[0].findNode("decide")->routes[0].first ==
          "score(speaker, listener, location) >= threshold(speaker.personality)");
    CHECK(diagnostics.size() == 1);
    CHECK(static_cast<int>(diagnostics[0].severity) ==
          static_cast<int>(ConversationDiagnostic::Severity::Warning));
    const std::string csv = exportConversationLocalizationCsv(assets);
    CHECK(csv.find("dialogue.greeting.friendly") != std::string::npos);
    CHECK(csv.find("Good day, {listener.name}.") != std::string::npos);
}
