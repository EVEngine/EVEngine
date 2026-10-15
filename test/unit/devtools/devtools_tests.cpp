#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_agent_development_session)
#include "PrivateTestScope.h"
#include <Poco/JSON/Parser.h>
#include "devtools/AgentDevelopmentMcp.hpp"
#include "devtools/AgentDevelopmentSession.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// agent_development_session.cpp
namespace test_agent_development_session {
using namespace eve::dev;

TEST_CASE("devtools.agentDevelopmentSession.enforcesWorkflowAndEvidenceGate") {
    auto& session = AgentDevelopmentSession::instance();
    session.reset();

    std::vector<AgentAcceptanceCriterion> criteria{
        {"runtime", "Runtime state matches the requested behavior", true, false},
        {"visual", "Rendered output is legible", true, false},
        {"note", "Optional design note", false, false},
    };
    auto started = session.start("Build one verified game feature", std::move(criteria));
    REQUIRE(started.isAccepted());
    CHECK_EQ(agentDevelopmentPhaseName(session.phase()), std::string_view("discover"));
    CHECK(!session.readyToComplete());
    const std::string id = session.sessionId();

    CHECK_EQ(session.advance(id, AgentDevelopmentPhase::Run).code, "invalid-transition");
    REQUIRE(session.advance(id, AgentDevelopmentPhase::Modify).isAccepted());
    REQUIRE(session.advance(id, AgentDevelopmentPhase::Run).isAccepted());
    REQUIRE(session.advance(id, AgentDevelopmentPhase::Observe).isAccepted());
    REQUIRE(session.record(id, {0, "visual", "manual", "pass", "looks fine", {}}).isAccepted());
    CHECK(!session.criteria()[1].passed);
    REQUIRE(session.record(id, {0, "runtime", "runtime-observation", "pass", "x reached 4", {}}).isAccepted());
    REQUIRE(session.advance(id, AgentDevelopmentPhase::Verify).isAccepted());
    CHECK_EQ(session.complete(id, "premature").code, "acceptance-incomplete");

    REQUIRE(session.record(id, {0, "visual", "screenshot", "pass", "frame inspected", "frame.png"}).isAccepted());
    CHECK(session.readyToComplete());
    REQUIRE(session.complete(id, "feature verified").isAccepted());
    CHECK_EQ(agentDevelopmentPhaseName(session.phase()), std::string_view("complete"));
    CHECK(!session.active());
    CHECK_EQ(session.evidence().size(), std::size_t{3});
}

TEST_CASE("devtools.agentDevelopmentSession.rejectsEvalAndRequiresScreenshotForVisual") {
    auto& session = AgentDevelopmentSession::instance();
    session.reset();
    REQUIRE(session
                .start("Verify without eval",
                       {{"visual", "Must see the frame", true, false}, {"runtime", "State moved", true, false}})
                .isAccepted());
    const std::string id = session.sessionId();
    REQUIRE(session.advance(id, AgentDevelopmentPhase::Modify).isAccepted());
    REQUIRE(session.advance(id, AgentDevelopmentPhase::Run).isAccepted());
    REQUIRE(session.advance(id, AgentDevelopmentPhase::Observe).isAccepted());
    CHECK_EQ(session.record(id, {0, "runtime", "eval", "pass", "eve_eval cheated", {}}).code, "invalid-evidence-kind");
    REQUIRE(session.record(id, {0, "runtime", "runtime-observation", "pass", "tick moved", {}}).isAccepted());
    REQUIRE(session.record(id, {0, "visual", "runtime-observation", "pass", "looked at numbers", {}}).isAccepted());
    CHECK(!session.criteria()[0].passed);
    REQUIRE(session.advance(id, AgentDevelopmentPhase::Verify).isAccepted());
    CHECK_EQ(session.complete(id, "missing screenshot").code, "acceptance-incomplete");
    REQUIRE(session.record(id, {0, "visual", "screenshot", "pass", "frame inspected", "frame.png"}).isAccepted());
    REQUIRE(session.record(id, {0, "runtime", "play-trace", "pass", "trace stable", "trace.json"}).isAccepted());
    REQUIRE(session.complete(id, "verified without eval").isAccepted());
}

TEST_CASE("devtools.agentDevelopmentSession.recoversAndAbortsExplicitly") {
    auto& session = AgentDevelopmentSession::instance();
    session.reset();
    REQUIRE(session.start("Recover a broken game", {{"smoke", "Smoke passes", true, false}}).isAccepted());
    const std::string id = session.sessionId();
    REQUIRE(session.advance(id, AgentDevelopmentPhase::Recover).isAccepted());
    REQUIRE(session.advance(id, AgentDevelopmentPhase::Modify).isAccepted());
    REQUIRE(session.abort(id, "dependency unavailable").isAccepted());
    CHECK_EQ(agentDevelopmentPhaseName(session.phase()), std::string_view("aborted"));
    CHECK_EQ(session.summary(), "dependency unavailable");
    CHECK_EQ(session.advance(id, AgentDevelopmentPhase::Run).code, "session-terminal");
}

TEST_CASE("devtools.agentDevelopmentSession.mcpAdapterReturnsVersionedSnapshot") {
    AgentDevelopmentSession::instance().reset();
    Poco::JSON::Parser parser;
    auto               args =
        parser
            .parse(
                R"({"objective":"Ship a playable loop","criteria":[{"id":"play","description":"Loop is playable"}]})")
            .extract<Poco::JSON::Object::Ptr>();
    Poco::JSON::Parser responseParser;
    auto               response = responseParser.parse(callAgentDevelopmentTool("eve_agent_session_start", args))
                        .extract<Poco::JSON::Object::Ptr>();
    REQUIRE(response);
    CHECK_EQ(response->getValue<std::string>("status"), "applied");
    CHECK_EQ(response->getValue<std::string>("schema"), "eve.agent-development-session");
    CHECK_EQ(response->getValue<int>("schemaVersion"), 1);
    CHECK_EQ(response->getValue<std::string>("phase"), "discover");
    CHECK_EQ(response->getArray("criteria")->size(), std::size_t{1});
}
}  // namespace test_agent_development_session
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_callgraph)
#include "PrivateTestScope.h"
#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <unordered_set>
#include "devtools/CallGraph.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// callgraph.cpp
namespace test_callgraph {
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
    CHECK(b.matches(loc("a.nut", 10)));  // function optional
    CHECK(b.matches(loc("", 10, "f")));  // source optional when empty
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
    CallGraph      g;
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
    crit.loc          = loc("m.nut", 7, "main");
    crit.variables    = {"a", "b"};
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
    c.eventId         = useId;
    c.variables       = {"x"};
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
    c.loc             = loc("u.nut", 3, "main");
    c.variables       = {"missing"};
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
    c.loc             = loc("ch.nut", 6, "main");
    c.variables       = {"c"};
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
    c.loc             = loc("cs.nut", 12, "boom");
    c.variables       = {"x"};
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
    c.loc                       = loc("uniq.nut", 3, "main");
    SliceResult           slice = g.sliceBackward(c);
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
    c.loc                    = loc("fmt.nut", 1);
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
    c.eventId         = seed;
    c.variables       = {"err"};
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
    c.loc             = loc("rb.nut", 102, "main");
    c.variables       = {"x"};
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
    c.loc             = loc("loop.nut", 7, "update");
    c.variables       = {"dt"};
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
    auto           it  = g.events().begin();
    const uint32_t id0 = it->id;
    ++it;
    const uint32_t id1 = it->id;
    CHECK_EQ(it->parentEventId, id0);
    ++it;
    CHECK_EQ(it->parentEventId, id1);
}
}  // namespace test_callgraph
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_console)
#include "PrivateTestScope.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include "devtools/ConsolePanel.hpp"
#include "devtools/DevTool.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// console.cpp
namespace test_console_unit {
using namespace eve::dev;

TEST_CASE("devtools.console.logRingBuffer") {
    auto& console = ConsolePanel::instance();
    console.clear();
    console.setVisible(false);
    CHECK(!console.isVisible());
    console.toggleVisible();
    CHECK(console.isVisible());
    console.setVisible(false);

    console.addInfo("hello");
    console.addWarn("careful");
    console.addError("boom");
    console.addLog("debug", "trace");

    const std::string out = console.format(16);
    CHECK(out.find("hello") != std::string::npos);
    CHECK(out.find("careful") != std::string::npos);
    CHECK(out.find("boom") != std::string::npos);
    CHECK(out.find("trace") != std::string::npos);

    const auto recent = console.recent(2);
    REQUIRE(recent.size() == 2);
    CHECK(recent[0].text == "boom");
    CHECK(recent[1].text == "trace");

    console.setMaxEntries(3);
    console.addInfo("overflow1");
    console.addInfo("overflow2");
    console.addInfo("overflow3");
    CHECK(console.format(100).find("hello") == std::string::npos);

    console.clear();
    CHECK(console.format(8).empty());
}
}  // namespace test_console_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_debugger)
#include <squirrel.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include "PrivateTestScope.h"
#include "common/Capability.h"
#include "common/IStateProvider.h"
#include "common/Module.h"
#include "common/StateValue.h"
#include "devtools/Debugger.hpp"
#include "devtools/DevTool.hpp"
#include "devtools/Snapshot.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// debugger.cpp
namespace test_debugger_unit {
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
    Debugger*   dbg      = nullptr;
    int         stops    = 0;
    int         lastLine = -1;
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
    CHECK_EQ(Debugger::normalizeSource("file://localhost/Users/x/main.nut"), std::string("/Users/x/main.nut"));
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
        const int  m        = static_cast<int>(d.mode());
        const bool stepping = m == static_cast<int>(RunMode::StepInto) || m == static_cast<int>(RunMode::StepOver);
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
}  // namespace test_debugger_unit
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_mcp_architecture_graph) || \
    defined(EVE_TEST_ORIGIN_mcp_whitebox)
