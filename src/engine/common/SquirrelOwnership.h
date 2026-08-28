#pragma once

/**
 * @file SquirrelOwnership.h
 * @brief Explicit Value/Owned/Borrowed semantics at the Squirrel boundary.
 */

#include "common/Export.h"
#include "common/Result.h"
#include "common/RuntimeHandle.h"
#include "common/Value.h"

#include <squirrel.h>
#include <simplesquirrel/simplesquirrel.hpp>

#include <atomic>
#include <cstdint>
#include <exception>
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

/**
 * @brief C++ owning storage used by an object factory.
 * @tparam T Object whose destruction belongs to this owner.
 * @remarks Prefer this alias over a raw owning pointer. Ownership transfer is
 *          explicit through move construction or `std::move`.
 */
template <class T>
using Owned = std::unique_ptr<T>;

/**
 * @brief Non-owning C++ observation with no implicit lifetime extension.
 * @tparam T Observed object type, possibly const-qualified.
 * @remarks The owner must outlive every use. This type intentionally has no
 *          reset-to-own or heap-management operation.
 */
template <class T>
class Borrowed {
public:
    constexpr Borrowed() noexcept = default;
    explicit constexpr Borrowed(T* value, std::uint64_t ownerEpoch = 0) noexcept
        : value_(value), ownerEpoch_(ownerEpoch) {}
    explicit constexpr Borrowed(T& value, std::uint64_t ownerEpoch = 0) noexcept
        : value_(&value), ownerEpoch_(ownerEpoch) {}

    /**
     * @brief Returns the observed pointer, or `nullptr` when unbound.
     * @return A nullable borrowed pointer; this wrapper never transfers ownership.
     * @ownership Borrowed from the owner that created this observation.
     * @nullable Yes.
     * @lifetime Valid only while the owner epoch remains current and no owner
     *           mutation, restore, clear or destruction has occurred.
     * @thread Use on the owner thread; the wrapper provides no synchronization.
     * @reentrancy Does not extend lifetime or invoke callbacks.
     */
    [[nodiscard]] constexpr T* get() const noexcept { return value_; }
    /** @brief Returns whether the observation currently names an object. */
    [[nodiscard]] constexpr bool isBound() const noexcept { return value_ != nullptr; }
    /** @brief Returns the epoch of the owner that produced this observation. */
    [[nodiscard]] constexpr std::uint64_t ownerEpoch() const noexcept { return ownerEpoch_; }
    /** @brief Pointer-like access to the borrowed object. */
    constexpr T* operator->() const noexcept { return value_; }
    /** @brief Dereferences the borrowed object; the caller owns validity proof. */
    constexpr T& operator*() const noexcept { return *value_; }

private:
    T*            value_      = nullptr;
    std::uint64_t ownerEpoch_ = 0;
};

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

/**
 * @brief Handle plus owner-lifetime epoch for a non-ECS registry object.
 *
 * The embedded `RuntimeHandle` supplies slot/generation stale detection. The
 * epoch additionally prevents a handle from an unloaded module instance from
 * accidentally resolving against a newly-created module instance whose slot
 * numbers happen to restart at the same values.
 */
template <class Tag>
struct RuntimeHandleRef {
    RuntimeHandle<Tag> handle     = RuntimeHandle<Tag>::invalid();
    std::uint64_t      ownerEpoch = 0;

    /** @brief Returns whether both the handle and its registry epoch are set. */
    [[nodiscard]] constexpr bool isValid() const noexcept { return handle.isValid() && ownerEpoch != 0; }
    /** @brief Returns the packed slot/generation projection. */
    [[nodiscard]] constexpr std::uint64_t packed() const noexcept { return handle.packed(); }
    friend constexpr bool operator==(const RuntimeHandleRef&, const RuntimeHandleRef&) noexcept = default;
};

/**
 * @brief Slot/generation registry whose objects are exclusively unique-owned.
 * @tparam T Non-ECS object type stored in slots.
 * @tparam Tag Empty tag making the handle type domain-specific.
 *
 * All methods are owner-thread-affine. `resolve()` returns Borrowed and never
 * extends object lifetime. `clear()` and destruction invalidate every prior
 * handle; a new registry instance receives a distinct owner epoch.
 */
template <class T, class Tag>
class RuntimeObjectRegistry {
public:
    using Handle = RuntimeHandle<Tag>;
    using Ref    = RuntimeHandleRef<Tag>;

