#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "devtools/CallGraph.hpp"

#include <algorithm>
#include <string>

using namespace eve::dev;

static SourceLoc loc(const char* src, int line, const char* fn = "") {
    SourceLoc l;
    l.source   = src;
    l.line     = line;
    l.function = fn ? fn : "";
    return l;
}

static bool sliceHasLoc(const SliceResult& s, const char* src, int line) {
    for (const auto& l : s.locations) {
        if (l.source == src && l.line == line) return true;
    }
    return false;
}

TEST_CASE("devtools.callgraph.stackAndEdges") {
    CallGraph g;
    g.onCall(loc("a.nut", 1, "main"), "main");
    g.onLine(loc("a.nut", 2, "main"));
    g.onCall(loc("a.nut", 3, "foo"), "foo");
    g.onLine(loc("a.nut", 10, "foo"));
    g.onReturn(loc("a.nut", 11, "foo"), "foo");
    g.onLine(loc("a.nut", 4, "main"));

    auto stack = g.currentStack();
    REQUIRE(stack.size() == 1u);
    CHECK_EQ(stack[0].loc.function, std::string("main"));

    auto edges = g.callEdges();
    REQUIRE(edges.size() == 1u);
    CHECK_EQ(edges[0].first.function, std::string("main"));
    CHECK_EQ(edges[0].second.function, std::string("foo"));
}

TEST_CASE("devtools.callgraph.dataFlowReachingDef") {
    CallGraph g;
    g.onCall(loc("bug.nut", 1, "main"), "main");
    g.onDef(loc("bug.nut", 2, "main"), "x");   // x = 1
    g.onDef(loc("bug.nut", 3, "main"), "x");   // x = 0  (bad)
    g.onUse(loc("bug.nut", 4, "main"), "x");   // use x
    g.onLine(loc("bug.nut", 5, "main"));       // throw / error site

    SliceCriterion c;
    c.loc = loc("bug.nut", 5, "main");
    c.variables.push_back("x");

    SliceResult slice = g.sliceBackward(c);
    CHECK(sliceHasLoc(slice, "bug.nut", 3));  // last def of x
    CHECK(sliceHasLoc(slice, "bug.nut", 4));  // use of x
    // First def may also appear via control chain; last def must be present.
    bool sawDefEdge = false;
    for (const auto& e : slice.dataFlow) {
        if (e.var == "x") {
            sawDefEdge = true;
            const TraceEvent* from = g.event(e.fromEventId);
            REQUIRE(from != nullptr);
            CHECK_EQ(from->loc.line, 3);  // reaching def is line 3, not 2
        }
    }
    CHECK(sawDefEdge);
}

TEST_CASE("devtools.callgraph.interProceduralSlice") {
    CallGraph g;
    g.onCall(loc("game.nut", 1, "main"), "main");
    g.onDef(loc("game.nut", 2, "main"), "hp");
    g.onCall(loc("game.nut", 3, "main"), "damage");
    g.onUse(loc("game.nut", 20, "damage"), "hp");
    g.onDef(loc("game.nut", 21, "damage"), "hp");  // hp = hp - 999
    g.onReturn(loc("game.nut", 22, "damage"), "damage");
    g.onUse(loc("game.nut", 4, "main"), "hp");
    g.onLine(loc("game.nut", 5, "main"));  // assert hp > 0 failed

    SliceCriterion c;
    c.loc = loc("game.nut", 5, "main");
    c.variables = {"hp"};

    SliceResult slice = g.sliceBackward(c);
    CHECK(sliceHasLoc(slice, "game.nut", 21));
    CHECK(slice.callStack.size() >= 1u);

    const std::string report = g.formatErrorReport("assertion failed: hp > 0", c);
    CHECK(report.find("Call stack") != std::string::npos);
    CHECK(report.find("Data flow") != std::string::npos);
    CHECK(report.find("game.nut:21") != std::string::npos);
}

TEST_CASE("devtools.callgraph.ignoresUnrelatedCode") {
    CallGraph g;
    g.onCall(loc("s.nut", 1, "main"), "main");
    g.onDef(loc("s.nut", 2, "main"), "a");
    g.onDef(loc("s.nut", 3, "main"), "b");  // unrelated
    g.onUse(loc("s.nut", 4, "main"), "a");
    g.onLine(loc("s.nut", 5, "main"));

    SliceCriterion c;
    c.loc       = loc("s.nut", 5, "main");
    c.variables = {"a"};

    SliceResult slice = g.sliceBackward(c);
    CHECK(sliceHasLoc(slice, "s.nut", 2));
    CHECK(sliceHasLoc(slice, "s.nut", 4));
    // Pure data slice for `a` should not require `b`'s def; control chain may
    // still include line 3. Ensure `a`'s def is preferred in data-flow edges.
    bool aFromLine2 = false;
    for (const auto& e : slice.dataFlow) {
        if (e.var != "a") continue;
        const TraceEvent* from = g.event(e.fromEventId);
        if (from && from->loc.line == 2) aFromLine2 = true;
    }
    CHECK(aFromLine2);
}
