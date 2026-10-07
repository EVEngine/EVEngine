#pragma once

#include "common/Result.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "common/Value.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
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

/**
 * @brief Project a binding-layer argument failure with a stable diagnostic source.
 * @param vm Active Squirrel VM.
 * @param source Diagnostic source tag (e.g. `editor.ui.theme.squirrel`).
 * @param code Structured diagnostic code.
 * @param message Human-readable failure text.
 * @param path Optional argument path for the diagnostic.
 * @return The common script result table for the failure status.
 */
[[nodiscard]] inline ssq::Table bindingFailure(HSQUIRRELVM vm, const char* source, DiagnosticCode code,
                                               std::string message, std::string path = {}) {
    return script::projectStatusResult(
        vm, Status::failure(Diagnostic::error(code, std::move(message), std::move(path), {}, source ? source : "")));
}

/**
 * @brief Project an undo/redo (or similar) result whose payload exposes `afterRevision`.
 * @param vm Active Squirrel VM.
 * @param result History mutation result.
 * @return Script result table; on success `value` is the revision after the mutation.
 */
template <class T>
[[nodiscard]] inline ssq::Table projectHistoryRevision(HSQUIRRELVM vm, const eve::Result<T>& result) {
    return project(vm, result, Value(result.ok() ? static_cast<std::int64_t>(result.value().afterRevision) : 0));
}

/**
 * @brief Wrap an already-built owned C++ instance as a script Result `{value, ownership}`.
 * @tparam T Script wrapper type previously registered with `addScriptClass`.
 * @param vm Active Squirrel VM.
 * @param instance Ownership transfers into the Squirrel instance on success.
 * @return Success table with owned `value`, or a projected failure status.
 */
template <class T>
[[nodiscard]] inline ssq::Table projectOwnedInstance(HSQUIRRELVM vm, std::unique_ptr<T> instance) {
    auto object = script::makeOwnedSquirrelInstance<T>(vm, std::move(instance));
    if (!object) return script::projectStatusResult(vm, object.status());
    ssq::Object owned  = std::move(object).takeValue();
    auto        result = script::projectStatusResult(vm, Status::success(StatusCode::Applied));
    result.set("value", owned);
    result.set("ownership", std::string("owned"));
    return result;
}

/**
 * @brief Create a target-id editor wrapper and project it as an owned script Result.
 * @tparam T Script wrapper constructed from `targetId`.
 * @param vm Active Squirrel VM.
 * @param source Diagnostic source tag used when `targetId` is empty.
 * @param emptyIdMessage Failure text for an empty target id.
 * @param targetId Editor target / catalog id.
 * @return Owned create result, or a binding failure when `targetId` is empty.
 */
template <class T>
[[nodiscard]] inline ssq::Table projectOwnedCreate(HSQUIRRELVM vm, const char* source, std::string emptyIdMessage,
                                                   const std::string& targetId) {
    if (targetId.empty())
        return bindingFailure(vm, source, DiagnosticCode::InvalidArgument, std::move(emptyIdMessage), "targetId");
    return projectOwnedInstance<T>(vm, std::make_unique<T>(targetId));
}

/**
 * @brief Register a script class whose default constructor returns null (factory-only types).
 * @tparam T Native wrapper type.
 * @param table Module or root table that owns the class registration.
 * @param name Script-visible class name.
 * @return The registered SimpleSquirrel class handle.
 */
template <class T>
[[nodiscard]] inline auto addScriptClass(ssq::Table& table, const char* name) {
    return table.addClass<T>(name, std::function<T*()>([]() -> T* { return nullptr; }), true);
}

/**
 * @brief Per-file binding helper that pins VM + diagnostic source for editor facades.
 *
 * Use this instead of copying `bindingFailure` / null-self / owned-create stamps into
 * every `*EditorScriptBindings.cpp`. Public `addFunc` names and arities stay unchanged.
 */
