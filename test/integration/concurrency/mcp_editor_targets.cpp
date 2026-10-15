// Editor target discovery over MCP.
//
// Before eve_editor_target_list an agent could neither enumerate the targets a
// project script had bound (a tile layer, a height map, a voxel world) nor learn
// which `type` values eve_editor_target_create accepts: the schema hard-coded a
// four-name enum that had already drifted from the loaded adapters. These cases
// pin the discovery contract and the drift-proof type source.

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Capability.h"
#include "common/EditorAutomation.h"
#include "devtools/McpServer.hpp"
#include "editor/Editor.h"
#include "voxel/editor/VoxelEditorModule.h"

#include <Poco/Dynamic/Var.h>
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
           "\"protocolVersion\":\"2025-06-18\",\"capabilities\":{},\"clientInfo\":{\"name\":\"target-test\"}}}";
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

/** Text payload of one tools/call round trip over a fresh stdio session. */
std::string callToolOnce(const std::string& name, const std::string& argumentsJson) {
    const std::vector<std::string> responses =
        runStdioSession(joinRequests({initializeRequest(1), callRequest(2, name, argumentsJson)}), 2);
    if (responses.size() < 2) return {};
    try {
        auto object  = parseObject(responses[1]);
        auto result  = object ? object->getObject("result") : Poco::JSON::Object::Ptr();
        auto content = result ? result->getArray("content") : Poco::JSON::Array::Ptr();
        if (!content || content->size() == 0) return {};
        return content->getObject(0)->getValue<std::string>("text");
    } catch (...) {
        return {};
    }
}

/** Entry of the `targets` array with the given id, or null. */
Poco::JSON::Object::Ptr findTarget(const std::string& listText, const std::string& id) {
    try {
        auto object  = parseObject(listText);
        auto targets = object ? object->getArray("targets") : Poco::JSON::Array::Ptr();
        if (!targets) return {};
        for (std::size_t index = 0; index < targets->size(); ++index) {
            auto item = targets->getObject(static_cast<unsigned>(index));
            if (item && item->optValue<std::string>("id", "") == id) return item;
        }
    } catch (...) {
    }
    return {};
}

}  // namespace

TEST_CASE("devtools.mcp.editorTargetListReportsRuntimeTypes") {
    auto& mcp = McpServer::instance();
    mcp.stop();
    eve::editor::Editor editor;

    // With no adapter loaded the advertised set is empty: it is a query of the
    // loaded factories, never a constant copied from the old schema.
    const std::string bare = callToolOnce("eve_editor_target_list", "{}");
    REQUIRE(!bare.empty());
    CHECK(bare.find("supportedTypes") != std::string::npos);
    CHECK(bare.find("voxel-catalog") == std::string::npos);

    {
        eve::voxel_editor::VoxelEditorModule adapter;

        const std::string listed = callToolOnce("eve_editor_target_list", "{}");
        CHECK(listed.find("voxel-catalog") != std::string::npos);
        CHECK(listed.find("voxel-model") != std::string::npos);
        CHECK(findTarget(listed, "agent.voxel") == nullptr);  // not created yet

        const std::string created =
            callToolOnce("eve_editor_target_create", R"({"target":"agent.voxel","type":"voxel-catalog"})");
        REQUIRE(created.find("\"status\":\"applied\"") != std::string::npos);

        const std::string afterCreate = callToolOnce("eve_editor_target_list", "{}");
        auto              entry       = findTarget(afterCreate, "agent.voxel");
        REQUIRE(entry);
        CHECK(entry->getValue<std::string>("type") == "voxel-catalog");
        CHECK(entry->optValue<bool>("owned", false));
        CHECK(entry->has("revision"));
        CHECK(entry->has("generation"));

        const std::string closed = callToolOnce("eve_editor_target_close", R"({"target":"agent.voxel"})");
        REQUIRE(closed.find("\"status\":\"applied\"") != std::string::npos);
        CHECK(findTarget(callToolOnce("eve_editor_target_list", "{}"), "agent.voxel") == nullptr);
    }

    // Unloading the adapter removes its types from discovery: the list tracks the
    // loaded providers instead of drifting from them.
    const std::string afterUnload = callToolOnce("eve_editor_target_list", "{}");
    CHECK(afterUnload.find("voxel-catalog") == std::string::npos);
}

TEST_CASE("devtools.mcp.editorTargetListRejectsUnknownType") {
    auto& mcp = McpServer::instance();
    mcp.stop();
    eve::editor::Editor editor;

    // An unknown type must be refused with a diagnostic naming the requested
    // value; discovering the accepted set is eve_editor_target_list's job.
    const std::string created =
        callToolOnce("eve_editor_target_create", R"({"target":"agent.nope","type":"definitely-not-a-type"})");
    CHECK(created.find("definitely-not-a-type") != std::string::npos);
    CHECK(created.find("\"status\":\"applied\"") == std::string::npos);
    CHECK(findTarget(callToolOnce("eve_editor_target_list", "{}"), "agent.nope") == nullptr);
}
