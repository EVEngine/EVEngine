#pragma once

#include <string>
#include <vector>

#include "common/Result.h"
#include "procgen/heightmap/TerrainSpawnPlan.h"

namespace eve::procgen {

/** @brief One owned Pcg BiomePreset spawner entry with its two activation scopes. */
struct TerrainBiomeSpawnerEntry {
    std::string entryId;
    TerrainSpawnPlan plan;
    bool activeInBiome = true;
    bool activeInStamper = true;
    bool autoAssignResources = true;
};

/**
 * @brief Own an ordered Pcg-style biome preset made from independent spawner plans.
 * Entries and plans are copied at insertion. Compilation namespaces each rule by its stable entry ID, preserving
 * order while allowing different spawners to use the same local rule ID. No scene pointer or asset object is held.
 */
class EVENGINE_API_DOMAINS TerrainBiomePreset {
public:
    /** @brief Append an owned nonempty spawner entry with a stable unique entry ID. */
    [[nodiscard]] Result<int> addSpawner(const std::string& entryId, const TerrainSpawnPlan& plan,
                                         bool activeInBiome, bool activeInStamper, bool autoAssignResources);
    /** @brief Change the biome-controller activation flag of one stable entry. */
    [[nodiscard]] Result<void> setActiveInBiome(const std::string& entryId, bool active);
    /** @brief Change the stamper-linked activation flag of one stable entry. */
    [[nodiscard]] Result<void> setActiveInStamper(const std::string& entryId, bool active);
    /** @brief Compile active biome-controller entries into one owned transactional plan. */
    [[nodiscard]] Result<TerrainSpawnPlan> compileForBiome() const;
    /** @brief Compile active stamper-linked entries into one owned transactional plan. */
    [[nodiscard]] Result<TerrainSpawnPlan> compileForStamper() const;
    /** @brief Return the number of retained spawner entries. */
    [[nodiscard]] int getSpawnerCount() const noexcept;
    /** @brief Return whether an entry requests prototype/resource assignment. */
    [[nodiscard]] Result<bool> getAutoAssignResources(const std::string& entryId) const;
    /** @brief Serialize schema version 1 as deterministic strict JSON with owned nested spawn plans. */
    [[nodiscard]] Result<std::string> snapshotJson() const;
    /** @brief Atomically restore version 1 or migrate version 0 (auto-assign defaults true). */
    [[nodiscard]] Result<void> restoreJson(const std::string& json);

private:
    [[nodiscard]] Result<TerrainSpawnPlan> compile(bool biomeScope) const;
    std::vector<TerrainBiomeSpawnerEntry> entries_;
};

}  // namespace eve::procgen
