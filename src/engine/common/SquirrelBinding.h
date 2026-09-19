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
#include <type_traits>
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
[[nodiscard]] EVENGINE_API Result<Value> valueFromSquirrel(HSQUIRRELVM vm, SQInteger index,
                                                           const SquirrelValueOptions& options = {});

/**
 * @brief Convert a rooted Squirrel object into the canonical owning Value tree.
 * @param object Rooted object whose VM owns the referenced value.
 * @param options Recursion, element-count and diagnostic-source policy.
 * @return An owning Value, or a path-aware conversion diagnostic.
 */
[[nodiscard]] EVENGINE_API Result<Value> valueFromSquirrel(const ssq::Object&          object,
                                                           const SquirrelValueOptions& options = {});

/**
 * @brief Push a canonical Value into the active Squirrel VM.
 * @param vm Active Squirrel VM.
 * @param value Owning value to project recursively.
 * @param options Recursion, element-count and diagnostic-source policy.
 * @return Success with exactly one value pushed, or a diagnostic; on failure
 *         the VM stack is restored to its original height.
 */
[[nodiscard]] EVENGINE_API Result<void> pushValue(HSQUIRRELVM vm, const Value& value,
                                                  const SquirrelValueOptions& options = {});

/** @brief Project one Diagnostic using the common script table schema. */
[[nodiscard]] EVENGINE_API ssq::Table projectDiagnostic(HSQUIRRELVM vm, const Diagnostic& diagnostic);

/** @brief Project one Status using the common script table schema. */
[[nodiscard]] EVENGINE_API ssq::Table projectStatus(HSQUIRRELVM vm, const Status& status);

/**
 * @brief Receiver marker for callFunction(): bind the VM's root table as `this`.
 *
 * A bare closure has no meaningful receiver, and the root table is the conventional
 * stand-in. Naming it keeps `callFunction(vm, fn, rootTable, ...)` readable instead of
 * spelling out a push/pop dance at every call site.
 */
struct RootTable {};
inline constexpr RootTable rootTable{};

/** @brief Push one callFunction() receiver (the root-table marker or a value). */
template <typename Receiver>
void pushReceiver(HSQUIRRELVM vm, const Receiver& receiver) {
    if constexpr (std::is_same_v<std::decay_t<Receiver>, RootTable>) {
        sq_pushroottable(vm);
    } else {
        ssq::detail::pushValue(vm, receiver);
    }
}

/**
 * @brief Prepare one callFunction() argument, converting C strings to `std::string`.
 *
 * simplesquirrel has no `const char*` overload of `pushValue`, and the generic template
 * it falls back to treats the value as an opaque user type: a `const char*` argument
 * therefore reached script as **userdata** (measured: `typeof v == "userdata"`), and a
 * string literal (`const char[N]`) did not compile at all -- `pushByCopy` fails with
 * "cannot convert from 'const char [9]' to 'char'". Both are wrong for the most natural
 * way to pass a script string, so C strings are converted to `std::string` here, which
 * is the overload that reaches `sq_pushstring`.
 *
 * @param arg Argument passed to callFunction(); anything that is not a C string is
 *            forwarded unchanged (referenced, not copied).
 * @return A `std::string` for `char*`, `const char*` and character arrays; otherwise the
 *         argument itself.
 */
template <typename Arg>
decltype(auto) scriptArgument(const Arg& arg) {
    using Bare = std::remove_cv_t<std::remove_reference_t<Arg>>;
    if constexpr (std::is_array_v<Bare> || std::is_same_v<Bare, char*> || std::is_same_v<Bare, const char*>) {
        return std::string(arg);
    } else {
        return (arg);
    }
}

