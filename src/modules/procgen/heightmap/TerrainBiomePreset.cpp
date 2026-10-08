#include "procgen/heightmap/TerrainBiomePreset.h"

#include <algorithm>
#include <type_traits>

namespace eve::procgen {
namespace {
std::string namespacedRuleId(const std::string& entryId, const std::string& ruleId) {
    return std::to_string(entryId.size()) + ":" + entryId + ruleId;
}
}  // namespace

Result<int> TerrainBiomePreset::addSpawner(const std::string& entryId, const TerrainSpawnPlan& plan,
                                            bool activeInBiome, bool activeInStamper,
                                            bool autoAssignResources) {
    if (entryId.empty() || plan.rules().empty())
        return Result<int>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.biomePreset: nonempty entry ID and spawn plan required"));
    if (std::any_of(entries_.begin(), entries_.end(), [&](const auto& entry) { return entry.entryId == entryId; }))
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.biomePreset: unique entry ID required"));
    auto candidate = entries_;
    candidate.push_back({entryId, plan, activeInBiome, activeInStamper, autoAssignResources});
    entries_.swap(candidate);
    return Result<int>::success(static_cast<int>(entries_.size()));
}

Result<void> TerrainBiomePreset::setActiveInBiome(const std::string& entryId, bool active) {
    auto candidate = entries_;
    auto found = std::find_if(candidate.begin(), candidate.end(), [&](const auto& entry) {
        return entry.entryId == entryId;
    });
    if (found == candidate.end())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.biomePreset: entry ID not found"));
    found->activeInBiome = active;
    entries_.swap(candidate);
    return Result<void>::success();
}

Result<void> TerrainBiomePreset::setActiveInStamper(const std::string& entryId, bool active) {
    auto candidate = entries_;
    auto found = std::find_if(candidate.begin(), candidate.end(), [&](const auto& entry) {
        return entry.entryId == entryId;
    });
    if (found == candidate.end())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.biomePreset: entry ID not found"));
    found->activeInStamper = active;
    entries_.swap(candidate);
    return Result<void>::success();
}

Result<TerrainSpawnPlan> TerrainBiomePreset::compile(bool biomeScope) const {
    TerrainSpawnPlan result;
    for (const auto& entry : entries_) {
        if (!(biomeScope ? entry.activeInBiome : entry.activeInStamper)) continue;
        for (const auto& variant : entry.plan.rules()) {
            auto added = std::visit([&](const auto& rule) -> Result<int> {
                using Rule = std::decay_t<decltype(rule)>;
                const auto id = namespacedRuleId(entry.entryId, rule.ruleId);
                auto insertion = [&]() -> Result<int> {
                    if constexpr (std::is_same_v<Rule, TerrainSplatSpawnRule>)
                        return result.addSplat(id, rule.paint, rule.targetLayer, rule.operation);
                    else if constexpr (std::is_same_v<Rule, TerrainDetailSpawnRule>)
                        return result.addDetail(id, rule.fitness, rule.settings, rule.operation, rule.mode, rule.seed);
                    else if constexpr (std::is_same_v<Rule, TerrainTreeSpawnRule>)
                        return result.addTrees(id, rule.fitness, rule.settings, rule.operation, rule.mode);
                    else if constexpr (std::is_same_v<Rule, TerrainObjectSpawnRule>)
                        return result.addObjects(id, rule.fitness, rule.settings, rule.operation, rule.mode);
                    else if constexpr (std::is_same_v<Rule, TerrainProbeSpawnRule>)
                        return result.addProbes(id, rule.fitness, rule.settings, rule.operation, rule.mode);
                    else
                        return result.addModifierStamp(id, rule.stamp, rule.operation, rule.localMask,
                                                       rule.globalMask);
                }();
                if (insertion.ok() && !rule.enabled) {
                    auto disabled = result.setEnabled(id, false);
                    if (!disabled.ok()) return Result<int>::failure(disabled.status());
                }
                return insertion;
            }, variant);
            if (!added.ok()) return Result<TerrainSpawnPlan>::failure(added.status());
        }
    }
    if (result.rules().empty())
        return Result<TerrainSpawnPlan>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.biomePreset: no active spawners"));
    return Result<TerrainSpawnPlan>::success(std::move(result));
}

Result<TerrainSpawnPlan> TerrainBiomePreset::compileForBiome() const { return compile(true); }
Result<TerrainSpawnPlan> TerrainBiomePreset::compileForStamper() const { return compile(false); }
int TerrainBiomePreset::getSpawnerCount() const noexcept { return static_cast<int>(entries_.size()); }

Result<bool> TerrainBiomePreset::getAutoAssignResources(const std::string& entryId) const {
    auto found = std::find_if(entries_.begin(), entries_.end(), [&](const auto& entry) {
        return entry.entryId == entryId;
    });
    if (found == entries_.end())
        return Result<bool>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.biomePreset: entry ID not found"));
    return Result<bool>::success(found->autoAssignResources);
}

}  // namespace eve::procgen
