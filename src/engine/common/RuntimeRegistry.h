#pragma once

/**
 * @file RuntimeRegistry.h
 * @brief Tag-free slot registry: handles, borrows, unique ownership and keep-alive pins.
 *
 * The Squirrel-independent half of what used to live in SquirrelOwnership.h. It is a
 * separate header so that consumers which never touch the VM - above all the resource
 * cache in common/Resource.h - do not have to see <squirrel.h>.
 */

#include "common/Export.h"
#include "common/Result.h"
#include "common/RuntimeHandle.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace eve::script {

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

    RuntimeSlotStore(const RuntimeSlotStore&)            = delete;
    RuntimeSlotStore& operator=(const RuntimeSlotStore&) = delete;
    // A RuntimePin holds this store's address, so relocating the store would leave that pin
    // unpinning the moved-from store while the payload lives in the destination. The store
    // must therefore stay at a fixed address for its whole lifetime.
    RuntimeSlotStore(RuntimeSlotStore&&)            = delete;
    RuntimeSlotStore& operator=(RuntimeSlotStore&&) = delete;
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

    /**
     * @brief Keeps one live slot's object alive across erase() and clear().
     * @param index Slot index taken from the reference.
     * @param generation Slot generation taken from the reference.
     * @param ownerEpoch Owner epoch taken from the reference.
     * @return The live object, or a stale-handle failure.
     * @ownership The object stays store-owned; the pin only postpones destruction.
     * @remarks Every successful pin must be matched by exactly one unpin().
     */
    [[nodiscard]] EVENGINE_API Result<void*> pin(std::uint32_t index, std::uint32_t generation,
                                                 std::uint64_t ownerEpoch);

    /**
     * @brief Releases one pin, destroying the object when it was the last one on
     *        an erased slot.
     * @param index Slot index taken from the reference.
     * @param ownerEpoch Owner epoch taken from the reference.
     * @remarks Safe to call on a stale slot or an already released pin: unpinning
     *          more often than pinning is a no-op, never a destruction.
     */
    EVENGINE_API void unpin(std::uint32_t index, std::uint64_t ownerEpoch) noexcept;

    /** @brief Destroys every live object and invalidates every prior handle. */
    EVENGINE_API void clear();

    /** @brief Returns the non-reusable lifetime epoch of this store. */
    [[nodiscard]] std::uint64_t ownerEpoch() const noexcept { return ownerEpoch_; }

