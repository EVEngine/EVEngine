#include "procgen/heightmap/TerrainSpawnPlan.h"

#include <algorithm>

namespace eve::procgen {
namespace {
const std::string& ruleId(const TerrainSpawnRule& rule) {
    return std::visit([](const auto& value) -> const std::string& { return value.ruleId; }, rule);
}
}  // namespace

bool TerrainSpawnPlan::contains(const std::string& id) const {
    return std::any_of(rules_.begin(), rules_.end(), [&](const auto& rule) { return ruleId(rule) == id; });
}

Result<int> TerrainSpawnPlan::addSplat(const std::string& id, const Heightmap& paint, int targetLayer,
                                       const TerrainStampSettings& operation) {
    if (id.empty() || contains(id))
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.spawnPlan: nonempty unique rule ID required"));
    if (targetLayer < 0)
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.spawnPlan: nonnegative splat layer required"));
    auto candidate = rules_;
    candidate.emplace_back(TerrainSplatSpawnRule{id, paint, operation, targetLayer});
    rules_.swap(candidate);
    return Result<int>::success(static_cast<int>(rules_.size()));
}

Result<int> TerrainSpawnPlan::addDetail(const std::string& id, const Heightmap& fitness,
                                         const TerrainDetailSettings& settings,
                                         const TerrainStampSettings& operation, TerrainDetailMode mode,
                                         std::int32_t seed) {
    if (id.empty() || contains(id))
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.spawnPlan: nonempty unique rule ID required"));
    auto candidate = rules_;
    candidate.emplace_back(TerrainDetailSpawnRule{id, fitness, settings, operation, mode, seed});
    rules_.swap(candidate);
    return Result<int>::success(static_cast<int>(rules_.size()));
}

Result<int> TerrainSpawnPlan::addTrees(const std::string& id, const Heightmap& fitness,
                                        const TerrainTreePlacementSettings& settings,
                                        const TerrainStampSettings& operation, TerrainTreeOperationMode mode) {
    if (id.empty() || contains(id))
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.spawnPlan: nonempty unique rule ID required"));
    auto candidate = rules_;
    candidate.emplace_back(TerrainTreeSpawnRule{id, fitness, settings, operation, mode});
    rules_.swap(candidate);
    return Result<int>::success(static_cast<int>(rules_.size()));
}

Result<int> TerrainSpawnPlan::addObjects(const std::string& id, const Heightmap& fitness,
                                          const TerrainObjectPlacementSettings& settings,
                                          const TerrainStampSettings& operation, TerrainObjectOperationMode mode) {
    if (id.empty() || contains(id))
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.spawnPlan: nonempty unique rule ID required"));
    auto candidate = rules_;
    candidate.emplace_back(TerrainObjectSpawnRule{id, fitness, settings, operation, mode});
    rules_.swap(candidate);
    return Result<int>::success(static_cast<int>(rules_.size()));
}

Result<int> TerrainSpawnPlan::addProbes(const std::string& id, const Heightmap& fitness,
                                         const TerrainProbePlacementSettings& settings,
                                         const TerrainStampSettings& operation, TerrainProbeOperationMode mode) {
    if (id.empty() || contains(id))
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.spawnPlan: nonempty unique rule ID required"));
    auto candidate = rules_;
    candidate.emplace_back(TerrainProbeSpawnRule{id, fitness, settings, operation, mode});
    rules_.swap(candidate);
    return Result<int>::success(static_cast<int>(rules_.size()));
}

Result<int> TerrainSpawnPlan::addModifierStamp(const std::string& id, const Heightmap& stamp,
                                                const TerrainStampSettings& operation, const Heightmap& localMask,
                                                const Heightmap& globalMask) {
    if (id.empty() || contains(id))
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.spawnPlan: nonempty unique rule ID required"));
    auto candidate = rules_;
    candidate.emplace_back(TerrainModifierStampSpawnRule{id, stamp, localMask, globalMask, operation});
    rules_.swap(candidate);
    return Result<int>::success(static_cast<int>(rules_.size()));
}

Result<void> TerrainSpawnPlan::setEnabled(const std::string& id, bool enabled) {
    auto candidate = rules_;
    auto found = std::find_if(candidate.begin(), candidate.end(), [&](const auto& rule) { return ruleId(rule) == id; });
    if (found == candidate.end())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.spawnPlan: rule ID not found"));
    std::visit([&](auto& value) { value.enabled = enabled; }, *found);
    rules_.swap(candidate);
    return Result<void>::success();
}

int TerrainSpawnPlan::getRuleCount() const noexcept { return static_cast<int>(rules_.size()); }
const std::vector<TerrainSpawnRule>& TerrainSpawnPlan::rules() const noexcept { return rules_; }

}  // namespace eve::procgen
