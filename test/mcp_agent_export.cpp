// Agent-facing runtime evidence: engine stderr, crash forensics and the
// project-script → MCP export registry.
//
// These cases pin the three capabilities an unattended agent needs beyond the
// built-in tool table:
//   1. Engine diagnostics (std::cerr / fprintf(stderr)) reach eve_console_read.
//   2. A restarted game can explain why the previous run died (eve_crash_report).
//   3. A project script publishes its own named, schema-carrying tools.

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/CrashLog.h"
#include "devtools/ConsolePanel.hpp"
#include "devtools/DevTool.hpp"
#include "devtools/McpScriptTools.hpp"
#include "devtools/McpServer.hpp"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace eve::dev;

namespace {

/** Run one pre-buffered stdio session and return the newline-framed responses. */
std::vector<std::string> runStdioSession(const std::string& requests, std::size_t expectedResponses) {
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

std::string initializeRequest(int id) {
    return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) + ",\"method\":\"initialize\",\"params\":{" +
           "\"protocolVersion\":\"2025-06-18\",\"capabilities\":{},\"clientInfo\":{\"name\":\"export-test\"}}}";
}

std::string callRequest(int id, const std::string& name, const std::string& argumentsJson) {
    return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) + ",\"method\":\"tools/call\",\"params\":{\"name\":\"" +
           name + "\",\"arguments\":" + argumentsJson + "}}";
}

std::string joinRequests(const std::vector<std::string>& requests) {
    std::string out;
    for (const auto& request : requests) {
        out += request;
        out += '\n';
    }
    return out;
}

Poco::JSON::Object::Ptr parseObject(const std::string& json) {
    Poco::JSON::Parser parser;
    return parser.parse(json).extract<Poco::JSON::Object::Ptr>();
}

/** Result object of a tools/call response line, or null when the shape is wrong. */
Poco::JSON::Object::Ptr toolResult(const std::string& line) {
    try {
        auto object = parseObject(line);
        if (!object || !object->has("result")) return {};
        return object->getObject("result");
    } catch (...) {
        return {};
    }
}

std::string toolText(const std::string& line) {
    auto result = toolResult(line);
    if (!result || !result->has("content")) return {};
    auto content = result->getArray("content");
    if (!content || content->size() == 0) return {};
    auto item = content->getObject(0);
    if (!item || !item->has("text")) return {};
    return item->get("text").convert<std::string>();
}

bool toolIsError(const std::string& line) {
    auto result = toolResult(line);
    return result && result->optValue<bool>("isError", false);
}

/** Call one tool over a fresh stdio session and return its response line. */
std::string callToolOnce(const std::string& name, const std::string& argumentsJson) {
    const std::vector<std::string> responses =
        runStdioSession(joinRequests({initializeRequest(1), callRequest(2, name, argumentsJson)}), 2);
    if (responses.size() < 2) return {};
    return responses[1];
}

/** Declared tool names from a tools/list response line. */
std::vector<std::string> declaredToolNames(const std::string& line) {
    std::vector<std::string> names;
    try {
        auto object = parseObject(line);
        auto result = object ? object->getObject("result") : Poco::JSON::Object::Ptr();
        auto tools  = result ? result->getArray("tools") : Poco::JSON::Array::Ptr();
        if (!tools) return names;
        for (std::size_t index = 0; index < tools->size(); ++index) {
            auto tool = tools->getObject(static_cast<unsigned>(index));
            if (tool) names.push_back(tool->getValue<std::string>("name"));
        }
    } catch (...) {
    }
    return names;
}

const char* kToolScript =
    "eve.mcp.tool(\"game_npcs\", {\n"
    "  description = \"List live NPCs\",\n"
    "  inputSchema = { type = \"object\", properties = { lane = { type = \"string\" } } },\n"
    "  handler = function(args) {\n"
    "    local lane = (\"lane\" in args) ? args.lane : \"none\";\n"
    "    return { count = 2, lane = lane };\n"
    "  }\n"
    "});\n"
    "eve.mcp.tool(\"game_boom\", {\n"
    "  description = \"Always fails\",\n"
    "  handler = function(args) { throw \"boom\"; }\n"
    "});\n";

}  // namespace

