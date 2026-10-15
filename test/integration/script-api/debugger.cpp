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

TEST_CASE("devtools.snapshot.v2NativeSection") {
    ssq::VM     vm(1024, ssq::Libs::ALL);
    HSQUIRRELVM v = vm.getHandle();

    {
        const SQInteger top = sq_gettop(v);
        sq_pushroottable(v);
        sq_pushstring(v, "gameState", -1);
        sq_newtable(v);
        sq_pushstring(v, "level", -1);
        sq_pushinteger(v, 3);
        sq_newslot(v, -3, SQFalse);
        sq_newslot(v, -3, SQFalse);
        sq_settop(v, top);
    }

    MockStateProvider provider;
    eve::cap::addListener<eve::caps::IStateProvider>(&provider);

    Snapshot& snap = Snapshot::instance();
    snap.clearRoots();
    std::string       err;
    const std::string json = snap.capture(v, &err);
    CHECK(err.empty());
    CHECK(json.find("version") != std::string::npos);
    CHECK(json.find("native") != std::string::npos);
    CHECK(json.find("mock") != std::string::npos);
    CHECK(json.find("gameState") != std::string::npos);

    // Mutate script state, then restore; both the script root and the native
    // provider state come back.
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

    CHECK(provider.lastRestored.isObject());
    const eve::StateValue* value = provider.lastRestored.find("value");
    REQUIRE(value != nullptr);
    CHECK_EQ(value->asInt(), int64_t(7));

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

    eve::cap::removeListener<eve::caps::IStateProvider>(&provider);
}

TEST_CASE("devtools.snapshot.v1Compat") {
    ssq::VM           vm(1024, ssq::Libs::ALL);
    HSQUIRRELVM       v  = vm.getHandle();
    const std::string v1 = R"({"version":1,"roots":{"gameState":{"level":3}}})";

    Snapshot& snap = Snapshot::instance();
    snap.clearRoots();
    std::string err;
    CHECK(snap.restore(v, v1, &err));
    CHECK(err.empty());

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

TEST_CASE("devtools.snapshot.rejectsFunctionInRoot") {
    ssq::VM     vm(1024, ssq::Libs::ALL);
    HSQUIRRELVM v = vm.getHandle();
    ssq::Script s = vm.compileSource("gameState <- { level = 3 }; gameState.tick <- function() { return 1; };");
    vm.run(s);

    Snapshot& snap = Snapshot::instance();
    snap.clearRoots();
    eve::StateValue out;
    std::string     err;
    CHECK(!snap.captureState(v, out, &err));
    CHECK(err.find("non-serializable") != std::string::npos);
    CHECK(err.find("gameState") != std::string::npos);
}

TEST_CASE("devtools.snapshot.rejectsInstanceRoot") {
    ssq::VM     vm(1024, ssq::Libs::ALL);
    HSQUIRRELVM v = vm.getHandle();
    ssq::Script s = vm.compileSource("class Hero { level = 1; } gameState <- Hero();");
    vm.run(s);

    Snapshot& snap = Snapshot::instance();
    snap.clearRoots();
    eve::StateValue out;
    std::string     err;
    CHECK(!snap.captureState(v, out, &err));
    CHECK(err.find("class instance") != std::string::npos);
}

TEST_CASE("devtools.snapshot.skipsInstanceRootInAutoScan") {
    ssq::VM     vm(1024, ssq::Libs::ALL);
    HSQUIRRELVM v = vm.getHandle();
    ssq::Script s =
        vm.compileSource("class Hero { level = 1; } hero <- Hero(); gameState <- { level = 3 };");
    vm.run(s);

    Snapshot& snap = Snapshot::instance();
    snap.clearRoots();
    eve::StateValue out;
    std::string     err;
    // Class instances at the root (module singletons, ECS worlds, ...) must not
    // abort the automatic state scan; plain-data roots are still captured.
    CHECK(snap.captureState(v, out, &err));
    CHECK(err.empty());
    const eve::StateValue* roots = out.find("roots");
    REQUIRE(roots != nullptr);
    CHECK(roots->find("gameState") != nullptr);
    CHECK(roots->find("hero") == nullptr);
}


TEST_CASE("devtools.debugger.conditionalBreakpoint") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    HSQUIRRELVM v = vm.getHandle();

    Debugger& d = Debugger::instance();
    d.detach();
    d.clearBreakpoints();
    d.attach(v);

    // Seed roottable flag used by the condition.
    {
        const SQInteger top = sq_gettop(v);
        sq_pushroottable(v);
        sq_pushstring(v, "flag", -1);
        sq_pushinteger(v, 1);
        sq_newslot(v, -3, SQFalse);
        sq_settop(v, top);
    }

    d.setBreakpoint("t.nut", 10, true, "flag == 2");
    SourceLoc loc;
    loc.source = "t.nut";
    loc.line   = 10;

    // flag == 1: condition false -> keep running.
    CHECK(!d.onScriptLine(loc));
    CHECK(!d.isPaused());

    // flag == 2: condition true -> stop.
    {
        const SQInteger top = sq_gettop(v);
        sq_pushroottable(v);
        sq_pushstring(v, "flag", -1);
        sq_pushinteger(v, 2);
        sq_newslot(v, -3, SQFalse);
        sq_settop(v, top);
    }
    CHECK(d.onScriptLine(loc));
    CHECK(d.isPaused());
    CHECK(static_cast<int>(d.lastPauseReason()) == static_cast<int>(PauseReason::Breakpoint));

    d.resume();
    d.detach();
}