#include "PrivateTestScope.h"
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <chrono>
#include <sstream>
#include <string>
#include <thread>
#include "common/BindingContracts.h"
#include "common/ScriptCompiler.h"
#include "devtools/McpArchitectureTools.hpp"
#include "devtools/McpInputTools.hpp"
#include "devtools/McpServer.hpp"
#include "devtools/McpToolFilter.hpp"
#include "devtools/McpWhiteboxTools.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// Responsibility: devtools_whitebox_model
namespace test_devtools_whitebox_model {
// mcp_architecture_graph.cpp
namespace {
namespace mcp_architecture_graph {
namespace {
[[maybe_unused]] Poco::JSON::Object::Ptr payload(const std::string& envelope) {
    Poco::JSON::Parser parser;
    auto               outer = parser.parse(envelope).extract<Poco::JSON::Object::Ptr>();
    return parser.parse(outer->getArray("content")->getObject(0)->getValue<std::string>("text"))
        .extract<Poco::JSON::Object::Ptr>();
}
[[maybe_unused]] Poco::JSON::Object::Ptr arguments(const std::string& seed) {
    Poco::JSON::Object::Ptr args = new Poco::JSON::Object();
    args->set("seed", seed);
    return args;
}
[[maybe_unused]] bool containsNode(Poco::JSON::Object::Ptr result, const std::string& id) {
    auto nodes = result->getArray("nodes");
    for (size_t i = 0; i < nodes->size(); ++i)
        if (nodes->getObject(static_cast<unsigned>(i))->getValue<std::string>("id") == id) return true;
    return false;
}
}  // namespace
TEST_CASE("devtools.architecture.impactConnectsWriterAndReader") {
    auto       args   = arguments("inventory/Bag.setExtra");
    const auto result = payload(eve::dev::callMcpArchitectureTool("eve_architecture_impact", args));
    REQUIRE(result->getValue<bool>("ok"));
    CHECK(containsNode(result, "api:inventory/Bag.getExtra"));
    CHECK(containsNode(result, "state:eve::inventory::Bag.extra_"));
    CHECK(containsNode(result, "owner:eve::inventory::Bag"));
    CHECK(!containsNode(result, "api:scene/SceneNodeRef.setVisible"));
    CHECK(!result->getValue<bool>("truncated"));
    CHECK(result->getArray("tests")->size() > 0);
    CHECK(result->getArray("inputHashes")->size() == 2);
    const auto repeat = eve::dev::callMcpArchitectureTool("eve_architecture_impact", args);
    CHECK_EQ(repeat, eve::dev::callMcpArchitectureTool("eve_architecture_impact", args));
    CHECK(!eve::dev::mcpToolMutates("eve_architecture_impact"));
    auto sceneArgs = arguments("scene/SceneNodeRef.setVisible");
    sceneArgs->set("task", "Investigate visibility");
    auto scene = payload(eve::dev::callMcpArchitectureTool("eve_task_context", sceneArgs));
    REQUIRE(scene->getValue<bool>("ok"));
    CHECK(containsNode(scene, "owner:eve::scene::SceneHost"));
    CHECK(containsNode(scene, "state:eve::scene::SceneNode.visible"));
    CHECK(!containsNode(scene, "api:inventory/Bag.setExtra"));
}
TEST_CASE("devtools.architecture.budgetsUnknownsAndStaleEvidence") {
    auto args    = arguments("scene/SceneNodeRef.isVisible");
    auto unknown = payload(eve::dev::callMcpArchitectureTool("eve_task_context", args));
    CHECK_EQ(unknown->getValue<std::string>("error"), std::string("invalid_query"));
    args->set("task", "Investigate visibility");
    unknown = payload(eve::dev::callMcpArchitectureTool("eve_task_context", args));
    REQUIRE(unknown->getValue<bool>("ok"));
    CHECK(unknown->getArray("unknowns")->size() > 0);
    args = arguments("inventory/Bag.setExtra");
    args->set("maxNodes", 1);
    auto bounded = payload(eve::dev::callMcpArchitectureTool("eve_architecture_query", args));
    CHECK(bounded->getValue<bool>("truncated"));
    CHECK_EQ(bounded->getArray("nodes")->size(), size_t(1));
    args->set("maxNodes", 64);
    args->set("maxDepth", 0);
    auto shallow = payload(eve::dev::callMcpArchitectureTool("eve_architecture_query", args));
    CHECK(shallow->getValue<bool>("truncated"));
    CHECK_EQ(shallow->getArray("nodes")->size(), size_t(1));
    args->set("maxDepth", 6);
    args->set("maxBytes", 1024);
    auto exhausted = payload(eve::dev::callMcpArchitectureTool("eve_architecture_query", args));
    CHECK_EQ(exhausted->getValue<std::string>("error"), std::string("context_budget_exceeded"));
    args->set("maxBytes", 16384);
    args->set("expectedInputHash", "old-version");
    auto stale = payload(eve::dev::callMcpArchitectureTool("eve_architecture_impact", args));
    CHECK_EQ(stale->getValue<std::string>("error"), std::string("stale_evidence"));
    const auto* contract = eve::script::generatedBindingContracts().find("inventory/Bag.setExtra");
    REQUIRE(contract != nullptr);
    REQUIRE(contract->architecture.has_value());
    args->set("expectedInputHash", contract->architecture->inputHash);
    args->set("task", "Change quest extras");
    auto context = payload(eve::dev::callMcpArchitectureTool("eve_task_context", args));
    REQUIRE(context->getValue<bool>("ok"));
    CHECK_EQ(context->getValue<std::string>("schema"), std::string("eve.task-context"));
    CHECK(containsNode(context, "api:inventory/Bag.getExtra"));
    CHECK(context->getArray("tests")->size() > 0);
    CHECK_EQ(context->getArray("contracts")->size(), size_t(2));
    bool readerContract = false;
    for (size_t index = 0; index < context->getArray("contracts")->size(); ++index) {
        auto item = context->getArray("contracts")->getObject(static_cast<unsigned>(index));
        if (item->getValue<std::string>("method") != "getExtra") continue;
        CHECK_EQ(item->getArray("parameters")->size(), size_t(2));
        CHECK_EQ(item->getObject("architecture")->getArray("reads")->size(), size_t(1));
        CHECK_EQ(item->getObject("architecture")->getArray("writes")->size(), size_t(0));
        readerContract = true;
    }
    CHECK(readerContract);
    args->set("seed", "not-a-real-api");
    auto missing = payload(eve::dev::callMcpArchitectureTool("eve_architecture_query", args));
    CHECK_EQ(missing->getValue<std::string>("error"), std::string("unknown_seed"));
}
}  // namespace mcp_architecture_graph
}  // namespace

// mcp_whitebox.cpp
namespace {
namespace mcp_whitebox {
// MCP white-box batch + playtest tools (scene_director.batch / eve_whitebox_playtest).

using eve::dev::callMcpWhiteboxTool;
using eve::dev::isMcpWhiteboxTool;
using eve::dev::mcpToolMutates;
using eve::dev::mcpToolTags;
using eve::dev::mcpWhiteboxToolSchemas;
using eve::dev::resetMcpInputBatchesForTest;

namespace {

[[maybe_unused]] Poco::JSON::Object::Ptr parseObject(const std::string& json) {
    Poco::JSON::Parser parser;
    auto               root = parser.parse(json);
    if (root.type() != typeid(Poco::JSON::Object::Ptr)) return {};
    return root.extract<Poco::JSON::Object::Ptr>();
}

[[maybe_unused]] Poco::JSON::Object::Ptr unwrapToolText(const std::string& envelope) {
    auto outer = parseObject(envelope);
    REQUIRE(outer);
    auto content = outer->getArray("content");
    REQUIRE(content);
    REQUIRE(content->size() > 0);
    return parseObject(content->getObject(0)->getValue<std::string>("text"));
}

}  // namespace

TEST_CASE("devtools.mcp.whiteboxToolIsRegisteredAndTagged") {
    CHECK(isMcpWhiteboxTool("eve_whitebox_playtest"));
    CHECK(!isMcpWhiteboxTool("eve_scene_batch"));
    CHECK(mcpToolMutates("eve_whitebox_playtest"));
    CHECK(mcpToolTags("eve_whitebox_playtest").size() >= 2);
    const std::string schemas(mcpWhiteboxToolSchemas());
    CHECK(schemas.find("eve_whitebox_playtest") != std::string::npos);
    CHECK(schemas.find("scene") != std::string::npos);
    CHECK(schemas.find("inputs") != std::string::npos);
}

TEST_CASE("devtools.mcp.whiteboxPlaytestRequiresLiveVm") {
    resetMcpInputBatchesForTest();
    Poco::JSON::Parser      parser;
    Poco::JSON::Object::Ptr args =
        parser.parse(R"({"reset":false,"scene":[{"action":"info"}]})").extract<Poco::JSON::Object::Ptr>();
    auto result = unwrapToolText(callMcpWhiteboxTool("eve_whitebox_playtest", args));
    REQUIRE(result);
    CHECK(!result->getValue<bool>("ok"));
    CHECK(result->has("error"));
}
}  // namespace mcp_whitebox
}  // namespace
}  // namespace test_devtools_whitebox_model
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_frame_stats)
#include "PrivateTestScope.h"
#include <cmath>
#include <limits>
#include <string>
#include "devtools/FrameStatsPanel.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// frame_stats.cpp
namespace test_frame_stats {
using namespace eve::dev;

TEST_CASE("devtools.frameStats.sampleAndFormat") {
    auto& stats = FrameStatsPanel::instance();
    stats.setVisible(false);
    stats.clearEntityCount();
    CHECK(!stats.isVisible());
    stats.toggleVisible();
    CHECK(stats.isVisible());
    stats.setVisible(false);

    stats.sample(0.016f);
    CHECK(stats.fps() > 50.f);
    CHECK(stats.fps() < 70.f);
    CHECK(std::fabs(stats.frameMs() - 16.f) < 1.5f);
    CHECK(!stats.hasEntityCount());
    CHECK(stats.entityCount() < 0);

    stats.setEntityCount(42);
    CHECK(stats.hasEntityCount());
    CHECK(stats.entityCount() == 42);
    const std::string line = stats.format();
    CHECK(line.find("fps") != std::string::npos);
    CHECK(line.find("ms") != std::string::npos);
    CHECK(line.find("42") != std::string::npos);

    stats.clearEntityCount();
    CHECK(!stats.hasEntityCount());

    // Reject non-finite / non-positive samples without changing last good frameMs.
    const float goodMs = stats.frameMs();
    stats.sample(0.f);
    stats.sample(-1.f);
    stats.sample(std::numeric_limits<float>::quiet_NaN());
    CHECK(std::fabs(stats.frameMs() - goodMs) < 0.01f);
}
}  // namespace test_frame_stats
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_language_index)
#include "PrivateTestScope.h"
#include <cstdint>
#include <string>
#include "devtools/LanguageIndex.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// language_index.cpp
namespace test_language_index {
using eve::dev::lsp::Position;
using eve::dev::lsp::WorkspaceIndex;

TEST_CASE("languageIndex.crossFileDefinitionAndReferences") {
    WorkspaceIndex index;
    index.update("game:/lib.nut", "file:///game/lib.nut",
                 "export function greet(name: string) -> string { return name }\n");
    index.update("game:/main.nut", "file:///game/main.nut",
                 "import { greet } from \"./lib.nut\"\n"
                 "local message = greet(\"Ada\")\n");

    const auto definition = index.definition("game:/main.nut", Position{1, 17});
    REQUIRE(definition.has_value());
    CHECK_EQ(definition->uri, std::string("file:///game/lib.nut"));
    CHECK_EQ(definition->range.start.line, size_t(0));
    CHECK_EQ(definition->range.start.character, size_t(16));

    const auto references = index.references("game:/lib.nut", Position{0, 17}, true);
    CHECK_EQ(references.size(), size_t(3));
    const auto usages = index.references("game:/lib.nut", Position{0, 17}, false);
    CHECK_EQ(usages.size(), size_t(2));
}

TEST_CASE("languageIndex.renameExportUpdatesImportAndUsages") {
    WorkspaceIndex index;
    index.update("game:/lib.nut", "file:///game/lib.nut", "export const SPEED = 2.0\n");
    index.update("game:/main.nut", "file:///game/main.nut",
                 "import { SPEED } from \"./lib.nut\"\nlocal value = SPEED\n");

    const auto edits = index.rename("game:/lib.nut", Position{0, 14}, "PLAYER_SPEED");
    REQUIRE(edits.has_value());
    CHECK_EQ(edits->size(), size_t(3));
    for (const auto& edit : *edits) CHECK_EQ(edit.newText, std::string("PLAYER_SPEED"));
}

TEST_CASE("languageIndex.renameImportAliasStaysLocal") {
    WorkspaceIndex index;
    index.update("game:/lib.nut", "file:///game/lib.nut", "export function greet() {}\n");
    index.update("game:/main.nut", "file:///game/main.nut",
                 "import { greet as salute } from \"./lib.nut\"\nsalute()\n");

    const auto definition = index.definition("game:/main.nut", Position{1, 2});
    REQUIRE(definition.has_value());
    CHECK_EQ(definition->uri, std::string("file:///game/lib.nut"));

    const auto edits = index.rename("game:/main.nut", Position{1, 2}, "hello");
    REQUIRE(edits.has_value());
    CHECK_EQ(edits->size(), size_t(2));
    for (const auto& edit : *edits) CHECK_EQ(edit.location.uri, std::string("file:///game/main.nut"));
}

TEST_CASE("languageIndex.namespaceImportSupportsDefinitionReferencesAndRename") {
    WorkspaceIndex index;
    index.update("game:/lib.nut", "file:///game/lib.nut", "export function greet() {}\n");
    index.update("game:/namespace.nut", "file:///game/namespace.nut",
                 "import * as api from \"./lib.nut\"\napi.greet()\n");
    index.update("game:/named.nut", "file:///game/named.nut", "import { greet } from \"./lib.nut\"\ngreet()\n");

    const auto definition = index.definition("game:/namespace.nut", Position{1, 6});
    REQUIRE(definition.has_value());
    CHECK_EQ(definition->uri, std::string("file:///game/lib.nut"));

    const auto references = index.references("game:/namespace.nut", Position{1, 6}, true);
    CHECK_EQ(references.size(), size_t(4));

    const auto edits = index.rename("game:/namespace.nut", Position{1, 6}, "welcome");
    REQUIRE(edits.has_value());
    CHECK_EQ(edits->size(), size_t(4));
    for (const auto& edit : *edits) CHECK_EQ(edit.newText, std::string("welcome"));
}

TEST_CASE("languageIndex.rejectsInvalidRename") {
    WorkspaceIndex index;
    index.update("game:/main.nut", "file:///game/main.nut", "local value = 1\n");
    CHECK(!index.rename("game:/main.nut", Position{0, 7}, "not-valid").has_value());
}

TEST_CASE("languageIndex.semanticTokensDistinguishClassFunctionVariable") {
    WorkspaceIndex index;
    index.update("game:/main.nut", "file:///game/main.nut",
                 "class Player {}\n"
                 "function greet(name) { return name }\n"
                 "local speed = 1\n"
                 "const MAX = 2\n"
                 "greet(\"x\")\n"
                 "Player()\n"
                 "speed = MAX\n");

    const auto tokens = index.semanticTokens("game:/main.nut");
    const auto typeAt = [&](std::string_view name, size_t line) -> uint32_t {
        for (const auto& token : tokens)
            if (token.name == name && token.start.line == line) return token.type;
        return 99;
    };
    CHECK_EQ(typeAt("Player", 0), eve::dev::lsp::SemanticTypes::Class);
    CHECK_EQ(typeAt("greet", 1), eve::dev::lsp::SemanticTypes::Function);
    CHECK_EQ(typeAt("speed", 2), eve::dev::lsp::SemanticTypes::Variable);
    CHECK_EQ(typeAt("MAX", 3), eve::dev::lsp::SemanticTypes::Variable);
    CHECK_EQ(typeAt("greet", 4), eve::dev::lsp::SemanticTypes::Function);
    CHECK_EQ(typeAt("Player", 5), eve::dev::lsp::SemanticTypes::Class);
    CHECK_EQ(typeAt("speed", 6), eve::dev::lsp::SemanticTypes::Variable);
}

TEST_CASE("languageIndex.semanticTokensLiteralsAndSelf") {
    WorkspaceIndex index;
    index.update("game:/main.nut", "file:///game/main.nut", "local ready = true\nthis.base = null\n");
    const auto tokens = index.semanticTokens("game:/main.nut");
    const auto typeAt = [&](std::string_view name, size_t line) -> uint32_t {
        for (const auto& token : tokens)
            if (token.name == name && token.start.line == line) return token.type;
        return 99;
    };
    CHECK_EQ(typeAt("true", 0), eve::dev::lsp::SemanticTypes::Keyword);
    CHECK_EQ(typeAt("this", 1), eve::dev::lsp::SemanticTypes::Keyword);
    CHECK_EQ(typeAt("base", 1), eve::dev::lsp::SemanticTypes::Keyword);
    CHECK_EQ(typeAt("null", 1), eve::dev::lsp::SemanticTypes::Keyword);
}
}  // namespace test_language_index
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_language_text)
#include "PrivateTestScope.h"
#include <string>
#include "devtools/LanguageText.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// language_text.cpp
namespace test_language_text {
using eve::dev::lsp::FormatOptions;
using eve::dev::lsp::Range;

TEST_CASE("devtools.languageText.incrementalFormatAndFold") {
    std::string source = "function foo() {\nlocal x = 1\n}\n";
    Range       insert{{1, 6}, {1, 7}};
    CHECK(eve::dev::lsp::applyIncremental(source, &insert, "value"));
    CHECK_EQ(source, std::string("function foo() {\nlocal value = 1\n}\n"));

    const std::string formatted = eve::dev::lsp::formatEveScript(source);
    CHECK_EQ(formatted, std::string("function foo() {\n    local value = 1\n}\n"));

    FormatOptions tabs;
    tabs.insertSpaces = false;
    tabs.tabSize      = 4;
    CHECK_EQ(eve::dev::lsp::formatEveScript("{\na\n}\n", tabs), std::string("{\n\ta\n}\n"));

    const auto folds        = eve::dev::lsp::foldingRanges("function foo() {\n    local x = 1\n}\n/*\nblock\n*/\n");
    bool       foundBrace   = false;
    bool       foundComment = false;
    for (const auto& fold : folds) {
        if (fold.kind == "region" && fold.startLine == 0 && fold.endLine == 2) foundBrace = true;
        if (fold.kind == "comment" && fold.startLine == 3 && fold.endLine == 5) foundComment = true;
    }
    CHECK(foundBrace);
    CHECK(foundComment);

    CHECK_EQ(eve::dev::lsp::sliceLines("a\nb\nc\n", 1, 1), std::string("b\n"));
    const Range span = eve::dev::lsp::coveringLines("a\nb\nc\n", 1, 1);
    CHECK_EQ(span.start.line, size_t(1));
    CHECK_EQ(span.end.line, size_t(2));
    CHECK_EQ(span.end.character, size_t(0));
}
}  // namespace test_language_text
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_mcp_console)
#include "PrivateTestScope.h"
#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <chrono>
#include <cstdint>
#include <simplesquirrel/simplesquirrel.hpp>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "devtools/ConsolePanel.hpp"
#include "devtools/DevTool.hpp"
#include "devtools/McpServer.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// mcp_console.cpp
namespace test_mcp_console_unit {
// MCP runtime-console tools: an unattended agent must be able to read the same
// ordered diagnosis stream the game writes through print()/script errors, and
// must be able to tell "nothing new" apart from "lines were dropped".
//
// These cases pin three things:
//   1. ConsolePanel sequence cursors survive eviction and clear().
//   2. The MCP console tools round-trip over the stdio transport, including a
//      Squirrel print() captured into the same log.
//   3. Every tool advertised by tools/list is actually routed by tools/call, so
//      the hand-maintained schema table cannot drift from the dispatcher.


using namespace eve::dev;

namespace {

/** Run one pre-buffered stdio session and return the newline-framed responses. */
[[maybe_unused]] std::vector<std::string> runStdioSession(const std::string& requests, std::size_t expectedResponses) {
    auto& mcp = McpServer::instance();
    mcp.stop();
    std::istringstream       in(requests);
    std::ostringstream       out;
    const bool               started = mcp.listenStdio(in, out);
    std::vector<std::string> responses;
    if (started) {
        for (int attempt = 0; attempt < 400; ++attempt) {
            mcp.poll();
            responses.clear();
            std::istringstream collected(out.str());
            std::string        line;
            while (std::getline(collected, line)) {
                if (!line.empty()) responses.push_back(line);
            }
            if (responses.size() >= expectedResponses) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    mcp.stop();
    return responses;
}

[[maybe_unused]] Poco::JSON::Object::Ptr parseObject(const std::string& json) {
    Poco::JSON::Parser parser;
    return parser.parse(json).extract<Poco::JSON::Object::Ptr>();
}

/** Text payload of a tools/call response line (empty when the shape is wrong). */
[[maybe_unused]] std::string responseContentText(const std::string& line) {
    try {
        auto object = parseObject(line);
        if (!object || !object->has("result")) return {};
        auto result = object->getObject("result");
        if (!result || !result->has("content")) return {};
        auto content = result->getArray("content");
        if (!content || content->size() == 0) return {};
        auto item = content->getObject(0);
        if (!item || !item->has("text")) return {};
        return item->get("text").convert<std::string>();
    } catch (...) {
        return {};
    }
}

[[maybe_unused]] std::string initializeRequest(int id, const std::string& protocol) {
    return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) + ",\"method\":\"initialize\",\"params\":{" +
           "\"protocolVersion\":\"" + protocol + "\",\"capabilities\":{}," +
           "\"clientInfo\":{\"name\":\"mcp-console-test\",\"version\":\"0\"}}}";
}

[[maybe_unused]] std::string callRequest(int id, const std::string& name, const std::string& argumentsJson) {
    return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) + ",\"method\":\"tools/call\",\"params\":{\"name\":\"" +
           name + "\",\"arguments\":" + argumentsJson + "}}";
}

[[maybe_unused]] std::string joinRequests(const std::vector<std::string>& requests) {
    std::string out;
    for (const auto& request : requests) {
        out += request;
        out += '\n';
    }
    return out;
}

}  // namespace

TEST_CASE("devtools.mcp.consoleCursorSurvivesEvictionAndClear") {
    auto& console = ConsolePanel::instance();
    auto& dt      = DevTool::instance();
    dt.detach();
    console.setMaxEntries(500);
    console.clear();

    // Seed one line so the cursor under test is never the 0 tail-read sentinel.
    console.addInfo("seed");
    const std::uint64_t base = console.nextSeq();
    console.addInfo("first");
    console.addWarn("second");
    console.addError("third");

    // Tail read (cursor 0) returns the newest lines; explicit cursors are
    // exclusive and repeatable.
    const ConsoleSlice tail = console.read(0, 2, {});
    REQUIRE(tail.lines.size() == 2);
    CHECK(tail.lines[0].text == "second");
    CHECK(tail.lines[1].text == "third");
    CHECK(tail.lines[1].seq > tail.lines[0].seq);
    CHECK(tail.nextSeq == console.nextSeq());
    CHECK(tail.cursor == tail.lines.back().seq);
    CHECK(!tail.truncated);

    const ConsoleSlice fromBase = console.read(base - 1, 100, {});
    REQUIRE(fromBase.lines.size() == 3);
    CHECK(fromBase.lines.front().seq == base);
    CHECK(fromBase.lines.front().text == "first");
    CHECK(fromBase.cursor == fromBase.lines.back().seq);
    CHECK(!fromBase.truncated);

    // Resuming from the returned cursor yields exactly the lines not yet seen:
    // nextSeq is not a cursor, it is the seq the next appended line will get.
    CHECK(console.read(fromBase.cursor, 100, {}).lines.empty());
    console.addInfo("after");
    const ConsoleSlice resumed = console.read(fromBase.cursor, 100, {});
    REQUIRE(resumed.lines.size() == 1);
    CHECK(resumed.lines.front().text == "after");
    CHECK(resumed.cursor == resumed.lines.front().seq);

    const ConsoleSlice again = console.read(tail.lines.front().seq, 100, {});
    REQUIRE(again.lines.size() == 2);
    CHECK(again.lines.front().text == "third");

    const ConsoleSlice filtered = console.read(base - 1, 100, "error");
    REQUIRE(filtered.lines.size() == 1);
    CHECK(filtered.lines.front().text == "third");
    CHECK(filtered.lines.front().level == "error");

    // Eviction is observable: a cursor older than the retained window reports
    // truncated instead of silently returning a partial stream.
    console.setMaxEntries(2);
    console.addInfo("fourth");
    CHECK(console.droppedThrough() >= base + 1);
    const ConsoleSlice stale = console.read(base - 1, 100, {});
    CHECK(stale.truncated);
    CHECK(stale.lines.front().text == "after");

    // clear() never rewinds the cursor, so a reader cannot stall or re-read.
    const std::uint64_t before = console.nextSeq();
    console.clear();
    CHECK(console.nextSeq() == before);
    CHECK(console.droppedThrough() == before - 1);
    const ConsoleSlice empty = console.read(before, 10, {});
    CHECK(empty.lines.empty());
    CHECK(!empty.truncated);

    console.setMaxEntries(500);
    console.clear();
}
}  // namespace test_mcp_console_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_mcp_tool_filter)
#include "PrivateTestScope.h"
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "devtools/McpServer.hpp"
#include "devtools/McpToolFilter.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// mcp_tool_filter.cpp
namespace test_mcp_tool_filter_unit {
// MCP toolset / read-only / exclude filters: tools/list and tools/call must agree.


using namespace eve::dev;

namespace {

#if defined(_WIN32)
[[maybe_unused]] int  setEnv(const char* key, const char* value) { return _putenv_s(key, value ? value : ""); }
[[maybe_unused]] void unsetEnv(const char* key) { _putenv_s(key, ""); }
#else
[[maybe_unused]] int  setEnv(const char* key, const char* value) { return setenv(key, value, 1); }
[[maybe_unused]] void unsetEnv(const char* key) { unsetenv(key); }
#endif

struct EnvGuard {
    explicit EnvGuard(std::initializer_list<std::pair<const char*, const char*>> values) {
        for (const auto& [key, value] : values) {
            keys.push_back(key);
            if (value)
                setEnv(key, value);
            else
                unsetEnv(key);
        }
        mcpToolFilterReloadForTest();
    }
    ~EnvGuard() {
        for (const char* key : keys) unsetEnv(key);
        mcpToolFilterReloadForTest();
    }
    std::vector<const char*> keys;
};

[[maybe_unused]] std::vector<std::string> runStdioSession(const std::string& requests, std::size_t expectedResponses) {
    auto& mcp = McpServer::instance();
    mcp.stop();
    std::istringstream       in(requests);
    std::ostringstream       out;
    const bool               started = mcp.listenStdio(in, out);
    std::vector<std::string> responses;
    if (started) {
        for (int attempt = 0; attempt < 400; ++attempt) {
            mcp.poll();
            responses.clear();
            std::istringstream collected(out.str());
            std::string        line;
            while (std::getline(collected, line)) {
                if (!line.empty()) responses.push_back(line);
            }
            if (responses.size() >= expectedResponses) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    mcp.stop();
    return responses;
}

[[maybe_unused]] bool toolsListContains(const std::string& name) {
    const std::string requests =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\","
        "\"capabilities\":{},\"clientInfo\":{\"name\":\"filter-test\"}}}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{}}\n";
    const auto responses = runStdioSession(requests, 2);
    if (responses.size() < 2) return false;
    Poco::JSON::Parser parser;
    auto               object = parser.parse(responses[1]).extract<Poco::JSON::Object::Ptr>();
    auto               result = object ? object->getObject("result") : Poco::JSON::Object::Ptr();
    auto               tools  = result ? result->getArray("tools") : Poco::JSON::Array::Ptr();
    if (!tools) return false;
    for (std::size_t i = 0; i < tools->size(); ++i) {
        auto tool = tools->getObject(static_cast<unsigned>(i));
        if (tool && tool->getValue<std::string>("name") == name) return true;
    }
    return false;
}

[[maybe_unused]] std::string callToolText(const std::string& name) {
    const std::string requests =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\","
        "\"capabilities\":{},\"clientInfo\":{\"name\":\"filter-test\"}}}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"" +
        name + "\",\"arguments\":{}}}\n";
    const auto responses = runStdioSession(requests, 2);
    if (responses.size() < 2) return {};
    Poco::JSON::Parser parser;
    auto               object  = parser.parse(responses[1]).extract<Poco::JSON::Object::Ptr>();
    auto               result  = object ? object->getObject("result") : Poco::JSON::Object::Ptr();
    auto               content = result ? result->getArray("content") : Poco::JSON::Array::Ptr();
    if (!content || content->size() == 0) return {};
    return content->getObject(0)->getValue<std::string>("text");
}

}  // namespace

TEST_CASE("devtools.mcp.toolTagsClassifyFamilies") {
    CHECK(mcpToolTags("eve_status").size() == 1);
    CHECK_EQ(mcpToolTags("eve_status")[0], std::string("core"));
    CHECK(mcpToolTags("eve_eval")[0] == "debug");
    CHECK(mcpToolTags("eve_input_batch")[0] == "input");
    CHECK(mcpToolTags("eve_whitebox_playtest")[0] == "input");
    CHECK(mcpToolMutates("eve_scene_batch"));
    CHECK(mcpToolMutates("eve_whitebox_playtest"));
    CHECK(mcpToolTags("eve_host_status")[0] == "host");
    CHECK(mcpToolMutates("eve_eval"));
    CHECK(!mcpToolMutates("eve_status"));
    CHECK(!mcpToolMutates("eve_input_poll"));
}
}  // namespace test_mcp_tool_filter_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_play_trace)
#include "PrivateTestScope.h"
#include <string>
#include <vector>
#include "common/Capability.h"
#include "common/Diagnostic.h"
#include "common/GameplayControl.h"
#include "common/GameplayControlJson.h"
#include "common/Value.h"
#include "devtools/AgentDevelopmentSession.hpp"
#include "devtools/PlayHost.h"
#include "devtools/PlayTrace.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// play_trace.cpp
namespace test_play_trace {
using namespace eve;
using namespace eve::dev;

namespace {

class FakePlayHostRuntime final : public IPlayHostRuntime {
public:
    bool          paused() const override { return paused_; }
    std::uint64_t hostFrame() const override { return hostFrame_; }
    void          pause() override { paused_ = true; }
    void          play() override { paused_ = false; }

