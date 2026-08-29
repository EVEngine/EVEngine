#include "editor/EditorNetworkTelemetry.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::editor;
TEST_CASE("editor.network.telemetry_computes_rates_and_bounds_history") {
    NetworkTelemetryModel model(2);
    NetworkTelemetrySample a;a.revision=1;a.timeSeconds=1;a.sentBytes=100;a.receivedBytes=200;a.completions=10;a.errors=1;
    NetworkTelemetrySample b=a;b.revision=2;b.timeSeconds=3;b.sentBytes=500;b.receivedBytes=1000;b.completions=14;b.errors=2;
    REQUIRE(model.ingest(a).accepted());REQUIRE(model.ingest(b).accepted());
    CHECK_EQ(model.samples().back().sendBytesPerSecond,200.0);CHECK_EQ(model.samples().back().receiveBytesPerSecond,400.0);CHECK_EQ(model.samples().back().errorRate,.25);
    NetworkTelemetrySample c=b;c.revision=3;c.timeSeconds=4;c.sentBytes=600;REQUIRE(model.ingest(c).accepted());CHECK_EQ(model.samples().size(),std::size_t{2});
    CHECK_EQ(static_cast<int>(model.ingest(c).status),static_cast<int>(EditorStatus::Conflict));
}
TEST_CASE("editor.network.telemetry_handles_reset_and_reports_health") {
    NetworkTelemetryModel model;
    NetworkTelemetrySample a;a.revision=4;a.timeSeconds=1;a.sentBytes=1000;a.completions=100;a.errors=1;
    NetworkTelemetrySample reset;reset.revision=5;reset.timeSeconds=2;reset.queuedTcpBytes=600*1024;reset.watchedTcp=4097;
    REQUIRE(model.ingest(a).accepted());REQUIRE(model.ingest(reset).accepted());
    CHECK_EQ(model.samples().back().sendBytesPerSecond,0.0);
    const auto diagnostics=model.diagnostics();CHECK_EQ(diagnostics.size(),std::size_t{2});
}
