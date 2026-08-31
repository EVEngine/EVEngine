#pragma once

/**
 * @file AssetCooker.h
 * @brief Deterministic canonical `.eva` to capability-targeted `.evpack` Cook.
 */

#include "asset/EvaArchive.h"
#include "asset/AssetDependency.h"
#include "asset/Evpack.h"

namespace eve::asset {

/** @brief Publication policy controls license admission without making legal inferences. */
enum class CookPublication : std::uint8_t {
    LocalInspection,
    PublicDistribution,
};

/** @brief Explicit deterministic target profile for one runtime variant. */
struct AssetCookProfile {
    EvpackVariant   variant;
    CookPublication publication = CookPublication::LocalInspection;
    std::uint32_t   bulkAlignment = 16;
    EvpackCodec     chunkCodec = EvpackCodec::Zstd;
    std::vector<AvailableAssetDependency> availableDependencies;
};

/** @brief Owning result of a completed and reopened deterministic Cook. */
struct AssetCookReceipt {
    PersistentId              packageId;
    PersistentId              buildId;
    std::uint32_t             chunkCount = 0;
    std::vector<std::uint8_t> bytes;
};

/**
 * @brief Resolve a stable CLI/tool target name to its exact capability profile.
 * @param target One of the documented `<os>-<arch>-<graphics>` target names.
 * @return A local-inspection profile, or Unsupported for an unknown target.
 * @thread Worker-safe; returns an owning value and uses no shared mutable state.
 */
[[nodiscard]] Result<AssetCookProfile> assetCookProfileForTarget(std::string_view target);

/**
 * @brief Cook an admitted source archive into one target-specific runtime pack.
 * @param source Owning source archive; it is revalidated before Cook.
 * @param profile Explicit capability and publication policy.
 * @param evaLimits Source revalidation budgets.
 * @param evpackLimits Runtime package budgets.
 * @return Verified package bytes and deterministic receipt.
 * @thread Worker-safe when source is not concurrently mutated.
 * @reentrancy Does not execute scripts, tools, network requests or callbacks.
 */
[[nodiscard]] Result<AssetCookReceipt> cookEvaToEvpack(
    const EvaArchive& source, const AssetCookProfile& profile,
    const EvaArchiveLimits& evaLimits = {}, const EvpackLimits& evpackLimits = {});

}  // namespace eve::asset
