#include <Poco/JSON/Parser.h>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "devtools/McpSceneLiteral.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("mcp.scene_authoring.parameters_preserve_nested_values_and_precision") {
    Poco::JSON::Parser parser;
    auto               input =
        parser.parse(R"({"id":"subject","pos":[1.123456789012345,0,0],"visible":false,"nested":{"a b":"quote\""}})");
    auto encoded = eve::dev::sceneLiteral(input);
    REQUIRE(encoded.ok());
    ssq::VM    vm(1024, ssq::Libs::ALL);
    const auto source = "parameters <- " + encoded.value() + ";";
    vm.run(vm.compileSource(source.c_str()));
    auto parameters = vm.find("parameters").toTable();
    REQUIRE_EQ(parameters.get<std::string>("id"), "subject");
    REQUIRE(!parameters.get<bool>("visible"));
    REQUIRE_EQ(parameters.get<ssq::Array>("pos").get<float>(0), static_cast<float>(1.123456789012345));
    REQUIRE_EQ(parameters.get<ssq::Table>("nested").get<std::string>("a b"), "quote\"");
    REQUIRE(!eve::dev::sceneLiteral(Poco::Dynamic::Var(std::numeric_limits<double>::infinity())).ok());
    REQUIRE(!eve::dev::sceneLiteral(input, 17).ok());
}