/**
 * @brief Report a failed engine-to-script call, consuming the VM's script error.
 *
 * A callback that throws used to leave nothing behind: the caller either discarded
 * `sq_call`'s failure or read it as a normal "no such entity", so a broken handler was
 * indistinguishable from an empty result. This reads the error with
 * `takeLastScriptError` and prints one line, remembering the last message so a callback
 * that breaks every frame does not flood the log. Every failure still counts, so the
 * condition stays observable even while the text is deduplicated.
 *
 * The formatted text is also the return value. Some callers cannot report by printing:
 * a tool response, an editor `err` string or a thrown exception has to carry the reason
 * back to whoever asked. Those callers used to be stuck with a generic "runtime failed"
 * because the text was only ever printed, and building it themselves would have made a
 * second consumer of the VM's pending error. Handing back the text this function already
 * formatted keeps the failure text single-sourced.
 *
 * @param vm Active Squirrel VM whose pending script error should be consumed.
 * @param what Short caller description used in the report.
 * @return The reported text: the formatted script error, or "unknown script error" when
 *         the VM had no pending context. Returned even when the stderr line was
 *         suppressed by deduplication, so the caller's own message stays specific.
 * @note Ownership/lifetime: consumes the VM's error context; nothing is retained except
 *       the last message text used for deduplication.
 * @note Thread affinity: the owning VM's thread only.
 * @note Reentrancy: not reentrant; takes an internal lock.
 */
EVENGINE_API std::string reportScriptCallFailure(HSQUIRRELVM vm, const char* what);

/** @brief Count of engine-to-script call failures reported since the last reset. */
[[nodiscard]] EVENGINE_API int reportedScriptCallFailures();

/** @brief Clear the failure count and the deduplication memory (diagnostics and tests). */
EVENGINE_API void resetReportedScriptCallFailures();

/**
 * @brief Compile and run one embedded script chunk against the root table.
 *
 * Modules install their script-facing base classes by compiling an embedded chunk and
 * running it (`SceneComponent.nut`, `SceneEntity.nut`, `UIComponent.nut`, ...). That
 * shape had been copied per module and had drifted: two copies discarded `sq_call`'s
 * result, so a chunk that failed to run left its class missing with no signal from the
 * engine -- the only symptom showed up much later as "the index 'SceneEntity' does not
 * exist". This is the one implementation, and both the compile failure and the run
 * failure are reported.
 *
 * @param vm Active Squirrel VM.
 * @param name Chunk name, used for compile diagnostics and the failure report.
 * @param source NUL-terminated script text; must not be null.
 * @return SQ_OK, or SQ_ERROR with the VM stack restored and the failure reported.
 * @note Ownership/lifetime: compiles and runs immediately; nothing is retained.
 * @note Thread affinity: the owning VM's thread only.
 * @note Reentrancy: runs script, so do not hold a lock across the call.
 */
EVENGINE_API SQRESULT compileAndRunChunk(HSQUIRRELVM vm, const char* name, const char* source);

/**
 * @brief Call a script function, deriving `sq_call`'s parameter count from the pushes.
 *
 * `sq_call(vm, params, ...)` takes the callee at `-(params)-1`, so `params` counts the
 * receiver slot plus the arguments. Writing that number by hand produced five separate
 * defects in this repository -- a callback handed the payload as `this` and received an
 * integer where it expected an entity, and three callbacks that never ran at all
 * because the shortened count failed the VM's arity check. All five were silent: the
 * caller either discarded the failed call's error or caught it as if it were a normal
 * rejection.
 *
 * These helpers remove the number from the call site. `params` is computed from the
 * stack, so it cannot disagree with what was pushed, and the receiver is passed
 * positionally, which makes the convention explicit rather than implied.
 *
 * @param vm Active Squirrel VM.
 * @param fn Closure or native closure to invoke.
 * @param receiver Bound as `this`. Pass the root table when the callee has none.
 * @param args Arguments, pushed in order through `ssq::detail::pushValue`, so any type
 *             that helper supports (HSQOBJECT, ssq handles, scalars, strings) works.
 *             C strings are converted first (see scriptArgument()): a `const char*` used
 *             to arrive as userdata and a string literal did not compile.
 * @return SQ_OK, or SQ_ERROR when the call failed; the VM keeps the error for
 *         `takeLastScriptError`.
 * @note Ownership/lifetime: nothing is retained; the pushed values belong to the VM.
 * @note Thread affinity: the owning VM's thread only.
 * @note Reentrancy: invoking a script may re-enter the engine, so do not hold a lock
 *       across the call.
 */
