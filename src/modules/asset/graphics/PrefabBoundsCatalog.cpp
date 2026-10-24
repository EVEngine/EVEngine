#include "asset/graphics/PrefabBoundsCatalog.h"
#include "asset/graphics/EvpackStaticPrefab.h"
namespace eve::asset_graphics {
Result<void> PrefabBoundsCatalog::add(const EvpackStaticPrefab& prefab) {
    auto bounds = prefab.localBounds();
    if (!bounds.ok()) return Result<void>::failure(bounds.status());
    auto placement = prefab.localBounds(PrefabRootPose::ReplaceTranslationRotation);
    if (!placement.ok()) return Result<void>::failure(placement.status());
    if (prefab.sourceBuildId().isNil())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "prefab package build identity must not be nil"));
    const VersionKey key{prefab.sourceAsset().id(), prefab.sourceBuildId()};
    if (entries_.contains(key))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "prefab bounds identity is already admitted"));
    if (entries_.size() >= 4096)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "prefab bounds catalogue budget exceeded"));
    entries_.emplace(key, std::array{bounds.value(), placement.value()});
    return Result<void>::success();
}
Result<std::array<float, 6>> PrefabBoundsCatalog::localBounds(const AssetRef& asset, const PersistentId& build,
                                                              PrefabRootPose rootPose) const {
    if (rootPose != PrefabRootPose::Authored && rootPose != PrefabRootPose::ReplaceTranslationRotation)
        return Result<std::array<float, 6>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "unknown prefab root pose"));
    const auto found = entries_.find({asset.id(), build});
    if (found != entries_.end())
        return Result<std::array<float, 6>>::success(found->second[static_cast<unsigned>(rootPose)]);
    const auto firstVersion = entries_.lower_bound({asset.id(), PersistentId{}});
    if (firstVersion == entries_.end() || firstVersion->first.first != asset.id())
        return Result<std::array<float, 6>>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "prefab bounds identity is not admitted"));
    return Result<std::array<float, 6>>::failure(
        Diagnostic::error(DiagnosticCode::Conflict, "prefab bounds package build is stale"));
}
}  // namespace eve::asset_graphics
