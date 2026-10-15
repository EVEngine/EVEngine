#pragma once

#include "common/Result.h"
#include "common/SquirrelBindContext.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "common/Value.h"
#include "editor/EditorWorkspace.h"

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
/** @brief Project. */
[[nodiscard]] inline ssq::Table project(HSQUIRRELVM vm, const eve::Result<T>& result, Value value) {
    if (!result.ok()) return script::projectStatusResult(vm, result.status());
    /** @brief Project status result. */
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
    /** @brief Project status result. */
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
/** @brief Project history revision. */
[[nodiscard]] inline ssq::Table projectHistoryRevision(HSQUIRRELVM vm, const eve::Result<T>& result) {
    /** @brief Project. */
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
/** @brief Project owned instance. */
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
/** @brief Project owned create. */
[[nodiscard]] inline ssq::Table projectOwnedCreate(HSQUIRRELVM vm, const char* source, std::string emptyIdMessage,
                                                   const std::string& targetId) {
    if (targetId.empty())
        /** @brief Binding failure. */
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
/** @brief Adds script class. */
[[nodiscard]] inline auto addScriptClass(ssq::Table& table, const char* name) {
    return table.addClass<T>(name, std::function<T*()>([]() -> T* { return nullptr; }), true);
}

/**
 * @brief Editor facade binder: common `script::BindContext` plus history/owned-create helpers.
 *
 * Use this instead of copying `bindingFailure` / null-self / owned-create stamps into
 * every `*EditorScriptBindings.cpp`. Public `addFunc` names and arities stay unchanged.
 * Non-editor domains should prefer `eve::script::BindContext` directly.
 */
class ScriptBind {
public:
    /**
     * @brief Construct a binder for one expose() translation unit.
     * @param vm Active Squirrel VM; must remain valid for the binder's lifetime.
     * @param source Stable diagnostic source tag for this binding file.
     * @ownership @p source is borrowed; it must outlive every use of this binder
     *            (typically a file-scope `kBindingSource` string literal).
     */
    constexpr ScriptBind(HSQUIRRELVM vm, const char* source) noexcept : ctx_(vm, source) {}

    /**
     * @brief Underlying common binder (VM + source + fail/checked).
     * @return Borrowed view of the embedded `script::BindContext`.
     * @ownership Borrowed from this `ScriptBind`; valid for this object's lifetime.
     * @lifetime Same as this binder.
     */
    [[nodiscard]] constexpr const script::BindContext& context() const noexcept { return ctx_; }

    /**
     * @brief Active Squirrel VM handle.
     * @return Borrowed VM pointer stored at construction.
     * @ownership Borrowed from the caller that constructed this binder; this
     *            accessor never retains or frees the VM.
     * @nullable Yes, when constructed with a null VM.
     * @lifetime Valid while the owning Runtime/VM remains alive and this binder
     *           is used only on the VM's owning thread.
     */
    [[nodiscard]] constexpr HSQUIRRELVM vm() const noexcept { return ctx_.vm(); }

    /**
     * @brief Diagnostic source tag for this binder.
     * @return Borrowed C-string pointer to the tag passed at construction.
     * @ownership Borrowed from the caller that constructed this binder; callers
     *            must not free or modify the returned text.
     * @nullable Yes, when constructed with a null source.
     * @lifetime Valid for at least as long as this binder; typically a
     *           file-scope string literal that outlives the process.
     */
    [[nodiscard]] constexpr const char* source() const noexcept { return ctx_.source(); }

    /**
     * @brief Project a binding-layer argument failure.
     * @param code Structured diagnostic code.
     * @param message Human-readable failure text.
     * @param path Optional argument path.
     * @return The common script result table.
     */
    [[nodiscard]] ssq::Table fail(DiagnosticCode code, std::string message, std::string path = {}) const {
        return ctx_.fail(code, std::move(message), std::move(path));
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
    /** @brief Checked. */
    [[nodiscard]] ssq::Table checked(bool ready, std::string nullMessage, Fn&& fn, std::string path = {}) const {
        if (!ready) return fail(DiagnosticCode::InvalidArgument, std::move(nullMessage), std::move(path));
        /** @brief Project. */
        return project(vm(), std::forward<Fn>(fn)());
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
    /** @brief Checked. */
    [[nodiscard]] ssq::Table checked(T* self, std::string nullMessage, Fn&& fn, std::string path = {}) const {
        /** @brief Checked. */
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
    /** @brief History. */
    [[nodiscard]] ssq::Table history(T* self, std::string nullMessage, Fn&& fn) const {
        if (!self) return fail(DiagnosticCode::InvalidArgument, std::move(nullMessage));
        /** @brief Project history revision. */
        return projectHistoryRevision(vm(), std::forward<Fn>(fn)());
    }

    /**
     * @brief Create `T(targetId)` and project it as an owned script Result.
     * @tparam T Script wrapper constructed from target id.
     * @param emptyIdMessage Failure text for an empty target id.
     * @param targetId Editor target / catalog id.
     * @return Owned create result table.
     */
    template <class T>
    /** @brief Owned create. */
    [[nodiscard]] ssq::Table ownedCreate(std::string emptyIdMessage, const std::string& targetId) const {
        return projectOwnedCreate<T>(vm(), source(), std::move(emptyIdMessage), targetId);
    }

    /**
     * @brief Project an already-built instance as an owned script Result.
     * @tparam T Script wrapper type.
     * @param instance Ownership transfers on success.
     * @return Owned create result table.
     */
    template <class T>
    /** @brief Owned instance. */
    [[nodiscard]] ssq::Table ownedInstance(std::unique_ptr<T> instance) const {
        return projectOwnedInstance<T>(vm(), std::move(instance));
    }

private:
    script::BindContext ctx_;
};

/**
 * @brief Register `configureWorkspace` for a Script* wrapper that exposes `editor()`.
 * @tparam ScriptT Wrapper type with `editor().configureWorkspace(EditorWorkspace&)`.
 * @param cls Script class being filled.
 * @param bind Per-file binder (VM + diagnostic source).
 * @param nullMessage Failure text when self or workspace is null.
 * @remarks Keeps the public `addFunc("configureWorkspace", ...)` name/arity unchanged.
 */
template <class ScriptT>
/** @brief Registers editor workspace. */
inline void registerEditorWorkspace(ssq::Class& cls, const ScriptBind& bind, const char* nullMessage) {
    cls.addFunc("configureWorkspace", [bind, nullMessage](ScriptT* self, EditorWorkspace* workspace) {
        return bind.checked(
            self && workspace, nullMessage, [&] { return self->editor().configureWorkspace(*workspace); }, "workspace");
    });
}

/**
 * @brief Register `configureWorkspace` when the script class *is* the editor (no `.editor()`).
 * @tparam EditorT Type with `configureWorkspace(EditorWorkspace&)`.
 * @param cls Script class being filled.
 * @param bind Per-file binder.
 * @param nullMessage Failure text when self or workspace is null.
 */
template <class EditorT>
/** @brief Registers direct editor workspace. */
inline void registerDirectEditorWorkspace(ssq::Class& cls, const ScriptBind& bind, const char* nullMessage) {
    cls.addFunc("configureWorkspace", [bind, nullMessage](EditorT* self, EditorWorkspace* workspace) {
        return bind.checked(self && workspace, nullMessage, [&] { return self->configureWorkspace(*workspace); });
    });
}

/**
 * @brief Register shared undo, redo, canUndo, canRedo, and getRevision methods.
 * @tparam ScriptT Wrapper type with editor undo, redo, canUndo, canRedo, and revision().
 * @param cls Script class being filled.
 * @param bind Per-file binder.
 * @param nullMessage Failure text when self is null (undo/redo).
 * @remarks Domain-only extras such as `getPreviewRevision` stay in the domain TU.
 */
template <class ScriptT>
/** @brief Registers editor history. */
inline void registerEditorHistory(ssq::Class& cls, const ScriptBind& bind, const char* nullMessage) {
    cls.addFunc("undo", [bind, nullMessage](ScriptT* self) {
        return bind.history(self, nullMessage, [&] { return self->editor().undo(); });
    });
    cls.addFunc("redo", [bind, nullMessage](ScriptT* self) {
        return bind.history(self, nullMessage, [&] { return self->editor().redo(); });
    });
    cls.addFunc("canUndo", [](ScriptT* self) { return self && self->editor().canUndo(); });
    cls.addFunc("canRedo", [](ScriptT* self) { return self && self->editor().canRedo(); });
    cls.addFunc("getRevision", [](ScriptT* self) { return self ? static_cast<int>(self->editor().revision()) : 0; });
}

/**
 * @brief Register module `create(targetId)` that builds an owned Script* wrapper.
 * @tparam ScriptT Wrapper constructed from `targetId`.
 * @tparam ModuleT Module facade pointer type (unused; matches SimpleSquirrel arity).
 * @param moduleClass Module class receiving `create`.
 * @param bind Per-file binder.
 * @param emptyIdMessage Failure text for an empty target id.
 */
template <class ScriptT, class ModuleT>
/** @brief Registers editor owned create. */
inline void registerEditorOwnedCreate(ssq::Class& moduleClass, const ScriptBind& bind, const char* emptyIdMessage) {
    moduleClass.addFunc("create", [bind, emptyIdMessage](ModuleT*, const std::string& targetId) {
        return bind.ownedCreate<ScriptT>(emptyIdMessage, targetId);
    });
}

}  // namespace eve::editor