template <typename Receiver, typename... Args>
SQRESULT callFunction(HSQUIRRELVM vm, const HSQOBJECT& fn, const Receiver& receiver, Args&&... args) {
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, fn);
    pushReceiver(vm, receiver);
    (ssq::detail::pushValue(vm, scriptArgument(args)), ...);
    // Everything above the callable: the receiver plus the arguments. The callable itself is not
    // part of the count (sq_call takes the callee at -(params)-1).
    const SQInteger params = sq_gettop(vm) - top - 1;
    const SQRESULT  result = sq_call(vm, params, SQFalse, SQTrue);
    if (SQ_FAILED(result)) reportScriptCallFailure(vm, "callFunction");
    sq_settop(vm, top);
    return result;
}

/**
 * @brief callFunction() that keeps the return value on top for the caller to read.
 *
 * The VM stack is *not* restored on success, so the caller must have recorded
 * `sq_gettop(vm)` beforehand and restore it after reading the result.
 *
 * @param vm Active Squirrel VM.
 * @param fn Closure or native closure to invoke.
 * @param receiver Bound as `this`.
 * @param args Arguments, pushed in order.
 * @return SQ_OK with the return value on top, or SQ_ERROR with the stack restored
 *         and the error left in the VM.
 */
template <typename Receiver, typename... Args>
SQRESULT callFunctionResult(HSQUIRRELVM vm, const HSQOBJECT& fn, const Receiver& receiver, Args&&... args) {
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, fn);
    pushReceiver(vm, receiver);
    (ssq::detail::pushValue(vm, scriptArgument(args)), ...);
    // Everything above the callable: the receiver plus the arguments. The callable itself is not
    // part of the count (sq_call takes the callee at -(params)-1).
    const SQInteger params = sq_gettop(vm) - top - 1;
    const SQRESULT  result = sq_call(vm, params, SQTrue, SQTrue);
    if (SQ_FAILED(result)) {
        reportScriptCallFailure(vm, "callFunctionResult");
        sq_settop(vm, top);
    }
    return result;
}

/**
 * @brief Project an already-consumed native status and optional Value payload.
 * @param vm Active Squirrel VM.
 * @param status Native status copied from the checked Result.
 * @param ok Whether the native operation completed successfully.
 * @param hasValue Whether the native Result had a payload.
 * @param value Payload to expose when `hasValue` is true.
 * @return A table with the stable Result projection schema.
 */
[[nodiscard]] EVENGINE_API ssq::Table projectStatusResult(HSQUIRRELVM vm, const Status& status, bool ok, bool hasValue,
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
[[nodiscard]] ssq::Table projectResult(HSQUIRRELVM vm, Result<T>&& result, Projector&& projector) {
    const bool   hasValue = result.ok();
    const Status status   = result.status();
    if (!hasValue) return projectStatusResult(vm, status, false, false);

    T payload = std::move(result).takeValue();
    return projectStatusResult(vm, status, true, true,
                               std::invoke(std::forward<Projector>(projector), std::move(payload)));
}

/** @brief Consume and project a void native Result using the common schema. */
[[nodiscard]] EVENGINE_API ssq::Table projectResult(HSQUIRRELVM vm, Result<void>&& result);

/**
 * @brief Mark a projected Result table as intentionally ignored by script.
 * @param result Projected Result table/object returned by a checked binding.
 * @param reason Non-empty reason retained in `ignoreReason`.
 * @return True when the table was marked; false for a non-table or empty reason.
 */
[[nodiscard]] EVENGINE_API bool ignoreResult(const ssq::Object& result, const std::string& reason);

/**
 * @brief Expose the common script helpers under `eve.result`.
 * @param eveTable Shared engine root table created by ModuleManager.
 * @remarks This is idempotent for the initial root-table exposure and must be
 *          called once by the common composition root.
 */
EVENGINE_API void exposeResultBindings(ssq::Table& eveTable);

/**
 * @brief Expose `eve.provider`: script-published providers and capability seams.
 *
 * The seam table is a common script contract, not a domain-module convenience: it
 * is how user script answers a native extension point without a C++ build. Every
 * operation returns the common `{ok, code, status, diagnostics, value}` result
 * table, so a script author gets the same structured failure shape they get from
 * `eve.result`.
 *
 * @param eveTable Shared engine root table created by ModuleManager.
 * @remarks Idempotent for the initial root-table exposure; call once from the
 *          common composition root, before any module constructor runs.
 */
EVENGINE_API void exposeProviderBindings(ssq::Table& eveTable);

}  // namespace eve::script
