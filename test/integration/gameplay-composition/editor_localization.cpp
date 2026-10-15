#include <zeroerr/assert.h>
#include <zeroerr/unittest.h>
#include "i18n/editing/LocalizationDocument.h"
#include "profiler/editing/ProfilerModel.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// editor_localization.cpp
using namespace eve::localization_editing;

TEST_CASE("editor.localization_reports_completeness_placeholders_and_voice_metadata") {
    LocalizationDocument document;
    REQUIRE(document.addRow("dialogue.greeting", "Hello {name}", "Opening line").ok());
    REQUIRE(
        document
            .setVariant("dialogue.greeting", "zh-CN", {"你好 {name}", "asset://voice/zh/greeting.ogg", "approved", 1.2})
            .ok());
    REQUIRE(document.setVariant("dialogue.greeting", "fr-FR", {"Bonjour", "", "missing", 0.0}).ok());
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
    EditorValue    broken = snapshot;
    auto*          object = broken.getIf<EditorValue::Object>();
    REQUIRE(object);
    (*object)["schemaVersion"] = int64_t{99};
    CHECK_EQ(static_cast<int>(restored.loadSnapshot(broken).code()), static_cast<int>(EditorStatus::Unsupported));
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

// editor_profiler.cpp
using namespace eve::profiler_editing;

namespace {
EditorProfilerFrame frame(std::uint64_t sequence, double cpuMs, double gpuMs) {
    EditorProfilerFrame value;
    value.sequence           = sequence;
    value.cpuFrameMs         = cpuMs;
    value.gpuFrameMs         = gpuMs;
    value.gpuTimingAvailable = true;
    value.zones              = {{"physics", "step", "main", 5.0, 7.0, 2, 1},
                                {"graphics", "draw", "render", 3.0, 4.0, 1, 1},
                                {"physics", "broadphase", "main", 2.0, 2.0, 1, 2}};
    return value;
}
}  // namespace

TEST_CASE("editor.profiler retains bounded owning history") {
    EditorProfilerModel model;
    auto                configured = model.configure({10.0, 10.0, 4.0, 2});
    REQUIRE(static_cast<int>(configured.code()) == static_cast<int>(EditorStatus::Applied));
    REQUIRE(static_cast<int>(model.ingest(frame(1, 8.0, 7.0)).code()) == static_cast<int>(EditorStatus::Applied));
    REQUIRE(static_cast<int>(model.ingest(frame(2, 12.0, 11.0)).code()) == static_cast<int>(EditorStatus::Applied));
    REQUIRE(static_cast<int>(model.ingest(frame(3, 9.0, 8.0)).code()) == static_cast<int>(EditorStatus::Applied));
    CHECK(model.history().size() == 2);
    CHECK(model.history().front().sequence == 2);

    const auto before = model.history().size();
    CHECK(static_cast<int>(model.ingest(frame(3, 9.0, 8.0)).code()) == static_cast<int>(EditorStatus::Conflict));
    CHECK(model.history().size() == before);
}

TEST_CASE("editor.profiler filters sorts and summarizes latest frame") {
    EditorProfilerModel model;
    REQUIRE(static_cast<int>(model.ingest(frame(1, 12.0, 11.0)).code()) == static_cast<int>(EditorStatus::Applied));

    EditorProfilerQuery query;
    query.module    = "physics";
    query.limit     = 1;
    const auto rows = model.query(query);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].name == "step");

    const auto modules = model.moduleSummary();
    REQUIRE(modules.size() == 2);
    CHECK(modules[0].module == "physics");
    CHECK(modules[0].selfMs == 7.0);
    CHECK(modules[0].count == 3);
}

TEST_CASE("editor.profiler reports configured budget violations") {
    EditorProfilerModel model;
    REQUIRE(static_cast<int>(model.configure({10.0, 10.0, 4.0, 30}).code()) == static_cast<int>(EditorStatus::Applied));
    REQUIRE(static_cast<int>(model.ingest(frame(1, 12.0, 11.0)).code()) == static_cast<int>(EditorStatus::Applied));
    CHECK(model.diagnostics().size() == 3);
}

TEST_CASE("editor.profiler rejects invalid frames atomically") {
    EditorProfilerModel model;
    auto                invalid = frame(1, 1.0, 1.0);
    invalid.zones[0].selfMs     = 8.0;
    CHECK(static_cast<int>(model.ingest(std::move(invalid)).code()) == static_cast<int>(EditorStatus::Rejected));
    CHECK(model.history().empty());
}
