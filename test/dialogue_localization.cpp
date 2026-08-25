#include "dialogue/ConversationLocalization.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::dialogue;

TEST_CASE("dialogueLocalization.roundtripMissingAndVoiceManifest") {
    ConversationAsset asset;
    asset.id    = "intro";
    asset.entry = "welcome";
    ConversationAsset::Node line;
    line.id      = "welcome";
    line.kind    = ConversationAsset::Node::Kind::Line;
    line.text    = "Welcome";
    line.speaker = "Guide";
    line.i18nKey = "intro.welcome";
    line.voice   = "voice/default";
    line.next    = "end";
    asset.nodes  = {line, {"end", ConversationAsset::Node::Kind::End}};

    ConversationLocalizationCatalog     catalog;
    std::vector<ConversationDiagnostic> diagnostics;
    const std::string                   csv =
        "i18n_key,locale,translation,voice,status,duration\r\n"
        "\"intro.welcome\",\"zh\",\"欢迎你，旅行者\",\"voice/zh/001\",\"recorded\",\"1.25\"\r\n";
    CHECK(catalog.importCsv(csv, "", diagnostics) == 1);
    CHECK(catalog.resolveText("intro.welcome", "zh-CN", "Welcome") == "欢迎你，旅行者");
    CHECK(catalog.resolveVoice("intro.welcome", "zh-CN", "default") == "voice/zh/001");
    CHECK(catalog.resolveStatus("intro.welcome", "zh-CN") == "recorded");
    CHECK(catalog.resolveDuration("intro.welcome", "zh-CN") == 1.25);
    CHECK(catalog.exportMissingCsv({asset}, "zh-CN").find("intro.welcome") == std::string::npos);
    CHECK(catalog.exportMissingCsv({asset}, "ja").find("intro.welcome") != std::string::npos);
    const std::string manifest = catalog.exportVoiceRecordingCsv({asset}, "zh-CN");
    CHECK(manifest.find("recorded") != std::string::npos);
    CHECK(manifest.find("voice/zh/001") != std::string::npos);
}

TEST_CASE("dialogueLocalization.rejectsPartialDuration") {
    ConversationLocalizationCatalog     catalog;
    std::vector<ConversationDiagnostic> diagnostics;
    const std::string                   csv =
        "i18n_key,locale,translation,duration\n"
        "intro.invalid,en,Hello,1.25seconds\n";
    CHECK(catalog.importCsv(csv, "", diagnostics) == 1);
    CHECK(catalog.resolveDuration("intro.invalid", "en") == 0.0);
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().message == "invalid duration was ignored");
}
