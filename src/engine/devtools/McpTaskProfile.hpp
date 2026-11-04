#pragma once

/**
 * @file McpTaskProfile.hpp
 * @brief Named Agent task profiles that narrow the MCP tool surface.
 *
 * Profiles stack with `EVE_MCP_TOOLSET` / `READONLY` / `EXCLUDE`:
 *   allowed = envFilter ∧ processProfile ∧ sessionProfile
 *
 * Process profile: `EVE_MCP_PROFILE` (connection-time, e.g. eve-mcp --profile=).
 * Session profile: set by `eve_agent_session_start` / cleared on reset.
 * Call-time enforcement does not depend on Cursor refreshing tools/list.
 *
 * @see docs/dev/2026-10-20-agent-task-profile-mcp-plan.md
 */

#include "common/Export.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eve::dev {

/** @brief Suggested profile change when a call is rejected by the active profile. */
struct McpProfileEscalate {
    std::string to;
    std::string reason;
    std::string errorCode;
};

/** @brief Proxy counters for profile A/B comparisons (not billing truth). */
struct McpFilterStats {
    std::uint64_t toolCalls         = 0;
    std::uint64_t toolsFiltered     = 0;
    std::uint64_t escalateCount     = 0;
    std::uint64_t approxResultBytes = 0;
};

/**
 * @brief Resolve taskKind + optional profile into a concrete profile id.
 * @param taskKind Empty defaults to `verify`.
 * @param profile Empty selects the taskKind default profile.
 * @param outProfile Receives the resolved profile id on success.
 * @param outTaskKind Receives the resolved taskKind on success.
 * @param errorCode Receives a stable rejection code on failure.
 * @param errorMessage Receives a human-readable rejection on failure.
 * @return true when both values are valid and consistent.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION bool mcpResolveTaskProfile(std::string_view taskKind, std::string_view profile,
                                                                 std::string* outProfile, std::string* outTaskKind,
                                                                 std::string* errorCode, std::string* errorMessage);

/** @brief Tag list advertised for `EVE_MCP_TOOLSET` when only a named profile is chosen. */
[[nodiscard]] EVENGINE_API_FOUNDATION std::vector<std::string> mcpProfileToolsetTags(std::string_view profile);

/** @brief Comma-joined toolset tags for env forwarding (`eve-mcp --profile=`). */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string mcpProfileToolsetEnv(std::string_view profile);

/** @brief Whether `tool` is allowed under a concrete named profile (empty = no profile gate). */
[[nodiscard]] EVENGINE_API_FOUNDATION bool mcpProfileAllowsTool(std::string_view profile, std::string_view tool);

/**
 * @brief Suggest an escalate edge when `tool` is denied by `profile`.
 * @return Empty when no legal edge exists for this denial.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION std::optional<McpProfileEscalate> mcpProfileEscalateHint(std::string_view profile,
                                                                                               std::string_view tool);

/** @brief Install or clear the process-level profile (`EVE_MCP_PROFILE` mirror for tests). */
EVENGINE_API_FOUNDATION void mcpSetProcessProfile(std::string_view profile);

/** @brief Current process-level profile; empty when unset. */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string mcpProcessProfile();

/** @brief Install or clear the session-level profile overlay. */
EVENGINE_API_FOUNDATION void mcpSetSessionProfile(std::string_view profile);

/** @brief Current session-level profile; empty when no session overlay. */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string mcpSessionProfile();

/** @brief Record a tools/call attempt for proxy metrics. */
EVENGINE_API_FOUNDATION void mcpFilterNoteCall(bool allowed, std::size_t resultBytes, bool offeredEscalate);

/** @brief Snapshot of proxy metrics since process start / last test reset. */
[[nodiscard]] EVENGINE_API_FOUNDATION McpFilterStats mcpFilterStats();

/** @brief Test helper: zero proxy metrics. */
EVENGINE_API_FOUNDATION void mcpFilterStatsResetForTest();

}  // namespace eve::dev