    /** @brief Creates an empty registry with a unique owner-lifetime epoch. */
    RuntimeObjectRegistry() : ownerEpoch_(nextEpoch()) {}
    RuntimeObjectRegistry(const RuntimeObjectRegistry&)                = delete;
    RuntimeObjectRegistry& operator=(const RuntimeObjectRegistry&)     = delete;
    RuntimeObjectRegistry(RuntimeObjectRegistry&&) noexcept            = default;
    RuntimeObjectRegistry& operator=(RuntimeObjectRegistry&&) noexcept = default;

    /**
     * @brief Transfers one unique-owned object into a fresh registry slot.
     * @param object Non-null object whose destruction becomes registry-owned.
     * @return Generation-qualified handle and owner epoch.
     */
    [[nodiscard]] eve::Result<Ref> emplace(Owned<T> object) {
        if (!object) {
            return eve::Result<Ref>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                    "runtime registry cannot own a null object", {}, {},
                                                                    "runtime.registry"));
        }

        try {
            std::uint32_t index    = Handle::invalidIndex;
            bool          appended = false;
            if (!freeSlots_.empty()) {
                index = freeSlots_.back();
            } else {
                if (slots_.size() >= Handle::invalidIndex) {
                    return failure<Ref>(eve::DiagnosticCode::Failed, "runtime registry exhausted its slot index space");
                }
                index = static_cast<std::uint32_t>(slots_.size());
                slots_.emplace_back();
                appended = true;
            }

            Slot& slot = slots_[index];
            if (slot.retired || slot.object) {
                if (appended) slots_.pop_back();
                return failure<Ref>(eve::DiagnosticCode::InvariantViolation,
                                    "runtime registry selected an occupied slot");
            }
            const Handle handle(index, slot.generation);
            slot.object = std::move(object);
            if (!freeSlots_.empty() && freeSlots_.back() == index) freeSlots_.pop_back();
            return eve::Result<Ref>::success(Ref{handle, ownerEpoch_});
        } catch (const std::exception& error) {
            return failure<Ref>(eve::DiagnosticCode::Failed,
                                std::string("runtime registry allocation failed: ") + error.what());
        } catch (...) {
            return failure<Ref>(eve::DiagnosticCode::Failed, "runtime registry allocation failed");
        }
    }

    /**
     * @brief Resolves a live object as a non-owning observation.
     * @return An empty Borrowed value when the generation/owner epoch is stale;
     *         otherwise a borrowed observation owned by this registry.
     * @ownership Borrowed; the registry's unique owner remains responsible for
     *            destruction and this call never transfers it.
     * @nullable The returned Borrowed may be unbound.
     * @lifetime Valid until registry mutation, clear, destruction or owner-epoch
     *           change; never retain it across those boundaries.
     * @thread Owner-thread-affine; no synchronization is provided.
     * @reentrancy Side-effect free and does not invoke callbacks.
     */
    [[nodiscard]] Borrowed<T> resolve(Ref ref) noexcept {
        const auto slotIndex = findSlotIndex(ref);
        if (!slotIndex) return Borrowed<T>();
        Slot& slot = slots_[*slotIndex];
        return slot.object && slot.generation == ref.handle.generation() ? Borrowed<T>(slot.object.get(), ownerEpoch_)
                                                                         : Borrowed<T>();
    }
    /**
     * @brief Resolves a live object as a const non-owning observation.
     * @return An empty Borrowed value when the generation/owner epoch is stale;
     *         otherwise a borrowed const observation owned by this registry.
     * @ownership Borrowed; the registry retains sole destruction responsibility.
     * @nullable The returned Borrowed may be unbound.
     * @lifetime Valid until registry mutation, clear, destruction or owner-epoch
     *           change; never retain it across those boundaries.
     * @thread Owner-thread-affine; no synchronization is provided.
     * @reentrancy Side-effect free and does not invoke callbacks.
     */
    [[nodiscard]] Borrowed<const T> resolve(Ref ref) const noexcept {
        const auto slotIndex = findSlotIndex(ref);
        if (!slotIndex) return Borrowed<const T>();
        const Slot& slot = slots_[*slotIndex];
        return slot.object && slot.generation == ref.handle.generation()
                   ? Borrowed<const T>(slot.object.get(), ownerEpoch_)
                   : Borrowed<const T>();
    }

    /**
     * @brief Destroys the object identified by a live handle.
     * @return Applied, or StaleHandle/InvalidArgument/Failed.
     */
    [[nodiscard]] eve::Result<void> erase(Ref ref) {
        if (!ref.handle.isValid()) {
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                     "cannot erase an invalid runtime handle", {}, {},
                                                                     "runtime.registry"));
        }
        if (ref.ownerEpoch != ownerEpoch_) return staleFailure<void>();
        const auto slotIndex = findSlotIndex(ref);
        if (!slotIndex) return staleFailure<void>();
        Slot& slot = slots_[*slotIndex];
        if (!slot.object || slot.generation != ref.handle.generation()) return staleFailure<void>();

        const auto next = Handle::nextGeneration(slot.generation);
        if (next) {
            try {
                freeSlots_.push_back(ref.handle.index());
            } catch (const std::exception& error) {
                return eve::Result<void>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::Failed,
                                           std::string("runtime registry release bookkeeping failed: ") + error.what(),
                                           {}, {}, "runtime.registry"));
            } catch (...) {
                return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Failed,
                                                                         "runtime registry release bookkeeping failed",
                                                                         {}, {}, "runtime.registry"));
            }
        }
        slot.object.reset();
        if (next)
            slot.generation = *next;
        else
            slot.retired = true;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    /**
     * @brief Transfers a live object out of the registry to a caller-owned value.
     * @param ref Generation- and owner-epoch-qualified object reference.
     * @return The unique owner, or a structured stale/invalid failure.
     * @remarks This is intended only for one-way legacy facades. New code should
     *          retain the reference and use `resolve()`/`erase()` instead.
     */
    [[nodiscard]] eve::Result<Owned<T>> take(Ref ref) {
        if (!ref.handle.isValid()) {
            return eve::Result<Owned<T>>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                         "cannot transfer an invalid runtime handle",
                                                                         {}, {}, "runtime.registry"));
        }
        if (ref.ownerEpoch != ownerEpoch_) return staleFailure<Owned<T>>();
        const auto slotIndex = findSlotIndex(ref);
        if (!slotIndex) return staleFailure<Owned<T>>();
        Slot& slot = slots_[*slotIndex];
        if (!slot.object || slot.generation != ref.handle.generation()) return staleFailure<Owned<T>>();

        const auto next = Handle::nextGeneration(slot.generation);
        if (next) {
            try {
                freeSlots_.push_back(ref.handle.index());
            } catch (const std::exception& error) {
                return eve::Result<Owned<T>>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::Failed,
                                           std::string("runtime registry release bookkeeping failed: ") + error.what(),
                                           {}, {}, "runtime.registry"));
            } catch (...) {
                return eve::Result<Owned<T>>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::Failed, "runtime registry release bookkeeping failed",
                                           {}, {}, "runtime.registry"));
            }
        }
        Owned<T> object = std::move(slot.object);
        if (next)
            slot.generation = *next;
        else
            slot.retired = true;
        return eve::Result<Owned<T>>::success(std::move(object));
    }

    /** @brief Reports whether a non-invalid handle can no longer resolve. */
    [[nodiscard]] bool isStale(Ref ref) const noexcept {
        if (!ref.handle.isValid()) return false;
        if (ref.ownerEpoch != ownerEpoch_) return true;
        const auto slotIndex = findSlotIndex(ref);
        if (!slotIndex) return true;
        const Slot& slot = slots_[*slotIndex];
        return !slot.object || slot.generation != ref.handle.generation();
    }

    /** @brief Invalidates all slots and releases every unique-owned object. */
    void clear() {
        // Reserve before mutating live slots so free-list bookkeeping cannot
        // fail after ownership has already been released.
        freeSlots_.reserve(slots_.size());
        freeSlots_.clear();
        for (std::uint32_t index = 0; index < slots_.size(); ++index) {
            Slot& slot = slots_[index];
            slot.object.reset();
            const auto next = Handle::nextGeneration(slot.generation);
            if (next) {
                slot.generation = *next;
                freeSlots_.push_back(index);
            } else {
                slot.retired = true;
            }
        }
    }

    /** @brief Returns the non-reusable lifetime epoch of this registry. */
    [[nodiscard]] constexpr std::uint64_t ownerEpoch() const noexcept { return ownerEpoch_; }

