// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "agent_development_session.cpp",
    "callgraph.cpp",
    "console.cpp",
    "debugger.cpp",
    "mcp_architecture_graph.cpp",
    "mcp_whitebox.cpp",
    "frame_stats.cpp",
    "language_index.cpp",
    "language_text.cpp",
    "mcp_console.cpp",
    "mcp_tool_filter.cpp",
    "play_trace.cpp",
    "game_world_context.cpp",
    "mcp_frame_capture.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/unittest.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_agent_development_session)
#include <Poco/JSON/Parser.h>
#include "devtools/AgentDevelopmentMcp.hpp"
#include "devtools/AgentDevelopmentSession.hpp"
#include "devtools/McpTaskProfile.hpp"

namespace test_agent_development_contract {
TEST_CASE("devtools.AgentDevelopmentSession workflow: transitions and evidence gate completion") {
    auto& session = eve::dev::AgentDevelopmentSession::instance();
    session.reset();
    REQUIRE(session
                .start("Verify one feature",
                       {{"runtime", "State advances", true, false}, {"visual", "Frame is legible", true, false}})
                .isAccepted());
    const auto id             = session.sessionId();
    const auto invalidAdvance = session.advance(id, eve::dev::AgentDevelopmentPhase::Run);
    CHECK_EQ(invalidAdvance.code, "invalid-transition");
    REQUIRE(session.advance(id, eve::dev::AgentDevelopmentPhase::Modify).isAccepted());
    REQUIRE(session.advance(id, eve::dev::AgentDevelopmentPhase::Run).isAccepted());
    REQUIRE(session.advance(id, eve::dev::AgentDevelopmentPhase::Observe).isAccepted());
    CHECK_EQ(session.record(id, {0, "runtime", "eval", "pass", "unsafe evidence", {}}).code, "invalid-evidence-kind");
    REQUIRE(session.record(id, {0, "runtime", "runtime-observation", "pass", "tick moved", {}}).isAccepted());
    REQUIRE(session.record(id, {0, "visual", "manual", "pass", "looks fine", {}}).isAccepted());
    REQUIRE(session.advance(id, eve::dev::AgentDevelopmentPhase::Verify).isAccepted());
    CHECK_EQ(session.complete(id, "too early").code, "acceptance-incomplete");
    REQUIRE(session.record(id, {0, "visual", "screenshot", "pass", "frame inspected", "frame.png"}).isAccepted());
    CHECK(session.readyToComplete());
    REQUIRE(session.complete(id, "verified").isAccepted());
    CHECK(!session.active());
    CHECK_EQ(eve::dev::agentDevelopmentPhaseName(session.phase()), std::string_view("complete"));
}

TEST_CASE("devtools.AgentDevelopmentSession profile: verify defaults restrict tools and escalation is explicit") {
    auto& session = eve::dev::AgentDevelopmentSession::instance();
    session.reset();
    REQUIRE(session.start("Narrow by default", {{"play", "Playable", true, false}}).isAccepted());
    CHECK_EQ(session.taskKind(), "verify");
    CHECK_EQ(session.profile(), "verify.play");
    CHECK(eve::dev::mcpProfileAllowsTool(session.profile(), "eve_play"));
    CHECK(!eve::dev::mcpProfileAllowsTool(session.profile(), "eve_scene_batch"));
    CHECK(!eve::dev::mcpProfileAllowsTool(session.profile(), "eve_eval"));
    const auto id = session.sessionId();
    CHECK_EQ(session.escalate(id, "no.such").code, "invalid-profile");
    REQUIRE(session.escalate(id, "content.scene").isAccepted());
    CHECK_EQ(session.taskKind(), "content");
    CHECK_EQ(session.profile(), "content.scene");
    CHECK_EQ(session.escalateCount(), std::uint64_t{1});
    session.reset();
}

TEST_CASE("devtools.AgentDevelopment MCP start: response exposes versioned session snapshot") {
    eve::dev::AgentDevelopmentSession::instance().reset();
    Poco::JSON::Parser parser;
    auto               arguments =
        parser
            .parse(
                R"({"objective":"Ship a playable loop","criteria":[{"id":"play","description":"Loop is playable"}]})")
            .extract<Poco::JSON::Object::Ptr>();
    auto response = parser.parse(eve::dev::callAgentDevelopmentTool("eve_agent_session_start", arguments))
                        .extract<Poco::JSON::Object::Ptr>();
    REQUIRE(response);
    CHECK_EQ(response->getValue<std::string>("status"), "applied");
    CHECK_EQ(response->getValue<std::string>("schema"), "eve.agent-development-session");
    CHECK_EQ(response->getValue<int>("schemaVersion"), 2);
    CHECK_EQ(response->getValue<std::string>("phase"), "discover");
    CHECK_EQ(response->getArray("criteria")->size(), std::size_t{1});
}
}  // namespace test_agent_development_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_callgraph)
#include "devtools/CallGraph.hpp"

