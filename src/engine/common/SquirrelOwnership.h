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
class EVENGINE_API_FOUNDATION_INLINE BorrowedSquirrelObject {
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
class EVENGINE_API_FOUNDATION_INLINE OwnedSquirrelObject {
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

namespace detail {

/** @brief Destroys one owned C++ wrapper without knowing its type. */
using OwnedInstanceDestroy = void (*)(void*) noexcept;

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

/** @brief Type-erased cleanup used when the VM never took the object. */
template <class T>
void ownedInstanceDestroy(void* pointer) noexcept {
    delete static_cast<T*>(pointer);
}

/** @brief Slot coordinates without the domain tag. */
struct RuntimeSlotCoordinates {
    std::uint32_t index      = 0;
    std::uint32_t generation = 0;
};

/**
 * @brief Tag-free slot store behind every RuntimeObjectRegistry<T, Tag>.
 *
 * Holds raw pointers and one destroy hook, so it never sees T or Tag. The
 * bookkeeping it performs -- free-list reuse, generation bumping, slot
 * retirement at generation exhaustion, owner-epoch staleness and the release
 * rollback -- is identical for every registry and is emitted once here instead
 * of once per (T, Tag) pair.
 *
 * @remarks Ownership rule for emplace(): the store takes the object only on
 *          success. A failed emplace leaves the object with the caller, which
 *          still owns it.
 * @thread Owner-thread-affine; no synchronization is provided.
 */
class RuntimeSlotStore {
public:
    /** @brief Constructs an empty store that owns no slot. */
    explicit RuntimeSlotStore(OwnedInstanceDestroy destroy) noexcept : destroy_(destroy), ownerEpoch_(nextEpoch()) {}

    RuntimeSlotStore(const RuntimeSlotStore&)                = delete;
    RuntimeSlotStore& operator=(const RuntimeSlotStore&)     = delete;
    RuntimeSlotStore(RuntimeSlotStore&&) noexcept            = default;
    RuntimeSlotStore& operator=(RuntimeSlotStore&&) noexcept = default;
    ~RuntimeSlotStore();

    /**
     * @brief Stores one object in a fresh or recycled slot.
     * @param object Non-null object whose destruction becomes store-owned.
     * @return The slot coordinates, or a structured failure.
     * @ownership On success the store owns @p object and destroys it through its
     *            destroy hook; on failure the caller keeps ownership.
     */
    [[nodiscard]] EVENGINE_API Result<RuntimeSlotCoordinates> emplace(void* object);

    /** @brief Borrows the live object for these coordinates, or null when stale. */
    [[nodiscard]] EVENGINE_API void* resolve(std::uint32_t index, std::uint32_t generation,
                                             std::uint64_t ownerEpoch) const noexcept;

    /** @brief Destroys the object in a live slot, then advances or retires it. */
    [[nodiscard]] EVENGINE_API Result<void> erase(std::uint32_t index, std::uint32_t generation,
                                                  std::uint64_t ownerEpoch);

    /** @brief Whether a non-invalid handle can no longer resolve. */
    [[nodiscard]] EVENGINE_API bool isStale(std::uint32_t index, std::uint32_t generation,
                                            std::uint64_t ownerEpoch) const noexcept;

    /** @brief Destroys every live object and invalidates every prior handle. */
    EVENGINE_API void clear();

    /** @brief Returns the non-reusable lifetime epoch of this store. */
    [[nodiscard]] std::uint64_t ownerEpoch() const noexcept { return ownerEpoch_; }

private:
    struct Slot {
        std::uint32_t generation = 1;
        bool          retired    = false;
        void*         object     = nullptr;
    };

    [[nodiscard]] std::optional<std::uint32_t> findSlot(std::uint32_t index, std::uint32_t generation,
                                                        std::uint64_t ownerEpoch) const noexcept;
    [[nodiscard]] static bool                  coordinatesValid(std::uint32_t index, std::uint32_t generation) noexcept;
    /** @brief Mirrors RuntimeHandle<Tag>::nextGeneration so the rule lives once. */
    [[nodiscard]] static std::optional<std::uint32_t> nextGeneration(std::uint32_t current) noexcept;
    [[nodiscard]] static std::uint64_t                nextEpoch() noexcept;
    void                                              destroySlot(Slot& slot) noexcept;

