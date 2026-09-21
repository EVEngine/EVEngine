// Capability-backed MCP domain tools.
//
// These families exist so a module that already owns a query capability is not
// invisible to an agent. Two contracts matter and are pinned here: the tool
// answers with live provider data, and when the provider is absent it says which
// capability is missing instead of silently reporting an empty world.

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Capability.h"
#include "common/DecalQuery.h"
#include "common/Module.h"
#include "common/Profile.h"
#include "common/ProfilerQuery.h"
#include "common/SensingQuery.h"
#include "decal/Decal.h"
#include "devtools/McpDomainTools.hpp"
#include "devtools/McpServer.hpp"
#include "profiler/Profiler.h"
#include "sensing/Sensing.h"

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

std::string callToolOnce(const std::string& name, const std::string& argumentsJson) {
    const std::string requests =
        std::string(
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\","
            "\"capabilities\":{},\"clientInfo\":{\"name\":\"domain-test\"}}}\n") +
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"" + name +
        "\",\"arguments\":" + argumentsJson + "}}\n";
    const std::vector<std::string> responses = runStdioSession(requests, 2);
    if (responses.size() < 2) return {};
    try {
        Poco::JSON::Parser parser;
        auto               object  = parser.parse(responses[1]).extract<Poco::JSON::Object::Ptr>();
        auto               result  = object ? object->getObject("result") : Poco::JSON::Object::Ptr();
        auto               content = result ? result->getArray("content") : Poco::JSON::Array::Ptr();
        if (!content || content->size() == 0) return {};
        return content->getObject(0)->getValue<std::string>("text");
    } catch (...) {
        return {};
    }
}

Poco::JSON::Object::Ptr parseObject(const std::string& json) {
    Poco::JSON::Parser parser;
    return parser.parse(json).extract<Poco::JSON::Object::Ptr>();
}

}  // namespace

TEST_CASE("devtools.mcp.decalToolsDriveTheLiveProvider") {
    auto* const decalModule = eve::ModuleManager::requireInstance<eve::decal::Decal>("Decal");
    REQUIRE(decalModule != nullptr);
    auto* provider = eve::cap::query<eve::IDecalQuery>();
    REQUIRE(provider != nullptr);
    provider->clearAll();
    provider->setLimit("blood", 0);  // no eviction, so counts stay predictable

    auto status = parseObject(callToolOnce("eve_decal_status", "{}"));
    REQUIRE(status);
    CHECK(status->getValue<bool>("ok"));
    CHECK_EQ(status->getValue<int>("count"), 0);

    auto projected = parseObject(callToolOnce("eve_decal_project", R"({"x":1.5,"y":0,"z":-2,"kind":"blood"})"));
    REQUIRE(projected);
    CHECK(projected->getValue<bool>("ok"));
    const int id = projected->getValue<int>("id");
    CHECK(id > 0);
    CHECK_EQ(projected->getValue<int>("count"), 1);
    CHECK_EQ(provider->count(), 1);

    // The default normal is up, so a caller that omits it gets a ground decal
    // rather than a rejected request.
    CHECK(callToolOnce("eve_decal_project", R"({"x":0,"y":0,"z":0,"kind":"blood"})").find("\"ok\":true") !=
          std::string::npos);

    const std::string removed = callToolOnce("eve_decal_remove", "{\"id\":" + std::to_string(id) + "}");
    CHECK(removed.find("\"ok\":true") != std::string::npos);
    CHECK(removed.find("\"count\":1") != std::string::npos);

    // Unknown ids and missing fields are refusals, not silent successes.
    CHECK(callToolOnce("eve_decal_remove", "{\"id\":999999}").find("\"ok\":false") != std::string::npos);
    CHECK(callToolOnce("eve_decal_remove", "{}").find("missing decal id") != std::string::npos);
    CHECK(callToolOnce("eve_decal_set_limit", "{\"kind\":\"blood\"}").find("limit must be") != std::string::npos);

    const std::string cleared = callToolOnce("eve_decal_clear", "{}");
    CHECK(cleared.find("\"ok\":true") != std::string::npos);
    CHECK_EQ(provider->count(), 0);
    CHECK(callToolOnce("eve_decal_set_limit", R"({"kind":"blood","limit":4})").find("\"ok\":true") !=
          std::string::npos);
}

