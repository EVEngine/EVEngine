// clang-format off
/* eve-test-policy:
{"schema":"evengine.test-policy","version":1,"role":"probe","module":"devtools","default":"local-logic"}
*/
// clang-format on
#include "devtools/McpToolFamilies.hpp"

#include <Poco/JSON/Array.h>
#include <Poco/JSON/Parser.h>
#include <iostream>
#include <stdexcept>

namespace eve::dev {
// Context is passed through; this isolated contract needs no live server.
class McpServer {};
}  // namespace eve::dev
namespace {
void require(bool value) {
    if (!value) throw std::runtime_error("MCP composition contract failed");
}
std::string_view schemas() { return R"({"name":"eve_contract","inputSchema":{"type":"object"}})"; }
std::string_view malformed() { return "not JSON"; }
std::string_view wrongShape() { return R"({"name":"eve_bad","inputSchema":42})"; }
std::string call(eve::dev::McpServer&, std::string_view name, Poco::JSON::Object::Ptr) { return std::string(name); }
void        rejected(const std::vector<eve::dev::McpToolFamily>& families) {
    try {
        const eve::dev::McpToolFamilyCatalog catalog(families);
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("invalid catalogue was accepted");
}
}  // namespace
int main() {
    using namespace eve::dev;
    try {
        const McpToolFamily        family{"contract", schemas, {call, McpToolPayload::ContentEnvelope}};
        const McpToolFamilyCatalog catalog({family});
        McpServer                  server;
        const auto                 route = catalog.find("eve_contract");
        require(route.has_value());
        require(route->payload == McpToolPayload::ContentEnvelope);
        require(route->call(server, "eve_contract", {}) == "eve_contract");
        require(!catalog.find("eve_unknown"));
        Poco::JSON::Parser parser;
        require(parser.parse("[" + std::string(catalog.schemas()) + "]").extract<Poco::JSON::Array::Ptr>()->size() ==
                1);
        rejected({family, family});
        auto other = family;
        other.id   = "another";
        rejected({family, other});
        other.schemas = malformed;
        rejected({other});
        other.schemas = wrongShape;
        rejected({other});
        other.schemas = nullptr;
        rejected({other});
        other            = family;
        other.route.call = nullptr;
        rejected({other});
        const auto& compiled = compiledMcpToolCatalog();
        const auto  optional = compiled.find("eve_optional");
        require(optional.has_value() == static_cast<bool>(MCP_EXPECT_PROVIDER));
        require(compiled.schemas().empty() == !static_cast<bool>(MCP_EXPECT_PROVIDER));
        if (optional) {
            require(optional->payload == McpToolPayload::TextValue);
            require(optional->call(server, "eve_optional", {}) == "eve_optional:empty");
            require(optional->call(server, "eve_optional", new Poco::JSON::Object()) == "eve_optional:args");
        }
        std::cout << "MCP catalogue, routing and provider-absence contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
