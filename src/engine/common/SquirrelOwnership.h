#pragma once

/**
 * @file SquirrelOwnership.h
 * @brief Explicit Value/Owned/Borrowed semantics at the Squirrel boundary.
 */

#include "common/Export.h"
#include "common/Result.h"
#include "common/RuntimeHandle.h"
#include "common/RuntimeRegistry.h"
#include "common/Value.h"

#include <squirrel.h>
#include <simplesquirrel/simplesquirrel.hpp>

#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace eve::script {

/**
 * @brief The only three meanings a script-facing object may have.
 *
 * `Value` is a detached canonical `eve::Value` tree. `Owned` is rooted or
 * otherwise responsible for destruction. `Borrowed` is an observation whose
 * lifetime is bounded by its owner and which must never be retained across a
 * mutation, frame, task, callback, restore, or module unload.
 */
enum class ObjectSemantic { Value, Owned, Borrowed };

/**
 * @brief Returns the stable name used by diagnostics and documentation.
 * @return A non-null borrowed pointer to immutable static text.
 * @ownership Borrowed from program-static storage; callers must not free or
 *            modify the returned text.
 * @nullable No.
 * @lifetime Static for the process lifetime.
 * @thread Thread-safe and side-effect free.
 * @reentrancy Does not access the VM or invoke callbacks.
 */
[[nodiscard]] inline const char* objectSemanticName(ObjectSemantic semantic) noexcept {
    switch (semantic) {
        case ObjectSemantic::Value: return "value";
        case ObjectSemantic::Owned: return "owned";
        case ObjectSemantic::Borrowed: return "borrowed";
    }
    return "unknown";
}


/** @brief Canonical detached script value; it never aliases a VM object. */
using ScriptValue = eve::Value;

/**
 * @brief Borrowed view of a rooted Squirrel object for one synchronous call.
 *
 * Copying this wrapper does not add a VM reference. Use `ownSquirrelObject()`
 * when the object must survive the current call or be stored by a registry.
 */
class EVENGINE_API BorrowedSquirrelObject {
public:
    constexpr BorrowedSquirrelObject() noexcept = default;
    explicit constexpr BorrowedSquirrelObject(const ssq::Object* object) noexcept : object_(object) {}

    /**
     * @brief Returns the borrowed object, or `nullptr` when unbound.
     * @return A nullable borrowed pointer valid only for the synchronous call.
     * @ownership Borrowed from the Squirrel VM/root owner; this view never adds
     *            a VM reference and the caller must not delete the object.
     * @nullable Yes, for an empty view.
     * @lifetime Valid until the current native call ends or the rooted owner is
     *           invalidated; use ownSquirrelObject() to extend it explicitly.
     * @thread The VM's owning thread only.
     * @reentrancy Does not retain or invoke Squirrel callbacks.
     */
    [[nodiscard]] constexpr const ssq::Object* get() const noexcept { return object_; }
    /** @brief Returns whether the view is bound to a non-empty object. */
    [[nodiscard]] bool isBound() const noexcept { return object_ != nullptr && !object_->isEmpty(); }

private:
    const ssq::Object* object_ = nullptr;
};

/** @brief Creates a one-call Borrowed view without retaining a VM reference. */
[[nodiscard]] inline BorrowedSquirrelObject borrowSquirrelObject(const ssq::Object& object) noexcept {
    return BorrowedSquirrelObject(&object);
}

/**
 * @brief Releases C++-owned Squirrel roots before Runtime destroys its VM.
 *
 * Providers register with `eve::cap::addListener<ISquirrelRootReleaser>()`.
 * `Runtime::shutdown()` notifies every listener while the VM is still alive.
 */
class EVENGINE_API ISquirrelRootReleaser {
public:
    static constexpr const char* capabilityName = "eve.script.ISquirrelRootReleaser";
    virtual ~ISquirrelRootReleaser()            = default;
    /**
     * @brief Drops every C++-owned Squirrel root this listener still holds.
     * @remarks Called from Runtime::shutdown() while the VM is still alive.
     *          Implementations must not throw.
     */
    virtual void releaseSquirrelRoots() noexcept = 0;
};

/**
 * @brief A rooted Squirrel reference owned by the C++ holder.
 *
 * This wrapper is move-only to make ownership transfer visible in C++ code.
 * The underlying `ssq::Object` releases its VM reference in its destructor;
 * it must be destroyed before the VM is destroyed.
 */
class EVENGINE_API OwnedSquirrelObject {
public:
    OwnedSquirrelObject() = default;
    explicit OwnedSquirrelObject(ssq::Object object) noexcept : object_(std::move(object)) {}

