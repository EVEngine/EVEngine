#pragma once

/** @file EvpackPointGraphLoader.h @brief Runtime binding for compiled PCG assets. */

#include "asset/EvpackResourceReader.h"

#include <memory>
#include <string>

namespace eve::procgen {
class PointGraph;
class SpatialData;
}

namespace eve::asset_procgen {

/** @brief Owning executable graph and its selected output node. */
struct LoadedPointGraph {
    AssetRef                      asset;
    std::unique_ptr<procgen::PointGraph> graph;
    std::string                   outputNode;
    asset::EvpackVariantSelection variant;
};

/** @brief Capability-aware loader for compiled `eve.pcg-graph/1` execution plans. */
class EvpackPointGraphLoader {
public:
    /** @brief Bind a borrowed immutable reader that must outlive this loader. */
    explicit EvpackPointGraphLoader(const asset::EvpackResourceReader& reader) noexcept
        : reader_(reader) {}

    /**
     * @brief Load, validate and bind every declared terrain slot transactionally.
     * @param graph Stable PCG asset identity.
     * @param capabilities Runtime platform capabilities.
     * @param terrain Borrowed spatial input copied into the resulting graph.
     * @param maximumDecodedBytes Aggregate definition and plan budget.
     * @return Owning executable graph; no caller or runtime state is mutated on failure.
     * @thread Worker-safe when the bound reader is read concurrently.
     */
    [[nodiscard]] Result<LoadedPointGraph> load(
        const AssetRef& graph, const asset::EvpackCapabilities& capabilities,
        const procgen::SpatialData& terrain,
        std::uint64_t maximumDecodedBytes = 64ull * 1024ull * 1024ull) const;

private:
    const asset::EvpackResourceReader& reader_;
};

}  // namespace eve::asset_procgen
