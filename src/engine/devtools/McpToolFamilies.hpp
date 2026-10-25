#pragma once

#include "common/Export.h"

#include <Poco/JSON/Object.h>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace eve::dev {
class McpServer;

/** @brief Wire shape returned by a compiled tool provider. */
enum class McpToolPayload { TextValue, ContentEnvelope };

/** @brief Owned route value; callbacks belong to the compiled binary, never a reloadable plugin. */
struct McpToolRoute {
    std::string (*call)(McpServer&, std::string_view, Poco::JSON::Object::Ptr);
    McpToolPayload payload;
};

/** @brief Process-lifetime compiled family descriptor, generated from its provider declaration. */
struct McpToolFamily {
    std::string_view id;
    std::string_view (*schemas)();
    McpToolRoute route;
};

/**
 * @brief Immutable schema/route catalogue built transactionally from compiled families.
 * @ownership Owns schema strings and tool-name keys; function pointers remain binary-owned.
 * @lifetime Immutable until destruction; not a plugin/unload registration mechanism.
 * @thread Construction is caller-owned; concurrent const reads are safe afterward.
 * @reentrancy Schema callbacks run without locks and must not request this catalogue while it is building.
 */
class EVENGINE_API_FOUNDATION McpToolFamilyCatalog {
public:
    /**
     * @brief Build and validate one complete snapshot, publishing nothing on failure.
     * @param families Borrowed during construction; descriptor contents are copied.
     * @throws std::invalid_argument On missing callbacks, malformed schemas or duplicate family/tool names.
     * @cost Linear schema parsing and route indexing per build, amortized over all subsequent requests.
     */
    explicit McpToolFamilyCatalog(const std::vector<McpToolFamily>& families);
    /** @brief Borrow immutable comma-separated schemas until this catalogue is destroyed. */
    [[nodiscard]] std::string_view schemas() const noexcept { return schemas_; }
    /** @brief Return an owned route copy, or no route for an unknown name; safe for concurrent const reads. */
    [[nodiscard]] std::optional<McpToolRoute> find(std::string_view name) const;

private:
    std::string                                      schemas_;
    std::map<std::string, McpToolRoute, std::less<>> routes_;
};

/**
 * @brief Build the process-immortal compiled catalogue once, then borrow it until exit.
 * @lifetime Survives static teardown together with McpServer's detached stdio reader.
 * @thread Synchronized first construction; schema callbacks cannot reenter this function during that build.
 * @throws std::invalid_argument If a compiled declaration violates the shared catalogue contract.
 * @cost First access parses all schemas; subsequent requests reuse the immutable snapshot.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION const McpToolFamilyCatalog& compiledMcpToolCatalog();

/** @brief Generated build input; returns binary-owned descriptors for exactly the compiled providers.
 * @cost Copies a descriptor per compiled family, once per catalogue construction.
 */
[[nodiscard]] std::vector<McpToolFamily> compiledMcpToolFamilies();

namespace detail {
/** @brief Adapt existing two-argument families and context-aware core handlers without retaining request data. */
template <auto Handler>
std::string invokeMcpFamily(McpServer& server, std::string_view name, Poco::JSON::Object::Ptr args) {
    if constexpr (std::is_invocable_v<decltype(Handler), McpServer&, std::string_view, Poco::JSON::Object::Ptr>) {
        return Handler(server, name, std::move(args));
    } else {
        return Handler(name, std::move(args));
    }
}
}  // namespace detail

/** @brief Declare one family beside its implementation; CMake derives composition from compiled sources. */
#define EVE_MCP_TOOL_FAMILY(Id, Schemas, Handler, Payload)                                     \
    McpToolFamily eveMcpToolFamily_##Id() {                                                    \
        return {#Id, &Schemas, {&detail::invokeMcpFamily<&Handler>, McpToolPayload::Payload}}; \
    }
}  // namespace eve::dev