private:
    struct Slot {
        std::uint32_t generation = 1;
        bool          retired    = false;
        Owned<T>      object;
    };

    template <class R>
    static eve::Result<R> failure(eve::DiagnosticCode code, std::string message) {
        return eve::Result<R>::failure(eve::Diagnostic::error(code, std::move(message), {}, {}, "runtime.registry"));
    }

    template <class R>
    static eve::Result<R> staleFailure() {
        return failure<R>(eve::DiagnosticCode::StaleHandle,
                          "runtime handle is stale or belongs to another owner epoch");
    }

    [[nodiscard]] std::optional<std::uint32_t> findSlotIndex(Ref ref) noexcept {
        if (ref.ownerEpoch != ownerEpoch_ || !ref.handle.isValid() || ref.handle.index() >= slots_.size())
            return std::nullopt;
        return ref.handle.index();
    }
    [[nodiscard]] std::optional<std::uint32_t> findSlotIndex(Ref ref) const noexcept {
        if (ref.ownerEpoch != ownerEpoch_ || !ref.handle.isValid() || ref.handle.index() >= slots_.size())
            return std::nullopt;
        return ref.handle.index();
    }

    [[nodiscard]] static std::uint64_t nextEpoch() noexcept {
        std::uint64_t value = nextEpoch_.fetch_add(1, std::memory_order_relaxed);
        if (value == 0) value = nextEpoch_.fetch_add(1, std::memory_order_relaxed);
        return value;
    }

    inline static std::atomic<std::uint64_t> nextEpoch_{1};
    std::uint64_t                            ownerEpoch_;
    std::vector<Slot>                        slots_;
    std::vector<std::uint32_t>               freeSlots_;
};

