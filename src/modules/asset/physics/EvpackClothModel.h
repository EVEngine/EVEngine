#pragma once

/** @file EvpackClothModel.h @brief Runtime package adapter for cloth models. */

#include "asset/EvpackResourceReader.h"
#include "physics/cloth/ClothModel.h"

namespace eve::asset_physics {

/** @brief Successfully decoded cloth model and selected package variant. */
struct LoadedClothModel {
    AssetRef                      asset;
    physics::ClothModel           model;
    asset::EvpackVariantSelection variant;
};

/** @brief Capability-aware adapter from admitted `.evpack` data to ClothModel. */
class EvpackClothModelLoader {
public:
    /**
     * @brief Bind a borrowed immutable reader.
     * @param reader Reader that must outlive this adapter.
     */
    explicit EvpackClothModelLoader(const asset::EvpackResourceReader& reader) noexcept : reader_(reader) {}

    /**
     * @brief Decode one self-contained `eve.cloth-model/1` definition atomically.
     * @param model Stable asset identity.
     * @param capabilities Runtime platform and backend capabilities.
     * @param maximumDecodedBytes Aggregate decoded byte budget.
     * @return Owning validated model; no runtime cloth is created on failure.
     * @thread May run on a loading thread; the reader and package are immutable.
     * @reentrancy Invokes no callbacks.
     */
    [[nodiscard("check cloth model asset load outcome")]]
    Result<LoadedClothModel> load(const AssetRef& model, const asset::EvpackCapabilities& capabilities,
                                  std::uint64_t maximumDecodedBytes = 256ull * 1024ull * 1024ull) const;

private:
    const asset::EvpackResourceReader& reader_;
};

}  // namespace eve::asset_physics
