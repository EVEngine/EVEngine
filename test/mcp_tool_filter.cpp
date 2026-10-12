// MCP toolset / read-only / exclude filters: tools/list and tools/call must agree.

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "devtools/McpServer.hpp"
#include "devtools/McpToolFilter.hpp"

#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>

#include <chrono>
#include <cstdlib>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace eve::dev;

namespace {

#if defined(_WIN32)
int setEnv(const char* key, const char* value) { return _putenv_s(key, value ? value : ""); }
void unsetEnv(const char* key) { _putenv_s(key, ""); }
#else
int setEnv(const char* key, const char* value) { return setenv(key, value, 1); }
void unsetEnv(const char* key) { unsetenv(key); }
#endif

struct EnvGuard {
    explicit EnvGuard(std::initializer_list<std::pair<const char*, const char*>> values) {
        for (const auto& [key, value] : values) {
            keys.push_back(key);
            if (value) setEnv(key, value);
            else unsetEnv(key);
        }
        mcpToolFilterReloadForTest();
    }
    ~EnvGuard() {
        for (const char* key : keys) unsetEnv(key);
        mcpToolFilterReloadForTest();
    }
    std::vector<const char*> keys;
};

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

bool toolsListContains(const std::string& name) {
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

std::string callToolText(const std::string& name) {
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
    CHECK(mcpToolTags("eve_host_status")[0] == "host");
    CHECK(mcpToolMutates("eve_eval"));
    CHECK(!mcpToolMutates("eve_status"));
    CHECK(!mcpToolMutates("eve_input_poll"));
}

TEST_CASE("devtools.mcp.toolsetFilterHidesAndRejects") {
    {
        EnvGuard env{{{"EVE_MCP_TOOLSET", "core"}, {"EVE_MCP_READONLY", nullptr}, {"EVE_MCP_EXCLUDE", nullptr}}};
        CHECK(mcpToolAllowed("eve_status"));
        CHECK(!mcpToolAllowed("eve_eval"));
        CHECK(toolsListContains("eve_status"));
        CHECK(!toolsListContains("eve_eval"));
        const std::string text = callToolText("eve_eval");
        CHECK(text.find("\"filtered\":true") != std::string::npos);
        CHECK(text.find("hint") != std::string::npos);
    }
    {
        EnvGuard env{{{"EVE_MCP_TOOLSET", "all"}, {"EVE_MCP_READONLY", "1"}, {"EVE_MCP_EXCLUDE", nullptr}}};
        CHECK(mcpToolAllowed("eve_status"));
        CHECK(!mcpToolAllowed("eve_eval"));
        CHECK(!mcpToolAllowed("eve_input_batch"));
        CHECK(mcpToolAllowed("eve_input_poll"));
    }
    {
        EnvGuard env{{{"EVE_MCP_TOOLSET", "all"},
                      {"EVE_MCP_READONLY", nullptr},
                      {"EVE_MCP_EXCLUDE", "eve_status,eve_restart"}}};
        CHECK(!mcpToolAllowed("eve_status"));
        CHECK(!mcpToolAllowed("eve_restart"));
        CHECK(mcpToolAllowed("eve_console_read"));
    }
}
