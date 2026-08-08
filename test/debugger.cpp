#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "devtools/Debugger.hpp"
#include "devtools/Snapshot.hpp"

#include <simplesquirrel/simplesquirrel.hpp>
#include <squirrel.h>

#include <cstdio>
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
    CHECK(d.lastPauseReason() == PauseReason::PauseKey);

    d.stepFrame();
    CHECK(d.shouldRunUpdate());
    d.notifyFrameDone();
    CHECK(d.isPaused());
    CHECK(d.lastPauseReason() == PauseReason::Step);

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
    CHECK(d.hasBreakpoint("/abs/path/main.nut", 42));
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
    CHECK(d.lastPauseReason() == PauseReason::Breakpoint);

    d.stepLine();
    CHECK(d.mode() == RunMode::StepLine);
    loc.line = 11;
    CHECK(d.onScriptLine(loc));
    CHECK(d.isPaused());
    CHECK(d.lastPauseReason() == PauseReason::Step);
    d.resume();
}

TEST_CASE("devtools.debugger.watchesAndEvaluate") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    HSQUIRRELVM v = vm.getHandle();

    // Seed roottable slots.
    {
        const SQInteger top = sq_gettop(v);
        sq_pushroottable(v);
        sq_pushstring(v, "score", -1);
        sq_pushinteger(v, 7);
        sq_newslot(v, -3, SQFalse);
        sq_pushstring(v, "player", -1);
        sq_newtable(v);
        sq_pushstring(v, "hp", -1);
        sq_pushinteger(v, 100);
        sq_newslot(v, -3, SQFalse);
        sq_newslot(v, -3, SQFalse);
        sq_settop(v, top);
    }

    Debugger& d = Debugger::instance();
    d.attach(v);
    d.clearWatches();
    d.addWatch("score");
    d.addWatch("player.hp");
    d.refreshWatches();
    auto watches = d.watches();
    REQUIRE(watches.size() == 2u);
    CHECK(watches[0].ok);
    CHECK(watches[0].value.find("7") != std::string::npos);
    CHECK(watches[1].ok);
    CHECK(watches[1].value.find("100") != std::string::npos);

    auto ev = d.evaluate("score");
    CHECK_EQ(ev.type, std::string("integer"));
    CHECK(ev.value.find("7") != std::string::npos);

    CHECK(d.removeWatch("score"));
    d.clearWatches();
    CHECK(d.watches().empty());
    d.detach();
}

TEST_CASE("devtools.snapshot.roundTripRoots") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    HSQUIRRELVM v = vm.getHandle();

    {
        const SQInteger top = sq_gettop(v);
        sq_pushroottable(v);
        sq_pushstring(v, "gameState", -1);
        sq_newtable(v);
        sq_pushstring(v, "level", -1);
        sq_pushinteger(v, 3);
        sq_newslot(v, -3, SQFalse);
        sq_pushstring(v, "name", -1);
        sq_pushstring(v, "hero", -1);
        sq_newslot(v, -3, SQFalse);
        sq_newslot(v, -3, SQFalse);
        sq_settop(v, top);
    }

    Snapshot& snap = Snapshot::instance();
    snap.clearRoots();
    // Prefer conventional gameState without markRoot.
    std::string err;
    const std::string json = snap.capture(v, &err);
    CHECK(!json.empty());
    CHECK(json.find("gameState") != std::string::npos);
    CHECK(json.find("hero") != std::string::npos);

    // Mutate then restore.
    {
        const SQInteger top = sq_gettop(v);
        sq_pushroottable(v);
        sq_pushstring(v, "gameState", -1);
        sq_newtable(v);
        sq_pushstring(v, "level", -1);
        sq_pushinteger(v, 99);
        sq_newslot(v, -3, SQFalse);
        sq_newslot(v, -3, SQFalse);
        sq_settop(v, top);
    }
    CHECK(snap.restore(v, json, &err));

    {
        const SQInteger top = sq_gettop(v);
        sq_pushroottable(v);
        sq_pushstring(v, "gameState", -1);
        REQUIRE(SQ_SUCCEEDED(sq_get(v, -2)));
        sq_pushstring(v, "level", -1);
        REQUIRE(SQ_SUCCEEDED(sq_get(v, -2)));
        SQInteger level = 0;
        sq_getinteger(v, -1, &level);
        CHECK_EQ(static_cast<int>(level), 3);
        sq_settop(v, top);
    }

    const char* path = "eve_test_snapshot.json";
    CHECK(snap.saveFile(v, path, &err));
    CHECK(snap.loadFile(v, path, &err));
    std::remove(path);
}

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

TEST_CASE("devtools.debugger.normalizeSource") {
    CHECK_EQ(Debugger::normalizeSource("file://./a/b.nut"), std::string("a/b.nut"));
    CHECK_EQ(Debugger::normalizeSource("C:\\x\\y.nut"), std::string("C:/x/y.nut"));
}
