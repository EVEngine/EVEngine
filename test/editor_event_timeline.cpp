#include "editor/EditorEventTimeline.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editor;

TEST_CASE("editor.timeline_filters_correlations_and_rejects_stale_pages") {
    EditorEventTimeline timeline;
    CHECK(timeline.append({0, 10, "network", "connected", "peer-1", "session-a",
                           DiagnosticSeverity::Info, EditorValue::Object{}}).isAccepted());
    CHECK(timeline.append({0, 12, "network", "timeout", "peer-2", "session-b",
                           DiagnosticSeverity::Warning, EditorValue::Object{{"retry", int64_t{2}}}}).isAccepted());
    EditorTimelineQuery filter; filter.domains = {"network"};
    filter.correlation = "session-b"; filter.minimumSeverity = DiagnosticSeverity::Warning;
    auto page = timeline.query(filter, 0, 10); REQUIRE(page.value);
    CHECK_EQ(page.value->values.size(), 1U);
    const auto generation = page.value->generation;
    CHECK(timeline.append({0, 13, "filesystem", "modified", "asset", {},
                           DiagnosticSeverity::Info, {}}).isAccepted());
    CHECK_EQ(static_cast<int>(timeline.query(filter, 0, 10, generation).status),
             static_cast<int>(EditorStatus::Conflict));
}

TEST_CASE("editor.timeline_capacity_drops_oldest_and_payload_is_bounded") {
    EditorEventTimeline timeline;
    CHECK(timeline.setCapacity(2).isAccepted());
    CHECK(timeline.append({0, 1, "a", "one", {}, {}, DiagnosticSeverity::Info, {}}).isAccepted());
    CHECK(timeline.append({0, 2, "a", "two", {}, {}, DiagnosticSeverity::Info, {}}).isAccepted());
    CHECK(timeline.append({0, 3, "a", "three", {}, {}, DiagnosticSeverity::Info, {}}).isAccepted());
    auto page = timeline.query({}, 0, 10); REQUIRE(page.value);
    CHECK_EQ(page.value->values.size(), 2U); CHECK_EQ(page.value->droppedEvents, 1U);
    CHECK_EQ(page.value->values.front().type, std::string("two"));
    CHECK_EQ(static_cast<int>(timeline.append({0, 4, "a", "large", {}, {},
        DiagnosticSeverity::Info, EditorValue::Array{1.0, 2.0}}, 2).status),
        static_cast<int>(EditorStatus::Rejected));
}
