#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "common/Result.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainDetailLayer.h"
#include "procgen/heightmap/TerrainObjectPlacement.h"
#include "procgen/heightmap/TerrainProbePlacement.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "procgen/heightmap/TerrainTreePlacement.h"

namespace eve::procgen {

/** @brief One owned Pcg-style terrain texture spawn rule. */
struct TerrainSplatSpawnRule {
    std::string ruleId;
    Heightmap paint;
    TerrainStampSettings operation;
    int targetLayer = 0;
    bool enabled = true;
};

/** @brief One owned Pcg-style detail spawn rule. */
struct TerrainDetailSpawnRule {
    std::string ruleId;
    Heightmap fitness;
    TerrainDetailSettings settings;
    TerrainStampSettings operation;
    TerrainDetailMode mode = TerrainDetailMode::Replace;
    std::int32_t seed = 1;
    bool enabled = true;
};

/** @brief One owned Pcg-style tree spawn rule. */
struct TerrainTreeSpawnRule {
    std::string ruleId;
    Heightmap fitness;
    TerrainTreePlacementSettings settings;
    TerrainStampSettings operation;
    TerrainTreeOperationMode mode = TerrainTreeOperationMode::Add;
    bool enabled = true;
};

/** @brief One owned Pcg-style game-object spawn rule. */
struct TerrainObjectSpawnRule {
    std::string ruleId;
    Heightmap fitness;
    TerrainObjectPlacementSettings settings;
    TerrainStampSettings operation;
    TerrainObjectOperationMode mode = TerrainObjectOperationMode::Add;
    bool enabled = true;
};

/** @brief One owned Pcg-style reflection or light probe spawn rule. */
struct TerrainProbeSpawnRule {
    std::string ruleId;
    Heightmap fitness;
    TerrainProbePlacementSettings settings;
    TerrainStampSettings operation;
    TerrainProbeOperationMode mode = TerrainProbeOperationMode::Add;
    bool enabled = true;
};

/** @brief One owned Pcg TerrainModifierStamp rule, including both ordered mask domains. */
struct TerrainModifierStampSpawnRule {
    std::string ruleId;
    Heightmap stamp;
    Heightmap localMask;
    Heightmap globalMask;
    TerrainStampSettings operation;
    bool enabled = true;
};

using TerrainSpawnRule =
    std::variant<TerrainSplatSpawnRule, TerrainDetailSpawnRule, TerrainTreeSpawnRule, TerrainObjectSpawnRule,
                 TerrainProbeSpawnRule, TerrainModifierStampSpawnRule>;

/**
 * @brief Own an ordered mixed-domain Pcg-style spawn recipe.
 * Rule IDs are stable and unique. Every raster and setting is copied at insertion, so later caller mutation cannot
 * alter replay. The plan is a value object with no scene references, callbacks, clock or mutable external state.
 */
class TerrainSpawnPlan {
public:
    /** @brief Append an owned terrain texture rule after validating a nonempty unique rule ID. */
    [[nodiscard]] Result<int> addSplat(const std::string& ruleId, const Heightmap& paint, int targetLayer,
                                      const TerrainStampSettings& operation);
    /** @brief Append an owned detail rule after validating a nonempty unique rule ID. */
    [[nodiscard]] Result<int> addDetail(const std::string& ruleId, const Heightmap& fitness,
                                        const TerrainDetailSettings& settings,
                                        const TerrainStampSettings& operation, TerrainDetailMode mode,
                                        std::int32_t seed);
    /** @brief Append an owned tree rule after validating a nonempty unique rule ID. */
    [[nodiscard]] Result<int> addTrees(const std::string& ruleId, const Heightmap& fitness,
                                       const TerrainTreePlacementSettings& settings,
                                       const TerrainStampSettings& operation, TerrainTreeOperationMode mode);
    /** @brief Append an owned object rule after validating a nonempty unique rule ID. */
    [[nodiscard]] Result<int> addObjects(const std::string& ruleId, const Heightmap& fitness,
                                         const TerrainObjectPlacementSettings& settings,
                                         const TerrainStampSettings& operation, TerrainObjectOperationMode mode);
    /** @brief Append an owned reflection or light probe rule after validating a nonempty unique rule ID. */
    [[nodiscard]] Result<int> addProbes(const std::string& ruleId, const Heightmap& fitness,
                                        const TerrainProbePlacementSettings& settings,
                                        const TerrainStampSettings& operation, TerrainProbeOperationMode mode);
    /** @brief Append an owned TerrainModifierStamp with independent local and global mask snapshots. */
    [[nodiscard]] Result<int> addModifierStamp(const std::string& ruleId, const Heightmap& stamp,
                                               const TerrainStampSettings& operation, const Heightmap& localMask,
                                               const Heightmap& globalMask);
    /** @brief Enable or disable a stable rule without changing its order. */
    [[nodiscard]] Result<void> setEnabled(const std::string& ruleId, bool enabled);
    /** @brief Return the number of retained rules. */
    [[nodiscard]] int getRuleCount() const noexcept;
    /** @brief Return immutable ordered rules for transactional execution. */
    [[nodiscard]] const std::vector<TerrainSpawnRule>& rules() const noexcept;
    /** @brief Serialize schema version 3 as deterministic strict JSON with an owning binary payload. */
    [[nodiscard]] Result<std::string> snapshotJson() const;
    /** @brief Atomically restore versions 0-3; version 0 migrates all rules to enabled. */
    [[nodiscard]] Result<void> restoreJson(const std::string& json);

private:
    [[nodiscard]] bool contains(const std::string& ruleId) const;
    std::vector<TerrainSpawnRule> rules_;
};

}  // namespace eve::procgen