    OwnedSquirrelObject(const OwnedSquirrelObject&)                = delete;
    OwnedSquirrelObject& operator=(const OwnedSquirrelObject&)     = delete;
    OwnedSquirrelObject(OwnedSquirrelObject&&) noexcept            = default;
    OwnedSquirrelObject& operator=(OwnedSquirrelObject&&) noexcept = default;
    ~OwnedSquirrelObject()                                         = default;

    /** @brief Returns the rooted object without transferring ownership. */
    [[nodiscard]] const ssq::Object& get() const noexcept { return object_; }
    /** @brief Returns whether this owner contains a rooted non-empty object. */
    [[nodiscard]] bool isBound() const noexcept { return !object_.isEmpty(); }
    /** @brief Transfers the rooted reference to the caller. */
    [[nodiscard]] ssq::Object release() && noexcept { return std::move(object_); }

private:
    ssq::Object object_;
};

/**
 * @brief Promotes a borrowed VM object to an explicit rooted owner.
 * @param borrowed Object valid for the duration of this call.
 * @return A move-only owner, or a structured invalid-argument failure.
 */
[[nodiscard]] inline eve::Result<OwnedSquirrelObject> ownSquirrelObject(BorrowedSquirrelObject borrowed) {
    if (!borrowed.isBound()) {
        return eve::Result<OwnedSquirrelObject>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                   "cannot own an empty or unbound Squirrel object", {}, {}, "squirrel.ownership"));
    }
    return eve::Result<OwnedSquirrelObject>::success(OwnedSquirrelObject(*borrowed.get()));
}

namespace detail {


template <class T>
[[nodiscard]] inline std::size_t squirrelTypeHash() {
    static const std::size_t value = std::hash<std::string>{}(typeid(T).name());
    return value;
}

template <class T>
SQInteger ownedInstanceReleaseHook(SQUserPointer pointer, SQInteger) noexcept {
    delete static_cast<T*>(pointer);
    return 0;
}

}  // namespace detail


namespace detail {

// squirrelTypeHash, ownedInstanceReleaseHook and ownedInstanceDestroy are
// declared earlier in this header, before RuntimeObjectRegistry, because the
// registry's constructor needs ownedInstanceDestroy<T>.

/** @brief Release hook signature shared by every owned Squirrel instance. */
using SquirrelReleaseHook = SQInteger (*)(SQUserPointer, SQInteger);

/** @brief Diagnostic for a null VM or null object, built once instead of per T. */
[[nodiscard]] EVENGINE_API Diagnostic ownedInstanceArgumentDiagnostic();

/**
 * @brief Non-template core of makeOwnedSquirrelInstance.
 *
 * Every type-dependent fact arrives as a value or a hook, so the ~45 lines of
 * stack discipline, instance creation, typing, rooting and exception handling
 * are emitted once instead of once per wrapper type.
 *
 * @param vm Active Squirrel VM.
 * @param object Ownership transfers to this call on entry.
 * @param releaseHook Hook the created instance will run to destroy @p object.
 * @param destroy Cleanup used only on a failure path that never reached the VM.
 * @param typeHash Value of squirrelTypeHash<T*>() for the instance type tag.
 * @return The rooted instance, or a structured failure.
 * @ownership On success the VM owns @p object. On failure this call either
 *            destroyed it once through @p destroy, or already handed it to a
 *            live instance whose release hook will run. It never leaks and
 *            never double-frees.
 */
[[nodiscard]] EVENGINE_API eve::Result<ssq::Object> makeOwnedSquirrelInstanceRaw(HSQUIRRELVM vm, void* object,
                                                                                 SquirrelReleaseHook  releaseHook,
                                                                                 OwnedInstanceDestroy destroy,
                                                                                 std::size_t          typeHash);

}  // namespace detail

/**
 * @brief Creates a Squirrel instance that owns a C++ wrapper through a release hook.
 *
 * The class for `T*` must have been registered in this VM with
 * `ssq::Table::addClass<T>()`. The returned `ssq::Object` is itself a rooted
 * owner and can safely cross the current native call into script storage.
 *
 * @remarks This is a thin wrapper: the only per-type code it adds is the two
 *          one-line adapters above and the type hash.
 */
template <class T>
[[nodiscard]] eve::Result<ssq::Object> makeOwnedSquirrelInstance(HSQUIRRELVM vm, Owned<T> object) {
    if (!vm || !object) {
        return eve::Result<ssq::Object>::failure(detail::ownedInstanceArgumentDiagnostic());
    }
    return detail::makeOwnedSquirrelInstanceRaw(vm, static_cast<void*>(object.release()),
                                                &detail::ownedInstanceReleaseHook<T>, &detail::ownedInstanceDestroy<T>,
                                                detail::squirrelTypeHash<T*>());
}

}  // namespace eve::script
