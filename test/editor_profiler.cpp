#include "profiler_editing/ProfilerModel.h"

#include <zeroerr/assert.h>
#include <zeroerr/unittest.h>

using namespace eve::profiler_editing;

namespace {
EditorProfilerFrame frame(std::uint64_t sequence, double cpuMs, double gpuMs) {
    EditorProfilerFrame value;
    value.sequence           = sequence;
    value.cpuFrameMs         = cpuMs;
    value.gpuFrameMs         = gpuMs;
    value.gpuTimingAvailable = true;
    value.zones = {{"physics", "step", "main", 5.0, 7.0, 2, 1},
                   {"graphics", "draw", "render", 3.0, 4.0, 1, 1},
                   {"physics", "broadphase", "main", 2.0, 2.0, 1, 2}};
    return value;
}
}  // namespace

TEST_CASE("editor.profiler retains bounded owning history") {
    EditorProfilerModel model;
    auto configured = model.configure({10.0, 10.0, 4.0, 2});
    REQUIRE(static_cast<int>(configured.status) == static_cast<int>(EditorStatus::Applied));
    REQUIRE(static_cast<int>(model.ingest(frame(1, 8.0, 7.0)).status) == static_cast<int>(EditorStatus::Applied));
    REQUIRE(static_cast<int>(model.ingest(frame(2, 12.0, 11.0)).status) == static_cast<int>(EditorStatus::Applied));
    REQUIRE(static_cast<int>(model.ingest(frame(3, 9.0, 8.0)).status) == static_cast<int>(EditorStatus::Applied));
    CHECK(model.history().size() == 2);
    CHECK(model.history().front().sequence == 2);

    const auto before = model.history().size();
    CHECK(static_cast<int>(model.ingest(frame(3, 9.0, 8.0)).status) == static_cast<int>(EditorStatus::Conflict));
    CHECK(model.history().size() == before);
}

TEST_CASE("editor.profiler filters sorts and summarizes latest frame") {
    EditorProfilerModel model;
    REQUIRE(static_cast<int>(model.ingest(frame(1, 12.0, 11.0)).status) == static_cast<int>(EditorStatus::Applied));

    EditorProfilerQuery query;
    query.module = "physics";
    query.limit  = 1;
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
    REQUIRE(static_cast<int>(model.configure({10.0, 10.0, 4.0, 30}).status) == static_cast<int>(EditorStatus::Applied));
    REQUIRE(static_cast<int>(model.ingest(frame(1, 12.0, 11.0)).status) == static_cast<int>(EditorStatus::Applied));
    CHECK(model.diagnostics().size() == 3);
}

TEST_CASE("editor.profiler rejects invalid frames atomically") {
    EditorProfilerModel model;
    auto invalid       = frame(1, 1.0, 1.0);
    invalid.zones[0].selfMs = 8.0;
    CHECK(static_cast<int>(model.ingest(std::move(invalid)).status) == static_cast<int>(EditorStatus::Rejected));
    CHECK(model.history().empty());
}
