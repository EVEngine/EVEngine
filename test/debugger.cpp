#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "devtools/Debugger.hpp"
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

void testDebugHook(HSQUIRRELVM v, SQInteger type, const SQChar* sourcename, SQInteger line,
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

TEST_CASE("devtools.debugger.stepOverSkipsCalleeLines") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    HSQUIRRELVM v = vm.getHandle();

    Debugger& d = Debugger::instance();
    d.detach();
    d.clearBreakpoints();
    d.attach(v);

    // Lines roughly:
    // 1 function callee() { local x = 1 }
    // 2 function caller() { callee(); local y = 2 }
    // 3 caller()
    const char* src =
        "function callee() {\n"
        "    local x = 1\n"
        "}\n"
        "function caller() {\n"
        "    callee()\n"
        "    local y = 2\n"
        "}\n"
        "caller()\n";

    HookState st;
    st.dbg = &d;
    sq_setforeignptr(v, &st);
    sq_enabledebuginfo(v, SQTrue);
    sq_setnativedebughook(v, testDebugHook);

    d.setBreakpoint("buffer", 5);  // callee() call site inside caller
    // Normalize: compile as named buffer
    SQRESULT rc = sq_compilebuffer(v, src, static_cast<SQInteger>(std::strlen(src)), _SC("buffer"),
                                   SQTrue);
    REQUIRE(SQ_SUCCEEDED(rc));
    sq_pushroottable(v);
    REQUIRE(SQ_SUCCEEDED(sq_call(v, 1, SQFalse, SQTrue)));
    sq_poptop(v);

    // Hit bp on call line, stepOver, then stop on `local y = 2` (not inside callee).
    CHECK(st.stops >= 2);
    CHECK_EQ(st.lastLine, 6);
    CHECK(st.lastFunc.find("caller") != std::string::npos);

    sq_setnativedebughook(v, nullptr);
    sq_setforeignptr(v, nullptr);
    d.detach();
}

TEST_CASE("devtools.debugger.stepIntoEntersCallee") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    HSQUIRRELVM v = vm.getHandle();

    Debugger& d = Debugger::instance();
    d.detach();
    d.clearBreakpoints();
    d.attach(v);

    const char* src =
        "function callee() {\n"
        "    local x = 1\n"
        "}\n"
        "function caller() {\n"
        "    callee()\n"
        "    local y = 2\n"
        "}\n"
        "caller()\n";

    struct IntoState {
        Debugger* dbg = nullptr;
        int       stops = 0;
        int       lastLine = -1;
        std::string lastFunc;
    } st;
    st.dbg = &d;

    auto hook = [](HSQUIRRELVM vmh, SQInteger type, const SQChar* sourcename, SQInteger line,
                   const SQChar* funcname) {
        auto* s = static_cast<IntoState*>(sq_getforeignptr(vmh));
        if (!s || !s->dbg || type != 'l') return;
        SourceLoc loc;
        loc.source   = sourcename ? sourcename : "";
        loc.line     = static_cast<int>(line);
        loc.function = funcname ? funcname : "";
        if (s->dbg->onScriptLine(loc)) {
            ++s->stops;
            s->lastLine = loc.line;
            s->lastFunc = loc.function;
            if (s->stops == 1)
                s->dbg->stepInto();
            else
                s->dbg->resume();
        }
    };

    sq_setforeignptr(v, &st);
    sq_enabledebuginfo(v, SQTrue);
    sq_setnativedebughook(v, +hook);
    d.setBreakpoint("buffer", 5);

    REQUIRE(SQ_SUCCEEDED(
        sq_compilebuffer(v, src, static_cast<SQInteger>(std::strlen(src)), _SC("buffer"), SQTrue)));
    sq_pushroottable(v);
    REQUIRE(SQ_SUCCEEDED(sq_call(v, 1, SQFalse, SQTrue)));
    sq_poptop(v);

    CHECK(st.stops >= 2);
    CHECK_EQ(st.lastLine, 2);  // first line body of callee
    CHECK(st.lastFunc.find("callee") != std::string::npos);

    sq_setnativedebughook(v, nullptr);
    sq_setforeignptr(v, nullptr);
    d.detach();
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
    CHECK(err.empty());
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
    err.clear();
    CHECK(snap.restore(v, json, &err));
    CHECK(err.empty());

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
    CHECK_EQ(Debugger::normalizeSource("file://localhost/Users/x/main.nut"),
             std::string("/Users/x/main.nut"));
    CHECK_EQ(Debugger::normalizeSource("C:\\x\\y.nut"), std::string("C:/x/y.nut"));
    CHECK(Debugger::sourcesMatch("/abs/game/main.nut", "main.nut"));
    CHECK(Debugger::sourcesMatch("/abs/game/scripts/a.nut", "scripts/a.nut"));
    CHECK(!Debugger::sourcesMatch("foo.nut", "barfoo.nut"));
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
