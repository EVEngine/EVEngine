#include "common/Profile.h"
#include "profiler/Profiler.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

namespace {
struct CaptureGuard {
    eve::profiler::Profiler *profiler = eve::profiler::Profiler::create();
    CaptureGuard() {
        profiler->reset();
        profiler->setEnabled(true);
        eve::debug::rtFrameBegin();
    }
    ~CaptureGuard() {
        profiler->setEnabled(false);
        profiler->reset();
    }
};
}  // namespace

TEST_CASE("profiler.core.reportMeasuresCapturedSpan") {
    CaptureGuard capture;
    {
        EV_PROFILE_MODULE("test", "measured-work");
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    eve::debug::rtFrameEnd();
    auto snapshot = capture.profiler->captureFrame();
    REQUIRE(snapshot.ok());
    REQUIRE(snapshot.value().cpuFrameMs >= 7.0);
    REQUIRE_NOT(snapshot.value().gpuTimingAvailable);
    double capturedMs = 0.0;
    REQUIRE_EQ(
        std::sscanf(eve::prof::Profiler::textReport().c_str(), "=== Profiler captured CPU span (%lf ms)", &capturedMs),
        1);
    REQUIRE(capturedMs >= 7.0);

    const auto &samples = eve::prof::Profiler::lastFrame();
    REQUIRE_EQ(samples.size(), 1u);
    REQUIRE(capturedMs + 0.02 >= samples.front().totalMs);
    REQUIRE_EQ(samples.front().thread, std::string("main"));
}

TEST_CASE("profiler.core.sameNameDifferentStorageAggregates") {
    CaptureGuard      capture;
    static const char first[]  = "same-zone";
    static const char second[] = "same-zone";
    eve::prof::Profiler::zoneBegin(first, "test");
    eve::prof::Profiler::zoneEnd();
    eve::prof::Profiler::zoneBegin(second, "test");
    eve::prof::Profiler::zoneEnd();
    eve::debug::rtFrameEnd();
    const auto &samples = eve::prof::Profiler::lastFrame();
    REQUIRE_EQ(samples.size(), 1u);
    REQUIRE_EQ(samples.front().count, 2);
    REQUIRE(samples.front().selfMs >= 0.0);
    REQUIRE(samples.front().selfMs <= samples.front().totalMs);
}

TEST_CASE("profiler.capture.automaticFrameBeginIncludesEarlier3DWork") {
    CaptureGuard capture;
    {
        eve::debug::RenderPassScope work("early-3d-work");
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    eve::debug::rtFrameBegin();
    { eve::debug::RenderPassScope work("later-2d-work"); }
    eve::debug::rtFrameEnd();
    auto snapshot = capture.profiler->captureFrame();
    REQUIRE(snapshot.ok());
    REQUIRE(snapshot.value().cpuFrameMs >= 7.0);
    REQUIRE_EQ(snapshot.value().zones.size(), 2u);
    eve::debug::rtFrameEnd();
    auto repeated = capture.profiler->captureFrame();
    REQUIRE(repeated.ok());
    REQUIRE_EQ(repeated.value().sequence, snapshot.value().sequence);
}
