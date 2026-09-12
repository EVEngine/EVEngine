#pragma once

#include "common/Result.h"
#include "procgen/MeshBuild.h"

#include <string_view>

namespace eve::procgen {

/**
 * @brief Evaluate a closed-triangle solid boolean using a BSP polygon split.
 * @param left Owning input is not retained; triangle attributes are copied.
 * @param right Cutter/second solid, likewise borrowed only for this call.
 * @param operation `union`, `difference`, or `intersection`.
 * @return An owning triangle mesh, or a diagnostic without mutating either input.
 * @thread Pure CPU operation; safe for distinct inputs on worker threads.
 * @reentrancy Does not invoke callbacks.
 */
[[nodiscard]] Result<MeshBuild> meshBooleanResult(const MeshBuild& left, const MeshBuild& right,
                                                  std::string_view operation);

}  // namespace eve::procgen
