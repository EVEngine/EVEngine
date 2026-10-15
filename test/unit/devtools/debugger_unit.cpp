#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Capability.h"
#include "common/IStateProvider.h"
#include "common/Module.h"
#include "common/StateValue.h"
#include "devtools/Debugger.hpp"
#include "devtools/DevTool.hpp"
#include "devtools/Snapshot.hpp"

#include <simplesquirrel/simplesquirrel.hpp>
#include <squirrel.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

using namespace eve::dev;

TEST_CASE("devtools.debugger.pauseResumeStepFrame") {
    Debugger& d = Debugger::instance();
    d.detach();
    CHECK(!d.isPaused());
    CHECK(d.shouldRunUpdate());

    d.pause(PauseReason::PauseKey);
    CHECK(d.isPaused());
    CHECK(!d.shouldRunUpdate());
    CHECK(static_cast<int>(d.lastPauseReason()) == static_cast<int>(PauseReason::PauseKey));

    d.stepFrame();
    CHECK(d.shouldRunUpdate());
    d.notifyFrameDone();
    CHECK(d.isPaused());
    CHECK(static_cast<int>(d.lastPauseReason()) == static_cast<int>(PauseReason::Step));

    d.resume();
    CHECK(!d.isPaused());
    CHECK(d.shouldRunUpdate());
}

TEST_CASE("devtools.debugger.breakpointsMatchBasename") {
    Debugger& d = Debugger::instance();
    d.detach();
    d.clearBreakpoints();

    const int id = d.setBreakpoint("scripts/main.nut", 42);
    CHECK(id > 0);
    CHECK(d.hasBreakpoint("main.nut", 42));
    CHECK(d.hasBreakpoint("scripts/main.nut", 42));
    REQUIRE(!d.hasBreakpoint("/game/other/main.nut", 42));
    CHECK(!d.hasBreakpoint("main.nut", 43));
    CHECK(d.clearBreakpoint("main.nut", 42));
    CHECK(!d.hasBreakpoint("main.nut", 42));
}

TEST_CASE("devtools.debugger.onScriptLineBreakpointAndStep") {
    Debugger& d = Debugger::instance();
    d.detach();
    d.clearBreakpoints();
    d.setBreakpoint("t.nut", 10);

    SourceLoc loc;
    loc.source = "t.nut";
    loc.line   = 9;
    CHECK(!d.onScriptLine(loc));

    loc.line = 10;
    CHECK(d.onScriptLine(loc));
    CHECK(d.isPaused());
    CHECK(static_cast<int>(d.lastPauseReason()) == static_cast<int>(PauseReason::Breakpoint));

    // No VM stack → stepInto/Over open as StepInto and stop on next line.
    d.stepInto();
    CHECK(static_cast<int>(d.mode()) == static_cast<int>(RunMode::StepInto));
    loc.line = 11;
    CHECK(d.onScriptLine(loc));
    CHECK(d.isPaused());
    CHECK(static_cast<int>(d.lastPauseReason()) == static_cast<int>(PauseReason::Step));

    // Smart step from a script stop → stepOver (depth 0 ⇒ StepInto).
    d.step();
    CHECK(static_cast<int>(d.mode()) == static_cast<int>(RunMode::StepInto));
    loc.line = 12;
    CHECK(d.onScriptLine(loc));
    CHECK(d.isPaused());
    d.resume();

    // Frame-level pause → smart step is stepFrame.
    d.pause(PauseReason::PauseKey);
    d.step();
    CHECK(static_cast<int>(d.mode()) == static_cast<int>(RunMode::StepFrame));
    d.notifyFrameDone();
    CHECK(d.isPaused());
    d.resume();
}

