// clang-format off
/* eve-test-policy:
{"schema":"evengine.test-policy","version":1,"module":"devtools"}
*/
// clang-format on
#include "common/Capability.h"
#include "common/SceneSearch.h"
#include "devtools/McpServer.hpp"
#include "devtools/McpTaskProfile.hpp"
#include "devtools/McpToolFamilies.hpp"
#include "devtools/McpToolFilter.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <Poco/JSON/Array.h>
#include <Poco/JSON/Parser.h>
#include <set>

TEST_CASE("devtools.mcp.compiledCatalogueHasExactlyOneRoutePerListedTool") {
    const auto&        catalog = eve::dev::compiledMcpToolCatalog();
    Poco::JSON::Parser parser;
    const auto tools = parser.parse("[" + std::string(catalog.schemas()) + "]").extract<Poco::JSON::Array::Ptr>();
    REQUIRE(tools);
    REQUIRE(tools->size() > 0);
    std::set<std::string> names;
    for (std::size_t i = 0; i < tools->size(); ++i) {
        const auto tool = tools->getObject(static_cast<unsigned>(i));
        REQUIRE(tool);
        const auto name = tool->getValue<std::string>("name");
        REQUIRE(names.insert(name).second);
        const auto route = catalog.find(name);
        REQUIRE(route.has_value());
        REQUIRE(static_cast<bool>(route->call));
    }
    REQUIRE(names.count("eve_status") == 1);
    REQUIRE(names.count("eve_api_search") == 1);
    REQUIRE(!catalog.find("eve_nonexistent_contract_tool"));
}

TEST_CASE("devtools.mcp.sceneSearchIsDiscoverableReadOnlyAndProviderOptional") {
    const auto& catalog = eve::dev::compiledMcpToolCatalog();
    for (const auto* name :
         {"eve_scene_search_prepare", "eve_scene_search_status", "eve_scene_search_publish", "eve_scene_search"}) {
        CHECK(catalog.find(name).has_value());
        CHECK(!eve::dev::mcpToolMutates(name));
        CHECK(eve::dev::mcpProfileAllowsTool("verify.play", name));
        CHECK(eve::dev::mcpProfileAllowsTool("content.scene", name));
    }
    auto* previous = eve::cap::query<eve::ISceneSearch>();
    struct Restore {
        eve::ISceneSearch* previous;
        ~Restore() {
            if (previous) eve::cap::provide<eve::ISceneSearch>(previous);
        }
    } restore{previous};
    if (previous) eve::cap::revoke<eve::ISceneSearch>(previous);
    const auto route = catalog.find("eve_scene_search_prepare");
    REQUIRE(route.has_value());
    Poco::JSON::Object::Ptr args = new Poco::JSON::Object;
    args->set("host", "absent-host");
    Poco::JSON::Parser parser;
    const auto response = parser.parse(route->call(eve::dev::McpServer::instance(), "eve_scene_search_prepare", args))
                              .extract<Poco::JSON::Object::Ptr>();
    CHECK(response->getValue<bool>("isError"));
    const auto payload = parser.parse(response->getArray("content")->getObject(0)->getValue<std::string>("text"))
                             .extract<Poco::JSON::Object::Ptr>();
    CHECK_EQ(payload->getValue<std::string>("error"), std::string("scene-unavailable"));
    args->set("unknown", 1);
    const auto malformed = route->call(eve::dev::McpServer::instance(), "eve_scene_search_prepare", args);
    CHECK(malformed.find("invalid_argument") != std::string::npos);
}
