// MCP playtest input tools: inject via IInputInjection, defer wait/capture across
// McpServer::poll ticks, and degrade with named unavailable when the provider is
// absent.

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Capability.h"
#include "common/InputInjection.h"
#include "common/Result.h"
#include "devtools/McpInputTools.hpp"
#include "devtools/McpServer.hpp"

#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>

#include <chrono>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace eve::dev;

namespace {

class FakeInputInjection final : public eve::IInputInjection {
public:
    int keys = 0;
    int moves = 0;
    int buttons = 0;
    int wheels = 0;
    int texts = 0;
    std::string lastKey;
    bool lastDown = false;

    [[nodiscard]] eve::Result<void> injectKey(std::string_view key, bool down, bool /*repeat*/) override {
        ++keys;
        lastKey = std::string(key);
        lastDown = down;
        return eve::Result<void>::success();
    }
    [[nodiscard]] eve::Result<void> injectMouseButton(int /*button*/, bool /*down*/, float /*x*/,
                                                      float /*y*/) override {
        ++buttons;
        return eve::Result<void>::success();
    }
    [[nodiscard]] eve::Result<void> injectMouseMove(float /*x*/, float /*y*/, float /*dx*/,
                                                    float /*dy*/) override {
        ++moves;
        return eve::Result<void>::success();
    }
    [[nodiscard]] eve::Result<void> injectWheel(float /*x*/, float /*y*/) override {
        ++wheels;
        return eve::Result<void>::success();
    }
    [[nodiscard]] eve::Result<void> injectText(std::string_view /*text*/) override {
        ++texts;
        return eve::Result<void>::success();
    }
};

std::vector<std::string> runStdioSession(const std::string& requests, std::size_t expectedResponses,
                                         bool resetBatches = true) {
    auto& mcp = McpServer::instance();
    mcp.stop();
    if (resetBatches) resetMcpInputBatchesForTest();
    std::istringstream       in(requests);
    std::ostringstream       out;
    const bool               started = mcp.listenStdio(in, out);
    std::vector<std::string> responses;
    if (started) {
        // Drain queued stdio requests once we have enough replies. Extra poll()
        // ticks would advance pending eve_input_batch waitFrames, so stop as
        // soon as the expected responses arrive.
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

std::string contentTextFromResponse(const std::string& line) {
    try {
        Poco::JSON::Parser parser;
        auto               object  = parser.parse(line).extract<Poco::JSON::Object::Ptr>();
        auto               result  = object ? object->getObject("result") : Poco::JSON::Object::Ptr();
        auto               content = result ? result->getArray("content") : Poco::JSON::Array::Ptr();
        if (!content || content->size() == 0) return {};
        return content->getObject(0)->getValue<std::string>("text");
    } catch (...) {
        return {};
    }
}

std::string callToolOnce(const std::string& name, const std::string& argumentsJson, bool resetBatches = true) {
    const std::string requests =
        std::string(
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\","
            "\"capabilities\":{},\"clientInfo\":{\"name\":\"input-test\"}}}\n") +
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"" + name +
        "\",\"arguments\":" + argumentsJson + "}}\n";
    const std::vector<std::string> responses = runStdioSession(requests, 2, resetBatches);
    if (responses.size() < 2) return {};
    return contentTextFromResponse(responses[1]);
}

Poco::JSON::Object::Ptr parseObject(const std::string& json) {
    Poco::JSON::Parser parser;
    return parser.parse(json).extract<Poco::JSON::Object::Ptr>();
}

}  // namespace

TEST_CASE("devtools.mcp.inputBatchInjectsThroughCapability") {
    resetMcpInputBatchesForTest();
    FakeInputInjection fake;
    eve::cap::provide<eve::IInputInjection>(&fake);

    auto status = parseObject(callToolOnce("eve_input_status", "{}"));
    REQUIRE(status);
    CHECK(status->getValue<bool>("ok"));
    CHECK(status->getValue<bool>("injection"));

    auto batch = parseObject(callToolOnce(
        "eve_input_batch",
        R"({"actions":[{"op":"key","key":"W","down":true},{"op":"key","key":"W","down":false},{"op":"mouse_move","x":10,"y":20},{"op":"wheel","y":-1},{"op":"text","text":"hi"}]})"));
    REQUIRE(batch);
    CHECK(batch->getValue<bool>("ok"));
    CHECK_EQ(batch->getValue<std::string>("status"), std::string("ready"));
    CHECK_EQ(batch->getValue<int>("injected"), 5);
    CHECK_EQ(fake.keys, 2);
    CHECK_EQ(fake.moves, 1);
    CHECK_EQ(fake.wheels, 1);
    CHECK_EQ(fake.texts, 1);
    CHECK_EQ(fake.lastKey, std::string("W"));
    CHECK(!fake.lastDown);

    eve::cap::revoke<eve::IInputInjection>(&fake);
    resetMcpInputBatchesForTest();
}

TEST_CASE("devtools.mcp.inputBatchWaitCompletesAcrossPoll") {
    // Drive the satellite API directly: stdio reader threads enqueue every
    // pre-buffered line at once, so a multi-call wait test cannot stage
    // idle poll ticks between tools/call requests.
    resetMcpInputBatchesForTest();
    FakeInputInjection fake;
    eve::cap::provide<eve::IInputInjection>(&fake);

    Poco::JSON::Parser        parser;
    Poco::JSON::Object::Ptr   args = parser.parse(R"({"actions":[{"op":"key","key":"A","down":true},{"op":"wait","frames":2}]})")
                                         .extract<Poco::JSON::Object::Ptr>();
    const std::string         batchEnvelope = callMcpInputTool("eve_input_batch", args);
    // Satellite tools return a full tools/call content envelope.
    auto                      batchResult   = parser.parse(batchEnvelope).extract<Poco::JSON::Object::Ptr>();
    REQUIRE(batchResult);
    auto batchContent = batchResult->getArray("content");
    REQUIRE(batchContent);
    auto batch = parseObject(batchContent->getObject(0)->getValue<std::string>("text"));
    REQUIRE(batch);
    CHECK(batch->getValue<bool>("ok"));
    CHECK_EQ(batch->getValue<std::string>("status"), std::string("pending"));
    const auto id = batch->getValue<Poco::Int64>("id");
    CHECK(id > 0);

    Poco::JSON::Object::Ptr pollArgs = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    pollArgs->set("id", static_cast<Poco::Int64>(id));
    auto pendingEnvelope = callMcpInputTool("eve_input_poll", pollArgs);
    auto pendingResult   = parser.parse(pendingEnvelope).extract<Poco::JSON::Object::Ptr>();
    auto pending =
        parseObject(pendingResult->getArray("content")->getObject(0)->getValue<std::string>("text"));
    REQUIRE(pending);
    CHECK_EQ(pending->getValue<std::string>("status"), std::string("pending"));

    pollMcpInputBatches();  // 2 -> 1
    pollMcpInputBatches();  // 1 -> 0, ready

    auto readyEnvelope = callMcpInputTool("eve_input_poll", pollArgs);
    auto readyResult   = parser.parse(readyEnvelope).extract<Poco::JSON::Object::Ptr>();
    auto ready = parseObject(readyResult->getArray("content")->getObject(0)->getValue<std::string>("text"));
    REQUIRE(ready);
    CHECK_EQ(ready->getValue<std::string>("status"), std::string("ready"));
    CHECK(ready->getValue<bool>("ok"));
    CHECK_EQ(ready->getValue<Poco::Int64>("id"), id);

    eve::cap::revoke<eve::IInputInjection>(&fake);
    resetMcpInputBatchesForTest();
}

TEST_CASE("devtools.mcp.inputBatchReportsMissingProvider") {
    resetMcpInputBatchesForTest();
    // Ensure no leftover provider from another case.
    if (auto* existing = eve::cap::query<eve::IInputInjection>()) {
        eve::cap::revoke<eve::IInputInjection>(existing);
    }

    auto batch =
        parseObject(callToolOnce("eve_input_batch", R"({"actions":[{"op":"key","key":"A","down":true}]})"));
    REQUIRE(batch);
    CHECK(!batch->getValue<bool>("ok"));
    if (!batch->has("unavailable")) {
        CHECK(batch->has("error"));
    }
    CHECK(batch->has("hint"));
}