namespace test_callgraph_contract {
eve::dev::SourceLoc location(const char* source, int line, const char* function = "") {
    eve::dev::SourceLoc result;
    result.source   = source;
    result.line     = line;
    result.function = function;
    return result;
}

TEST_CASE("devtools.CallGraph calls: stack and edges record entry/return in order") {
    eve::dev::CallGraph graph;
    graph.onCall(location("game.nut", 1, "main"), "main");
    graph.onCall(location("game.nut", 2, "attack"), "attack");
    const auto inside = graph.events().back().id;
    graph.onReturn(location("game.nut", 3, "attack"), "attack");
    REQUIRE_EQ(graph.currentStack().size(), 1u);
    CHECK_EQ(graph.currentStack()[0].loc.function, std::string("main"));
    REQUIRE_EQ(graph.stackAt(inside).size(), 2u);
    CHECK_EQ(graph.stackAt(inside)[1].loc.function, std::string("attack"));
    REQUIRE_EQ(graph.callEdges().size(), 1u);
    CHECK_EQ(graph.callEdges()[0].second.function, std::string("attack"));
    graph.clear();
    CHECK_EQ(graph.eventCount(), 0u);
    CHECK(graph.currentStack().empty());
}

TEST_CASE("devtools.CallGraph sliceBackward: latest definition reaches selected use") {
    eve::dev::CallGraph graph;
    graph.onCall(location("game.nut", 1, "main"), "main");
    graph.onDef(location("game.nut", 2, "main"), "health");
    graph.onDef(location("game.nut", 3, "main"), "health");
    graph.onUse(location("game.nut", 4, "main"), "health");
    graph.onLine(location("game.nut", 5, "main"));
    eve::dev::SliceCriterion criterion;
    criterion.loc = location("game.nut", 5, "main");
    criterion.variables.push_back("health");
    auto slice    = graph.sliceBackward(criterion);
    bool latest   = false;
    bool obsolete = false;
    for (const auto& place : slice.locations) {
        if (place.line == 3) latest = true;
        if (place.line == 2) obsolete = true;
    }
    REQUIRE(latest);
    CHECK(!obsolete);
}

TEST_CASE("devtools.CallGraph event ring: capacity keeps newest IDs without stale lookup") {
    eve::dev::CallGraph graph;
    graph.setMaxEvents(3);
    graph.onCall(location("game.nut", 1, "main"), "main");
    const auto old = graph.events().back().id;
    for (int line = 2; line <= 6; ++line) graph.onLine(location("game.nut", line, "main"));
    CHECK_EQ(graph.eventCount(), 3u);
    CHECK(graph.event(old) == nullptr);
    REQUIRE(graph.event(graph.events().back().id) != nullptr);
    CHECK_EQ(graph.event(graph.events().back().id)->loc.line, 6);
}
}  // namespace test_callgraph_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_console)
#include "devtools/ConsolePanel.hpp"
namespace test_console_contract {
TEST_CASE("devtools.ConsolePanel records severity in order and returns newest entries") {
    auto& panel = eve::dev::ConsolePanel::instance();
    panel.clear();
    panel.addInfo("first");
    panel.addWarn("second");
    panel.addError("third");
    const auto newest = panel.recent(2);
    REQUIRE_EQ(newest.size(), 2u);
    CHECK_EQ(newest[0].text, std::string("second"));
    CHECK_EQ(newest[1].text, std::string("third"));
    CHECK(panel.format(3).find("first") != std::string::npos);
    panel.clear();
    CHECK(panel.recent(3).empty());
}

TEST_CASE("devtools.ConsolePanel evicts oldest entries and exposes visibility") {
    auto& panel = eve::dev::ConsolePanel::instance();
    panel.clear();
    panel.setMaxEntries(2);
    panel.setVisible(false);
    CHECK(!panel.isVisible());
    panel.toggleVisible();
    CHECK(panel.isVisible());
    panel.addInfo("discard");
    panel.addInfo("keep-one");
    panel.addInfo("keep-two");
    const auto entries = panel.recent(5);
    REQUIRE_EQ(entries.size(), 2u);
    CHECK_EQ(entries.front().text, std::string("keep-one"));
    CHECK_EQ(entries.back().text, std::string("keep-two"));
    panel.clear();
    panel.setMaxEntries(1000);
    panel.setVisible(false);
}
}  // namespace test_console_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_debugger)
#include "devtools/Debugger.hpp"
#include "devtools/Snapshot.hpp"
namespace test_debugger_contract {
using eve::dev::Debugger;
using eve::dev::PauseReason;
using eve::dev::RunMode;
using eve::dev::SourceLoc;

TEST_CASE("devtools.Debugger pause and frame step: one update then pause again") {
    auto& debugger = Debugger::instance();
    debugger.detach();
    REQUIRE(debugger.shouldRunUpdate());
    debugger.pause(PauseReason::PauseKey);
    CHECK(!debugger.shouldRunUpdate());
    debugger.stepFrame();
    CHECK(debugger.shouldRunUpdate());
    debugger.notifyFrameDone();
    CHECK(debugger.isPaused());
    CHECK_EQ(debugger.lastPauseReason(), PauseReason::Step);
    debugger.resume();
    CHECK(debugger.shouldRunUpdate());
}

TEST_CASE("devtools.Debugger breakpoints: path aliases and enabled state") {
    auto& debugger = Debugger::instance();
    debugger.detach();
    debugger.clearBreakpoints();
    debugger.setBreakpointsEnabled(true);
    const int id = debugger.setBreakpoint("scripts/main.nut", 42, true, "flag");
    REQUIRE(id > 0);
    CHECK(debugger.hasBreakpoint("main.nut", 42));
    CHECK(!debugger.hasBreakpoint("main.nut", 43));
    CHECK(!debugger.hasBreakpoint("other.nut", 42));
    REQUIRE_EQ(debugger.breakpoints().size(), 1u);
    CHECK_EQ(debugger.breakpoints().front().condition, std::string("flag"));
    CHECK_EQ(debugger.setBreakpoint("scripts/main.nut", 42, true, ""), id);
    CHECK(debugger.breakpoints().front().condition.empty());
    CHECK(debugger.setBreakpointEnabled(id, false));
    CHECK(!debugger.setBreakpointEnabled(-1, false));
    SourceLoc location;
    location.source = "main.nut";
    location.line   = 42;
    CHECK(!debugger.onScriptLine(location));
    CHECK(debugger.setBreakpointEnabled(id, true));
    CHECK(debugger.onScriptLine(location));
    debugger.resume();
    debugger.clearBreakpoints("main.nut");
    CHECK(!debugger.hasBreakpoint("scripts/main.nut", 42));
}

TEST_CASE("devtools.Debugger step over: duplicate source line does not stop") {
    auto& debugger = Debugger::instance();
    debugger.detach();
    debugger.clearBreakpoints();
    debugger.setBreakpoint("main.nut", 10);
    SourceLoc location;
    location.source = "main.nut";
    location.line   = 10;
    REQUIRE(debugger.onScriptLine(location));
    debugger.stepOver();
    CHECK(!debugger.onScriptLine(location));
    location.line = 11;
    CHECK(debugger.onScriptLine(location));
    CHECK_EQ(debugger.lastPauseReason(), PauseReason::Step);
    debugger.resume();
    debugger.clearBreakpoints();
}

TEST_CASE("devtools.Debugger pause location: explicit stop retains source then manual pause clears it") {
    auto& debugger = Debugger::instance();
    debugger.detach();
    SourceLoc location;
    location.source   = "error.nut";
    location.line     = 12;
    location.function = "fail";
    debugger.pauseAt(PauseReason::Exception, location);
    REQUIRE_EQ(debugger.pauseLocation().source, std::string("error.nut"));
    CHECK_EQ(debugger.pauseLocation().line, 12);
    CHECK_EQ(debugger.pauseLocation().function, std::string("fail"));
    debugger.pause(PauseReason::PauseKey);
    CHECK(debugger.pauseLocation().empty());
    debugger.resume();
}

TEST_CASE("devtools.Debugger source matching: normalize URI and reject unrelated suffix") {
    REQUIRE_EQ(Debugger::normalizeSource("file://./a/b.nut"), std::string("a/b.nut"));
    CHECK_EQ(Debugger::normalizeSource("C:\\a\\b.nut"), std::string("C:/a/b.nut"));
    CHECK(Debugger::sourcesMatch("/game/scripts/main.nut", "scripts/main.nut"));
    CHECK(!Debugger::sourcesMatch("foo.nut", "barfoo.nut"));
    CHECK(!Debugger::sourcesMatch("/a/main.nut", "/b/main.nut"));
}

TEST_CASE("devtools.Snapshot roots: engine bindings are excluded and user roots are mutable") {
    CHECK(eve::dev::Snapshot::isEngineBinding("eve"));
    CHECK(!eve::dev::Snapshot::isEngineBinding("gameState"));
    auto& snapshot = eve::dev::Snapshot::instance();
    snapshot.clearRoots();
    snapshot.markRoot("gameState");
    REQUIRE_EQ(snapshot.roots().size(), 1u);
    CHECK_EQ(snapshot.roots().front(), std::string("gameState"));
    snapshot.unmarkRoot("gameState");
    CHECK(snapshot.roots().empty());
}
}  // namespace test_debugger_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_mcp_architecture_graph)
#include "devtools/McpArchitectureTools.hpp"
namespace test_architecture_graph_contract {
Poco::JSON::Object::Ptr result(const std::string& response) {
    Poco::JSON::Parser parser;
    auto               envelope = parser.parse(response).extract<Poco::JSON::Object::Ptr>();
    return parser.parse(envelope->getArray("content")->getObject(0)->getValue<std::string>("text"))
        .extract<Poco::JSON::Object::Ptr>();
}
bool containsNode(const Poco::JSON::Object::Ptr& graph, const std::string& id) {
    auto nodes = graph->getArray("nodes");
    for (size_t i = 0; i < nodes->size(); ++i) {
        if (nodes->getObject(static_cast<unsigned>(i))->getValue<std::string>("id") == id) return true;
    }
    return false;
}
TEST_CASE("devtools.Architecture impact: a writer resolves state owner and reader") {
    Poco::JSON::Object::Ptr args = new Poco::JSON::Object;
    args->set("seed", "inventory/Bag.setExtra");
    auto graph = result(eve::dev::callMcpArchitectureTool("eve_architecture_impact", args));
    REQUIRE(graph->getValue<bool>("ok"));
    CHECK(containsNode(graph, "owner:eve::inventory::Bag"));
    CHECK(containsNode(graph, "state:eve::inventory::Bag.extra_"));
    CHECK(containsNode(graph, "api:inventory/Bag.getExtra"));
    CHECK(!containsNode(graph, "api:scene/SceneNodeRef.setVisible"));
    CHECK_EQ(graph->getArray("inputHashes")->size(), 2u);
    CHECK_EQ(eve::dev::callMcpArchitectureTool("eve_architecture_impact", args),
             eve::dev::callMcpArchitectureTool("eve_architecture_impact", args));
}
TEST_CASE("devtools.Architecture query: limits and stale evidence yield explicit errors") {
    Poco::JSON::Object::Ptr args = new Poco::JSON::Object;
    args->set("seed", "inventory/Bag.setExtra");
    args->set("maxNodes", 1);
    auto bounded = result(eve::dev::callMcpArchitectureTool("eve_architecture_query", args));
    REQUIRE(bounded->getValue<bool>("truncated"));
    CHECK_EQ(bounded->getArray("nodes")->size(), 1u);
    args->set("maxNodes", 64);
    args->set("maxBytes", 1024);
    auto budget = result(eve::dev::callMcpArchitectureTool("eve_architecture_query", args));
    CHECK_EQ(budget->getValue<std::string>("error"), std::string("context_budget_exceeded"));
    args->set("maxBytes", 16384);
    args->set("expectedInputHash", "obsolete");
    auto stale = result(eve::dev::callMcpArchitectureTool("eve_architecture_impact", args));
    CHECK_EQ(stale->getValue<std::string>("error"), std::string("stale_evidence"));
    args->set("seed", "missing/api");
    args->remove("expectedInputHash");
    auto missing = result(eve::dev::callMcpArchitectureTool("eve_architecture_query", args));
    CHECK_EQ(missing->getValue<std::string>("error"), std::string("unknown_seed"));
}
}  // namespace test_architecture_graph_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_mcp_whitebox)
#include "devtools/McpInputTools.hpp"
#include "devtools/McpToolFilter.hpp"
#include "devtools/McpWhiteboxTools.hpp"
namespace test_whitebox_contract {
TEST_CASE("devtools.Whitebox tool: discovery declares mutation and schema") {
    REQUIRE(eve::dev::isMcpWhiteboxTool("eve_whitebox_playtest"));
    CHECK(!eve::dev::isMcpWhiteboxTool("eve_scene_batch"));
    CHECK(eve::dev::mcpToolMutates("eve_whitebox_playtest"));
    CHECK(std::string(eve::dev::mcpWhiteboxToolSchemas()).find("eve_whitebox_playtest") != std::string::npos);
}
TEST_CASE("devtools.Whitebox playtest: absent live VM returns an error envelope") {
    eve::dev::resetMcpInputBatchesForTest();
    Poco::JSON::Parser parser;
    auto args = parser.parse(R"({"reset":false,"scene":[{"action":"info"}]})").extract<Poco::JSON::Object::Ptr>();
    auto envelope =
        parser.parse(eve::dev::callMcpWhiteboxTool("eve_whitebox_playtest", args)).extract<Poco::JSON::Object::Ptr>();
    auto payload = parser.parse(envelope->getArray("content")->getObject(0)->getValue<std::string>("text"))
                       .extract<Poco::JSON::Object::Ptr>();
    CHECK(!payload->getValue<bool>("ok"));
    CHECK(payload->has("error"));
}
}  // namespace test_whitebox_contract

