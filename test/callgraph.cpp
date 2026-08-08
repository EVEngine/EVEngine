#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "devtools/CallGraph.hpp"

#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <unordered_set>

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

static bool sliceHasKind(const CallGraph& g, const SliceResult& s, TraceKind kind) {
    for (uint32_t id : s.eventIds) {
        const TraceEvent* e = g.event(id);
        if (e && e->kind == kind) return true;
    }
    return false;
}

static bool dataFlowFromLine(const CallGraph& g, const SliceResult& s, const char* var, int line) {
    for (const auto& e : s.dataFlow) {
        if (e.var != var) continue;
        const TraceEvent* from = g.event(e.fromEventId);
        if (from && from->loc.line == line) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// SourceLoc
// ---------------------------------------------------------------------------

TEST_CASE("devtools.sourceloc.emptyAndMatches") {
    SourceLoc a;
    CHECK(a.empty());
    SourceLoc b = loc("a.nut", 10, "f");
    CHECK(!b.empty());
    CHECK(b.matches(loc("a.nut", 10, "f")));
    CHECK(b.matches(loc("a.nut", 10)));          // function optional
    CHECK(b.matches(loc("", 10, "f")));          // source optional when empty
    CHECK(!b.matches(loc("a.nut", 11, "f")));
    CHECK(!b.matches(loc("b.nut", 10, "f")));
    CHECK(b.toString().find("a.nut:10") != std::string::npos);
    CHECK(b.toString().find("in f") != std::string::npos);
    SourceLoc unk;
    unk.line = 3;
    CHECK(unk.toString().find("<unknown>:3") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Recording basics
// ---------------------------------------------------------------------------

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

TEST_CASE("devtools.callgraph.enterHelperAndEventLookup") {
    CallGraph g;
    const uint32_t lineId = g.enter(loc("e.nut", 1, "main"), "main");
    REQUIRE(g.event(lineId) != nullptr);
    const bool isLine = g.event(lineId)->kind == TraceKind::Line;
    CHECK(isLine);
    CHECK(g.event(0) == nullptr);
    CHECK(g.event(999999) == nullptr);
    CHECK_EQ(g.eventCount(), 2u);  // Call + Line
    CHECK_EQ(g.currentStack().size(), 1u);
}

TEST_CASE("devtools.callgraph.emptyVarIgnored") {
    CallGraph g;
    g.onCall(loc("v.nut", 1, "main"), "main");
    CHECK_EQ(g.onDef(loc("v.nut", 2), ""), 0u);
    CHECK_EQ(g.onUse(loc("v.nut", 3), ""), 0u);
    CHECK_EQ(g.eventCount(), 1u);
}

TEST_CASE("devtools.callgraph.clearResetsState") {
    CallGraph g;
    g.onCall(loc("c.nut", 1, "main"), "main");
    g.onDef(loc("c.nut", 2), "x");
    g.onUse(loc("c.nut", 3), "x");
    CHECK(g.eventCount() > 0);
    g.clear();
    CHECK_EQ(g.eventCount(), 0u);
    CHECK(g.currentStack().empty());
    CHECK(g.callEdges().empty());
    SliceResult empty = g.sliceBackward({});
    CHECK(empty.eventIds.empty());
    CHECK(empty.summary.find("empty") != std::string::npos);
}

TEST_CASE("devtools.callgraph.nestedStackDepth") {
    CallGraph g;
    g.onCall(loc("n.nut", 1, "a"), "a");
    g.onCall(loc("n.nut", 2, "b"), "b");
    g.onCall(loc("n.nut", 3, "c"), "c");
    auto deep = g.currentStack();
    REQUIRE(deep.size() == 3u);
    CHECK_EQ(deep[0].loc.function, std::string("a"));
    CHECK_EQ(deep[2].loc.function, std::string("c"));

    const uint32_t mid = g.events().back().id;
    g.onReturn(loc("n.nut", 4, "c"), "c");
    g.onReturn(loc("n.nut", 5, "b"), "b");
    CHECK_EQ(g.currentStack().size(), 1u);

    auto atMid = g.stackAt(mid);
    REQUIRE(atMid.size() == 3u);
    CHECK_EQ(atMid[2].loc.function, std::string("c"));

    auto edges = g.callEdges();
    CHECK_EQ(edges.size(), 2u);  // a→b, b→c (returns don't add)
}

TEST_CASE("devtools.callgraph.stackAtInvalid") {
    CallGraph g;
    CHECK(g.stackAt(0).empty());
    CHECK(g.stackAt(42).empty());
    g.onCall(loc("s.nut", 1, "main"), "main");
    CHECK_EQ(g.stackAt(g.events().back().id).size(), 1u);
}

TEST_CASE("devtools.callgraph.funcNameFallsBackToLoc") {
    CallGraph g;
    SourceLoc l = loc("f.nut", 1, "fromLoc");
    g.onCall(l);  // empty funcName → use loc.function
    REQUIRE(!g.currentStack().empty());
    CHECK_EQ(g.currentStack()[0].loc.function, std::string("fromLoc"));
}

// ---------------------------------------------------------------------------
// Data-flow / slicer
// ---------------------------------------------------------------------------

TEST_CASE("devtools.callgraph.dataFlowReachingDef") {
    CallGraph g;
    g.onCall(loc("bug.nut", 1, "main"), "main");
    g.onDef(loc("bug.nut", 2, "main"), "x");
    g.onDef(loc("bug.nut", 3, "main"), "x");
    g.onUse(loc("bug.nut", 4, "main"), "x");
    g.onLine(loc("bug.nut", 5, "main"));

    SliceCriterion c;
    c.loc = loc("bug.nut", 5, "main");
    c.variables.push_back("x");

    SliceResult slice = g.sliceBackward(c);
    CHECK(sliceHasLoc(slice, "bug.nut", 3));
    CHECK(sliceHasLoc(slice, "bug.nut", 4));
    CHECK(dataFlowFromLine(g, slice, "x", 3));
    CHECK(!dataFlowFromLine(g, slice, "x", 2));
}

TEST_CASE("devtools.callgraph.multiVariableCriterion") {
    CallGraph g;
    g.onCall(loc("m.nut", 1, "main"), "main");
    g.onDef(loc("m.nut", 2, "main"), "a");
    g.onDef(loc("m.nut", 3, "main"), "b");
    g.onDef(loc("m.nut", 4, "main"), "c");
    g.onUse(loc("m.nut", 5, "main"), "a");
    g.onUse(loc("m.nut", 6, "main"), "b");
    g.onLine(loc("m.nut", 7, "main"));

    SliceCriterion crit;
    crit.loc       = loc("m.nut", 7, "main");
    crit.variables = {"a", "b"};
    SliceResult slice = g.sliceBackward(crit);
    CHECK(dataFlowFromLine(g, slice, "a", 2));
    CHECK(dataFlowFromLine(g, slice, "b", 3));
}

TEST_CASE("devtools.callgraph.eventIdSeed") {
    CallGraph g;
    g.onCall(loc("id.nut", 1, "main"), "main");
    g.onDef(loc("id.nut", 2, "main"), "x");
    const uint32_t useId = g.onUse(loc("id.nut", 3, "main"), "x");
    g.onDef(loc("id.nut", 4, "main"), "x");  // later def should not shadow seed
    g.onLine(loc("id.nut", 5, "main"));

    SliceCriterion c;
    c.eventId = useId;
    c.variables = {"x"};
    SliceResult slice = g.sliceBackward(c);
    CHECK(dataFlowFromLine(g, slice, "x", 2));
    // Seeded at use on line 3; later def on 4 need not be in data-flow of this use.
    bool laterDefInData = dataFlowFromLine(g, slice, "x", 4);
    CHECK(!laterDefInData);
}

TEST_CASE("devtools.callgraph.useWithoutDef") {
    CallGraph g;
    g.onCall(loc("u.nut", 1, "main"), "main");
    g.onUse(loc("u.nut", 2, "main"), "missing");
    g.onLine(loc("u.nut", 3, "main"));
    SliceCriterion c;
    c.loc       = loc("u.nut", 3, "main");
    c.variables = {"missing"};
    SliceResult slice = g.sliceBackward(c);
    CHECK(!slice.eventIds.empty());
    CHECK(slice.dataFlow.empty());  // no reaching def
}

TEST_CASE("devtools.callgraph.interProceduralSlice") {
    CallGraph g;
    g.onCall(loc("game.nut", 1, "main"), "main");
    g.onDef(loc("game.nut", 2, "main"), "hp");
    g.onCall(loc("game.nut", 3, "main"), "damage");
    g.onUse(loc("game.nut", 20, "damage"), "hp");
    g.onDef(loc("game.nut", 21, "damage"), "hp");
    g.onReturn(loc("game.nut", 22, "damage"), "damage");
    g.onUse(loc("game.nut", 4, "main"), "hp");
    g.onLine(loc("game.nut", 5, "main"));

    SliceCriterion c;
    c.loc       = loc("game.nut", 5, "main");
    c.variables = {"hp"};

    SliceResult slice = g.sliceBackward(c);
    CHECK(sliceHasLoc(slice, "game.nut", 21));
    CHECK(slice.callStack.size() >= 1u);

    const std::string report = g.formatErrorReport("assertion failed: hp > 0", c);
    CHECK(report.find("Call stack") != std::string::npos);
    CHECK(report.find("Data flow") != std::string::npos);
    CHECK(report.find("game.nut:21") != std::string::npos);
    CHECK(report.find("Vars:") != std::string::npos);
    CHECK(report.find("hp") != std::string::npos);
}

TEST_CASE("devtools.callgraph.chainOfAssignments") {
    CallGraph g;
    g.onCall(loc("ch.nut", 1, "main"), "main");
    g.onDef(loc("ch.nut", 2, "main"), "a");
    g.onUse(loc("ch.nut", 3, "main"), "a");
    g.onDef(loc("ch.nut", 3, "main"), "b");  // b = a
    g.onUse(loc("ch.nut", 4, "main"), "b");
    g.onDef(loc("ch.nut", 4, "main"), "c");  // c = b
    g.onUse(loc("ch.nut", 5, "main"), "c");
    g.onLine(loc("ch.nut", 6, "main"));

    SliceCriterion c;
    c.loc       = loc("ch.nut", 6, "main");
    c.variables = {"c"};
    SliceResult slice = g.sliceBackward(c);
    CHECK(dataFlowFromLine(g, slice, "c", 4));
    // Control/data may pull b's def; require c's immediate def at least.
    CHECK(sliceHasLoc(slice, "ch.nut", 4));
}

TEST_CASE("devtools.callgraph.ignoresUnrelatedCode") {
    CallGraph g;
    g.onCall(loc("s.nut", 1, "main"), "main");
    g.onDef(loc("s.nut", 2, "main"), "a");
    g.onDef(loc("s.nut", 3, "main"), "b");
    g.onUse(loc("s.nut", 4, "main"), "a");
    g.onLine(loc("s.nut", 5, "main"));

    SliceCriterion c;
    c.loc       = loc("s.nut", 5, "main");
    c.variables = {"a"};

    SliceResult slice = g.sliceBackward(c);
    CHECK(sliceHasLoc(slice, "s.nut", 2));
    CHECK(sliceHasLoc(slice, "s.nut", 4));
    CHECK(dataFlowFromLine(g, slice, "a", 2));
}

TEST_CASE("devtools.callgraph.sliceIncludesCallSite") {
    CallGraph g;
    g.onCall(loc("cs.nut", 1, "main"), "main");
    g.onCall(loc("cs.nut", 2, "main"), "boom");
    g.onDef(loc("cs.nut", 10, "boom"), "x");
    g.onUse(loc("cs.nut", 11, "boom"), "x");
    g.onLine(loc("cs.nut", 12, "boom"));

    SliceCriterion c;
    c.loc       = loc("cs.nut", 12, "boom");
    c.variables = {"x"};
    SliceResult slice = g.sliceBackward(c);
    CHECK(sliceHasKind(g, slice, TraceKind::Call));
    CHECK(slice.callStack.size() == 2u);
}

TEST_CASE("devtools.callgraph.locationsAreUnique") {
    CallGraph g;
    g.onCall(loc("uniq.nut", 1, "main"), "main");
    for (int i = 0; i < 5; ++i) {
        g.onDef(loc("uniq.nut", 2, "main"), "x");
        g.onUse(loc("uniq.nut", 2, "main"), "x");
    }
    g.onLine(loc("uniq.nut", 3, "main"));
    SliceCriterion c;
    c.loc = loc("uniq.nut", 3, "main");
    SliceResult slice = g.sliceBackward(c);
    std::set<std::string> keys;
    for (const auto& l : slice.locations) {
        const std::string k = l.toString();
        CHECK(keys.insert(k).second);
    }
}

TEST_CASE("devtools.callgraph.emptyCriterionUsesLastEvent") {
    CallGraph g;
    g.onCall(loc("last.nut", 1, "main"), "main");
    g.onLine(loc("last.nut", 9, "main"));
    SliceResult slice = g.sliceBackward({});
    CHECK(!slice.eventIds.empty());
    CHECK(sliceHasLoc(slice, "last.nut", 9));
}

TEST_CASE("devtools.callgraph.formatReportEmptyStack") {
    CallGraph g;
    // No calls — only a line
    g.onLine(loc("fmt.nut", 1));
    SliceCriterion c;
    c.loc = loc("fmt.nut", 1);
    const std::string report = g.formatErrorReport("oops", c);
    CHECK(report.find("Script Error Trace") != std::string::npos);
    CHECK(report.find("(empty)") != std::string::npos);
    CHECK(report.find("oops") != std::string::npos);
}

TEST_CASE("devtools.callgraph.deepCallThenReturnSlice") {
    CallGraph g;
    g.onCall(loc("d.nut", 1, "main"), "main");
    for (int i = 0; i < 8; ++i) {
        char name[16];
        std::snprintf(name, sizeof(name), "f%d", i);
        g.onCall(loc("d.nut", 10 + i, name), name);
    }
    g.onDef(loc("d.nut", 100, "f7"), "err");
    g.onUse(loc("d.nut", 101, "f7"), "err");
    // Unwind all
    for (int i = 7; i >= 0; --i) {
        char name[16];
        std::snprintf(name, sizeof(name), "f%d", i);
        g.onReturn(loc("d.nut", 200 + i, name), name);
    }
    g.onLine(loc("d.nut", 2, "main"));
    // Error after unwind — stack is main only; slice by eventId inside f7
    uint32_t seed = 0;
    for (const auto& e : g.events()) {
        if (e.kind == TraceKind::Use && e.name == "err") seed = e.id;
    }
    REQUIRE(seed != 0u);
    SliceCriterion c;
    c.eventId   = seed;
    c.variables = {"err"};
    SliceResult slice = g.sliceBackward(c);
    CHECK(sliceHasLoc(slice, "d.nut", 100));
    CHECK(slice.callStack.size() == 9u);  // main + f0..f7
}

// ---------------------------------------------------------------------------
// Ring buffer
// ---------------------------------------------------------------------------

TEST_CASE("devtools.callgraph.ringBufferDropsOne") {
    CallGraph g;
    g.setMaxEvents(16);
    g.onCall(loc("r.nut", 1, "main"), "main");
    for (int i = 0; i < 40; ++i) g.onLine(loc("r.nut", 2 + i, "main"));

    CHECK_EQ(g.eventCount(), 16u);
    CHECK_EQ(g.maxEvents(), 16u);
    REQUIRE(!g.events().empty());
    const uint32_t newest = g.events().back().id;
    const uint32_t oldest = g.events().front().id;
    CHECK_EQ(newest - oldest, 15u);
    CHECK(g.event(oldest) != nullptr);
    CHECK(g.event(oldest - 1) == nullptr);
    CHECK(g.event(newest) != nullptr);
}

TEST_CASE("devtools.callgraph.setMaxEventsClampsAndShrinks") {
    CallGraph g;
    g.setMaxEvents(2);  // clamped to 16
    CHECK_EQ(g.maxEvents(), 16u);
    for (int i = 0; i < 20; ++i) g.onLine(loc("z.nut", i + 1));
    CHECK_EQ(g.eventCount(), 16u);
    g.setMaxEvents(16);
    g.setMaxEvents(20);  // grow cap only
    CHECK_EQ(g.eventCount(), 16u);
    // Shrink: force drop down to 16 min still
    g.setMaxEvents(16);
    CHECK_EQ(g.eventCount(), 16u);
}

TEST_CASE("devtools.callgraph.ringBufferKeepsRecentDataFlow") {
    CallGraph g;
    g.setMaxEvents(16);
    // Flood with unrelated lines so early defs fall out of the window.
    g.onCall(loc("rb.nut", 1, "main"), "main");
    g.onDef(loc("rb.nut", 2, "main"), "old");
    for (int i = 0; i < 30; ++i) g.onLine(loc("rb.nut", 10 + i, "main"));
    g.onDef(loc("rb.nut", 100, "main"), "x");
    g.onUse(loc("rb.nut", 101, "main"), "x");
    g.onLine(loc("rb.nut", 102, "main"));

    CHECK_EQ(g.eventCount(), 16u);
    SliceCriterion c;
    c.loc       = loc("rb.nut", 102, "main");
    c.variables = {"x"};
    SliceResult slice = g.sliceBackward(c);
    CHECK(dataFlowFromLine(g, slice, "x", 100));
    // Old def should be gone from the window.
    bool sawOld = false;
    for (const auto& e : g.events()) {
        if (e.kind == TraceKind::Def && e.name == "old") sawOld = true;
    }
    CHECK(!sawOld);
}

TEST_CASE("devtools.callgraph.ringBufferOneInOneOutInvariant") {
    CallGraph g;
    g.setMaxEvents(16);
    for (int i = 0; i < 16; ++i) g.onLine(loc("inv.nut", i + 1));
    CHECK_EQ(g.eventCount(), 16u);
    const uint32_t beforeFront = g.events().front().id;
    g.onLine(loc("inv.nut", 100));
    CHECK_EQ(g.eventCount(), 16u);
    CHECK_EQ(g.events().front().id, beforeFront + 1);
    CHECK_EQ(g.events().back().loc.line, 100);
}

TEST_CASE("devtools.callgraph.manyFramesSimulateGameLoop") {
    CallGraph g;
    g.setMaxEvents(64);
    for (int frame = 0; frame < 20; ++frame) {
        g.onCall(loc("loop.nut", 1, "update"), "update");
        g.onDef(loc("loop.nut", 2, "update"), "dt");
        g.onUse(loc("loop.nut", 3, "update"), "dt");
        g.onReturn(loc("loop.nut", 4, "update"), "update");
    }
    // Corrupt last frame
    g.onCall(loc("loop.nut", 1, "update"), "update");
    g.onDef(loc("loop.nut", 2, "update"), "dt");
    g.onDef(loc("loop.nut", 5, "update"), "dt");  // bad
    g.onUse(loc("loop.nut", 6, "update"), "dt");
    g.onLine(loc("loop.nut", 7, "update"));

    SliceCriterion c;
    c.loc       = loc("loop.nut", 7, "update");
    c.variables = {"dt"};
    SliceResult slice = g.sliceBackward(c);
    CHECK(dataFlowFromLine(g, slice, "dt", 5));
    CHECK(g.eventCount() <= 64u);
}

TEST_CASE("devtools.callgraph.parentEventIdChain") {
    CallGraph g;
    g.onCall(loc("p.nut", 1, "main"), "main");
    g.onLine(loc("p.nut", 2, "main"));
    g.onLine(loc("p.nut", 3, "main"));
    REQUIRE(g.events().size() >= 3u);
    auto it = g.events().begin();
    const uint32_t id0 = it->id;
    ++it;
    const uint32_t id1 = it->id;
    CHECK_EQ(it->parentEventId, id0);
    ++it;
    CHECK_EQ(it->parentEventId, id1);
}
