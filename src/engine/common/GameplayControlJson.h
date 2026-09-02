#pragma once

/**
 * @file GameplayControlJson.h
 * @brief Versioned JSON facade shared by scripting, MCP and scenario tooling.
 */

#include "common/GameplayControl.h"

namespace eve {

/**
 * @brief Routes one versioned owning request to the uniquely matching gameplay provider.
 * @param request Request object with schema id `evengine.gameplay-control-request` and version 1.
 * @return Owning response object, or structured parse/routing/domain diagnostics.
 * @remarks Unknown root fields are rejected. Provider callbacks execute synchronously on
 *          the gameplay owner thread; this function retains no request or provider pointer.
 */
[[nodiscard]] Result<Value> executeGameplayControlRequest(const Value& request);

/**
 * @brief Parse, execute and serialize one gameplay-control JSON request.
 * @param requestJson Strict UTF-8 JSON request.
 * @return Deterministic compact response JSON or structured diagnostics.
 */
[[nodiscard]] Result<std::string> executeGameplayControlJson(std::string_view requestJson);

}  // namespace eve