TEST_CASE("devtools.mcp.decalToolsReportMissingProvider") {
    // No decal module in this process: the tool must name the absent capability
    // instead of reporting an empty decal list as if the world were clean.
    REQUIRE(eve::cap::query<eve::IDecalQuery>() == nullptr);
    const std::string status = callToolOnce("eve_decal_status", "{}");
    CHECK(status.find("\"ok\":false") != std::string::npos);
    CHECK(status.find("IDecalQuery") != std::string::npos);
    CHECK(callToolOnce("eve_decal_project", R"({"x":0,"y":0,"z":0})").find("IDecalQuery") != std::string::npos);
    CHECK(callToolOnce("eve_decal_clear", "{}").find("IDecalQuery") != std::string::npos);
}

TEST_CASE("devtools.mcp.profilerToolsReadTheLiveCore") {
    auto* const profilerModule = eve::ModuleManager::requireInstance<eve::profiler::Profiler>("Profiler");
    REQUIRE(profilerModule != nullptr);
    auto* provider = eve::cap::query<eve::IProfilerQuery>();
    REQUIRE(provider != nullptr);

    // Aggregate one synthetic frame the way the frame loop does, so the tool has
    // real zone data to report.
    profilerModule->beginFrame();
    {
        eve::prof::Profiler::zoneBegin("mcp.test.zone", "mcp.test");
        eve::prof::Profiler::zoneEnd();
    }
    profilerModule->endFrame();

    const std::string frame = callToolOnce("eve_profiler_frame", "{}");
    REQUIRE(!frame.empty());
    CHECK(frame.find("\"schema\":\"eve.profiler.frame\"") != std::string::npos);
    CHECK(frame.find("\"hasFrame\":true") != std::string::npos);
    CHECK(frame.find("mcp.test.zone") != std::string::npos);
    CHECK(frame.find("\"gpuTimingAvailable\":") != std::string::npos);

    // The text report is the same hotspot view the profiler panel shows.
    CHECK(callToolOnce("eve_profiler_report", "{}").find("mcp.test.zone") != std::string::npos);
}

TEST_CASE("devtools.mcp.spatialQueryToolsValidateAndReportMissingProviders") {
    // 3D spatial queries ride on capabilities the base test process does not
    // load, so this pins both the argument validation and the named-unavailable
    // contract for each of them.
    const std::string castMissing = callToolOnce("eve_physics_sphere_cast", R"({"from":[0,1,0],"to":[0,0,0]})");
    CHECK(castMissing.find("\"ok\":false") != std::string::npos);
    CHECK(castMissing.find("ICameraObstructionQuery") != std::string::npos);
    CHECK(callToolOnce("eve_physics_sphere_cast", "{}").find("from and to are required") != std::string::npos);

    const std::string projectMissing = callToolOnce("eve_world_project_down", R"({"x":1,"z":2})");
    CHECK(projectMissing.find("\"ok\":false") != std::string::npos);
    CHECK(projectMissing.find("IProcgenWorldQuery") != std::string::npos);

    // Both tools are advertised, so an agent can discover them before a provider
    // exists; routing is covered by devtools.mcp.everyDeclaredToolIsRouted.
    CHECK(eve::dev::isMcpDomainTool("eve_physics_sphere_cast"));
    CHECK(eve::dev::isMcpDomainTool("eve_world_project_down"));
}

TEST_CASE("devtools.mcp.sensingToolsReadLiveWorldQueries") {
    auto* const sensingModule = eve::ModuleManager::requireInstance<eve::sensing::Sensing>("Sensing");
    REQUIRE(sensingModule != nullptr);
    auto* provider = eve::cap::query<eve::ISensingQuery>();
    REQUIRE(provider != nullptr);
    CHECK_EQ(provider->worldCount(), 0);
    CHECK(provider->lastQueries().empty());

    const std::string empty = callToolOnce("eve_sensing_last_query", "{}");
    CHECK(empty.find("\"ok\":true") != std::string::npos);
    CHECK(empty.find("\"worldCount\":0") != std::string::npos);
    CHECK(callToolOnce("eve_sensing_last_query", R"({"world":3})").find("no live sensing world") != std::string::npos);
}