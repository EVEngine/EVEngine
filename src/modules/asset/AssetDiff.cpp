#include "asset/AssetDiff.h"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>

namespace eve::asset {
namespace {
using DependencyKey = std::tuple<PersistentId, PersistentId, EvaDependencyKind, std::string,
                                 std::string, std::string, std::string>;
Result<std::set<DependencyKey>> dependencyKeys(const EvaManifest& manifest) {
    std::set<DependencyKey> result;
    for (const auto& value : manifest.dependencies) {
        auto predicate = Value(value.predicate).toJson();
        if (!predicate)
            return Result<std::set<DependencyKey>>::failure(predicate.status());
        auto fallback = Value(value.fallback).toJson();
        if (!fallback)
            return Result<std::set<DependencyKey>>::failure(fallback.status());
        result.emplace(value.from.id(), value.to.id(), value.kind, value.path,
                       value.expectedType, std::move(predicate).takeValue(),
                       std::move(fallback).takeValue());
    }
    return Result<std::set<DependencyKey>>::success(std::move(result));
}
}  // namespace

Result<EvaPackageDiff> diffEvaManifests(const EvaManifest& before, const EvaManifest& after) {
    std::map<PersistentId, const EvaAssetEntry*> left, right;
    for (const auto& asset : before.assets) left.emplace(asset.asset.id(), &asset);
    for (const auto& asset : after.assets) right.emplace(asset.asset.id(), &asset);
    EvaPackageDiff result;
    for (const auto& [id, asset] : left) {
        const auto found = right.find(id);
        if (found == right.end()) {
            result.assets.push_back({id, EvaAssetChangeKind::Removed, asset->type,
                                     asset->schemaVersion, asset->contentHash, {}, {}, {}});
        } else if (asset->type != found->second->type || asset->schemaVersion != found->second->schemaVersion ||
                   asset->contentHash != found->second->contentHash) {
            result.assets.push_back({id, EvaAssetChangeKind::Changed, asset->type,
                                     asset->schemaVersion, asset->contentHash, found->second->type,
                                     found->second->schemaVersion, found->second->contentHash});
        }
    }
    for (const auto& [id, asset] : right)
        if (!left.contains(id))
            result.assets.push_back({id, EvaAssetChangeKind::Added, {}, {}, {}, asset->type,
                                     asset->schemaVersion, asset->contentHash});
    std::sort(result.assets.begin(), result.assets.end(), [](const auto& a, const auto& b) {
        return a.assetId < b.assetId;
    });
    const auto leftDependencies = dependencyKeys(before);
    if (!leftDependencies) return Result<EvaPackageDiff>::failure(leftDependencies.status());
    const auto rightDependencies = dependencyKeys(after);
    if (!rightDependencies) return Result<EvaPackageDiff>::failure(rightDependencies.status());
    std::vector<DependencyKey> added, removed;
    std::set_difference(rightDependencies.value().begin(), rightDependencies.value().end(),
                        leftDependencies.value().begin(), leftDependencies.value().end(),
                        std::back_inserter(added));
    std::set_difference(leftDependencies.value().begin(), leftDependencies.value().end(),
                        rightDependencies.value().begin(), rightDependencies.value().end(),
                        std::back_inserter(removed));
    result.addedDependencies = static_cast<std::uint32_t>(added.size());
    result.removedDependencies = static_cast<std::uint32_t>(removed.size());
    result.entrypointsChanged = before.entrypoints != after.entrypoints;
    return Result<EvaPackageDiff>::success(std::move(result));
}
}  // namespace eve::asset