TEST_CASE("devtools.debugger.expressionEvaluateAndVariableTree") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    HSQUIRRELVM v = vm.getHandle();

    Debugger& d = Debugger::instance();
    d.detach();
    d.attach(v);
    {
        const SQInteger top = sq_gettop(v);
        sq_pushroottable(v);

        sq_pushstring(v, "score", -1);
        sq_pushinteger(v, 7);
        sq_newslot(v, -3, SQFalse);

        sq_pushstring(v, "cfg", -1);
        sq_newtable(v);
        sq_pushstring(v, "max", -1);
        sq_pushinteger(v, 3);
        sq_newslot(v, -3, SQFalse);
        sq_newslot(v, -3, SQFalse);

        sq_pushstring(v, "list", -1);
        sq_newarray(v, 0);
        sq_pushinteger(v, 10);
        sq_arrayappend(v, -2);
        sq_pushinteger(v, 20);
        sq_arrayappend(v, -2);
        sq_pushinteger(v, 30);
        sq_arrayappend(v, -2);
        sq_newslot(v, -3, SQFalse);

        sq_settop(v, top);
    }

    // Full expression evaluation (arithmetic + member access + calls).
    auto r1 = d.evaluate("score * 2");
    CHECK_EQ(r1.type, std::string("integer"));
    CHECK(r1.value.find("14") != std::string::npos);

    auto r2 = d.evaluate("cfg.max + 1");
    CHECK_EQ(r2.type, std::string("integer"));
    CHECK(r2.value.find("4") != std::string::npos);

    auto r3 = d.evaluate("list.len()");
    CHECK_EQ(r3.type, std::string("integer"));
    CHECK(r3.value.find("3") != std::string::npos);

    // Globals tree: containers are expandable.
    auto g = d.globals();
    bool foundCfg = false;
    for (const auto& vv : g) {
        if (vv.name == "cfg") {
            foundCfg = vv.expandable;
        }
    }
    CHECK(foundCfg);

    auto cfgKids = d.containerChildren(VarKind::Globals, 0, {"cfg"});
    REQUIRE(cfgKids.size() >= 1u);
    bool foundMax = false;
    for (const auto& vv : cfgKids) {
        if (vv.name == "max" && vv.value.find("3") != std::string::npos) foundMax = true;
    }
    CHECK(foundMax);

    auto listKids = d.containerChildren(VarKind::Globals, 0, {"list"});
    REQUIRE(listKids.size() == 3u);
    CHECK(listKids[0].value.find("10") != std::string::npos);
    CHECK(listKids[2].value.find("30") != std::string::npos);

    d.detach();
}

