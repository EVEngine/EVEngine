#include "dialogue/ConversationCompiler.h"
#include "dialogue/DnutParser.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::dialogue;

TEST_CASE("dialogueCompiler.singleParserMixedDocumentAndStrictSchema") {
    const std::string source = R"(
schema "eve.dnut"
version 1
pool greeting {
  guide: "A // inside string is not a comment" id="greeting.line"
}
conversation greeting.scene entry=end {
  parameter count int optional default=2
  node end end
}
)";
    DnutDocument document;
    std::vector<ConversationDiagnostic> diagnostics;
    REQUIRE(parseDnutDocument(source, "mixed.dnut", document, diagnostics));
    CHECK_EQ(document.conversations.size(), size_t(1));
    CHECK_EQ(document.conversations[0].parameters[0].defaultValue.asInt(), 2);
    REQUIRE(document.poolRoot.find("pools") != nullptr);

    DnutDocument rejected;
    diagnostics.clear();
    CHECK(!parseDnutDocument("schema \"eve.dnut\"\nversion 1\nunknown thing\n", "bad.dnut", rejected,
                             diagnostics));
    REQUIRE(!diagnostics.empty());
    CHECK_EQ(diagnostics[0].code, std::string("DnutParseError"));
}

TEST_CASE("dialogueCompiler.parameterizedExpressionsAndLocalization") {
    const std::string source = R"DNUT(
schema "eve.dnut"
version 1
conversation common.greeting version=3 entry=decide {
parameter speaker string required
parameter listener string required
parameter location string required
node decide branch {
route friendly when="score(speaker, listener, location) >= threshold(speaker.personality)" target=friendly
route formal target=formal
}
node friendly line speaker=speaker pool=greeting.friendly i18n=dialogue.greeting.friendly voice=voice.greeting.friendly next=end
node formal line speaker=speaker text="Good day, {listener.name}." i18n=dialogue.greeting.formal next=end
node unused end
node end end
}
)DNUT";
    std::vector<ConversationAsset> assets;
    std::vector<ConversationDiagnostic> diagnostics;
    CHECK(compileDnutConversations(source, "greeting.dnut", assets, diagnostics));
    CHECK(assets.size() == 1);
    CHECK(assets[0].version == 3);
    CHECK(assets[0].findNode("decide")->routes[0].first == "friendly");
    CHECK(diagnostics.size() == 1);
    CHECK(static_cast<int>(diagnostics[0].severity) ==
          static_cast<int>(ConversationDiagnostic::Severity::Warning));
    const std::string csv = exportConversationLocalizationCsv(assets);
    CHECK(csv.find("dialogue.greeting.friendly") != std::string::npos);
    CHECK(csv.find("Good day, {listener.name}.") != std::string::npos);
}
