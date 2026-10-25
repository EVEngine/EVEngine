// clang-format off
/* eve-test-policy:
{"schema":"evengine.test-policy","version":1,"module":"devtools"}
*/
// clang-format on
#include "devtools/McpToolFamilies.hpp"
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