namespace detail {

template <class T>
SQInteger ownedInstanceReleaseHook(SQUserPointer pointer, SQInteger) noexcept {
    delete static_cast<T*>(pointer);
    return 0;
}

}  // namespace detail

/**
 * @brief Creates a Squirrel instance that owns a C++ wrapper through a release hook.
 *
 * The class for `T*` must have been registered in this VM with
 * `ssq::Table::addClass<T>()`. The returned `ssq::Object` is itself a rooted
 * owner and can safely cross the current native call into script storage.
 */
template <class T>
[[nodiscard]] eve::Result<ssq::Object> makeOwnedSquirrelInstance(HSQUIRRELVM vm, Owned<T> object) {
    if (!vm || !object) {
        return eve::Result<ssq::Object>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "owned Squirrel instance requires a VM and non-null object", {}, {},
            "squirrel.ownership"));
    }

    const SQInteger top         = sq_gettop(vm);
    bool            transferred = false;
    try {
        const size_t hashCode = ssq::detail::stableTypeHash<T*>();
        const HSQOBJECT& classObject = ssq::detail::getClassObj(vm, hashCode);
        sq_pushobject(vm, classObject);
        if (SQ_FAILED(sq_createinstance(vm, -1)))
            throw ssq::RuntimeException("failed to create owned Squirrel instance");
        sq_remove(vm, -2);
        if (SQ_FAILED(sq_setinstanceup(vm, -1, static_cast<SQUserPointer>(object.get()))))
            throw ssq::RuntimeException("failed to attach owned Squirrel instance");
        sq_settypetag(vm, -1, reinterpret_cast<SQUserPointer>(hashCode));
        sq_setreleasehook(vm, -1, &detail::ownedInstanceReleaseHook<T>);
        object.release();
        transferred = true;

        ssq::Object result(vm);
        if (SQ_FAILED(sq_getstackobj(vm, -1, &result.getRaw())))
            throw ssq::RuntimeException("failed to root owned Squirrel instance");
        sq_addref(vm, &result.getRaw());
        sq_settop(vm, top);
        return eve::Result<ssq::Object>::success(std::move(result));
    } catch (const std::exception& error) {
        sq_settop(vm, top);
        if (!transferred) {
            return eve::Result<ssq::Object>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Failed, std::string("owned Squirrel instance creation failed: ") + error.what(),
                {}, {}, "squirrel.ownership"));
        }
        return eve::Result<ssq::Object>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, std::string("owned Squirrel instance rooting failed: ") + error.what(), {}, {},
            "squirrel.ownership"));
    } catch (...) {
        sq_settop(vm, top);
        return eve::Result<ssq::Object>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "owned Squirrel instance creation failed", {}, {}, "squirrel.ownership"));
    }
}

}  // namespace eve::script
