#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "os/OS.h"
#include "timer/Timer.h"

#include <cmath>
#include <string>

using namespace eve::os;

TEST_CASE("os.module.basics") {
    auto *sys = OS::create();
    REQUIRE(sys != nullptr);
    CHECK_EQ(sys->getName(), std::string("OS"));
    CHECK(!sys->getEngineVersion().empty());
    CHECK(!sys->getPlatform().empty());
    CHECK(!sys->getOS().empty());
}

TEST_CASE("os.cpuAndRam") {
    auto *sys = OS::create();
    CHECK_GE(sys->getProcessorCount(), 1);
    CHECK_GE(sys->getSystemRAM(), 1);
    // Process RSS may be 0 on some platforms before pages are faulted; allow >= 0.
    CHECK_GE(sys->getProcessMemoryMB(), 0);
    CHECK_GE(sys->getCPUCacheLineSize(), 0);
}

TEST_CASE("os.wallTimeAndSleep") {
    auto *sys = OS::create();
    float t0  = sys->getWallTime();
    // float epoch seconds only have ~1s precision; just sanity-check magnitude.
    CHECK_GT(t0, 1.0e9f);
    // sleep must not throw / hang forever — measure with Timer (monotonic).
    auto *timer = eve::timer::Timer::create();
    timer->step();
    sys->sleepMilliseconds(30);
    float dt = timer->step();
    CHECK_GE(dt, 0.02f);
}

TEST_CASE("os.power") {
    auto *sys   = OS::create();
    std::string st = sys->getPowerState();
    CHECK((st == "unknown" || st == "on_battery" || st == "no_battery" || st == "charging" ||
           st == "charged"));
    int secs = sys->getPowerSecondsLeft();
    int pct  = sys->getPowerPercent();
    CHECK_GE(secs, -1);
    CHECK_GE(pct, -1);
    if (pct >= 0) CHECK_LE(pct, 100);
}

TEST_CASE("os.gpuBeforeInit") {
    // Graphics may exist as a module singleton but not be Vulkan-initialized in unit tests.
    auto *sys = OS::create();
    // Must not crash; empty/0 is fine.
    (void)sys->getGpuName();
    (void)sys->getGpuVendor();
    (void)sys->getGpuDeviceType();
    CHECK_GE(sys->getGpuMemoryTotalMB(), 0);
}

TEST_CASE("os.clipboardRoundTrip") {
    auto *sys = OS::create();
    // Clipboard may fail in headless CI; soft-check.
    try {
        sys->setClipboardText("eve-os-clipboard-test");
        std::string got = sys->getClipboardText();
        CHECK_EQ(got, std::string("eve-os-clipboard-test"));
    } catch (...) {
        // Accept failure when no clipboard provider is available.
        CHECK(true);
    }
}
