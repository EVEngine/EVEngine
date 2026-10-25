// clang-format off
/* eve-test-policy:
{"schema":"evengine.test-policy","version":1,"role":"helper","module":"devtools","default":"local-logic"}
*/
// clang-format on
#include "devtools/McpToolFamilies.hpp"

namespace eve::dev {
namespace {
std::string_view schemas() { return R"({"name":"eve_optional","inputSchema":{"type":"object"}})"; }
std::string      call(std::string_view name, Poco::JSON::Object::Ptr args) {
    return std::string(name) + (args ? ":args" : ":empty");
}
}  // namespace
EVE_MCP_TOOL_FAMILY(ContractOptional, schemas, call, TextValue)
}  // namespace eve::dev