    Result<void> stepFrames(std::int64_t count) override {
        hostFrame_ += static_cast<std::uint64_t>(count);
        paused_ = true;
        return Result<void>::success();
    }

    Result<Value> loadContract() const override { return Result<Value>::success(contract); }

    Result<Value> observeScriptRoot(std::string_view root, const std::vector<std::string>& fields) const override {
        const auto* object = roots.getIf<Value::Object>();
        REQUIRE(object);
        const auto found = object->find(std::string(root));
        REQUIRE(found != object->end());
        if (fields.empty()) return Result<Value>::success(found->second);
        Value::Object projected;
        for (const auto& field : fields) {
            const auto* node = found->second.getIf<Value::Object>();
            REQUIRE(node);
            auto child = node->find(field);
            REQUIRE(child != node->end());
            projected.emplace(field, child->second);
        }
        return Result<Value>::success(Value(std::move(projected)));
    }

    Result<Value> capturePng(std::string path) override {
        return Result<Value>::success(Value(Value::Object{
            {"height", Value(std::int64_t{2})}, {"path", Value(std::move(path))}, {"width", Value(std::int64_t{3})}}));
    }

    Result<std::string> captureCheckpoint() override { return Result<std::string>::success(checkpoint); }

    Result<void> restoreCheckpoint(std::string_view json) override {
        auto parsed = Value::fromJson(json);
        REQUIRE(parsed.ok());
        const auto* object = parsed.value().getIf<Value::Object>();
        REQUIRE(object);
        const auto found = object->find("roots");
        REQUIRE(found != object->end());
        roots = found->second;
        return Result<void>::success();
    }

