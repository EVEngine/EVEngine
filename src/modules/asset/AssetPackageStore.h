#pragma once

/**
 * @file AssetPackageStore.h
 * @brief Reopen-verified atomic publication for `.eva` and `.evpack` files.
 */

#include "asset/EvaArchive.h"
#include "asset/Evpack.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>

namespace eve::asset {

/** @brief Explicit test/tool gate result immediately before atomic replacement. */
enum class PackagePublishGateDecision : std::uint8_t {
    Proceed,
    Reject,
};

/** @brief Durable information returned only after atomic replacement succeeds. */
struct PackagePublishReceipt {
    std::filesystem::path       destination;
    PersistentId               packageId;
    std::optional<PersistentId> buildId;
    std::uint64_t               byteSize = 0;
};

/**
 * @brief Filesystem publisher whose failed prepare/verify leaves the destination unchanged.
 * @remarks The optional gate is invoked after reopening the temporary file and before
 * replacement, without a store lock. It must not retain references to its path arguments.
 */
class AtomicAssetPackageStore {
public:
    using BeforeReplace = std::function<PackagePublishGateDecision(
        const std::filesystem::path& temporary, const std::filesystem::path& destination)>;

    /** @brief Construct a publisher, optionally with a failure-injection/policy gate. */
    explicit AtomicAssetPackageStore(BeforeReplace beforeReplace = {});

    /**
     * @brief Build, reopen-verify and atomically publish one `.eva` source archive.
     * @return Receipt after replacement, or a diagnostic with the old destination intact.
     * @thread Concurrent calls on this instance are safe.
     * @reentrancy The gate may call unrelated services but must not recursively publish to this store.
     */
    [[nodiscard]] Result<PackagePublishReceipt> publishEva(
        const std::filesystem::path& destination, const EvaManifest& manifest,
        std::vector<EvaArchiveEntry> entries, const EvaArchiveLimits& limits = {});

    /**
     * @brief Reopen-verify and atomically publish already cooked `.evpack` bytes.
     * @return Receipt after replacement, or a diagnostic with the old destination intact.
     * @thread Concurrent calls on this instance are safe.
     * @reentrancy The gate may call unrelated services but must not recursively publish to this store.
     */
    [[nodiscard]] Result<PackagePublishReceipt> publishEvpack(
        const std::filesystem::path& destination, std::span<const std::uint8_t> bytes,
        const EvpackLimits& limits = {}, const EvpackTrust& trust = {});

private:
    BeforeReplace              beforeReplace_;
    std::atomic<std::uint64_t> sequence_{0};
};

}  // namespace eve::asset
