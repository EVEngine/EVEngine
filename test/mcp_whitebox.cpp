// MCP white-box batch + playtest tools (scene_director.batch / eve_whitebox_playtest).
#include "devtools/McpInputTools.hpp"
#include "devtools/McpWhiteboxTools.hpp"
#include "devtools/McpToolFilter.hpp"

#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <string>

using eve::dev::callMcpWhiteboxTool;
using eve::dev::isMcpWhiteboxTool;
using eve::dev::mcpToolMutates;
using eve::dev::mcpToolTags;
using eve::dev::mcpWhiteboxToolSchemas;
using eve::dev::resetMcpInputBatchesForTest;

namespace {

Poco::JSON::Object::Ptr parseObject(const std::string& json) {
    Poco::JSON::Parser parser;
    auto               root = parser.parse(json);
    if (root.type() != typeid(Poco::JSON::Object::Ptr)) return {};
    return root.extract<Poco::JSON::Object::Ptr>();
}

Poco::JSON::Object::Ptr unwrapToolText(const std::string& envelope) {
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
