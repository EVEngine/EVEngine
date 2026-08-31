#include "dialogue/ConversationLocalization.h"
#include "dialogue/DialogueFlow.h"
#include "i18n/I18n.h"
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

TEST_CASE("dialogueLocalization.validatesExactRuntimeLocaleCoverage") {
    auto* localization = eve::i18n::I18n::create();
    auto* flow         = DialogueFlow::create();
    REQUIRE(localization != nullptr);
    REQUIRE(flow != nullptr);
    localization->clear();
    flow->clear();
    auto bundle = localization->replaceBundleFromJson(R"({
      "schema":"eve.i18n.bundle","version":1,"defaultLocale":"en",
      "locales":{
        "en":{"intro":{"welcome":"Welcome"}},
        "zh-CN":{"intro":{"welcome":"欢迎"}}
      }
    })");
    REQUIRE(bundle.ok());
    REQUIRE_EQ(flow->loadFromDnut("conversation intro version=1 entry=welcome\n"
                                  "node welcome line speaker=guide i18n=intro.welcome next=end\n"
                                  "node end end\nendconversation\n",
                                  "localized.dnut"),
               1);
    auto validated = flow->validateLocalization(*localization, "zh-CN");
    REQUIRE(validated.ok());
    CHECK_EQ(validated.value(), 1);
    auto missingLocale = flow->validateLocalization(*localization, "fr");
    CHECK(!missingLocale.ok());

    REQUIRE_EQ(flow->loadFromDnut("conversation broken version=1 entry=line\n"
                                  "node line line speaker=guide i18n=intro.missing next=end\n"
                                  "node end end\nendconversation\n",
                                  "missing.dnut"),
               1);
    auto missingKey = flow->validateLocalization(*localization, "en");
    CHECK(!missingKey.ok());
    flow->clear();
    localization->clear();
}
