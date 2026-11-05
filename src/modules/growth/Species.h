#pragma once
#include "common/Export.h"

#include "common/Result.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eve::growth {

/**
 * @brief Optional Earth-biology label; custom lifeforms use Custom + free-form kindId.
 * @remarks Simulation never branches on this enum — needs / reproduction / yields drive behavior.
 */
enum class OrganismKind : std::uint8_t { Plant = 0, Animal = 1, Custom = 2 };

/** @brief One authored growth stage for a species. */
struct GrowthStageDef {
    std::string id;
    /** @brief Baseline duration of this stage in simulation days. */
    double durationDays = 1.0;
};

/** @brief One item emitted by harvest or periodic produce. */
struct YieldDef {
    std::string itemId;
    int         minCount = 1;
    int         maxCount = 1;
};

/** @brief Named vital meter (water, radiation, ether, …) — not fixed to Earth biology. */
struct NeedMeterDef {
    std::string id;
    double      drainPerDay       = 0.0;  ///< Permille (0..1000) drained per sim day.
    double      minPermille       = 0.0;  ///< Below this: stress + optional growth stall.
    bool        requiredForGrowth = true; ///< When true, meter below min stalls growth.
    bool        lethalWhenEmpty   = false;
};

/** @brief Authored reproduction strategy — pair gestation is only one mode. */
enum class ReproductionMode : std::uint8_t {
    None          = 0,  ///< No reproduction.
    PairGestation = 1,  ///< Two parents; carrier gestates then spawns offspring.
    SelfBud       = 2,  ///< Single mature parent; interval then spawn (alien budding).
    Fission       = 3,  ///< Single parent splits; optional parent death or stage reset.
    SporeBurst    = 4,  ///< Periodic offspring bursts without a partner.
};

/** @brief Species reproduction parameters selected by @ref ReproductionMode. */
struct ReproductionDef {
    ReproductionMode mode                   = ReproductionMode::None;
    double           gestationDays          = 0.0;  ///< PairGestation only.
    double           intervalDays           = 0.0;  ///< SelfBud / Fission / SporeBurst cooldown.
    int              offspringCountMin      = 1;
    int              offspringCountMax      = 1;
    std::string      offspringSpeciesId;
    bool             requiresPartner        = false;
    bool             partnerMustSameSpecies = true;
    bool             parentDiesOnBirth      = false;
    bool             parentResetsStagesOnBirth = false;
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
 * @brief Static species definition — fully data-driven life cycle.
 * @remarks Definitions are not ECS entities. Register via SpeciesRegistry.
 *          Legacy plant/animal JSON fields still parse and normalize into needs/reproduction.
 */
struct SpeciesDef {
    std::string                 id;
    std::string                 displayName;
    /** @brief Free-form lifeform tag (`plant`, `animal`, `xeno.crystal`, …). */
    std::string                 kindId = "organism";
    /** @brief Convenience label derived from kindId when plant/animal; else Custom. */
    OrganismKind                kind = OrganismKind::Custom;
    std::vector<GrowthStageDef> stages;
    std::vector<NeedMeterDef>   needs;

    /**
     * @brief Growth multipliers indexed by calendar season order.
     * @remarks Empty means 1.0 for every season. Length need not be 4.
     */
    std::vector<double> seasonMul = {1.0, 1.0, 1.0, 1.0};
    /** @brief Optional multipliers keyed by calendar season id (preferred when set). */
    std::unordered_map<std::string, double> seasonMulById;

    std::vector<YieldDef> harvestYield;
    std::vector<YieldDef> produceYield;
    double                produceIntervalDays = 0.0;

    ReproductionDef reproduction;

    std::vector<DiseaseSusceptibility> diseases;
    double                             baseDiseaseResist = 0.5;

    /**
     * @brief True when the species declares a need meter with this id.
     * @remarks Inline so SHARED Windows consumers do not need a DLL export.
     */
    [[nodiscard]] bool hasNeed(std::string_view needId) const { return findNeed(needId) != nullptr; }
    /**
     * @brief Looks up a need meter; nullptr when absent.
     * @ownership Borrowed from this SpeciesDef; caller must not free it.
     * @lifetime Valid while this SpeciesDef remains alive / registered.
     * @nullable Yes when the id is unknown.
     * @remarks Inline so SHARED Windows consumers do not need a DLL export.
     */
    [[nodiscard]] const NeedMeterDef* findNeed(std::string_view needId) const {
        for (const NeedMeterDef& need : needs) {
            if (need.id == needId) return &need;
        }
        return nullptr;
    }
};

/** @brief Process-wide species table used by GrowthWorld and Organism factories. */
class EVENGINE_API_FOUNDATION SpeciesRegistry {
public:
    /** @brief Registers or replaces a species after normalizing legacy fields. Empty id fails. */
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

/** @brief Parses kind text: plant/animal/custom or any free-form tag → Custom. */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<OrganismKind> parseOrganismKind(std::string_view text);
/** @brief Parses reproduction.mode text. */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<ReproductionMode> parseReproductionMode(std::string_view text);
/**
 * @brief Fills needs/reproduction from legacy plant/animal fields when authors omit the new schema.
 * @cost Cheap: copies a handful of meter/reproduction fields.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<void> normalizeSpeciesDef(SpeciesDef& def);

}  // namespace eve::growth
