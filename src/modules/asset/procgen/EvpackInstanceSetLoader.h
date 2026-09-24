#pragma once
#include "common/Export.h"

/** @file EvpackInstanceSetLoader.h @brief Runtime decoding of canonical static instances. */

#include "asset/EvpackResourceReader.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace eve::asset_procgen {

/** @brief One exact canonical static instance using meters and an XYZW quaternion. */
struct RuntimeInstance {
    std::string          prototype;
    std::array<float, 3> position{};
    std::array<float, 4> rotation{};
    std::array<float, 3> scale{1.f, 1.f, 1.f};
};

/** @brief Runtime vegetation prototype metadata retained from the source terrain. */
struct RuntimeInstancePrototype {
    std::string  prototype;
    std::string  renderMode;
    bool         usePrototypeMesh = false;
    bool         useInstancing    = false;
    float        minWidth = 1.f, maxWidth = 1.f, minHeight = 1.f, maxHeight = 1.f;
    std::int64_t noiseSeed   = 0;
    float        noiseSpread = .1f, density = 1.f, alignToGround = 0.f, positionJitter = 0.f;
    /** Resolved runtime asset URI; empty only for migrated v1/v2 compatibility data. */
    std::string  resourceAsset;
    std::array<float, 4> healthyColor{1.f, 1.f, 1.f, 1.f};
    std::array<float, 4> dryColor{1.f, 1.f, 1.f, 1.f};
    float                bendFactor        = 0.f;
    float                holeEdgePadding   = 0.f;
    bool                 useDensityScaling = true;
};

/** @brief Bounds applied before allocating untrusted instance records and strings. */
struct InstanceSetLoadLimits {
    std::uint32_t maximumInstances    = 10'000'000;
    std::uint32_t maximumStringBytes  = 1024 * 1024;
    std::uint64_t maximumDecodedBytes = 1024ull * 1024ull * 1024ull;
};

/** @brief Owning exact static-instance set selected for one runtime variant. */
struct LoadedInstanceSet {
    AssetRef                              asset;
    float                                 wavingGrassAmount = 0.f;
    float                                 wavingGrassSpeed = 0.f;
    float                                 wavingGrassStrength = 0.f;
    std::array<float, 4>                  wavingGrassTint{1.f, 1.f, 1.f, 1.f};
    std::vector<RuntimeInstancePrototype> prototypes;
    std::vector<RuntimeInstance>          instances;
    asset::EvpackVariantSelection         variant;
};

/** @brief Capability-aware `eve.instance-set/5` EVINST decoder with v1-v4 compatibility. */
class EVENGINE_API_ORCHESTRATION EvpackInstanceSetLoader {
public:
    /** @brief Bind a borrowed immutable reader that must outlive this loader. */
    explicit EvpackInstanceSetLoader(const asset::EvpackResourceReader& reader) noexcept : reader_(reader) {}

    /**
     * @brief Validate metadata, exact byte layout and every TRS record transactionally.
     * @return Owning instances; failure exposes no partially decoded set.
     * @thread Worker-safe when the bound reader is read concurrently.
     */
    [[nodiscard]] Result<LoadedInstanceSet> load(const AssetRef&                  instances,
                                                 const asset::EvpackCapabilities& capabilities,
                                                 const InstanceSetLoadLimits&     limits = {}) const;

private:
    const asset::EvpackResourceReader& reader_;
};

}  // namespace eve::asset_procgen