    OwnedInstanceDestroy       destroy_;
    std::uint64_t              ownerEpoch_;
    std::vector<Slot>          slots_;
    std::vector<std::uint32_t> freeSlots_;

    inline static std::atomic<std::uint64_t> nextEpoch_{1};
};

}  // namespace detail

/**
 * @brief Slot/generation registry whose objects are exclusively unique-owned.
 * @tparam T Non-ECS object type stored in slots.
 * @tparam Tag Empty tag making the handle type domain-specific.
 *
 * All methods are owner-thread-affine. `resolve()` returns Borrowed and never
 * extends object lifetime. `clear()` and destruction invalidate every prior
 * handle; a new registry instance receives a distinct owner epoch.
 *
 * @remarks Only the coordinate arithmetic in Ref and the pointer cast are
 *          per-(T, Tag); the store below performs the bookkeeping once.
 */
template <class T, class Tag>
class RuntimeObjectRegistry {
public:
    using Handle = RuntimeHandle<Tag>;
    using Ref    = RuntimeHandleRef<Tag>;

    /** @brief Creates an empty registry with a unique owner-lifetime epoch. */
    RuntimeObjectRegistry() : store_(&detail::ownedInstanceDestroy<T>) {}
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
        auto slot = store_.emplace(static_cast<void*>(object.get()));
        if (!slot.ok()) return eve::Result<Ref>::failure(slot.status());
        const detail::RuntimeSlotCoordinates coordinates = slot.value();
        object.release();
        return eve::Result<Ref>::success(Ref{Handle(coordinates.index, coordinates.generation), store_.ownerEpoch()});
    }

    /**
     * @brief Resolves a live object as a non-owning observation.
     * @return An empty Borrowed value when the generation/owner epoch is stale;
     *         otherwise a borrowed observation owned by this registry.
     * @ownership Borrowed; the registry remains responsible for destruction.
     * @nullable The returned Borrowed may be unbound.
     * @lifetime Valid until registry mutation, clear, destruction or owner-epoch
     *           change; never retain it across those boundaries.
     * @thread Owner-thread-affine; no synchronization is provided.
     * @reentrancy Side-effect free and does not invoke callbacks.
     */
    [[nodiscard]] Borrowed<T> resolve(Ref ref) noexcept {
        return Borrowed<T>(static_cast<T*>(store_.resolve(ref.handle.index(), ref.handle.generation(), ref.ownerEpoch)),
                           store_.ownerEpoch());
    }

    /** @brief Const overload of resolve(); see the non-const form for its contract. */
    [[nodiscard]] Borrowed<const T> resolve(Ref ref) const noexcept {
        return Borrowed<const T>(
            static_cast<const T*>(store_.resolve(ref.handle.index(), ref.handle.generation(), ref.ownerEpoch)),
            store_.ownerEpoch());
    }

    /**
     * @brief Destroys the object identified by a live handle.
     * @return Applied, or StaleHandle/InvalidArgument/Failed.
     */
    [[nodiscard]] eve::Result<void> erase(Ref ref) {
        return store_.erase(ref.handle.index(), ref.handle.generation(), ref.ownerEpoch);
    }

    /** @brief Reports whether a non-invalid handle can no longer resolve. */
    [[nodiscard]] bool isStale(Ref ref) const noexcept {
        return store_.isStale(ref.handle.index(), ref.handle.generation(), ref.ownerEpoch);
    }

    /** @brief Invalidates all slots and releases every unique-owned object. */
    void clear() { store_.clear(); }

    /** @brief Returns the non-reusable lifetime epoch of this registry. */
    [[nodiscard]] std::uint64_t ownerEpoch() const noexcept { return store_.ownerEpoch(); }

private:
    detail::RuntimeSlotStore store_;
};

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
