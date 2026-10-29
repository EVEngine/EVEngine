#pragma once

#include "agent/Balance.h"
#include "common/Value.h"

namespace eve::agent::balance {
/**
 * @brief Decode version-1 problem data; reject unknown schema/version/fields and invalid types.
 * @return Owning unchecked domain specification; configure validates bounds and budgets atomically.
 * @cost Linear in input size, bounded to 64 parameters, 128 targets and 128 seeds per set.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<Problem> decodeProblem(const Value& value);
/**
 * @brief Project an owning version-1 evidence report, including the full reproduction specification.
 * @cost Linear in parameters, targets and seed counts; serialize at job completion rather than per tick.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Value encodeBalanceReport(const Report& report);
}  // namespace eve::agent::balance
