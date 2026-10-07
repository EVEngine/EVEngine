#pragma once

/**
 * @file SquirrelBindContext.h
 * @brief Per-translation-unit VM + diagnostic-source helper for Squirrel bindings.
 *
 * Domain and editor facades pin one `BindContext` (or editor `ScriptBind` that
 * embeds it) instead of copying `bindingFailure` / null-self stamps. Result
 * tables always come from `projectResult` / `projectStatusResult`.
 */

#include "common/Export.h"
#include "common/Result.h"
#include "common/SquirrelBinding.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>
#include <type_traits>
#include <utility>

namespace eve::script {

/**
 * @brief Pins the active VM and a stable diagnostic source for one expose() TU.
 *
 * @ownership @p source is borrowed; it must outlive every use of this binder
 *            (typically a file-scope `kBindingSource` string literal).
 * @lifetime Valid while the owning Runtime/VM remains alive and this binder is
 *           used only on the VM's owning thread.
 */
class EVENGINE_API_FOUNDATION_INLINE BindContext {
public:
    /**
     * @brief Construct a binder for one expose() translation unit.
     * @param vm Active Squirrel VM; must remain valid for the binder's lifetime.
     * @param source Stable diagnostic source tag for this binding file.
     * @ownership @p source is borrowed; it must outlive every use of this binder.
     */
    constexpr BindContext(HSQUIRRELVM vm, const char* source) noexcept : vm_(vm), source_(source) {}

    /**
     * @brief Active Squirrel VM handle.
     * @return Borrowed VM pointer stored at construction.
     * @ownership Borrowed; this accessor never retains or frees the VM.
     * @nullable Yes, when constructed with a null VM.
     * @lifetime Valid while the owning Runtime/VM remains alive.
     */
    [[nodiscard]] constexpr HSQUIRRELVM vm() const noexcept { return vm_; }

    /**
     * @brief Diagnostic source tag for this binder.
     * @return Borrowed C-string pointer to the tag passed at construction.
     * @ownership Borrowed; callers must not free or modify the returned text.
     * @nullable Yes, when constructed with a null source.
     * @lifetime Valid for at least as long as this binder.
     */
    [[nodiscard]] constexpr const char* source() const noexcept { return source_; }

    /**
     * @brief Project a binding-layer argument failure.
     * @param code Structured diagnostic code.
     * @param message Human-readable failure text.
     * @param path Optional argument path.
     * @return The common script result table.
     */
    [[nodiscard]] ssq::Table fail(DiagnosticCode code, std::string message, std::string path = {}) const {
        return projectStatusResult(vm_, Status::failure(Diagnostic::error(code, std::move(message), std::move(path), {},
                                                                          source_ ? source_ : "")));
    }

    /**
     * @brief Convenience InvalidArgument failure with this binder's source tag.
     * @param message Human-readable failure text.
     * @param path Optional argument path.
     * @return The common script result table.
     */
    [[nodiscard]] ssq::Table failInvalid(std::string message, std::string path = {}) const {
        return fail(DiagnosticCode::InvalidArgument, std::move(message), std::move(path));
    }

    /**
     * @brief Run @p fn and project its void Result when @p ready; otherwise fail.
     * @param ready Guard condition (typically `self != nullptr`).
     * @param nullMessage Failure text when the guard fails.
     * @param fn Invoked only when ready; must return `eve::Result<void>` (or convertible).
     * @param path Optional argument path for the failure diagnostic.
     * @return Projected success/failure table.
     */
    template <class Fn>
    [[nodiscard]] ssq::Table checked(bool ready, std::string nullMessage, Fn&& fn, std::string path = {}) const {
        if (!ready) return failInvalid(std::move(nullMessage), std::move(path));
        return projectResult(vm_, std::forward<Fn>(fn)());
    }

    /**
     * @brief Null-self guard then project a void Result-producing call.
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

private:
    HSQUIRRELVM vm_     = nullptr;
    const char* source_ = nullptr;
};

/**
 * @brief Register one script method while keeping a literal name for Binding Contracts.
 * @tparam F Callable accepted by `ssq::Class::addFunc`.
 * @param cls Script class being filled.
 * @param name Script-visible method name; must be a string literal at the call site
 *             so `scripts/generate_binding_contracts.py` can scrape it.
 * @param fn Member pointer, lambda, or `std::function` passed through to SimpleSquirrel.
 * @remarks Prefer this (or `cls.addFunc("literal", ...)`) over building names at runtime.
 */
template <class F>
inline void bindMethod(ssq::Class& cls, const char* name, F&& fn) {
    cls.addFunc(name, std::forward<F>(fn));
}

/**
 * @brief Register a null-safe getter that returns @p whenNull when `self` is null.
 * @tparam ScriptT Script wrapper pointer type.
 * @tparam Getter Callable `(ScriptT&) -> R`.
 * @param cls Script class being filled.
 * @param name Script-visible method name (string literal for Contracts scrape).
 * @param getter Invoked only when self is non-null.
 * @param whenNull Value returned for a null self.
 */
template <class ScriptT, class Getter>
inline void bindNullSafe(ssq::Class& cls, const char* name, Getter getter,
                         std::invoke_result_t<Getter, ScriptT&> whenNull) {
    cls.addFunc(name, [getter = std::move(getter), whenNull = std::move(whenNull)](ScriptT* self) {
        return self ? getter(*self) : whenNull;
    });
}

}  // namespace eve::script