    std::vector<std::string> gameplayDomains() const override { return {"json-fixture"}; }

    Result<Value> invokeScriptAction(std::string_view id, std::string_view source) override {
        lastAction = std::string(id);
        lastScript = std::string(source);
        if (id == "reset") {
            auto* object = roots.getIf<Value::Object>();
            if (object) {
                auto gameState = object->find("gameState");
                if (gameState != object->end()) {
                    auto* state = gameState->second.getIf<Value::Object>();
                    if (state) {
                        Value::Object copy     = *state;
                        copy["tick"]           = Value(std::int64_t{0});
                        (*object)["gameState"] = Value(std::move(copy));
                    }
                }
            }
        }
        return Result<Value>::success(Value(Value::Object{{"action", Value(std::string(id))}}));
    }

    Value       contract;
    Value       roots;
    std::string checkpoint;
    std::string lastAction;
    std::string lastScript;

private:
    bool          paused_    = false;
    std::uint64_t hostFrame_ = 0;
};

Value parse(std::string_view json) {
    auto value = Value::fromJson(json);
    REQUIRE(value.ok());
    return std::move(value).takeValue();
}

SubjectRef subject(const char* text) {
    const auto id = PersistentId::parse(text);
    REQUIRE(id.has_value());
    return SubjectRef::fromPersistentId(*id);
}

LogicalId logical(const char* text) {
    const auto id = LogicalId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

class JsonProvider final : public IGameplayControlProvider {
public:
    explicit JsonProvider(SubjectRef instance) : instance_(instance) {
        cap::addListener<IGameplayControlProvider>(this);
    }
    ~JsonProvider() override { cap::removeListener<IGameplayControlProvider>(this); }

    std::string_view                          gameplayDomain() const noexcept override { return "json-fixture"; }
    [[nodiscard]] Result<GameplayObservation> observeGameplay(const GameplaySession&,
                                                              SubjectRef instance) const override {
        if (instance != instance_)
            return Result<GameplayObservation>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "fixture not found", "instance"));
        GameplayObservation result;
        result.domain   = logical("gameplay:json-fixture");
        result.instance = instance_;
        result.tick     = tick_;
        result.revision = revision_;
        result.state    = Value(Value::Object{{"count", Value(count_)}});
        return Result<GameplayObservation>::success(std::move(result));
    }
    [[nodiscard]] Result<std::vector<GameplayActionDescriptor>> availableGameplayActions(const GameplaySession&,
                                                                                         SubjectRef,
                                                                                         SubjectRef) const override {
        return Result<std::vector<GameplayActionDescriptor>>::success(
            {{logical("fixture:increment"), Value(Value::Object{})}});
    }
    [[nodiscard]] Result<GameplayCommandReceipt> submitGameplay(const GameplaySession&, SubjectRef,
                                                                const GameplayCommand& command) override {
        ++count_;
        ++revision_;
        GameplayCommandReceipt result;
        result.commandId         = command.id;
        result.executionId       = "fixture-execution";
        result.acceptedTick      = tick_;
        result.resultingRevision = revision_;
        result.details           = Value(Value::Object{{"count", Value(count_)}});
        return Result<GameplayCommandReceipt>::success(std::move(result));
    }
    [[nodiscard]] Result<GameplayObservation> advanceGameplay(const GameplaySession& session, SubjectRef instance,
                                                              const SimulationStep& step) override {
        tick_ = step.tick;
        return observeGameplay(session, instance);
    }
    [[nodiscard]] Result<std::vector<GameplayEvent>> gameplayEvents(const GameplaySession&, SubjectRef,
                                                                    std::uint64_t) const override {
        return Result<std::vector<GameplayEvent>>::success({});
    }

private:
    SubjectRef     instance_;
    SimulationTick tick_     = SimulationTick::zero();
    std::uint64_t  revision_ = 1;
    int            count_    = 0;
};

const char* kScriptContract = R"json({
  "schemaId": "evengine.game-agent-contract",
  "schemaVersion": 1,
  "id": "examples/ai-game",
  "entry": "main.nut",
  "clock": {"defaultStep": "frame"},
  "observations": [
    {"id": "combat-alive", "kind": "script-root", "path": "gameState", "fields": ["tick"]}
  ],
  "actions": {"source": "script-map", "map": [{"id": "reset", "script": "game.reset()"}]},
  "capture": {"requiredFor": ["visual"], "backend": "engine-readback"}
})json";

const char* kGameplayContract = R"json({
  "schemaId": "evengine.game-agent-contract",
  "schemaVersion": 1,
  "id": "json-fixture-play",
  "entry": "main.nut",
  "clock": {"defaultStep": "frame"},
  "observations": [
    {"id": "combat-alive", "kind": "script-root", "path": "gameState", "fields": ["tick"]}
  ],
  "actions": {
    "source": "gameplay-domain",
    "domain": "json-fixture",
    "map": [{"id": "increment", "action": "fixture:increment"}]
  }
})json";

}  // namespace