// The source catalogue groups recipe/workshop MCP tests with the whitebox origin.
#include "devtools/McpRecipeTools.hpp"
#include "devtools/McpTaskProfile.hpp"
#include "devtools/McpToolFilter.hpp"
#include "devtools/McpWorkshopTools.hpp"
namespace test_recipe_workshop_contract {
Poco::JSON::Object::Ptr result(const std::string& response) {
    Poco::JSON::Parser parser;
    auto               envelope = parser.parse(response).extract<Poco::JSON::Object::Ptr>();
    return parser.parse(envelope->getArray("content")->getObject(0)->getValue<std::string>("text"))
        .extract<Poco::JSON::Object::Ptr>();
}
TEST_CASE("devtools.Recipe list: discover recipes without a live VM") {
    CHECK(eve::dev::isMcpRecipeTool("eve_recipe_list"));
    CHECK(eve::dev::mcpToolMutates("eve_recipe_apply"));
    CHECK(!eve::dev::mcpToolMutates("eve_recipe_list"));
    auto listed = result(eve::dev::callMcpRecipeTool("eve_recipe_list", {}));
    REQUIRE(listed->getValue<bool>("ok"));
    CHECK(listed->getValue<int>("count") >= 2);
    CHECK(eve::dev::mcpProfileAllowsTool("content.scene", "eve_recipe_apply"));
    CHECK(!eve::dev::mcpProfileAllowsTool("verify.play", "eve_recipe_apply"));
}
TEST_CASE("devtools.Recipe apply: absent live VM reports failure") {
    Poco::JSON::Parser parser;
    auto args     = parser.parse(R"({"id":"ruins.moonlit","params":{"seed":7}})").extract<Poco::JSON::Object::Ptr>();
    auto response = result(eve::dev::callMcpRecipeTool("eve_recipe_apply", args));
    CHECK(!response->getValue<bool>("ok"));
    CHECK(response->has("error"));
}
TEST_CASE("devtools.Workshop facade: mode changes and unsupported operation is explicit") {
    eve::dev::resetMcpWorkshopStateForTest();
    CHECK(eve::dev::isMcpWorkshopTool("eve_workshop"));
    CHECK(eve::dev::mcpProfileAllowsTool("workshop.rpg", "eve_workshop"));
    CHECK(!eve::dev::mcpProfileAllowsTool("verify.play", "eve_workshop"));
    Poco::JSON::Parser parser;
    auto               statusArgs = parser.parse(R"({"op":"status"})").extract<Poco::JSON::Object::Ptr>();
    CHECK_EQ(result(eve::dev::callMcpWorkshopTool("eve_workshop", statusArgs))->getValue<std::string>("mode"),
             std::string("play"));
    auto modeArgs = parser.parse(R"({"op":"set_mode","mode":"edit"})").extract<Poco::JSON::Object::Ptr>();
    CHECK_EQ(result(eve::dev::callMcpWorkshopTool("eve_workshop", modeArgs))->getValue<std::string>("mode"),
             std::string("edit"));
    auto unsupportedArgs = parser.parse(R"({"op":"map.brush"})").extract<Poco::JSON::Object::Ptr>();
    auto unsupported     = result(eve::dev::callMcpWorkshopTool("eve_workshop", unsupportedArgs));
    CHECK(!unsupported->getValue<bool>("ok"));
    CHECK_EQ(unsupported->getValue<std::string>("errorCode"), std::string("Unsupported"));
}
}  // namespace test_recipe_workshop_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_frame_stats)
#include <cmath>
#include <limits>
#include "devtools/FrameStatsPanel.hpp"
namespace test_frame_stats_contract {
TEST_CASE("devtools.FrameStats sample: valid delta produces FPS and milliseconds") {
    auto& stats = eve::dev::FrameStatsPanel::instance();
    stats.clearEntityCount();
    stats.sample(0.016f);
    REQUIRE(stats.fps() > 50.f);
    CHECK(stats.fps() < 70.f);
    CHECK(std::fabs(stats.frameMs() - 16.f) < 1.5f);
    stats.setEntityCount(42);
    CHECK(stats.hasEntityCount());
    CHECK_EQ(stats.entityCount(), 42);
    CHECK(stats.format().find("42") != std::string::npos);
    stats.clearEntityCount();
    CHECK(!stats.hasEntityCount());
}
TEST_CASE("devtools.FrameStats sample: invalid delta leaves last valid measurement") {
    auto& stats = eve::dev::FrameStatsPanel::instance();
    stats.sample(0.020f);
    const float before = stats.frameMs();
    stats.sample(0.f);
    stats.sample(-1.f);
    stats.sample(std::numeric_limits<float>::quiet_NaN());
    REQUIRE(std::fabs(stats.frameMs() - before) < 0.01f);
}
}  // namespace test_frame_stats_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_language_index)
#include "devtools/LanguageIndex.h"
namespace test_language_index_contract {
using eve::dev::lsp::Position;
using eve::dev::lsp::WorkspaceIndex;
TEST_CASE("devtools.WorkspaceIndex definition and references: import resolves to exported symbol") {
    WorkspaceIndex index;
    index.update("game:/lib.nut", "file:///game/lib.nut",
                 "export function greet(name: string) -> string { return name }\n");
    index.update("game:/main.nut", "file:///game/main.nut",
                 "import { greet } from \"./lib.nut\"\nlocal message = greet(\"Ada\")\n");
    auto definition = index.definition("game:/main.nut", Position{1, 17});
    REQUIRE(definition.has_value());
    CHECK_EQ(definition->uri, std::string("file:///game/lib.nut"));
    CHECK_EQ(definition->range.start.line, 0u);
    CHECK_EQ(index.references("game:/lib.nut", Position{0, 17}, true).size(), 3u);
    CHECK_EQ(index.references("game:/lib.nut", Position{0, 17}, false).size(), 2u);
}
TEST_CASE("devtools.WorkspaceIndex rename: exported name updates declaration import and use") {
    WorkspaceIndex index;
    index.update("game:/lib.nut", "file:///game/lib.nut", "export const SPEED = 2.0\n");
    index.update("game:/main.nut", "file:///game/main.nut",
                 "import { SPEED } from \"./lib.nut\"\nlocal value = SPEED\n");
    auto edits = index.rename("game:/lib.nut", Position{0, 14}, "PLAYER_SPEED");
    REQUIRE(edits.has_value());
    REQUIRE_EQ(edits->size(), 3u);
    for (const auto& edit : *edits) CHECK_EQ(edit.newText, std::string("PLAYER_SPEED"));
    CHECK(!index.rename("game:/lib.nut", Position{0, 14}, "bad-name").has_value());
}
TEST_CASE("devtools.WorkspaceIndex alias rename: edits remain within importing document") {
    WorkspaceIndex index;
    index.update("game:/lib.nut", "file:///game/lib.nut", "export function greet() {}\n");
    index.update("game:/main.nut", "file:///game/main.nut",
                 "import { greet as salute } from \"./lib.nut\"\nsalute()\n");
    auto edits = index.rename("game:/main.nut", Position{1, 2}, "hello");
    REQUIRE(edits.has_value());
    REQUIRE_EQ(edits->size(), 2u);
    for (const auto& edit : *edits) CHECK_EQ(edit.location.uri, std::string("file:///game/main.nut"));
}
TEST_CASE("devtools.WorkspaceIndex semantic tokens: declaration and use share type") {
    WorkspaceIndex index;
    index.update("game:/main.nut", "file:///game/main.nut",
                 "class Player {}\nfunction greet() {}\nlocal speed = 1\ngreet()\nPlayer()\n");
    auto tokens = index.semanticTokens("game:/main.nut");
    auto typeAt = [&](std::string_view name, size_t line) -> uint32_t {
        for (const auto& token : tokens) {
            if (token.name == name && token.start.line == line) return token.type;
        }
        return 99;
    };
    REQUIRE_EQ(typeAt("Player", 0), eve::dev::lsp::SemanticTypes::Class);
    CHECK_EQ(typeAt("Player", 4), eve::dev::lsp::SemanticTypes::Class);
    CHECK_EQ(typeAt("greet", 1), eve::dev::lsp::SemanticTypes::Function);
    CHECK_EQ(typeAt("greet", 3), eve::dev::lsp::SemanticTypes::Function);
    CHECK_EQ(typeAt("speed", 2), eve::dev::lsp::SemanticTypes::Variable);
}
}  // namespace test_language_index_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_language_text)
#include "devtools/LanguageText.h"
namespace test_language_text_contract {
TEST_CASE("devtools.LanguageText incremental edit: range replacement changes only selected text") {
    std::string          source = "function foo() {\nlocal x = 1\n}\n";
    eve::dev::lsp::Range selection{{1, 6}, {1, 7}};
    REQUIRE(eve::dev::lsp::applyIncremental(source, &selection, "value"));
    CHECK_EQ(source, std::string("function foo() {\nlocal value = 1\n}\n"));
    CHECK_EQ(eve::dev::lsp::sliceLines(source, 1, 1), std::string("local value = 1\n"));
    CHECK_EQ(eve::dev::lsp::offsetAt("abc\nnext", {0, 100}), size_t(3));
    CHECK_EQ(eve::dev::lsp::offsetAt("abc\nnext", {1, 100}), size_t(8));
}
TEST_CASE("devtools.LanguageText format and fold: braces and comments define ranges") {
    REQUIRE_EQ(eve::dev::lsp::formatEveScript("{\na\n}\n"), std::string("{\n    a\n}\n"));
    eve::dev::lsp::FormatOptions tabs;
    tabs.insertSpaces = false;
    tabs.tabSize      = 4;
    CHECK_EQ(eve::dev::lsp::formatEveScript("{\na\n}\n", tabs), std::string("{\n\ta\n}\n"));
    const auto folds   = eve::dev::lsp::foldingRanges("function foo() {\nlocal x = 1\n}\n/*\nblock\n*/\n");
    bool       block   = false;
    bool       comment = false;
    for (const auto& fold : folds) {
        if (fold.kind == "region" && fold.startLine == 0 && fold.endLine == 2) block = true;
        if (fold.kind == "comment" && fold.startLine == 3 && fold.endLine == 5) comment = true;
    }
    CHECK(block);
    CHECK(comment);
}
}  // namespace test_language_text_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_mcp_console)
#include "devtools/ConsolePanel.hpp"
namespace test_mcp_console_contract {
TEST_CASE("devtools.Console cursor: tail and resume read each entry once") {
    auto& console = eve::dev::ConsolePanel::instance();
    console.setMaxEntries(100);
    console.clear();
    console.addInfo("seed");
    const auto firstExpected = console.nextSeq();
    console.addWarn("warning");
    console.addError("failure");
    auto tail = console.read(0, 1, {});
    REQUIRE_EQ(tail.lines.size(), 1u);
    CHECK_EQ(tail.lines.front().text, std::string("failure"));
    auto fromSeed = console.read(firstExpected - 1, 10, {});
    REQUIRE_EQ(fromSeed.lines.size(), 2u);
    CHECK_EQ(fromSeed.lines.front().text, std::string("warning"));
    CHECK_EQ(fromSeed.lines.back().text, std::string("failure"));
    CHECK(console.read(fromSeed.cursor, 10, {}).lines.empty());
    console.addInfo("later");
    auto resumed = console.read(fromSeed.cursor, 10, {});
    REQUIRE_EQ(resumed.lines.size(), 1u);
    CHECK_EQ(resumed.lines.front().text, std::string("later"));
    console.clear();
}
TEST_CASE("devtools.Console cursor: eviction and clear expose lost lines without rewinding sequence") {
    auto& console = eve::dev::ConsolePanel::instance();
    console.clear();
    console.setMaxEntries(2);
    console.addInfo("old");
    const auto old = console.nextSeq() - 1;
    console.addInfo("middle");
    console.addInfo("new");
    auto stale = console.read(old, 10, {});
    CHECK(stale.truncated);
    REQUIRE_EQ(stale.lines.size(), 2u);
    CHECK_EQ(stale.lines.front().text, std::string("middle"));
    const auto beforeClear = console.nextSeq();
    console.clear();
    CHECK_EQ(console.nextSeq(), beforeClear);
    CHECK_EQ(console.droppedThrough(), beforeClear - 1);
    CHECK(console.read(beforeClear, 10, {}).lines.empty());
    console.setMaxEntries(500);
}
}  // namespace test_mcp_console_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_mcp_tool_filter)
#include "devtools/McpToolFilter.hpp"
namespace test_mcp_tool_filter_contract {
TEST_CASE("devtools.McpToolFilter: tags classify exposed tool families") {
    REQUIRE_EQ(eve::dev::mcpToolTags("eve_status").at(0), std::string("core"));
    CHECK_EQ(eve::dev::mcpToolTags("eve_eval").at(0), std::string("debug"));
    CHECK_EQ(eve::dev::mcpToolTags("eve_input_batch").at(0), std::string("input"));
    CHECK_EQ(eve::dev::mcpToolTags("eve_host_status").at(0), std::string("host"));
}
TEST_CASE("devtools.McpToolFilter: mutating calls differ from observation calls") {
    REQUIRE(eve::dev::mcpToolMutates("eve_scene_batch"));
    CHECK(eve::dev::mcpToolMutates("eve_whitebox_playtest"));
    CHECK(eve::dev::mcpToolMutates("eve_eval"));
    CHECK(!eve::dev::mcpToolMutates("eve_status"));
    CHECK(!eve::dev::mcpToolMutates("eve_input_poll"));
}
}  // namespace test_mcp_tool_filter_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_game_world_context)
#include "devtools/GameWorldContext.hpp"
namespace test_game_world_contract {
eve::Value parse(std::string_view json) {
    auto parsed = eve::Value::fromJson(json);
    REQUIRE(parsed.ok());
    return std::move(parsed).takeValue();
}
TEST_CASE("devtools.GameWorldContext parse: unknown fields and missing title fail") {
    eve::dev::clearWorldContext();
    auto unknown = eve::dev::parseWorldObject(parse(R"({"title":"World","extra":true})"));
    CHECK(!unknown.ok());
    CHECK_EQ(unknown.status().diagnostics().front().code(), eve::DiagnosticCode::ParseError);
    CHECK(!eve::dev::parseWorldObject(parse(R"({})")).ok());
    CHECK(!eve::dev::parseWorldObject(parse(R"({"title":"World","dimensions":"4d"})")).ok());
}
TEST_CASE("devtools.GameWorldContext snapshot: configuration is used before runtime publication") {
    eve::dev::clearWorldContext();
    eve::dev::GameWorldSnapshotInput input;
    input.contractId        = "examples/test";
    input.contractLoaded    = true;
    input.configTitle       = "Configured World";
    input.configuredModules = {"physics", "particles"};
    input.liveModules       = {"physics", "ui"};
    auto snapshot           = eve::dev::buildGameWorldSnapshot(input);
    REQUIRE(snapshot.ok());
    const auto* object = snapshot.value().getIf<eve::Value::Object>();
    REQUIRE(object != nullptr);
    CHECK_EQ(object->at("source").asString(), std::string("config"));
    CHECK_EQ(object->at("world").getIf<eve::Value::Object>()->at("title").asString(), std::string("Configured World"));
    CHECK_EQ(object->at("configuredModules").getIf<eve::Value::Array>()->size(), 2u);
}
TEST_CASE("devtools.GameWorldContext publication: runtime fields override title and clear restores config") {
    eve::dev::clearWorldContext();
    eve::dev::GameWorldSnapshotInput input;
    input.configTitle = "Configured World";
    REQUIRE(eve::dev::publishWorldContext(parse(R"({"title":"Runtime World","summary":"Live"})")).ok());
    auto runtime = eve::dev::buildGameWorldSnapshot(input);
    REQUIRE(runtime.ok());
    const auto* object = runtime.value().getIf<eve::Value::Object>();
    REQUIRE(object != nullptr);
    CHECK_EQ(object->at("source").asString(), std::string("runtime"));
    CHECK_EQ(object->at("world").getIf<eve::Value::Object>()->at("title").asString(), std::string("Runtime World"));
    eve::dev::clearWorldContext();
    auto config = eve::dev::buildGameWorldSnapshot(input);
    REQUIRE(config.ok());
    CHECK_EQ(config.value().getIf<eve::Value::Object>()->at("source").asString(), std::string("config"));
}
}  // namespace test_game_world_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_mcp_frame_capture)
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>
#include "common/FrameCaptureImage.h"
#include "devtools/FrameCapturePreview.hpp"
#include "devtools/McpFrameCapture.hpp"
namespace test_frame_capture_contract {
eve::FrameCaptureCapabilities capabilities(size_t budget = 1024) {
    eve::FrameCaptureCapabilities value;
    value.backend     = "contract-test";
    value.maxBytes    = budget;
    value.attachments = {eve::FrameAttachment::Depth, eve::FrameAttachment::Velocity, eve::FrameAttachment::Final};
    return value;
}
eve::FrameCaptureSnapshot pixelSnapshot(eve::FrameAttachment attachment = eve::FrameAttachment::Depth) {
    eve::FrameCaptureSnapshot snapshot;
    eve::FrameCaptureBuffer   buffer;
    buffer.attachment = attachment;
    buffer.width = buffer.height = 1;
    buffer.bytes = std::make_shared<const std::vector<uint8_t>>(std::initializer_list<uint8_t>{128, 64, 255, 255});
    snapshot.buffers.push_back(std::move(buffer));
    return snapshot;
}
TEST_CASE("devtools.FrameCaptureStore publication: partial capture is hidden until complete") {
    eve::FrameCaptureStore store;
    store.configure(capabilities());
    eve::FrameCaptureRequest request;
    request.attachments = {eve::FrameAttachment::Depth};
    auto queued         = store.request(request);
    REQUIRE(queued.ok());
    const auto token = queued.value();
    REQUIRE(store.begin(token, 42, 4).ok());
    auto pending = store.snapshot(token);
    REQUIRE(pending.ok());
    CHECK_EQ(pending.value().phase, eve::FrameCapturePhase::Recording);
    CHECK(pending.value().buffers.empty());
    REQUIRE(store.submitted(token).ok());
    CHECK(!store.complete(token, {}).ok());
    REQUIRE(store.complete(token, pixelSnapshot()).ok());
    store.retire(token);
    auto ready = store.snapshot(token);
    REQUIRE(ready.ok());
    CHECK_EQ(ready.value().phase, eve::FrameCapturePhase::Ready);
    CHECK_EQ(ready.value().frameId, 42u);
    REQUIRE(store.release(token).ok());
    CHECK_EQ(ready.value().buffers.front().bytes->at(1), uint8_t{64});
}
TEST_CASE("devtools.FrameCaptureStore lifecycle: duplicate key expires and generation invalidates") {
    auto                   now = std::chrono::steady_clock::time_point{};
    eve::FrameCaptureStore store([&] { return now; });
    store.configure(capabilities(16));
    eve::FrameCaptureRequest request;
    request.attachments = {eve::FrameAttachment::Final};
    request.requestKey  = "same";
    auto queued         = store.request(request);
    REQUIRE(queued.ok());
    auto duplicate = store.request(request);
    REQUIRE(duplicate.ok());
    CHECK_EQ(duplicate.value(), queued.value());
    CHECK(!store.begin(queued.value(), 1, 9).ok());
    now += std::chrono::seconds(6);
    auto expired = store.snapshot(queued.value());
    REQUIRE(expired.ok());
    CHECK_EQ(expired.value().phase, eve::FrameCapturePhase::Expired);
    store.invalidate();
    CHECK(!store.snapshot(queued.value()).ok());
}
TEST_CASE("devtools.FrameCaptureImage pixel: decode valid sample and reject out-of-bounds") {
    auto buffer  = pixelSnapshot().buffers.front();
    auto sampled = eve::frameCapturePixel(buffer, 0, 0);
    REQUIRE(sampled.ok());
    CHECK_EQ(sampled.value().stored[1], uint8_t{64});
    CHECK(std::abs(sampled.value().decoded[1] - (128.0 / 255.0 - 1.0)) < 1e-8);
    CHECK(!eve::frameCapturePixel(buffer, 1, 0).ok());
    auto preview = eve::dev::frameCapturePreview(buffer, {});
    REQUIRE(preview.ok());
    CHECK_EQ(preview.value().width, 1u);
    CHECK_EQ(preview.value().png[0], uint8_t{137});
    eve::dev::FramePreviewOptions invalid;
    invalid.view = "unknown";
    CHECK(!eve::dev::frameCapturePreview(buffer, invalid).ok());
}
TEST_CASE("devtools.FrameCaptureImage depth: projection and BGRA channel mapping") {
    eve::FrameCaptureBuffer depth;
    depth.attachment = eve::FrameAttachment::HwDepth;
    depth.format     = eve::FramePixelFormat::Depth32Float;
    depth.width = depth.height = 1;
    auto        bytes          = std::make_shared<std::vector<uint8_t>>(4);
    const float raw            = 0.25f;
    std::memcpy(bytes->data(), &raw, sizeof raw);
    depth.bytes = bytes;
    std::array<float, 16> inverse{};
    inverse[0] = inverse[5] = inverse[15] = 1.f;
    inverse[14]                           = -1.f;
    inverse[11]                           = -0.9f;
    auto linear                           = eve::frameCaptureLinearDepth(depth, 0, 0, inverse);
    REQUIRE(linear.ok());
    CHECK(std::abs(linear.value() - 1.0 / 0.775) < 1e-6);
    auto bgra   = pixelSnapshot(eve::FrameAttachment::Final).buffers.front();
    bgra.format = eve::FramePixelFormat::Bgra8;
    auto pixel  = eve::frameCapturePixel(bgra, 0, 0);
    REQUIRE(pixel.ok());
    CHECK_EQ(pixel.value().stored[0], uint8_t{255});
    CHECK_EQ(pixel.value().stored[2], uint8_t{128});
}
}  // namespace test_frame_capture_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_play_trace)
#include "devtools/PlayTrace.h"
namespace test_play_trace_contract {
eve::Value parse(std::string_view json) {
    auto parsed = eve::Value::fromJson(json);
    REQUIRE(parsed.ok());
    return std::move(parsed).takeValue();
}
TEST_CASE("devtools.PlayTrace parse: schema accepts empty steps and rejects unknown fields") {
    auto valid = eve::dev::parsePlayTrace(parse(R"({"schemaId":"evengine.play-trace","schemaVersion":1,"steps":[]})"));
    CHECK(valid.ok());
    auto unknown = eve::dev::parsePlayTrace(
        parse(R"({"schemaId":"evengine.play-trace","schemaVersion":1,"steps":[],"extra":true})"));
    CHECK(!unknown.ok());
    CHECK_EQ(unknown.status().diagnostics().front().code(), eve::DiagnosticCode::ParseError);
}
TEST_CASE("devtools.PlayTrace digest: equal projections yield stable digest and changed state differs") {
    auto first   = eve::dev::playObservationDigest(parse(R"({"tick":4,"alive":true})"));
    auto equal   = eve::dev::playObservationDigest(parse(R"({"alive":true,"tick":4})"));
    auto changed = eve::dev::playObservationDigest(parse(R"({"tick":5,"alive":true})"));
    REQUIRE(first.ok());
    REQUIRE(equal.ok());
    REQUIRE(changed.ok());
    CHECK_EQ(first.value(), equal.value());
    CHECK_NE(first.value(), changed.value());
    CHECK(first.value().find("fnv1a64:") == 0);
}
TEST_CASE("devtools.PlayTrace buffer: append creates one versioned step and clear empties it") {
    auto& buffer = eve::dev::PlayTraceBuffer::instance();
    buffer.clear();
    CHECK(buffer.empty());
    buffer.begin("game", "hash", 7, "checkpoint");
    CHECK(buffer.isRecording());
    buffer.append(parse(R"({"op":"observe"})"), parse(R"({"state":{"tick":1}})"), 10);
    CHECK(!buffer.empty());
    auto        recording = buffer.exportTrace();
    const auto* object    = recording.getIf<eve::Value::Object>();
    REQUIRE(object != nullptr);
    CHECK_EQ(object->at("schemaId").asString(), std::string("evengine.play-trace"));
    CHECK_EQ(object->at("steps").getIf<eve::Value::Array>()->size(), 1u);
    buffer.clear();
    CHECK(buffer.empty());
}
}  // namespace test_play_trace_contract
#endif
