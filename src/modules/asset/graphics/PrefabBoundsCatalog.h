#pragma once
#include <map>
#include <string>
#include <utility>
#include "common/ProcgenAssetBounds.h"
namespace eve::asset_graphics {
class EvpackStaticPrefab;
/** @brief Owning CPU bounds catalogue; no retained prefab, reader or GPU pointers.
 * @thread Generation-owner affine. Build before registering; prevent mutation during queries.
 * @lifetime Composition host owns capability registration and unregisters before destroying this object.
 */
class EVENGINE_API_WORLD PrefabBoundsCatalog final : public IProcgenAssetBounds {
public:
    /** @brief Admit one loaded prefab snapshot, including its verified package build identity.
     * @param prefab Borrowed only during this call; identities and CPU bounds are copied, no GPU pointers retained.
     * @return Success or duplicate-version/missing/budget failure without replacing existing entries.
     * @cost Cheap logarithmic insertion after expensive prefab loading, at most 4096 asset/build versions.
     * @remarks Reimport admits another immutable version; historical references keep their exact bounds.
     */
    [[nodiscard]] Result<void> add(const EvpackStaticPrefab& prefab);
    /** @copydoc IProcgenAssetBounds::localBounds */
    [[nodiscard]] Result<std::array<float, 6>> localBounds(
        const AssetRef& asset, const PersistentId& build,
        PrefabRootPose rootPose = PrefabRootPose::Authored) const override;

private:
    using VersionKey = std::pair<PersistentId, PersistentId>;
    std::map<VersionKey, std::array<std::array<float, 6>, 2>> entries_;
};
}  // namespace eve::asset_graphics