TEST_CASE("devtools.playTrace.rejectsUnknownFieldsAndReplaysStableDigest") {
    PlayTraceBuffer::instance().clear();
    FakePlayHostRuntime runtime;
    runtime.contract   = parse(kScriptContract);
    runtime.roots      = parse(R"({"gameState":{"tick":4}})");
    runtime.checkpoint = R"({"version":2,"roots":{"gameState":{"tick":4}}})";

    auto unknown =
        parsePlayTrace(parse(R"({"schemaId":"evengine.play-trace","schemaVersion":1,"steps":[],"extra":true})"));
    CHECK(!unknown.ok());
    CHECK_EQ(unknown.status().diagnostics().front().code(), DiagnosticCode::ParseError);

    auto observed = executePlayRequest(
        parse(
            R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"observe","observation":"combat-alive","trace":"append"})"),
        runtime);
    REQUIRE(observed.ok());
    auto acted = executePlayRequest(
        parse(R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"act","action":"reset","trace":"append"})"),
        runtime);
    REQUIRE(acted.ok());
    CHECK_EQ(runtime.lastAction, std::string("reset"));
    CHECK_EQ(runtime.lastScript, std::string("game.reset()"));
    auto afterReset = executePlayRequest(
        parse(
            R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"observe","observation":"combat-alive","trace":"append"})"),
        runtime);
    REQUIRE(afterReset.ok());
    CHECK_EQ(afterReset.value().getIf<Value::Object>()->at("state").getIf<Value::Object>()->at("tick").asInt(),
             std::int64_t{0});

    auto        exported    = PlayTraceBuffer::instance().exportTrace();
    const auto* traceObject = exported.getIf<Value::Object>();
    REQUIRE(traceObject);
    Value recording = exported;
    recording.set("startCheckpoint", Value(std::string(R"({"version":2,"roots":{"gameState":{"tick":4}}})")));

    runtime.roots = parse(R"({"gameState":{"tick":99}})");
    auto first    = replayPlayTrace(recording, runtime);
    REQUIRE(first.ok());
    auto digestA = playObservationDigest(runtime.roots.getIf<Value::Object>()->at("gameState"));
    REQUIRE(digestA.ok());

    runtime.roots = parse(R"({"gameState":{"tick":77}})");
    auto second   = replayPlayTrace(recording, runtime);
    REQUIRE(second.ok());
    auto digestB = playObservationDigest(runtime.roots.getIf<Value::Object>()->at("gameState"));
    REQUIRE(digestB.ok());
    CHECK_EQ(digestA.value(), digestB.value());

    PlayTraceBuffer::instance().clear();
    runtime.roots = parse(R"({"gameState":{"tick":4}})");
    REQUIRE(
        executePlayRequest(
            parse(
                R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"observe","observation":"combat-alive","trace":"append"})"),
            runtime)
            .ok());
    auto passOne = PlayTraceBuffer::instance().exportTrace();
    PlayTraceBuffer::instance().clear();
    runtime.roots = parse(R"({"gameState":{"tick":4}})");
    REQUIRE(
        executePlayRequest(
            parse(
                R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"observe","observation":"combat-alive","trace":"append"})"),
            runtime)
            .ok());
    auto              passTwo     = PlayTraceBuffer::instance().exportTrace();
    const std::string firstDigest = passOne.getIf<Value::Object>()
                                        ->at("steps")
                                        .getIf<Value::Array>()
                                        ->at(0)
                                        .getIf<Value::Object>()
                                        ->at("observationDigest")
                                        .asString();
    const std::string secondDigest = passTwo.getIf<Value::Object>()
                                         ->at("steps")
                                         .getIf<Value::Array>()
                                         ->at(0)
                                         .getIf<Value::Object>()
                                         ->at("observationDigest")
                                         .asString();
    CHECK_EQ(firstDigest, secondDigest);
}