TEST_CASE("devtools.debugger.evaluateLocalsInFrame") {
    struct St {
        Debugger* dbg = nullptr;
        bool      captured = false;
        std::string type;
        std::string value;
    };
    auto hook = [](HSQUIRRELVM v, SQInteger type, const SQChar* sourcename, SQInteger line,
                   const SQChar* funcname) {
        auto* st = static_cast<St*>(sq_getforeignptr(v));
        if (!st || !st->dbg || type != 'l') return;
        SourceLoc loc;
        loc.source   = sourcename ? sourcename : "";
        loc.line     = static_cast<int>(line);
        loc.function = funcname ? funcname : "";
        if (st->dbg->onScriptLine(loc)) {
            auto r = st->dbg->evaluate("x + y", 0);
            st->type     = r.type;
            st->value    = r.value;
            st->captured = true;
            st->dbg->resume();
        }
    };

    const char* src =
        "function f() {\n"     // line 1
        "    local x = 5\n"    // line 2
        "    local y = 10\n"   // line 3
        "    local z = x + y\n"  // line 4 <-- breakpoint
        "}\n"                  // line 5
        "f()\n";               // line 6

    ssq::VM vm(1024, ssq::Libs::ALL);
    HSQUIRRELVM v = vm.getHandle();

    Debugger& d = Debugger::instance();
    d.detach();
    d.clearBreakpoints();
    d.attach(v);
    d.setBreakpoint("buffer", 4);

    St st;
    st.dbg = &d;
    sq_setforeignptr(v, &st);
    sq_enabledebuginfo(v, SQTrue);
    sq_setnativedebughook(v, +hook);

    REQUIRE(SQ_SUCCEEDED(
        sq_compilebuffer(v, src, static_cast<SQInteger>(std::strlen(src)), _SC("buffer"), SQTrue)));
    sq_pushroottable(v);
    REQUIRE(SQ_SUCCEEDED(sq_call(v, 1, SQFalse, SQTrue)));
    sq_poptop(v);

    CHECK(st.captured);
    CHECK_EQ(st.type, std::string("integer"));
    CHECK(st.value.find("15") != std::string::npos);

    sq_setnativedebughook(v, nullptr);
    sq_setforeignptr(v, nullptr);
    d.detach();
}


TEST_CASE("devtools.debugger.conditionalBreakpointUsesLocals") {
    struct St {
        Debugger* dbg = nullptr;
        int       stops = 0;
    };
    auto hook = [](HSQUIRRELVM v, SQInteger type, const SQChar* sourcename, SQInteger line,
                   const SQChar* funcname) {
        auto* st = static_cast<St*>(sq_getforeignptr(v));
        if (!st || !st->dbg || type != 'l') return;
        SourceLoc loc;
        loc.source   = sourcename ? sourcename : "";
        loc.line     = static_cast<int>(line);
        loc.function = funcname ? funcname : "";
        if (st->dbg->onScriptLine(loc)) {
            ++st->stops;
            st->dbg->resume();
        }
    };

    const char* body =
        "function f() {\n"     // 1
        "    local n = 1\n"    // 2
        "    n = 2\n"          // 3  condition n == 2 should stop here
        "    n = 3\n"          // 4
        "}\n"
        "f()\n";

    ssq::VM vm(1024, ssq::Libs::ALL);
    HSQUIRRELVM v = vm.getHandle();
    Debugger& d = Debugger::instance();
    d.detach();
    d.clearBreakpoints();
    d.attach(v);
    d.setBreakpoint("buffer", 3, true, "n == 2");
    d.setBreakpoint("buffer", 2, true, "n == 2");  // n is 1 here → skip

    St st;
    st.dbg = &d;
    sq_setforeignptr(v, &st);
    sq_enabledebuginfo(v, SQTrue);
    sq_setnativedebughook(v, +hook);

    REQUIRE(SQ_SUCCEEDED(sq_compilebuffer(v, body, static_cast<SQInteger>(std::strlen(body)),
                                          _SC("buffer"), SQTrue)));
    sq_pushroottable(v);
    REQUIRE(SQ_SUCCEEDED(sq_call(v, 1, SQFalse, SQTrue)));
    sq_poptop(v);

    CHECK_EQ(st.stops, 1);

    sq_setnativedebughook(v, nullptr);
    sq_setforeignptr(v, nullptr);
    d.clearBreakpoints();
    d.detach();
}