TEST_CASE("devtools.mcp.stderrCaptureFeedsConsole") {
    auto& console = ConsolePanel::instance();
    auto& dt      = DevTool::instance();
    dt.detach();
    console.setMaxEntries(500);
    console.clear();

    ssq::VM vm(512, ssq::Libs::ALL);
    dt.attach(vm, false);
    CHECK(console.isCapturingStderr());

    // Both engine output sinks must be covered: a streambuf swap would miss the
    // forty-odd fprintf(stderr, ...) call sites.
    std::fprintf(stderr, "engine-fprintf-marker\n");
    std::fflush(stderr);
    std::cerr << "engine-cerr-marker" << std::endl;

    bool sawFprintf = false;
    bool sawCerr    = false;
    for (int attempt = 0; attempt < 300 && !(sawFprintf && sawCerr); ++attempt) {
        const std::string text = console.format(200);
        sawFprintf             = text.find("engine-fprintf-marker") != std::string::npos;
        sawCerr                = text.find("engine-cerr-marker") != std::string::npos;
        if (!(sawFprintf && sawCerr)) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(sawFprintf);
    CHECK(sawCerr);

    bool          tagged   = false;
    bool          seqRanUp = false;
    std::uint64_t previous = 0;
    for (const auto& line : console.recent(200)) {
        if (line.level == "engine" && line.text == "engine-cerr-marker") tagged = true;
        if (line.seq <= previous) seqRanUp = true;
        previous = line.seq;
    }
    CHECK(tagged);
    CHECK(!seqRanUp);

    dt.detach();
    CHECK(!console.isCapturingStderr());
    console.setMaxEntries(500);
    console.clear();
}

TEST_CASE("devtools.mcp.engineLineDropsScriptEchoOnly") {
    auto& console = ConsolePanel::instance();
    console.setMaxEntries(500);
    console.clear();

    // capturePrint/captureError forward to stderr, so the descriptor capture
    // sees the same text again. Only that echo is dropped.
    console.addLog("print", "echo-line");
    console.addEngineLine("echo-line");
    console.addLog("error", "echo-error");
    console.addEngineLine("echo-error");
    // A repeat produced by the engine itself is not an echo and must survive.
    console.addEngineLine("engine-only");
    console.addEngineLine("engine-only");

    int echoPrint  = 0;
    int echoError  = 0;
    int engineOnly = 0;
    for (const auto& line : console.recent(50)) {
        if (line.text == "echo-line") ++echoPrint;
        if (line.text == "echo-error") ++echoError;
        if (line.text == "engine-only") ++engineOnly;
    }
    CHECK_EQ(echoPrint, 1);
    CHECK_EQ(echoError, 1);
    CHECK_EQ(engineOnly, 2);

    console.setMaxEntries(500);
    console.clear();
}

TEST_CASE("devtools.mcp.crashReportExplainsPreviousRun") {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto dir   = std::filesystem::temp_directory_path() / ("eve-mcp-crash-" + std::to_string(stamp));
    std::filesystem::create_directories(dir);

    eve::initSystemLogging(dir.string());
    const std::string path = eve::crashLogPath();
    REQUIRE(!path.empty());
    CHECK(path.find("eve.log") != std::string::npos);

    // Session 1 crashes, session 2 is the restart asking the question, session 3
    // would be the next restart after a clean exit.
    eve::recordCrashEvent("[crash] code=0x00000005 at 0x0\n");
    eve::recordSessionStart();

    const std::string beforeLine = callToolOnce("eve_crash_report", "{\"lines\":60}");
    const std::string before     = toolText(beforeLine);
    REQUIRE(!before.empty());
    CHECK(!toolIsError(beforeLine));

    auto summary = parseObject(before);
    REQUIRE(summary);
    CHECK(summary->getValue<bool>("exists"));
    CHECK(summary->getValue<int>("crashes") >= 1);
    CHECK(summary->getValue<int>("sessions") >= 2);
    CHECK(!summary->getValue<std::string>("lastCrashAt").empty());
    CHECK(before.find("[crash] code=0x00000005") != std::string::npos);
    CHECK(before.find("tail") != std::string::npos);
    // The session before the current one did not end: that is the crash verdict.
    CHECK(summary->optValue<bool>("previousSessionCrashed", false));
    CHECK(!summary->optValue<bool>("previousSessionEnded", true));

    // A clean exit writes the end marker, which is how a reader distinguishes
    // "the previous run finished" from "the previous run died".
    eve::recordSessionEnd(0);
    eve::recordSessionStart();
    const std::string after = toolText(callToolOnce("eve_crash_report", "{}"));
    auto              clean = parseObject(after);
    REQUIRE(clean);
    CHECK(clean->optValue<bool>("previousSessionEnded", false));
    CHECK(!clean->optValue<bool>("previousSessionCrashed", true));

    // eve_status points at the same artifact without reading it.
    const std::string status = toolText(callToolOnce("eve_status", "{}"));
    CHECK(status.find(path) != std::string::npos);
    CHECK(status.find("\"crashLog\"") != std::string::npos);
}

TEST_CASE("devtools.mcp.scriptToolExportRoundTrip") {
    auto& console = ConsolePanel::instance();
    auto& dt      = DevTool::instance();
    dt.detach();
    console.setMaxEntries(500);
    console.clear();

    ssq::VM vm(1024, ssq::Libs::ALL);
    {
        auto bootstrap = vm.compileSource("eve <- {};\n");
        vm.run(bootstrap);
    }
    dt.attach(vm, false);
    dt.exposeScriptApi(vm);
    {
        auto script = vm.compileSource(kToolScript);
        vm.run(script);
    }
    CHECK_EQ(scriptToolCount(), 2);
    CHECK(isScriptTool("game_npcs"));
    CHECK(isScriptTool("game_boom"));

    const std::vector<std::string> listed = runStdioSession(
        joinRequests({initializeRequest(1), "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{}}"}),
        2);
    REQUIRE(listed.size() == 2);
    const std::vector<std::string> names = declaredToolNames(listed[1]);
    REQUIRE(!names.empty());
    CHECK(std::find(names.begin(), names.end(), "game_npcs") != names.end());
    CHECK(std::find(names.begin(), names.end(), "game_boom") != names.end());
    CHECK(listed[1].find("List live NPCs") != std::string::npos);
    CHECK(listed[1].find("\"properties\":{\"lane\":{\"type\":\"string\"}}") != std::string::npos);

    // Anti-shadowing invariant: nothing a project can register may collide with
    // a built-in, because built-ins win at dispatch time.
    std::string shadowable;
    for (const auto& name : names) {
        if (name == "game_npcs" || name == "game_boom") continue;
        if (isValidScriptToolName(name)) shadowable += name + " ";
    }
    CHECK_EQ(shadowable, std::string());

    // Handler arguments arrive as a Squirrel table; the return value is JSON.
    const std::string okLine = callToolOnce("game_npcs", "{\"lane\":\"north\"}");
    CHECK(!toolIsError(okLine));
    auto ok = parseObject(toolText(okLine));
    REQUIRE(ok);
    CHECK_EQ(ok->getValue<int>("count"), 2);
    CHECK_EQ(ok->getValue<std::string>("lane"), std::string("north"));

    // Missing arguments become an empty table rather than an error.
    auto emptyArgs = parseObject(toolText(callToolOnce("game_npcs", "{}")));
    REQUIRE(emptyArgs);
    CHECK_EQ(emptyArgs->getValue<std::string>("lane"), std::string("none"));

    // A throwing handler is reported as a tool error, not a protocol failure.
    const std::string boomLine = callToolOnce("game_boom", "{}");
    CHECK(toolIsError(boomLine));
    CHECK(toolText(boomLine).find("error:") == 0);

    // Unregistering removes it from discovery as well as dispatch.
    {
        auto removeScript = vm.compileSource(
            "local removed = eve.mcp.remove(\"game_npcs\");\n"
            "local left = eve.mcp.tools();\n");
        vm.run(removeScript);
    }
    CHECK(!isScriptTool("game_npcs"));
    CHECK_EQ(scriptToolCount(), 1);
    CHECK(!isScriptTool("eve_status"));  // built-ins are never in this registry
    const std::vector<std::string> afterRemove = runStdioSession(
        joinRequests({initializeRequest(1), "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{}}"}),
        2);
    REQUIRE(afterRemove.size() == 2);
    CHECK(afterRemove[1].find("game_npcs") == std::string::npos);

    // Detaching the VM must not leave orphaned handlers behind.
    dt.detach();
    CHECK_EQ(scriptToolCount(), 0);
    console.setMaxEntries(500);
    console.clear();
}

TEST_CASE("devtools.mcp.scriptToolNameRules") {
    CHECK(isValidScriptToolName("game_npcs"));
    CHECK(isValidScriptToolName("abc"));
    CHECK(!isValidScriptToolName("ab"));             // too short
    CHECK(!isValidScriptToolName("Game"));           // uppercase
    CHECK(!isValidScriptToolName("1game"));          // leading digit
    CHECK(!isValidScriptToolName("game-npcs"));      // punctuation
    CHECK(!isValidScriptToolName("eve_status"));     // built-in prefix
    CHECK(!isValidScriptToolName("eve_custom"));     // built-in prefix
    CHECK(!isValidScriptToolName("inspect_thing"));  // built-in prefix
    CHECK(!isValidScriptToolName("set_thing"));      // built-in prefix
    CHECK(!isValidScriptToolName("capture_thing"));  // built-in prefix
    CHECK(!isValidScriptToolName("get_thing"));      // built-in prefix
    CHECK(!isValidScriptToolName(std::string(65, 'a')));

    // A rejected registration must not enter the registry.
    auto& dt = DevTool::instance();
    dt.detach();
    ssq::VM vm(512, ssq::Libs::ALL);
    {
        auto bootstrap = vm.compileSource("eve <- {};\n");
        vm.run(bootstrap);
    }
    dt.attach(vm, false);
    dt.exposeScriptApi(vm);
    {
        auto script = vm.compileSource(
            "local ok = eve.mcp.tool(\"eve_shadow\", { handler = function(a) { return 1; } });\n"
            "local ok2 = eve.mcp.tool(\"no_handler\", { description = \"x\" });\n");
        vm.run(script);
    }
    CHECK_EQ(scriptToolCount(), 0);
    dt.detach();
}