TEST_CASE("devtools.playHost.actMatchesGameplayControlSubmit") {
    const auto          instance     = subject("00000000-0000-7000-8000-000000000a01");
    const std::string   instanceText = instance.format();
    FakePlayHostRuntime runtime;
    runtime.contract = parse(kGameplayContract);
    runtime.roots    = parse(R"({"gameState":{"tick":1}})");

    Value::Object command{{"id", Value("json-command-1")},
                          {"action", Value("fixture:increment")},
                          {"subject", Value(instanceText)},
                          {"observedTick", Value(std::int64_t{0})},
                          {"expectedRevision", Value(std::int64_t{1})},
                          {"parameters", Value(Value::Object{})}};
    Value         session{Value::Object{{"id", Value("test")},
                                        {"access", Value("player")},
                                        {"controlledSubjects", Value(Value::Array{Value(instanceText)})}}};

    std::int64_t playRevision = 0;
    {
        JsonProvider provider(instance);
        Value        playRequest{Value::Object{
                   {"schemaId", Value("evengine.play-request")},
                   {"schemaVersion", Value(std::int64_t{1})},
                   {"op", Value("act")},
                   {"action", Value("increment")},
                   {"instance", Value(instanceText)},
                   {"session", session},
                   {"command", Value(command)},
        }};
        auto         viaPlay = executePlayRequest(playRequest, runtime);
        REQUIRE(viaPlay.ok());
        playRevision = viaPlay.value()
                           .getIf<Value::Object>()
                           ->at("gameplay")
                           .getIf<Value::Object>()
                           ->at("receipt")
                           .getIf<Value::Object>()
                           ->at("resultingRevision")
                           .asInt();
    }
    std::int64_t jsonRevision = 0;
    {
        JsonProvider provider(instance);
        Value        gameplayRequest{Value::Object{
                   {"schemaId", Value("evengine.gameplay-control-request")},
                   {"schemaVersion", Value(std::int64_t{1})},
                   {"op", Value("submit")},
                   {"domain", Value("json-fixture")},
                   {"instance", Value(instanceText)},
                   {"session", session},
                   {"command", Value(command)},
        }};
        auto         viaJson = executeGameplayControlRequest(gameplayRequest);
        REQUIRE(viaJson.ok());
        jsonRevision = viaJson.value()
                           .getIf<Value::Object>()
                           ->at("receipt")
                           .getIf<Value::Object>()
                           ->at("resultingRevision")
                           .asInt();
    }
    CHECK_EQ(playRevision, jsonRevision);
    CHECK_EQ(playRevision, std::int64_t{2});
}