namespace {

struct HookState {
    Debugger* dbg = nullptr;
    int       stops = 0;
    int       lastLine = -1;
    std::string lastFunc;
};

[[maybe_unused]] void testDebugHook(HSQUIRRELVM v, SQInteger type, const SQChar* sourcename, SQInteger line,
                   const SQChar* funcname) {
    auto* st = static_cast<HookState*>(sq_getforeignptr(v));
    if (!st || !st->dbg) return;
    if (type != 'l') return;
    SourceLoc loc;
    loc.source   = sourcename ? sourcename : "";
    loc.line     = static_cast<int>(line);
    loc.function = funcname ? funcname : "";
    if (st->dbg->onScriptLine(loc)) {
        ++st->stops;
        st->lastLine = loc.line;
        st->lastFunc = loc.function;
        // Drive step from the hook without waitWhilePaused.
        // First stop: stepOver; second stop ends the test script.
        if (st->stops == 1) {
            st->dbg->stepOver();
        } else {
            st->dbg->resume();
        }
    }
}

}  // namespace


TEST_CASE("devtools.snapshot.markRootAndSkipEngine") {
    CHECK(Snapshot::isEngineBinding("eve"));
    CHECK(Snapshot::isEngineBinding("gfx"));
    CHECK(!Snapshot::isEngineBinding("gameState"));

    Snapshot& snap = Snapshot::instance();
    snap.clearRoots();
    snap.markRoot("custom");
    auto roots = snap.roots();
    REQUIRE(roots.size() == 1u);
    CHECK_EQ(roots[0], std::string("custom"));
    snap.unmarkRoot("custom");
    CHECK(snap.roots().empty());
}

namespace {

/** Fake provider: reports a fixed value and remembers what it restored. */
class MockStateProvider : public eve::caps::IStateProvider {
public:
    const char* stateKind() const override { return "mock"; }

    eve::Result<eve::Value> capture() override {
        eve::Value out(eve::Value::Object{});
        out.set("value", eve::Value::integer(7));
        return eve::Result<eve::Value>::success(std::move(out));
    }

    eve::Result<void> restore(const eve::Value& in) override {
        lastRestored = eve::toLegacyStateValue(in);
        return eve::Result<void>::success();
    }

    eve::Result<void> resetToDefaults() override { return eve::Result<void>::success(); }

    eve::StateValue lastRestored;
};

}  // namespace


TEST_CASE("devtools.debugger.normalizeSource") {
    CHECK_EQ(Debugger::normalizeSource("file://./a/b.nut"), std::string("a/b.nut"));
    CHECK_EQ(Debugger::normalizeSource("file://localhost/Users/x/main.nut"),
             std::string("/Users/x/main.nut"));
    CHECK_EQ(Debugger::normalizeSource("C:\\x\\y.nut"), std::string("C:/x/y.nut"));
    CHECK(Debugger::sourcesMatch("/abs/game/main.nut", "main.nut"));
    CHECK(Debugger::sourcesMatch("/abs/game/scripts/a.nut", "scripts/a.nut"));
    CHECK(!Debugger::sourcesMatch("foo.nut", "barfoo.nut"));
    REQUIRE(!Debugger::sourcesMatch("/proj/a/main.nut", "/proj/b/main.nut"));
    REQUIRE(!Debugger::sourcesMatch("scripts/main.nut", "other/main.nut"));
}

TEST_CASE("devtools.debugger.clearBreakpointsMatchesAlias") {
    Debugger& d = Debugger::instance();
    d.detach();
    d.clearBreakpoints();
    d.setBreakpoint("/abs/game/main.nut", 10);
    // VS Code may clear using the same absolute path, or a relative form.
    d.clearBreakpoints("main.nut");
    CHECK(!d.hasBreakpoint("/abs/game/main.nut", 10));
}

TEST_CASE("devtools.debugger.stepOverSkipsSameLine") {
    Debugger& d = Debugger::instance();
    d.detach();
    d.clearBreakpoints();

    SourceLoc loc;
    loc.source = "t.nut";
    loc.line   = 10;
    d.setBreakpoint("t.nut", 10);
    CHECK(d.onScriptLine(loc));
    CHECK(d.isPaused());

    d.stepOver();
    // Extra _OP_LINE on the same source line must not stop again.
    CHECK(!d.onScriptLine(loc));
    CHECK(!d.isPaused());
    {
        const int m = static_cast<int>(d.mode());
        const bool stepping = m == static_cast<int>(RunMode::StepInto) ||
                              m == static_cast<int>(RunMode::StepOver);
        CHECK(stepping);
    }

    loc.line = 11;
    CHECK(d.onScriptLine(loc));
    CHECK(d.isPaused());
    CHECK(static_cast<int>(d.lastPauseReason()) == static_cast<int>(PauseReason::Step));
    d.resume();
}


