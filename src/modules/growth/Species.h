#pragma once
#include "common/Export.h"

#include "common/Result.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::growth {

/** @brief Biological category of a growable organism. */
enum class OrganismKind : std::uint8_t { Plant = 0, Animal = 1 };

/** @brief One authored growth stage for a species. */
struct GrowthStageDef {
    std::string id;
    /** @brief Baseline duration of this stage in simulation days. */
    double durationDays = 1.0;
};

/** @brief One item emitted by harvest or periodic animal produce. */
struct YieldDef {
    std::string itemId;
    int         minCount = 1;
    int         maxCount = 1;
};

/** @brief Per-day infection chance while the organism is stressed. */
struct DiseaseSusceptibility {
    std::string diseaseId;
    double      chancePerDay = 0.0;
    /** @brief Duration of the disease in simulation days when contracted. */
    double durationDays = 3.0;
    /** @brief Growth-rate multiplier while diseased (typically &lt; 1). */
    double growthMul = 0.5;
};

/**
 * @brief Static species definition for plants and animals.
 * @remarks Definitions are not ECS entities. Register via SpeciesRegistry.
 */
struct SpeciesDef {
    std::string                 id;
    std::string                 displayName;
    OrganismKind                kind = OrganismKind::Plant;
    std::vector<GrowthStageDef> stages;

    double waterDrainPerDay     = 50.0;   ///< Permille (0..1000) drained per sim day.
    double foodDrainPerDay      = 0.0;    ///< Animals; plants ignore.
    double fertilityDrainPerDay = 20.0;   ///< Plants; animals ignore.
    double minWaterPermille     = 200.0;  ///< Below this, growth stalls and stress rises.
    double minFoodPermille      = 200.0;
    double minFertilityPermille = 100.0;

    /** @brief Growth multiplier per season: Spring, Summer, Autumn, Winter. */
    double seasonMul[4] = {1.0, 1.0, 1.0, 0.4};

    std::vector<YieldDef> harvestYield;
    std::vector<YieldDef> produceYield;
    double                produceIntervalDays = 0.0;

    bool        canBreed      = false;
    double      gestationDays = 0.0;
    std::string offspringSpeciesId;

    std::vector<DiseaseSusceptibility> diseases;
    double                             baseDiseaseResist = 0.5;
};

/** @brief Process-wide species table used by GrowthWorld and Organism factories. */
class EVENGINE_API_FOUNDATION SpeciesRegistry {
public:
    /** @brief Registers or replaces a species. Empty id fails. */
    [[nodiscard]] static Result<void> registerSpecies(const SpeciesDef& def);
    /** @brief Parses a JSON object or array of species and registers each. */
    [[nodiscard]] static Result<int> loadFromJson(std::string_view json);
    /**
     * @brief Looks up a species; nullptr when missing.
     * @ownership Borrowed from the process-wide registry; caller must not free it.
     * @lifetime Valid until the next `registerSpecies` / `loadFromJson` / `clear`.
     * @nullable Yes when the id is unknown.
     */
    [[nodiscard]] static const SpeciesDef* find(const std::string& id);
    /** @brief Number of registered species. */
    [[nodiscard]] static int count();
    /** @brief True when id is registered. */
    [[nodiscard]] static bool has(const std::string& id);
    /** @brief Clears the table (tests). */
    static void clear();

private:
    static std::unordered_map<std::string, SpeciesDef>& table();
};

/** @brief Parses OrganismKind from "plant"/"animal" (case-insensitive). */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<OrganismKind> parseOrganismKind(std::string_view text);

}  // namespace eve::growth