class ScriptBind {
public:
    /**
     * @brief Construct a binder for one expose() translation unit.
     * @param vm Active Squirrel VM.
     * @param source Stable diagnostic source tag for this binding file.
     */
    constexpr ScriptBind(HSQUIRRELVM vm, const char* source) noexcept : vm_(vm), source_(source) {}

    /** @brief Active Squirrel VM handle. */
    [[nodiscard]] constexpr HSQUIRRELVM vm() const noexcept { return vm_; }
    /** @brief Diagnostic source tag for this binder. */
    [[nodiscard]] constexpr const char* source() const noexcept { return source_; }

    /**
     * @brief Project a binding-layer argument failure.
     * @param code Structured diagnostic code.
     * @param message Human-readable failure text.
     * @param path Optional argument path.
     * @return The common script result table.
     */
    [[nodiscard]] ssq::Table fail(DiagnosticCode code, std::string message, std::string path = {}) const {
        return bindingFailure(vm_, source_, code, std::move(message), std::move(path));
    }

    /**
     * @brief Run @p fn and project its Result when @p ready; otherwise emit InvalidArgument.
     * @param ready Guard condition (typically `self != nullptr`).
     * @param nullMessage Failure text when the guard fails.
     * @param fn Invoked only when ready; must return a projectable `eve::Result`.
     * @param path Optional argument path for the failure diagnostic.
     * @return Projected success/failure table.
     */
    template <class Fn>
    [[nodiscard]] ssq::Table checked(bool ready, std::string nullMessage, Fn&& fn, std::string path = {}) const {
        if (!ready) return fail(DiagnosticCode::InvalidArgument, std::move(nullMessage), std::move(path));
        return project(vm_, std::forward<Fn>(fn)());
    }

    /**
     * @brief Null-self guard then project a void/`Result<void>` editor call.
     * @tparam T Script wrapper pointer type.
     * @param self Nullable `this` from SimpleSquirrel.
     * @param nullMessage Failure text when @p self is null.
     * @param fn Invoked only when self is non-null.
     * @param path Optional argument path.
     * @return Projected success/failure table.
     */
    template <class T, class Fn>
    [[nodiscard]] ssq::Table checked(T* self, std::string nullMessage, Fn&& fn, std::string path = {}) const {
        return checked(self != nullptr, std::move(nullMessage), std::forward<Fn>(fn), std::move(path));
    }

    /**
     * @brief Null-self guard then project a history mutation that exposes `afterRevision`.
     * @tparam T Script wrapper pointer type.
     * @param self Nullable `this` from SimpleSquirrel.
     * @param nullMessage Failure text when @p self is null.
     * @param fn Invoked only when self is non-null; return type must provide `afterRevision`.
     * @return Projected result with revision payload on success.
     */
    template <class T, class Fn>
    [[nodiscard]] ssq::Table history(T* self, std::string nullMessage, Fn&& fn) const {
        if (!self) return fail(DiagnosticCode::InvalidArgument, std::move(nullMessage));
        return projectHistoryRevision(vm_, std::forward<Fn>(fn)());
    }

    /**
     * @brief Create `T(targetId)` and project it as an owned script Result.
     * @tparam T Script wrapper constructed from target id.
     * @param emptyIdMessage Failure text for an empty target id.
     * @param targetId Editor target / catalog id.
     * @return Owned create result table.
     */
    template <class T>
    [[nodiscard]] ssq::Table ownedCreate(std::string emptyIdMessage, const std::string& targetId) const {
        return projectOwnedCreate<T>(vm_, source_, std::move(emptyIdMessage), targetId);
    }

    /**
     * @brief Project an already-built instance as an owned script Result.
     * @tparam T Script wrapper type.
     * @param instance Ownership transfers on success.
     * @return Owned create result table.
     */
    template <class T>
    [[nodiscard]] ssq::Table ownedInstance(std::unique_ptr<T> instance) const {
        return projectOwnedInstance<T>(vm_, std::move(instance));
    }

private:
    HSQUIRRELVM vm_     = nullptr;
    const char* source_ = nullptr;
};

}  // namespace eve::editor
