#include "devtools/McpToolFamilies.hpp"

#include "devtools/Immortal.hpp"
#include "devtools/McpJson.hpp"

#include <Poco/JSON/Parser.h>
#include <set>
#include <stdexcept>

namespace eve::dev {
McpToolFamilyCatalog::McpToolFamilyCatalog(const std::vector<McpToolFamily>& families) {
    std::set<std::string_view> ids;
    // All state belongs to this unexposed object until construction succeeds.
    for (const auto& family : families) {
        if (family.id.empty() || !family.schemas || !family.route.call || !ids.insert(family.id).second) {
            throw std::invalid_argument("invalid or duplicate MCP family: " + std::string(family.id));
        }
        Poco::JSON::Array::Ptr tools;
        try {
            Poco::JSON::Parser parser;
            tools = parser.parse("[" + std::string(family.schemas()) + "]").extract<Poco::JSON::Array::Ptr>();
        } catch (const std::exception& error) {
            throw std::invalid_argument("invalid MCP schemas in " + std::string(family.id) + ": " + error.what());
        }
        for (std::size_t i = 0; i < tools->size(); ++i) {
            const auto tool = tools->getObject(static_cast<unsigned>(i));
            if (!tool || !tool->has("name") || !tool->get("name").isString() || !tool->getObject("inputSchema")) {
                throw std::invalid_argument("MCP schema needs a string name and object inputSchema: " +
                                            std::string(family.id));
            }
            const auto name = tool->getValue<std::string>("name");
            if (name.empty() || !routes_.emplace(name, family.route).second) {
                throw std::invalid_argument("empty or duplicate MCP tool name: " + name);
            }
            if (!schemas_.empty()) schemas_ += ',';
            schemas_ += mcpStringify(Poco::Dynamic::Var(tool));
        }
    }
}

std::optional<McpToolRoute> McpToolFamilyCatalog::find(std::string_view name) const {
    const auto found = routes_.find(name);
    if (found == routes_.end()) return std::nullopt;
    return found->second;
}

const McpToolFamilyCatalog& compiledMcpToolCatalog() {
    // The detached stdio reader can outlive normal static teardown, exactly
    // like McpServer. Keep its immutable routing snapshot alive with that host.
    struct CompiledCatalog {
        McpToolFamilyCatalog value{compiledMcpToolFamilies()};
    };
    return Immortal<CompiledCatalog>::get().value;
}
}  // namespace eve::dev