TEST_CASE("devtools.debugger.conditionalBreakpointStopsOnEvalError") {
    struct St {
        Debugger* dbg = nullptr;
        int       stops = 0;
    };
    auto hook = [](HSQUIRRELVM v, SQInteger type, const SQChar* sourcename, SQInteger line,
                   const SQChar* funcname) {
        auto* st = static_cast<St*>(sq_getforeignptr(v));
        if (!st || !st->dbg || type != 'l') return;
        SourceLoc loc;
        loc.source   = sourcename ? sourcename : "";
        loc.line     = static_cast<int>(line);
        loc.function = funcname ? funcname : "";
        if (st->dbg->onScriptLine(loc)) {
            ++st->stops;
            st->dbg->resume();
        }
    };

    const char* body =
        "function f() {\n"
        "    local n = 1\n"
        "}\n"
        "f()\n";

    ssq::VM vm(1024, ssq::Libs::ALL);
    HSQUIRRELVM v = vm.getHandle();
    Debugger& d = Debugger::instance();
    d.detach();
    d.clearBreakpoints();
    d.attach(v);
    d.setBreakpoint("buffer", 2, true, "no_such_local + 1");

    St st;
    st.dbg = &d;
    sq_setforeignptr(v, &st);
    sq_enabledebuginfo(v, SQTrue);
    sq_setnativedebughook(v, +hook);

    REQUIRE(SQ_SUCCEEDED(sq_compilebuffer(v, body, static_cast<SQInteger>(std::strlen(body)),
                                          _SC("buffer"), SQTrue)));
    sq_pushroottable(v);
    REQUIRE(SQ_SUCCEEDED(sq_call(v, 1, SQFalse, SQTrue)));
    sq_poptop(v);

    CHECK_EQ(st.stops, 1);

    sq_setnativedebughook(v, nullptr);
    sq_setforeignptr(v, nullptr);
    d.clearBreakpoints();
    d.detach();
}

TEST_CASE("devtools.debugger.scriptApiSetBreakpointCondition") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    auto& dt = DevTool::instance();
    dt.detach();
    Debugger::instance().clearBreakpoints();
    dt.attach(vm, false);
    dt.exposeScriptApi(vm);

    ssq::Script script = vm.compileSource(
        "function check() {\n"
        "    local a = eve.dev.setBreakpoint(\"api.nut\", 10)\n"
        "    local b = eve.dev.setBreakpoint(\"api.nut\", 11, \"n == 2\")\n"
        "    return a > 0 && b > 0\n"
        "}\n");
    vm.run(script);
    REQUIRE(vm.callFunc(vm.findFunc("check"), vm).toBool());

    bool sawPlain = false;
    bool sawCond  = false;
    for (const auto& bp : Debugger::instance().breakpoints()) {
        if (bp.line == 10) {
            sawPlain = true;
            CHECK(bp.condition.empty());
        }
        if (bp.line == 11) {
            sawCond = true;
            REQUIRE_EQ(bp.condition, std::string("n == 2"));
        }
    }
    CHECK(sawPlain);
    REQUIRE(sawCond);

    Debugger::instance().clearBreakpoints();
    dt.detach();
}
