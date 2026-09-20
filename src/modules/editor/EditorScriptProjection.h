#pragma once

#include "common/Result.h"
#include "common/SquirrelBinding.h"
#include "common/Value.h"

#include <utility>

namespace eve::editor {

/**
 * @brief Project a completed editing result that has no payload.
 * @param vm Active Squirrel VM.
 * @param result Checked editing result.
 * @return The common script result table, with `ok` and `hasValue` derived from `result`.
 * @remarks One shared definition replaces the copy each `*EditorScriptBindings.cpp`
 *          used to carry, so the projection rules cannot drift apart per domain.
 */
[[nodiscard]] inline ssq::Table project(HSQUIRRELVM vm, const eve::Result<void>& result) {
    return script::projectStatusResult(vm, result.status());
}

/**
 * @brief Project a completed editing result together with its already-converted payload.
 * @param vm Active Squirrel VM.
 * @param result Checked editing result.
 * @param value Payload to expose; it is attached only for a successful result.
 * @return The common script result table.
 * @remarks `hasValue` follows the attached payload, so a failed result cannot claim one.
 */
template <class T>
[[nodiscard]] inline ssq::Table project(HSQUIRRELVM vm, const eve::Result<T>& result, Value value) {
    if (!result.ok()) return script::projectStatusResult(vm, result.status());
    return script::projectStatusResult(vm, result.status(), std::move(value));
}

}  // namespace eve::editor
