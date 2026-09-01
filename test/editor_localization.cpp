#include "localization_editing/LocalizationDocument.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::localization_editing;

TEST_CASE("editor.localization_reports_completeness_placeholders_and_voice_metadata") {
    LocalizationDocument document;
    REQUIRE(document.addRow("dialogue.greeting", "Hello {name}", "Opening line").ok());
    REQUIRE(document.setVariant("dialogue.greeting", "zh-CN",
                                {"你好 {name}", "asset://voice/zh/greeting.ogg", "approved", 1.2}).ok());
    REQUIRE(document.setVariant("dialogue.greeting", "fr-FR",
                                {"Bonjour", "", "missing", 0.0}).ok());
    const LocalizationAnalysis analysis = document.analyze({"zh-CN", "fr-FR"}, true);
    CHECK_EQ(static_cast<int>(analysis.status), static_cast<int>(EditorStatus::Failed));
    CHECK_EQ(analysis.locales.size(), size_t{2});
    CHECK_EQ(analysis.locales[0].translated, 1);
    CHECK_EQ(analysis.locales[0].voiced, 1);
    CHECK_EQ(analysis.locales[0].approved, 1);
    CHECK(analysis.diagnostics.size() >= size_t{2});
}

TEST_CASE("editor.localization_snapshot_round_trip_is_atomic_and_deterministic") {
    LocalizationDocument source;
    REQUIRE(source.addRow("ui.quit", "Quit").ok());
    REQUIRE(source.setVariant("ui.quit", "zh-CN", {"退出", "", "", 0.0}).ok());
    const EditorValue snapshot = source.snapshotValue();

    LocalizationDocument restored;
    REQUIRE(restored.loadSnapshot(snapshot).ok());
    CHECK_EQ(restored.snapshotValue(), snapshot);
    const Revision before = restored.revision();
    EditorValue broken = snapshot;
    auto* object = broken.getIf<EditorValue::Object>();
    REQUIRE(object);
    (*object)["schemaVersion"] = int64_t{99};
    CHECK_EQ(static_cast<int>(restored.loadSnapshot(broken).code()),
             static_cast<int>(EditorStatus::Unsupported));
    CHECK_EQ(restored.revision(), before);
    CHECK_EQ(restored.snapshotValue(), snapshot);
}

TEST_CASE("editor.localization_rejects_duplicate_keys_and_invalid_voice_status") {
    LocalizationDocument document;
    REQUIRE(document.addRow("line.one", "One").ok());
    CHECK_EQ(static_cast<int>(document.addRow("line.one", "Duplicate").code()),
             static_cast<int>(EditorStatus::Conflict));
    CHECK_EQ(static_cast<int>(document.setVariant("line.one", "en-US", {"One", "", "done", 0.0}).code()),
             static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(static_cast<int>(document.setVariant("missing", "en-US", {"Missing", "", "", 0.0}).code()),
             static_cast<int>(EditorStatus::NotFound));
}