TEST_CASE("devtools.debugger.breakOnErrorFlag") {
    Debugger& d = Debugger::instance();
    d.detach();
    CHECK(!d.breakOnError());
    d.setBreakOnError(true);
    CHECK(d.breakOnError());
    d.setBreakOnError(false);
    CHECK(!d.breakOnError());
}

TEST_CASE("devtools.debugger.breakpointsEnabledMasterSwitch") {
    Debugger& d = Debugger::instance();
    d.detach();
    d.clearBreakpoints();
    d.setBreakpoint("t.nut", 3);
    d.setBreakpointsEnabled(false);
    CHECK(d.hasBreakpoint("t.nut", 3));  // registered even while skip-all is on

    SourceLoc loc;
    loc.source = "t.nut";
    loc.line   = 3;
    CHECK(!d.onScriptLine(loc));  // skipped while master switch is off
    CHECK(!d.isPaused());

    d.setBreakpointsEnabled(true);
    CHECK(d.onScriptLine(loc));
    CHECK(d.isPaused());
    d.resume();
}

TEST_CASE("devtools.debugger.pauseAtRecordsLocation") {
    Debugger& d = Debugger::instance();
    d.detach();
    d.resume();

    SourceLoc loc;
    loc.source   = "err.nut";
    loc.line     = 12;
    loc.function = "boom";
    d.pauseAt(PauseReason::Exception, loc);
    CHECK(d.isPaused());
    CHECK_EQ(d.pauseLocation().source, std::string("err.nut"));
    CHECK_EQ(d.pauseLocation().line, 12);
    CHECK_EQ(d.pauseLocation().function, std::string("boom"));
    CHECK(static_cast<int>(d.lastPauseReason()) == static_cast<int>(PauseReason::Exception));

    d.pauseAt(PauseReason::Snapshot, d.pauseLocation());
    CHECK(d.isPaused());
    CHECK(static_cast<int>(d.lastPauseReason()) == static_cast<int>(PauseReason::Snapshot));
    CHECK_EQ(d.pauseLocation().line, 12);

    d.pause(PauseReason::PauseKey);
    CHECK(d.pauseLocation().empty());
    CHECK(static_cast<int>(d.lastPauseReason()) == static_cast<int>(PauseReason::PauseKey));
    d.resume();
}

TEST_CASE("devtools.debugger.breakpointConditionReplaceAndEnableById") {
    Debugger& d = Debugger::instance();
    d.detach();
    d.clearBreakpoints();
    d.setBreakpointsEnabled(true);

    const int id = d.setBreakpoint("t.nut", 10, true, "flag");
    CHECK(id > 0);
    auto bps = d.breakpoints();
    REQUIRE(!bps.empty());
    CHECK_EQ(bps[0].condition, std::string("flag"));

    CHECK_EQ(d.setBreakpoint("t.nut", 10, true, ""), id);
    bps = d.breakpoints();
    CHECK(bps[0].condition.empty());

    CHECK(d.setBreakpointEnabled(id, false));
    CHECK(!d.setBreakpointEnabled(99999, false));
    CHECK(d.hasBreakpoint("t.nut", 10));

    SourceLoc loc;
    loc.source = "t.nut";
    loc.line   = 10;
    CHECK(!d.onScriptLine(loc));
    CHECK(!d.isPaused());

    CHECK(d.setBreakpointEnabled(id, true));
    CHECK(d.onScriptLine(loc));
    CHECK(d.isPaused());
    d.resume();
    d.clearBreakpoints();
}
