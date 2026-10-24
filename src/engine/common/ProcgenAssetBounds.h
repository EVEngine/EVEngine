#pragma once
#include <array>
#include <string_view>
#include "common/Export.h"
#include "common/PrefabRootPose.h"
#include "common/ResourceRef.h"
#include "common/Result.h"
namespace eve {
/** @brief Optional immutable asset-bounds capability for explicit procedural bakes. */
class EVENGINE_API_FOUNDATION_INLINE IProcgenAssetBounds {
public:
    static constexpr const char* capabilityName = "IProcgenAssetBounds";
    /** @brief Destroy the interface; registration lifetime remains owned by the composition host. */
    virtual ~IProcgenAssetBounds() = default;
    /** @brief Resolve prefab-local min XYZ/max XYZ at an exact admitted package build identity.
     * @param asset Canonical asset:// UUID, borrowed only during this call.
     * @param build Canonical package build UUID; stale builds must fail.
     * @param rootPose Placement convention shared with renderer preparation; unknown modes must fail.
     * @return Owning finite ordered bounds or a structured missing/stale failure.
     * @thread Caller generation thread; owner prevents concurrent catalogue mutation.
     * @reentrant No retained inputs, callbacks or locks; provider must outlive the synchronous bake.
     * @cost Cheap catalogue lookup amortized after explicit loading; never invoked per render frame.
     */
    [[nodiscard]] virtual Result<std::array<float, 6>> localBounds(
        const AssetRef& asset, const PersistentId& build, PrefabRootPose rootPose = PrefabRootPose::Authored) const = 0;
};
}  // namespace eve
