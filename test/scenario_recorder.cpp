#include "common/Module.h"
#include "common/Runtime.h"
#include "devtools/DevTool.hpp"
#include "devtools/ScenarioRecorder.h"
#include "platform_event/PlatformEvent.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <string>

using namespace eve;

namespace {

const char* kTapScript = R"SQ(
hits <- 0;
function consumeTap(name) {
    if (name == "tap") {
        hits++;
        if (hits >= 3) throw "reproduced";
    }
    return hits;
}
)SQ";

// Polls every pending event and dispatches "tap" into consumeTap, catching the
// thrown error. Mirrors how the game loop consumes the event queue per frame.
bool drainTaps(ssq::VM& vm, bool& threw) {
    auto* ev = eve::ModuleManager::requireInstance<eve::platform_event::PlatformEvent>("PlatformEvent");
    if (!ev) return false;
    while (auto msg = ev->pollOwned()) {
        if (msg->name == "tap") {
            try {
                vm.callFunc(vm.findFunc("consumeTap"), vm, std::string("tap"));
            } catch (const std::exception&) {
                threw = true;
            }
        }
    }
    return true;
}

}  // namespace

TEST_CASE("devtools.scenario.recordPersistLoadRoundTrip") {
    dev::DevTool::instance().detach();
    dev::ScenarioRecorder::instance().cancel();

    Runtime rt(256, ssq::Libs::ALL);
    rt.runSource(kTapScript, "taps.nut");
    auto* ev = eve::ModuleManager::requireInstance<eve::platform_event::PlatformEvent>("PlatformEvent");
    ev->clear();

    auto& rec = dev::ScenarioRecorder::instance();
    CHECK(rec.begin(rt.handle()));
    CHECK(rec.recording());
    CHECK(!rec.baseline().empty());

    // Two frames; three "tap" messages consumed across them.
    rec.markFrame();
    ev->pushData("tap", "a");
    ev->pollOwned();
    rec.markFrame();
    ev->pushData("tap", "b");
    ev->pollOwned();
    ev->pushData("tap", "c");
    ev->pollOwned();

    const std::string path = "scenario_roundtrip_test.json";
    CHECK(rec.end(path));
    CHECK(!rec.recording());

    // Reload the persisted scenario independently of the recorder's memory.
    std::string baseline;
    std::vector<dev::ScenarioFrame> frames;
    std::string er, es;
    CHECK(dev::ScenarioRecorder::load(path, &baseline, &frames, &er, &es));
    CHECK(!baseline.empty());
    int taps = 0;
    for (const auto& f : frames)
        for (const auto& e : f.events)
            if (e.name == "tap") ++taps;
    CHECK_EQ(taps, 3);
    CHECK_EQ(static_cast<int>(frames.size()), 2);

    dev::ScenarioRecorder::instance().cancel();
    dev::DevTool::instance().detach();
}

TEST_CASE("devtools.scenario.replayReproducesStateDrivenError") {
    dev::DevTool::instance().detach();
    dev::ScenarioRecorder::instance().cancel();

    // --- Record the failing run ---
    Runtime rt(256, ssq::Libs::ALL);
    rt.runSource(kTapScript, "taps.nut");
    auto* ev = eve::ModuleManager::requireInstance<eve::platform_event::PlatformEvent>("PlatformEvent");
    ev->clear();

    auto& rec = dev::ScenarioRecorder::instance();
    CHECK(rec.begin(rt.handle()));  // baseline: hits = 0
    rec.markFrame();
    ev->pushData("tap", "a");
    ev->pollOwned();
    rec.markFrame();
    ev->pushData("tap", "b");
    ev->pollOwned();
    ev->pushData("tap", "c");
    ev->pollOwned();

    const std::string path = "scenario_repro_test.json";
    CHECK(rec.end(path));
    dev::DevTool::instance().detach();

    // --- Replay on a fresh VM: restore baseline, re-stage the recorded taps ---
    Runtime rt2(256, ssq::Libs::ALL);
    rt2.runSource(kTapScript, "taps.nut");
    CHECK(rec.beginReplay(rt2.handle(), path));

    // Baseline restored: hits is back to 0 before any staged input.
    const int64_t before = rt2.vm().find("hits").toInt();
    CHECK_EQ(before, 0);

    bool reproduced = false;
    while (rec.framesRemaining() > 0) {
        CHECK(rec.stageFrame());
        CHECK(drainTaps(rt2.vm(), reproduced));
    }
    // All 3 taps replayed from the clean baseline -> hits reaches 3 -> throws.
    CHECK(reproduced);

    dev::ScenarioRecorder::instance().cancel();
    dev::DevTool::instance().detach();
}
