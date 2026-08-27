#pragma once

/**
 * @file SquirrelBinding.h
 * @brief The single Squirrel projection for common Result, Status and Value.
 *
 * The conversion surface deliberately lives in common. Domain bindings may
 * project their own checked result payload into `eve::Value`, but they must
 * not invent a second result-table schema or a second recursive Squirrel
 * value converter.
 */

#include "common/Export.h"
#include "common/Result.h"
#include "common/Value.h"

#include <simplesquirrel/simplesquirrel.hpp>
#include <squirrel.h>

#include <cstddef>
#include <functional>
#include <string>
#include <utility>

namespace eve::script {

/**
 * @brief Limits and provenance used by the Squirrel ↔ Value adapter.
 *
 * Squirrel tables and arrays are untrusted at this boundary. Conversion is
 * bounded before any recursive storage is committed; non-finite numbers,
 * unsupported object kinds and cyclic containers return diagnostics instead of
 * being silently coerced.
 */
struct EVENGINE_API SquirrelValueOptions {
    std::size_t maxDepth = 64;
    std::size_t maxElements = 100000;
    std::string source = "squirrel.binding";
};

/**
 * @brief Convert one Squirrel value into the canonical owning Value tree.
 * @param vm Active Squirrel VM; it must remain valid for the call.
 * @param index Stack index of the value; both positive and negative indices
 *              are accepted.
 * @param options Recursion, element-count and diagnostic-source policy.
 * @return An owning Value, or a path-aware conversion diagnostic.
 * @remarks The VM stack is restored to its original height before returning.
 */
[[nodiscard]] EVENGINE_API Result<Value> valueFromSquirrel(
    HSQUIRRELVM vm, SQInteger index, const SquirrelValueOptions& options = {});

/**
 * @brief Convert a rooted Squirrel object into the canonical owning Value tree.
 * @param object Rooted object whose VM owns the referenced value.
 * @param options Recursion, element-count and diagnostic-source policy.
 * @return An owning Value, or a path-aware conversion diagnostic.
 */
[[nodiscard]] EVENGINE_API Result<Value> valueFromSquirrel(
    const ssq::Object& object, const SquirrelValueOptions& options = {});

/**
 * @brief Push a canonical Value into the active Squirrel VM.
 * @param vm Active Squirrel VM.
 * @param value Owning value to project recursively.
 * @param options Recursion, element-count and diagnostic-source policy.
 * @return Success with exactly one value pushed, or a diagnostic; on failure
 *         the VM stack is restored to its original height.
 */
[[nodiscard]] EVENGINE_API Result<void> pushValue(
    HSQUIRRELVM vm, const Value& value, const SquirrelValueOptions& options = {});

/** @brief Project one Diagnostic using the common script table schema. */
[[nodiscard]] EVENGINE_API ssq::Table projectDiagnostic(
    HSQUIRRELVM vm, const Diagnostic& diagnostic);

/** @brief Project one Status using the common script table schema. */
[[nodiscard]] EVENGINE_API ssq::Table projectStatus(
    HSQUIRRELVM vm, const Status& status);

/**
 * @brief Project an already-consumed native status and optional Value payload.
 * @param vm Active Squirrel VM.
 * @param status Native status copied from the checked Result.
 * @param ok Whether the native operation completed successfully.
 * @param hasValue Whether the native Result had a payload.
 * @param value Payload to expose when `hasValue` is true.
 * @return A table with the stable Result projection schema.
 */
[[nodiscard]] EVENGINE_API ssq::Table projectStatusResult(
    HSQUIRRELVM vm, const Status& status, bool ok, bool hasValue,
    const Value& value = {});

/**
 * @brief Consume and project a value-bearing native Result.
 * @tparam T Native Result payload type.
 * @tparam Projector Pure projection from the native payload to Value.
 * @param vm Active Squirrel VM.
 * @param result Checked Result returned by a domain API.
 * @param projector Domain-specific, non-throwing data projection.
 * @return A common `{ok, code, status, diagnostics, value, ...}` table.
 * @remarks Calling this helper observes the Result on both success and
 *          failure. It therefore never relies on a domain `lastError` slot.
 */
template <class T, class Projector>
[[nodiscard]] ssq::Table projectResult(HSQUIRRELVM vm, Result<T>&& result,
                                       Projector&& projector) {
    const bool hasValue = result.ok();
    const Status status = result.status();
    if (!hasValue)
        return projectStatusResult(vm, status, false, false);

    T payload = std::move(result).takeValue();
    return projectStatusResult(
        vm, status, true, true,
        std::invoke(std::forward<Projector>(projector), std::move(payload)));
}

/** @brief Consume and project a void native Result using the common schema. */
[[nodiscard]] EVENGINE_API ssq::Table projectResult(
    HSQUIRRELVM vm, Result<void>&& result);

/**
 * @brief Mark a projected Result table as intentionally ignored by script.
 * @param result Projected Result table/object returned by a checked binding.
 * @param reason Non-empty reason retained in `ignoreReason`.
 * @return True when the table was marked; false for a non-table or empty reason.
 */
[[nodiscard]] EVENGINE_API bool ignoreResult(
    const ssq::Object& result, const std::string& reason);

/**
 * @brief Expose the common script helpers under `eve.result`.
 * @param eveTable Shared engine root table created by ModuleManager.
 * @remarks This is idempotent for the initial root-table exposure and must be
 *          called once by the common composition root.
 */
EVENGINE_API void exposeResultBindings(ssq::Table& eveTable);

}  // namespace eve::script