TEST_CASE("devtools.playHost.sessionAutoRecordsEvidenceWithoutEval") {
    PlayTraceBuffer::instance().clear();
    AgentDevelopmentSession::instance().reset();
    FakePlayHostRuntime runtime;
    runtime.contract = parse(kScriptContract);
    runtime.roots    = parse(R"({"gameState":{"tick":4}})");

    auto& session = AgentDevelopmentSession::instance();
    REQUIRE(session
                .start("Play a mapped action", {{"combat-alive", "Combat state is observable", true, false},
                                                {"visual", "Frame is captured", true, false}})
                .isAccepted());
    const std::string id = session.sessionId();
    REQUIRE(session.advance(id, AgentDevelopmentPhase::Modify).isAccepted());
    REQUIRE(session.advance(id, AgentDevelopmentPhase::Run).isAccepted());
    REQUIRE(session.advance(id, AgentDevelopmentPhase::Observe).isAccepted());

    REQUIRE(
        executePlayRequest(
            parse(
                R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"observe","observation":"combat-alive"})"),
            runtime)
            .ok());
    REQUIRE(executePlayRequest(
                parse(R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"capture","path":"frame.png"})"),
                runtime)
                .ok());
    bool sawObserve = false;
    bool sawShot    = false;
    for (const auto& evidence : session.evidence()) {
        if (evidence.kind == "runtime-observation" && evidence.criterionId == "combat-alive") sawObserve = true;
        if (evidence.kind == "screenshot" && evidence.criterionId == "visual") sawShot = true;
        CHECK_NE(evidence.kind, std::string("eval"));
    }
    CHECK(sawObserve);
    CHECK(sawShot);
    REQUIRE(session.advance(id, AgentDevelopmentPhase::Verify).isAccepted());
    REQUIRE(session.complete(id, "play evidence closed the session").isAccepted());
    session.reset();
    PlayTraceBuffer::instance().clear();
}
}  // namespace test_play_trace
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_game_world_context)
#include <string>
#include <vector>
#include "PrivateTestScope.h"
#include "common/Value.h"
#include "devtools/GameWorldContext.hpp"
#include "devtools/PlayHost.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// game_world_context.cpp
namespace test_game_world_context {
using namespace eve;
using namespace eve::dev;

namespace {

Value parse(std::string_view json) {
    auto value = Value::fromJson(json);
    REQUIRE(value.ok());
    return std::move(value).takeValue();
}

}  // namespace

TEST_CASE("devtools.gameWorld.parseRejectsUnknownAndEmptyWorld") {
    clearWorldContext();
    auto unknown = parseWorldObject(parse(R"({"title":"X","extra":true})"));
    CHECK(!unknown.ok());
    CHECK_EQ(unknown.status().diagnostics().front().code(), DiagnosticCode::ParseError);

    auto empty = parseWorldObject(parse(R"({})"));
    CHECK(!empty.ok());
    CHECK_EQ(empty.status().diagnostics().front().code(), DiagnosticCode::ParseError);

    auto badDim = parseWorldObject(parse(R"({"title":"X","dimensions":"4d"})"));
    CHECK(!badDim.ok());
}

TEST_CASE("devtools.gameWorld.configTitleAndModulesWithoutPublish") {
    clearWorldContext();
    CHECK(!hasWorldContext());

    GameWorldSnapshotInput input;
    input.contractId        = "examples/test";
    input.contractLoaded    = true;
    input.gameRoot          = "/tmp/game";
    input.configTitle       = "EVEngine AI Game";
    input.domains           = {"inventory"};
    input.configuredModules = {"physics", "particles"};
    input.liveModules       = {"physics", "ui", "win", "gfx"};

    auto snapshot = buildGameWorldSnapshot(input);
    REQUIRE(snapshot.ok());
    const auto* root = snapshot.value().getIf<Value::Object>();
    REQUIRE(root);
    CHECK_EQ(root->at("schemaId").asString(), std::string(kGameWorldSchemaId));
    CHECK_EQ(root->at("source").asString(), std::string("config"));
    CHECK_EQ(root->at("configTitle").asString(), std::string("EVEngine AI Game"));
    CHECK_EQ(root->at("world").getIf<Value::Object>()->at("title").asString(), std::string("EVEngine AI Game"));
    CHECK_EQ(root->at("configuredModules").getIf<Value::Array>()->size(), std::size_t{2});
    CHECK_EQ(root->at("liveModules").getIf<Value::Array>()->size(), std::size_t{4});
}

TEST_CASE("devtools.gameWorld.publishRuntimeBriefAndClear") {
    clearWorldContext();
    CHECK(!hasWorldContext());

    auto published = publishWorldContext(parse(R"({
      "title":"Runtime World",
      "summary":"Published after systems init",
      "systems":["physics","ui"],
      "tags":["runtime"],
      "dimensions":"2d"
    })"));
    REQUIRE(published.ok());
    CHECK(hasWorldContext());

    GameWorldSnapshotInput input;
    input.contractId        = "examples/test";
    input.contractLoaded    = true;
    input.gameRoot          = "/tmp/game";
    input.configTitle       = "From Config";
    input.domains           = {"inventory"};
    input.configuredModules = {"physics", "particles"};
    input.liveModules       = {"physics", "ui", "win", "gfx"};

    auto snapshot = buildGameWorldSnapshot(input);
    REQUIRE(snapshot.ok());
    const auto* root = snapshot.value().getIf<Value::Object>();
    REQUIRE(root);
    CHECK_EQ(root->at("schemaId").asString(), std::string(kGameWorldSchemaId));
    CHECK_EQ(root->at("source").asString(), std::string("runtime"));
    CHECK_EQ(root->at("contractId").asString(), std::string("examples/test"));
    const auto* world = root->at("world").getIf<Value::Object>();
    REQUIRE(world);
    CHECK_EQ(world->at("title").asString(), std::string("Runtime World"));
    CHECK_EQ(world->at("dimensions").asString(), std::string("2d"));
    const auto* systems = world->at("systems").getIf<Value::Array>();
    REQUIRE(systems);
    CHECK_EQ(systems->size(), std::size_t{2});
    CHECK_EQ(root->at("configuredModules").getIf<Value::Array>()->size(), std::size_t{2});
    CHECK_EQ(root->at("liveModules").getIf<Value::Array>()->size(), std::size_t{4});

    // Summary-only publish still fills title from config.
    clearWorldContext();
    REQUIRE(publishWorldContext(parse(R"({"summary":"Narrative only"})")).ok());
    auto merged = buildGameWorldSnapshot(input);
    REQUIRE(merged.ok());
    CHECK_EQ(merged.value().getIf<Value::Object>()->at("source").asString(), std::string("runtime"));
    CHECK_EQ(merged.value().getIf<Value::Object>()->at("world").getIf<Value::Object>()->at("title").asString(),
             std::string("From Config"));
    CHECK_EQ(merged.value().getIf<Value::Object>()->at("world").getIf<Value::Object>()->at("summary").asString(),
             std::string("Narrative only"));

    clearWorldContext();
    CHECK(!hasWorldContext());
    input.configTitle.clear();
    auto afterClear = buildGameWorldSnapshot(input);
    REQUIRE(afterClear.ok());
    CHECK_EQ(afterClear.value().getIf<Value::Object>()->at("source").asString(), std::string("none"));
    CHECK(afterClear.value().getIf<Value::Object>()->at("world").isNull());
}

TEST_CASE("devtools.playHost.statusIncludesRuntimeGameWorld") {
    clearWorldContext();
    REQUIRE(publishWorldContext(parse(R"({
      "title":"AI Game",
      "summary":"Minimal duel after systems init",
      "systems":["ui"]
    })"))
                .ok());

    class Runtime final : public IPlayHostRuntime {
    public:
        bool          paused() const override { return false; }
        std::uint64_t hostFrame() const override { return 3; }
        void          pause() override {}
        void          play() override {}
        Result<void>  stepFrames(std::int64_t) override { return Result<void>::success(); }
        Result<Value> loadContract() const override { return Result<Value>::success(contract); }
        Result<Value> observeScriptRoot(std::string_view, const std::vector<std::string>&) const override {
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::Unsupported, "unused", "observation"));
        }
        Result<Value> capturePng(std::string) override {
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::Unsupported, "unused", "capture"));
        }
        Result<std::string> captureCheckpoint() override {
            return Result<std::string>::failure(Diagnostic::error(DiagnosticCode::Unsupported, "unused", "checkpoint"));
        }
        Result<void> restoreCheckpoint(std::string_view) override {
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, "unused", "checkpoint"));
        }
        std::vector<std::string> gameplayDomains() const override { return {"tactics"}; }
        Result<Value>            invokeScriptAction(std::string_view, std::string_view) override {
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::Unsupported, "unused", "action"));
        }
        Value contract = parse(R"json({
          "schemaId": "evengine.game-agent-contract",
          "schemaVersion": 1,
          "id": "examples/ai-game",
          "entry": "main.nut",
          "clock": {"defaultStep": "frame"},
          "observations": [
            {"id": "combat-alive", "kind": "script-root", "path": "gameState", "fields": ["tick"]}
          ]
        })json");
    } runtime;

    auto status =
        executePlayRequest(parse(R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"status"})"), runtime);
    REQUIRE(status.ok());
    const auto* object = status.value().getIf<Value::Object>();
    REQUIRE(object);
    CHECK(object->at("contractLoaded").asBool());
    CHECK_EQ(object->at("contractId").asString(), std::string("examples/ai-game"));
    const auto* gameWorld = object->at("gameWorld").getIf<Value::Object>();
    REQUIRE(gameWorld);
    CHECK_EQ(gameWorld->at("source").asString(), std::string("runtime"));
    CHECK_EQ(gameWorld->at("world").getIf<Value::Object>()->at("title").asString(), std::string("AI Game"));

    // Contract must reject a static world field — agents read files / runtime publish instead.
    runtime.contract = parse(R"json({
      "schemaId": "evengine.game-agent-contract",
      "schemaVersion": 1,
      "id": "examples/ai-game",
      "entry": "main.nut",
      "world": {"title":"should-fail"},
      "observations": [
        {"id": "combat-alive", "kind": "script-root", "path": "gameState"}
      ]
    })json");
    auto rejected =
        executePlayRequest(parse(R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"status"})"), runtime);
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.status().diagnostics().front().code(), DiagnosticCode::ParseError);

    clearWorldContext();
}
}  // namespace test_game_world_context
#endif