private:
    struct Slot {
        std::uint32_t generation = 1;
        bool          retired    = false;
        /** @brief Erased while pinned: the object outlives its handle. */
        bool orphaned = false;
        /** @brief Outstanding keep-alive pins. */
        std::uint32_t pins   = 0;
        void*         object = nullptr;
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

template <class T, class Tag>
class RuntimeObjectRegistry;

/**
 * @brief RAII keep-alive for one `RuntimeObjectRegistry` entry.
 *
 * `Borrowed<T>` deliberately does not extend an object's lifetime; a `RuntimePin`
 * does. The payload survives `erase()` and `clear()` for as long as one pin holds
 * it, while the handle itself becomes stale immediately. The pin must not outlive
 * the registry that produced it.
 *
 * @tparam T Object type stored by the producing registry.
 * @tparam Tag Owner-specific tag of the producing registry.
 */
template <class T, class Tag>
class RuntimePin {
public:
    using Ref = RuntimeHandleRef<Tag>;

    /** @brief Constructs an unbound pin. */
    RuntimePin()                             = default;
    RuntimePin(const RuntimePin&)            = delete;
    RuntimePin& operator=(const RuntimePin&) = delete;

    /** @brief Moves the keep-alive to a new pin; the source becomes unbound. */
    RuntimePin(RuntimePin&& other) noexcept
        : store_(other.store_), reference_(other.reference_), object_(other.object_) {
        other.store_  = nullptr;
        other.object_ = nullptr;
    }

    /** @brief Move-assigns, releasing any keep-alive this pin already held. */
    RuntimePin& operator=(RuntimePin&& other) noexcept {
        if (this != &other) {
            release();
            store_        = other.store_;
            reference_    = other.reference_;
            object_       = other.object_;
            other.store_  = nullptr;
            other.object_ = nullptr;
        }
        return *this;
    }

    /** @brief Releases the keep-alive; an erased object is destroyed here. */
    ~RuntimePin() { release(); }

    /** @brief Whether this pin still keeps an object alive. */
    [[nodiscard]] bool isBound() const noexcept { return store_ != nullptr && object_ != nullptr; }

    /** @brief The reference this pin was created from. */
    [[nodiscard]] const Ref& reference() const noexcept { return reference_; }

    /** @brief Borrows the pinned object; valid until the pin is released. */
    [[nodiscard]] T* get() const noexcept { return static_cast<T*>(object_); }

    /** @brief Borrows the pinned object together with its owner epoch. */
    [[nodiscard]] Borrowed<T> borrow() const noexcept { return Borrowed<T>(get(), reference_.ownerEpoch); }

    /** @brief Pointer-like access to the pinned object. */
    [[nodiscard]] T* operator->() const noexcept { return get(); }

    /** @brief Dereferences the pinned object. */
    [[nodiscard]] T& operator*() const noexcept { return *get(); }

private:
    friend class RuntimeObjectRegistry<T, Tag>;

    RuntimePin(detail::RuntimeSlotStore* store, Ref reference, void* object) noexcept
        : store_(store), reference_(reference), object_(object) {}

    void release() noexcept {
        if (store_ != nullptr) store_->unpin(reference_.handle.index(), reference_.ownerEpoch);
        store_  = nullptr;
        object_ = nullptr;
    }

    detail::RuntimeSlotStore* store_ = nullptr;
    Ref                       reference_{};
    void*                     object_ = nullptr;
};

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
    RuntimeObjectRegistry() : store_(std::make_unique<detail::RuntimeSlotStore>(&detail::ownedInstanceDestroy<T>)) {}
    RuntimeObjectRegistry(const RuntimeObjectRegistry&)            = delete;
    RuntimeObjectRegistry& operator=(const RuntimeObjectRegistry&) = delete;
    // The store lives behind a stable heap address, so moving the registry relocates only
    // that pointer: pins issued before the move keep referencing the same store and stay
    // valid, which is why the registry may be movable while RuntimeSlotStore is not.
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
        auto slot = store_->emplace(static_cast<void*>(object.get()));
        if (!slot.ok()) return eve::Result<Ref>::failure(slot.status());
        const detail::RuntimeSlotCoordinates coordinates = slot.value();
        object.release();
        return eve::Result<Ref>::success(Ref{Handle(coordinates.index, coordinates.generation), store_->ownerEpoch()});
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
        return Borrowed<T>(
            static_cast<T*>(store_->resolve(ref.handle.index(), ref.handle.generation(), ref.ownerEpoch)),
            store_->ownerEpoch());
    }

    /** @brief Const overload of resolve(); see the non-const form for its contract. */
    [[nodiscard]] Borrowed<const T> resolve(Ref ref) const noexcept {
        return Borrowed<const T>(
            static_cast<const T*>(store_->resolve(ref.handle.index(), ref.handle.generation(), ref.ownerEpoch)),
            store_->ownerEpoch());
    }

    /**
     * @brief Destroys the object identified by a live handle.
     * @return Applied, or StaleHandle/InvalidArgument/Failed.
     * @remarks When a pin still holds the object, the handle becomes stale at once
     *          but the object is destroyed only when the last pin is released.
     */
    [[nodiscard]] eve::Result<void> erase(Ref ref) {
        return store_->erase(ref.handle.index(), ref.handle.generation(), ref.ownerEpoch);
    }

    /**
     * @brief Keeps one entry alive until the returned pin is released.
     * @param ref Reference returned by emplace().
     * @return A move-only keep-alive pin, or a stale-handle failure.
     * @ownership The registry keeps owning the object; the pin postpones its
     *            destruction across erase()/clear().
     * @lifetime The pin must not outlive this registry.
     * @thread Owner-thread-affine; no synchronization is provided.
     */
    [[nodiscard]] eve::Result<RuntimePin<T, Tag>> pin(Ref ref) {
        auto object = store_->pin(ref.handle.index(), ref.handle.generation(), ref.ownerEpoch);
        if (!object.ok()) return eve::Result<RuntimePin<T, Tag>>::failure(object.status());
        return eve::Result<RuntimePin<T, Tag>>::success(RuntimePin<T, Tag>(store_.get(), ref, object.value()));
    }

    /** @brief Reports whether a non-invalid handle can no longer resolve. */
    [[nodiscard]] bool isStale(Ref ref) const noexcept {
        return store_->isStale(ref.handle.index(), ref.handle.generation(), ref.ownerEpoch);
    }

    /** @brief Invalidates all slots and releases every unique-owned object. */
    void clear() { store_->clear(); }

    /** @brief Returns the non-reusable lifetime epoch of this registry. */
    [[nodiscard]] std::uint64_t ownerEpoch() const noexcept { return store_->ownerEpoch(); }

private:
    /** @brief Heap-stable store, so pins stay valid across registry moves. */
    std::unique_ptr<detail::RuntimeSlotStore> store_;
};

}  // namespace eve::script
