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

#include <squirrel.h>
#include <simplesquirrel/simplesquirrel.hpp>

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
struct EVENGINE_API_FOUNDATION_INLINE SquirrelValueOptions {
    std::size_t maxDepth    = 64;
    std::size_t maxElements = 100000;
    std::string source      = "squirrel.binding";
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
[[nodiscard]] EVENGINE_API_FOUNDATION Result<Value> valueFromSquirrel(HSQUIRRELVM vm, SQInteger index,
                                                           const SquirrelValueOptions& options = {});

/**
 * @brief Convert a rooted Squirrel object into the canonical owning Value tree.
 * @param object Rooted object whose VM owns the referenced value.
 * @param options Recursion, element-count and diagnostic-source policy.
 * @return An owning Value, or a path-aware conversion diagnostic.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<Value> valueFromSquirrel(const ssq::Object&          object,
                                                           const SquirrelValueOptions& options = {});

/**
 * @brief Push a canonical Value into the active Squirrel VM.
 * @param vm Active Squirrel VM.
 * @param value Owning value to project recursively.
 * @param options Recursion, element-count and diagnostic-source policy.
 * @return Success with exactly one value pushed, or a diagnostic; on failure
 *         the VM stack is restored to its original height.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<void> pushValue(HSQUIRRELVM vm, const Value& value,
                                                  const SquirrelValueOptions& options = {});

/** @brief Project one Diagnostic using the common script table schema. */
[[nodiscard]] EVENGINE_API_FOUNDATION ssq::Table projectDiagnostic(HSQUIRRELVM vm, const Diagnostic& diagnostic);

/** @brief Project one Status using the common script table schema. */
[[nodiscard]] EVENGINE_API_FOUNDATION ssq::Table projectStatus(HSQUIRRELVM vm, const Status& status);

/**
 * @brief Project a checked native status that carries no payload.
 * @param vm Active Squirrel VM.
 * @param status Native status copied from the checked Result.
 * @return A table with the stable Result projection schema and `hasValue = false`.
 * @remarks `ok` is derived from `status`, so a caller can no longer hand the same
 *          outcome twice and make the table contradict itself.
 */
[[nodiscard]] EVENGINE_API ssq::Table projectStatusResult(HSQUIRRELVM vm, const Status& status);

/**
 * @brief Project a checked native status together with its canonical payload.
 * @param vm Active Squirrel VM.
 * @param status Native status copied from the checked Result.
 * @param value Payload to expose; passing one sets `hasValue = true`.
 * @return A table with the stable Result projection schema.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION ssq::Table projectStatusResult(HSQUIRRELVM vm, const Status& status, const Value& value);

/**
 * @brief Project a checked native status together with an already-bound script object.
 * @param vm Active Squirrel VM.
 * @param status Native status copied from the checked Result.
 * @param value Payload produced by makeOwnedSquirrelInstance/makeOwnedProxy and friends.
 * @return A table with the stable Result projection schema.
 * @remarks Payload presence is carried by the overload; there is no flag to disagree with.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION ssq::Table projectStatusResult(HSQUIRRELVM vm, const Status& status, ssq::Object value);

/**
 * @brief Declare that a projected table received a payload the caller attached itself.
 * @param result Table returned by a payload-free projectStatusResult call.
 * @remarks For payloads that only simplesquirrel can bind (a raw registered pointer, for
 *          example). Sets `hasValue = true` so the schema stays truthful; call it in the
 *          same statement that attaches the value, never on its own.
 */
EVENGINE_API_FOUNDATION void markResultHasValue(ssq::Table& result);

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
[[nodiscard]] ssq::Table projectResult(HSQUIRRELVM vm, Result<T>&& result, Projector&& projector) {
    if (!result.ok()) return projectStatusResult(vm, result.status());

    const Status status  = result.status();
    T            payload = std::move(result).takeValue();
    return projectStatusResult(vm, status, std::invoke(std::forward<Projector>(projector), std::move(payload)));
}

/** @brief Consume and project a void native Result using the common schema. */
[[nodiscard]] EVENGINE_API_FOUNDATION ssq::Table projectResult(HSQUIRRELVM vm, Result<void>&& result);

/**
 * @brief Mark a projected Result table as intentionally ignored by script.
 * @param result Projected Result table/object returned by a checked binding.
 * @param reason Non-empty reason retained in `ignoreReason`.
 * @return True when the table was marked; false for a non-table or empty reason.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION bool ignoreResult(const ssq::Object& result, const std::string& reason);

/**
 * @brief Expose the common script helpers under `eve.result`.
 * @param eveTable Shared engine root table created by ModuleManager.
 * @remarks This is idempotent for the initial root-table exposure and must be
 *          called once by the common composition root.
 */
EVENGINE_API_FOUNDATION void exposeResultBindings(ssq::Table& eveTable);

}  // namespace eve::script
