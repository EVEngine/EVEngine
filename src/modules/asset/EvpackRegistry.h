#pragma once

/**
 * @file EvpackRegistry.h
 * @brief Prepare/commit runtime package publication with stale-generation detection.
 */

#include "asset/EvpackResourceReader.h"

#include <map>
#include <memory>
#include <mutex>
#include <functional>

namespace eve::asset {

/** @brief Generation-scoped handle to one mounted runtime package. */
struct EvpackHandle {
    PersistentId packageId;
    PersistentId buildId;
    std::uint64_t generation = 0;

    friend bool operator==(const EvpackHandle&, const EvpackHandle&) noexcept = default;
};

/** @brief Generation-scoped handle to one verified chunk in a mounted package. */
struct EvpackChunkHandle {
    EvpackHandle package;
    std::uint32_t chunkIndex = 0;
};

/** @brief Generation-scoped stable-reference resolution across mounted packages. */
struct EvpackAssetHandle {
    EvpackHandle       package;
    AssetRef           asset;
    std::string        expectedType;
    EvpackCapabilities capabilities;
};

/** @brief Receipt returned after an atomic registry publication. */
struct EvpackMountReceipt {
    EvpackHandle handle;
    bool         replacedExisting = false;
    std::vector<Diagnostic> callbackDiagnostics;
};

/** @brief Observable registry lifecycle operation dispatched after commit and outside locks. */
enum class EvpackRegistryEventKind : std::uint8_t { Mounted, Replaced, Unmounted };

/** @brief Immutable lifecycle event delivered to registry subscribers. */
struct EvpackRegistryEvent {
    EvpackRegistryEventKind kind = EvpackRegistryEventKind::Mounted;
    EvpackHandle            handle;
};

/** @brief Opaque identity used to cancel a registry subscription. */
struct EvpackRegistrySubscription {
    std::uint64_t value = 0;
    friend bool operator==(const EvpackRegistrySubscription&,
                           const EvpackRegistrySubscription&) noexcept = default;
};

/** @brief Receipt proving one generation was atomically unmounted. */
struct EvpackUnmountReceipt {
    EvpackHandle            handle;
    std::vector<Diagnostic> callbackDiagnostics;
};

/**
 * @brief Owning admitted mount candidate that has not changed observable registry state.
 * @remarks Destruction only releases the candidate; it never implicitly commits.
 */
class PreparedEvpackMount {
public:
    PreparedEvpackMount(PreparedEvpackMount&&) noexcept = default;
    PreparedEvpackMount& operator=(PreparedEvpackMount&&) noexcept = default;
    PreparedEvpackMount(const PreparedEvpackMount&) = delete;
    PreparedEvpackMount& operator=(const PreparedEvpackMount&) = delete;

    /** @brief Identity of the fully admitted candidate package. */
    [[nodiscard]] const PersistentId& packageId() const noexcept { return pack_->packageId(); }
    /** @brief Build identity of the fully admitted candidate package. */
    [[nodiscard]] const PersistentId& buildId() const noexcept { return pack_->buildId(); }

private:
    friend Result<PreparedEvpackMount> prepareEvpackMount(
        std::span<const std::uint8_t>, const EvpackLimits&, const EvpackTrust&);
    friend class EvpackRegistry;
    explicit PreparedEvpackMount(std::shared_ptr<const Evpack> pack) : pack_(std::move(pack)) {}
    std::shared_ptr<const Evpack> pack_;
};

/**
 * @brief Fully parse and verify package bytes without publishing them.
 * @param bytes Untrusted complete package bytes, copied into the candidate.
 * @param limits Admission budgets.
 * @return Owning candidate suitable for an explicit commit.
 * @thread Worker-safe.
 * @reentrancy Does not invoke callbacks.
 */
[[nodiscard]] Result<PreparedEvpackMount> prepareEvpackMount(
    std::span<const std::uint8_t> bytes, const EvpackLimits& limits = {},
    const EvpackTrust& trust = {});

/** @brief Thread-safe owner of current runtime package generations. */
class EvpackRegistry {
public:
    using Callback = std::function<Result<void>(const EvpackRegistryEvent&)>;

    /**
     * @brief Subscribe to committed lifecycle changes.
     * @remarks Callbacks run synchronously on the committing thread, outside the registry lock.
     * A callback may cancel itself. Callback failures are returned in the operation receipt.
     */
    [[nodiscard]] Result<EvpackRegistrySubscription> subscribe(Callback callback);

    /** @brief Cancel a live subscription; an unknown token returns NotFound. */
    [[nodiscard]] Result<void> unsubscribe(EvpackRegistrySubscription subscription);

    /**
     * @brief Atomically publish an admitted package and invalidate its previous generation.
     * @param candidate Owning candidate consumed only by this call.
     * @return Mount receipt identifying the new generation.
     * @thread Safe from any thread.
     * @reentrancy Does not invoke callbacks while holding the registry lock.
     */
    [[nodiscard]] Result<EvpackMountReceipt> commit(PreparedEvpackMount candidate);

    /** @brief Resolve a current package handle to shared immutable ownership. */
    [[nodiscard]] Result<std::shared_ptr<const Evpack>> resolve(const EvpackHandle& handle) const;

    /**
     * @brief Resolve one stable AssetRef across uniquely owning mounted packages.
     * @return A generation-qualified handle after exact type and capability selection.
     * @thread Safe from any thread; the returned handle becomes stale after provider replacement.
     */
    [[nodiscard]] Result<EvpackAssetHandle> resolveAsset(
        const AssetRef& asset, std::string_view expectedType,
        const EvpackCapabilities& capabilities) const;

    /**
     * @brief Decode an asset through its generation-qualified provider handle.
     * @return Owning payload, StaleHandle after replacement, or NotFound after unload.
     * @thread Safe from any thread.
     */
    [[nodiscard]] Result<RuntimeAssetPayload> readAsset(
        const EvpackAssetHandle& handle, std::uint64_t maximumDecodedBytes) const;

    /**
     * @brief Resolve an exact asset/type/kind/variant tuple to a generation-scoped chunk handle.
     * @return Exact chunk handle, NotFound, or StaleHandle.
     */
    [[nodiscard]] Result<EvpackChunkHandle> resolveChunk(
        const EvpackHandle& handle, const PersistentId& assetId, std::string_view type,
        EvpackChunkKind kind, std::uint32_t chunkId, std::uint32_t variantIndex) const;

    /** @brief Copy verified chunk bytes so the result remains valid across hot replacement. */
    [[nodiscard]] Result<std::vector<std::uint8_t>> copyChunkBytes(const EvpackChunkHandle& handle) const;

    /** @brief Unmount exactly the referenced current generation and dispatch a lock-free event. */
    [[nodiscard]] Result<EvpackUnmountReceipt> unmount(const EvpackHandle& handle);

private:
    struct Entry {
        std::shared_ptr<const Evpack> pack;
        std::uint64_t generation = 0;
    };
    mutable std::mutex             mutex_;
    std::map<PersistentId, Entry> packages_;
    std::map<PersistentId, std::uint64_t> nextGenerations_;
    std::map<std::uint64_t, Callback> callbacks_;
    std::uint64_t nextSubscription_ = 0;
};

}  // namespace eve::asset
