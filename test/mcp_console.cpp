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

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "devtools/ConsolePanel.hpp"
#include "devtools/DevTool.hpp"
#include "devtools/McpServer.hpp"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>

#include <simplesquirrel/simplesquirrel.hpp>

#include <chrono>
#include <cstdint>
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

Poco::JSON::Object::Ptr parseObject(const std::string& json) {
    Poco::JSON::Parser parser;
    return parser.parse(json).extract<Poco::JSON::Object::Ptr>();
}

/** Text payload of a tools/call response line (empty when the shape is wrong). */
std::string responseContentText(const std::string& line) {
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

std::string initializeRequest(int id, const std::string& protocol) {
    return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) + ",\"method\":\"initialize\",\"params\":{" +
           "\"protocolVersion\":\"" + protocol + "\",\"capabilities\":{}," +
           "\"clientInfo\":{\"name\":\"mcp-console-test\",\"version\":\"0\"}}}";
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

TEST_CASE("devtools.mcp.consoleToolsRoundTripScriptPrint") {
    auto& console = ConsolePanel::instance();
    auto& dt      = DevTool::instance();
    dt.detach();
    console.setMaxEntries(500);
    console.clear();

    ssq::VM vm(512, ssq::Libs::ALL);
    dt.attach(vm, false);
    REQUIRE(console.isAttached());
    {
        auto script = vm.compileSource("print(\"boot-marker\\n\");\n");
        vm.run(script);
    }

    const std::vector<std::string> responses =
        runStdioSession(joinRequests({
                            initializeRequest(1, "2025-06-18"),
                            callRequest(2, "eve_console_write", "{\"text\":\"agent-marker\",\"level\":\"warn\"}"),
                            callRequest(3, "eve_console_read", "{\"sinceSeq\":0,\"limit\":50}"),
                            callRequest(4, "eve_console_write", "{\"text\":\"bad\",\"level\":\"print\"}"),
                            callRequest(5, "eve_console_read", "{\"level\":\"nope\"}"),
                            callRequest(6, "eve_console_clear", "{}"),
                            callRequest(7, "eve_console_read", "{\"sinceSeq\":0}"),
                        }),
                        7);
    REQUIRE(responses.size() == 7);

    // The Squirrel print() is now reachable through MCP: this is the whole point
    // of the console tool family.
    const std::string readText = responseContentText(responses[2]);
    CHECK(readText.find("boot-marker") != std::string::npos);
    CHECK(readText.find("agent-marker") != std::string::npos);
    CHECK(readText.find("\"attached\":true") != std::string::npos);
    CHECK(readText.find("\"truncated\":false") != std::string::npos);

    auto readObject = parseObject(readText);
    REQUIRE(readObject);
    const Poco::Int64 nextSeq = readObject->getValue<Poco::Int64>("nextSeq");
    CHECK(nextSeq > 1);
    auto lines = readObject->getArray("lines");
    REQUIRE(lines && lines->size() >= 2);
    Poco::Int64 previous = 0;
    for (std::size_t index = 0; index < lines->size(); ++index) {
        const Poco::Int64 seq = lines->getObject(static_cast<unsigned>(index))->getValue<Poco::Int64>("seq");
        CHECK(seq > previous);
        previous = seq;
    }
    CHECK(previous == nextSeq - 1);
    CHECK(readObject->getValue<Poco::Int64>("cursor") == nextSeq - 1);

    // An agent marker must not impersonate the game's print/result channels.
    CHECK(responseContentText(responses[3]).find("unknown level") != std::string::npos);
    CHECK(responseContentText(responses[4]).find("unknown level") != std::string::npos);

    // Cursor-based incremental read: nothing new after clear().
    auto cleared = parseObject(responseContentText(responses[5]));
    REQUIRE(cleared);
    CHECK(cleared->getValue<std::int64_t>("droppedThrough") == nextSeq - 1);
    const std::string afterClear = responseContentText(responses[6]);
    CHECK(afterClear.find("\"count\":0") != std::string::npos);
    CHECK(afterClear.find("\"truncated\":false") != std::string::npos);

    dt.detach();
    CHECK(!console.isAttached());
    console.setMaxEntries(500);
    console.clear();
}

TEST_CASE("devtools.mcp.everyDeclaredToolIsRouted") {
    const std::vector<std::string> listResponses =
        runStdioSession(joinRequests({initializeRequest(1, "2025-06-18"),
                                      "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{}}"}),
                        2);
    REQUIRE(listResponses.size() == 2);

    auto listObject = parseObject(listResponses[1]);
    REQUIRE(listObject);
    auto result = listObject->getObject("result");
    REQUIRE(result);
    auto tools = result->getArray("tools");
    REQUIRE(tools);
    REQUIRE(tools->size() > 100);

    std::vector<std::string> requests{initializeRequest(1, "2025-06-18")};
    std::vector<std::string> names;
    for (std::size_t index = 0; index < tools->size(); ++index) {
        auto tool = tools->getObject(static_cast<unsigned>(index));
        REQUIRE(tool);
        const std::string name = tool->getValue<std::string>("name");
        REQUIRE(!name.empty());
        names.push_back(name);
        requests.push_back(callRequest(static_cast<int>(index) + 2, name, "{}"));
    }

    const std::vector<std::string> responses = runStdioSession(joinRequests(requests), names.size() + 1);
    REQUIRE(responses.size() == names.size() + 1);
    std::string unrouted;
    for (std::size_t index = 0; index < names.size(); ++index) {
        const std::string text = responseContentText(responses[index + 1]);
        if (text.find("unknown tool") != std::string::npos) unrouted += names[index] + " ";
    }
    // Advertised tools with no dispatch branch fail here, with their names.
    CHECK_EQ(unrouted, std::string());
}

TEST_CASE("devtools.mcp.initializeNegotiatesSupportedProtocol") {
    const std::vector<std::string> responses =
        runStdioSession(joinRequests({initializeRequest(1, "2025-06-18"), initializeRequest(2, "1999-01-01")}), 2);
    REQUIRE(responses.size() == 2);

    auto supported = parseObject(responses[0])->getObject("result");
    REQUIRE(supported);
    CHECK(supported->getValue<std::string>("protocolVersion") == "2025-06-18");
    CHECK(!supported->getValue<std::string>("serverInfo.name").empty());

    // An unknown revision must not be echoed back as if it were implemented.
    auto unknown = parseObject(responses[1])->getObject("result");
    REQUIRE(unknown);
    CHECK(unknown->getValue<std::string>("protocolVersion") == "2025-06-18");
}
