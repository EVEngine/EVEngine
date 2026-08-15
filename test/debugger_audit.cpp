#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "devtools/Debugger.hpp"

#include <simplesquirrel/simplesquirrel.hpp>
#include <squirrel.h>

#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace eve::dev;

namespace {

struct AuditDriver {
    Debugger* dbg = nullptr;
    int       stops = 0;
    std::vector<int>         stopLines;
    std::vector<std::string> stopFuncs;
    std::vector<int>         stopDepths;
    std::vector<std::function<void()>> actions;
    std::function<void()> capture;  // optional: capture extra state on each stop
};

void auditHook(HSQUIRRELVM v, SQInteger type, const SQChar* sourcename, SQInteger line,
               const SQChar* funcname) {
    auto* st = static_cast<AuditDriver*>(sq_getforeignptr(v));
    if (!st || !st->dbg) return;
    if (type != 'l') return;
    SourceLoc loc;
    loc.source   = sourcename ? sourcename : "";
    loc.line     = static_cast<int>(line);
    loc.function = funcname ? funcname : "";
    if (st->dbg->onScriptLine(loc)) {
        const int idx = st->stops;
        st->stopLines.push_back(loc.line);
        st->stopFuncs.push_back(loc.function);
        st->stopDepths.push_back(st->dbg->scriptStackDepth());
        if (st->capture) st->capture();
        if (idx < static_cast<int>(st->actions.size())) {
            st->actions[idx]();
        } else {
            st->dbg->resume();
        }
        ++st->stops;
    }
}

/** Compile+run a buffer through the Debugger, driving it with `driver`. */
void runAudit(const char* src, const char* bufname, AuditDriver& driver) {
    ssq::VM vm(1024, ssq::Libs::ALL);
    HSQUIRRELVM v = vm.getHandle();

    Debugger& d = Debugger::instance();
    d.detach();
    driver.dbg = &d;
    d.attach(v);

    sq_setforeignptr(v, &driver);
    sq_enabledebuginfo(v, SQTrue);
    sq_setnativedebughook(v, auditHook);

    SQRESULT rc = sq_compilebuffer(v, src, static_cast<SQInteger>(std::strlen(src)), _SC(bufname),
                                   SQTrue);
    REQUIRE(SQ_SUCCEEDED(rc));
    sq_pushroottable(v);
    REQUIRE(SQ_SUCCEEDED(sq_call(v, 1, SQFalse, SQTrue)));
    sq_poptop(v);

    sq_setnativedebughook(v, nullptr);
    sq_setforeignptr(v, nullptr);
    d.detach();
    driver.dbg = nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------
// Call stack retrieval at a breakpoint (nested calls)
// ---------------------------------------------------------------------------

TEST_CASE("devtools.audit.callStackAtNestedBreakpoint") {
    const char* src =
        "function inner() {\n"
        "    local a = 1\n"
        "}\n"
        "function outer() {\n"
        "    inner()\n"
        "}\n"
        "outer()\n";

    AuditDriver driver;
    driver.actions = {[]() { Debugger::instance().resume(); }};

    std::vector<StackFrameInfo> captured;
    driver.capture = [&]() { captured = Debugger::instance().stackTrace(16); };

    Debugger& d = Debugger::instance();
    d.clearBreakpoints();
    d.setBreakpoint("audit1.nut", 2);
    runAudit(src, "audit1.nut", driver);

    REQUIRE(driver.stops >= 1);
    REQUIRE(captured.size() >= 3u);
    CHECK_EQ(captured[0].loc.line, 2);
    CHECK(captured[0].name.find("inner") != std::string::npos);
    CHECK_EQ(captured[0].loc.source, std::string("audit1.nut"));
    CHECK(captured[1].name.find("outer") != std::string::npos);
    CHECK_EQ(captured[1].loc.line, 5);
    // Top-level script frame present.
    CHECK(!captured[2].loc.source.empty());
}

// ---------------------------------------------------------------------------
// stepOut: finish current function, stop in the caller after the call
// ---------------------------------------------------------------------------

TEST_CASE("devtools.audit.stepOutReturnsToCaller") {
    const char* src =
        "function callee() {\n"
        "    local a = 1\n"
        "    local b = 2\n"
        "}\n"
        "function caller() {\n"
        "    callee()\n"
        "    local c = 3\n"
        "}\n"
        "caller()\n";

    AuditDriver driver;
    // Stop 1 (callee body) -> stepOut. Stop 2 (caller after call) -> resume.
    driver.actions = {
        []() { Debugger::instance().stepOut(); },
        []() { Debugger::instance().resume(); },
    };

    Debugger& d = Debugger::instance();
    d.clearBreakpoints();
    d.setBreakpoint("audit2.nut", 2);
    runAudit(src, "audit2.nut", driver);

    REQUIRE(driver.stops >= 2);
    CHECK_EQ(driver.stopLines[0], 2);
    CHECK(driver.stopFuncs[0].find("callee") != std::string::npos);
    // After stepOut we should be back in the caller, past the call site.
    CHECK(driver.stopFuncs[1].find("caller") != std::string::npos);
    CHECK_EQ(driver.stopLines[1], 7);
}

// ---------------------------------------------------------------------------
// stepInto: enter the callee and grow the call stack
// ---------------------------------------------------------------------------

TEST_CASE("devtools.audit.stepIntoEntersAndGrowsStack") {
    const char* src =
        "function callee() {\n"
        "    local a = 1\n"
        "}\n"
        "function caller() {\n"
        "    callee()\n"
        "    local b = 2\n"
        "}\n"
        "caller()\n";

    AuditDriver driver;
    driver.actions = {
        []() { Debugger::instance().stepInto(); },
        []() { Debugger::instance().resume(); },
    };

    Debugger& d = Debugger::instance();
    d.clearBreakpoints();
    d.setBreakpoint("audit3.nut", 5);  // callee() call site in caller
    runAudit(src, "audit3.nut", driver);

    REQUIRE(driver.stops >= 2);
    CHECK_EQ(driver.stopLines[0], 5);
    CHECK(driver.stopFuncs[1].find("callee") != std::string::npos);
    CHECK(driver.stopDepths[1] > driver.stopDepths[0]);
}

// ---------------------------------------------------------------------------
// stepOver: skip the callee, stop on the next statement in the caller
// ---------------------------------------------------------------------------

TEST_CASE("devtools.audit.stepOverSkipsCallee") {
    const char* src =
        "function callee() {\n"
        "    local a = 1\n"
        "}\n"
        "function caller() {\n"
        "    callee()\n"
        "    local b = 2\n"
        "}\n"
        "caller()\n";

    AuditDriver driver;
    driver.actions = {
        []() { Debugger::instance().stepOver(); },
        []() { Debugger::instance().resume(); },
    };

    Debugger& d = Debugger::instance();
    d.clearBreakpoints();
    d.setBreakpoint("audit4.nut", 5);  // callee() call site in caller
    runAudit(src, "audit4.nut", driver);

    REQUIRE(driver.stops >= 2);
    CHECK_EQ(driver.stopLines[1], 6);  // local b = 2 in caller, not inside callee
    CHECK(driver.stopFuncs[1].find("caller") != std::string::npos);
    CHECK_EQ(driver.stopDepths[1], driver.stopDepths[0]);
}

// ---------------------------------------------------------------------------
// Breakpoints keep hitting on every loop iteration after continue
// ---------------------------------------------------------------------------

TEST_CASE("devtools.audit.breakpointRehitsInLoop") {
    const char* src =
        "local total = 0\n"
        "for (local i = 0; i < 3; ++i) {\n"
        "    total += i\n"
        "}\n"
        "print(total)\n";

    AuditDriver driver;
    // Continue (resume) after every stop; the breakpoint must hit each iteration.
    driver.actions = {
        []() { Debugger::instance().resume(); },
        []() { Debugger::instance().resume(); },
        []() { Debugger::instance().resume(); },
    };

    Debugger& d = Debugger::instance();
    d.clearBreakpoints();
    d.setBreakpoint("audit5.nut", 3);
    runAudit(src, "audit5.nut", driver);

    // i = 0,1,2 �?exactly three stops at the loop body line.
    CHECK_EQ(driver.stops, 3);
    for (int l : driver.stopLines) CHECK_EQ(l, 3);
}

// ---------------------------------------------------------------------------
// Locals of the current frame are readable at a breakpoint
// ---------------------------------------------------------------------------

TEST_CASE("devtools.audit.localsAtBreakpoint") {
    const char* src =
        "function compute(x, y) {\n"
        "    local z = x + y\n"
        "    local name = \"audit\"\n"
        "    return z\n"
        "}\n"
        "compute(3, 4)\n";

    AuditDriver driver;
    driver.actions = {[]() { Debugger::instance().resume(); }};

    std::vector<VariableInfo> locals;
    driver.capture = [&]() { locals = Debugger::instance().locals(0); };

    Debugger& d = Debugger::instance();
    d.clearBreakpoints();
    // Pause on `return z`: all four locals (x, y, z, name) are assigned by then.
    d.setBreakpoint("audit6.nut", 4);
    runAudit(src, "audit6.nut", driver);

    REQUIRE(driver.stops >= 1);
    bool sawX = false, sawY = false, sawZ = false, sawName = false;
    for (const auto& lv : locals) {
        if (lv.name == "x") {
            sawX = true;
            CHECK(lv.value.find("3") != std::string::npos);
        } else if (lv.name == "y") {
            sawY = true;
            CHECK(lv.value.find("4") != std::string::npos);
        } else if (lv.name == "z") {
            sawZ = true;
            CHECK(lv.value.find("7") != std::string::npos);
        } else if (lv.name == "name") {
            sawName = true;
            CHECK(lv.value.find("audit") != std::string::npos);
        }
    }
    CHECK(sawX);
    CHECK(sawY);
    CHECK(sawZ);
    CHECK(sawName);

    // evaluate() must read the CURRENT frame's locals (regression: it used to
    // read the caller's frame at level 1).
    auto ev = Debugger::instance().evaluate("z");
    CHECK_EQ(ev.type, std::string("integer"));
    CHECK(ev.value.find("7") != std::string::npos);
}
