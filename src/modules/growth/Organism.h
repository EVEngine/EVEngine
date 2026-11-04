#pragma once
#include "common/Export.h"

#include "common/ECS.h"
#include "common/Result.h"
#include "growth/Species.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::growth {

class GrowthWorld;

/** @brief One harvested or produced item line (no inventory dependency). */
struct YieldReceipt {
    std::string itemId;
    int         count = 0;
};

/**
 * @brief Growth short-root entity for any configured lifeform.
 *
 * Orthogonal state lives in components. Scene / hex / inventory links are
 * owned by adapters outside this module (spatial binding 2A).
 */
class EVENGINE_API_FOUNDATION Organism : public ecs::Entity {
public:
    ENTITY(Organism, ecs::Entity)

    void release() override { ecs::DestroyEntity(this); }

    /** @brief Species binding and stage progress. */
    struct Growth {
        std::string  speciesId;
        std::string  kindId;
        OrganismKind kind              = OrganismKind::Custom;
        int          stageIndex        = 0;
        double       stageProgressDays = 0.0;
        bool         mature            = false;
        bool         dead              = false;
        bool         harvested         = false;
    };

    /** @brief Named need meters in permille [0, 1000], keyed by species need id. */
    struct VitalNeeds {
        std::unordered_map<std::string, double> meters;
    };

    /** @brief Disease state; empty diseaseId means healthy. */
    struct Health {
        std::string activeDiseaseId;
        double      diseaseDaysLeft  = 0.0;
        double      diseaseGrowthMul = 1.0;
    };

    /** @brief Reproduction progress for any authored mode. */
    struct Reproduction {
        bool          pregnant               = false;
        double        gestationProgressDays  = 0.0;
        double        daysSinceReproduce     = 0.0;
        std::uint64_t partnerStableId        = 0;
    };

    /** @brief Periodic produce timer (milk, crystals, spores as yield — not offspring). */
    struct Produce {
        double daysSinceProduce = 0.0;
    };

    COMPONENT(Growth, growth)
    COMPONENT(VitalNeeds, vitalNeeds)
    COMPONENT(Health, health)
    COMPONENT(Reproduction, reproduction)
    COMPONENT(Produce, produce)

    /**
     * @brief Creates an organism bound to a registered species.
     * @param speciesId Must already be registered.
     * @return Borrowed ECS entity; caller releases via release()/destroy.
     */
    [[nodiscard]] static Result<Organism*> createOrganism(const std::string& speciesId);

    /** @brief Live organisms currently visible to Growth views. */
    [[nodiscard]] static const std::vector<Organism*>& liveOrganisms();

    [[nodiscard]] const std::string& speciesId();
    [[nodiscard]] const std::string& kindId();
    [[nodiscard]] OrganismKind       kind();
    [[nodiscard]] int                stageIndex();
    [[nodiscard]] std::string        stageId();
    [[nodiscard]] double             stageProgressDays();
    [[nodiscard]] bool               isMature();
    [[nodiscard]] bool               isDead();
    [[nodiscard]] bool               isHarvested();
    [[nodiscard]] bool               isDiseased();
    [[nodiscard]] bool               isPregnant();
    [[nodiscard]] std::string        diseaseId();

    /** @brief Convenience meters when the species declares water/food/fertility needs. */
    [[nodiscard]] double waterPermille();
    [[nodiscard]] double foodPermille();
    [[nodiscard]] double fertilityPermille();
    /** @brief Generic meter read; missing need returns 0. */
    [[nodiscard]] double needPermille(const std::string& needId);

    /**
     * @brief Refills a named need declared by the species.
     * @cost Cheap: O(1) meter write + optional event.
     */
    [[nodiscard]] Result<void> refillNeed(const std::string& needId, double permille);
    /**
     * @brief Adds water when the species declares a `water` need.
     * @cost Cheap: O(1) meter write + optional event.
     */
    [[nodiscard]] Result<void> waterBy(double permille);
    /**
     * @brief Adds food when the species declares a `food` need.
     * @cost Cheap: O(1) meter write + optional event.
     */
    [[nodiscard]] Result<void> feed(double permille);
    /**
     * @brief Adds fertility when the species declares a `fertility` need.
     * @cost Cheap: O(1) meter write + optional event.
     */
    [[nodiscard]] Result<void> fertilize(double permille);

    /**
     * @brief Harvests once when mature and harvestYield is non-empty (any lifeform).
     * @cost Moderate: copies yield defs and marks harvested; emits one event.
     */
    [[nodiscard]] Result<std::vector<YieldReceipt>> harvest();

    /** @brief Stable ECS id for event correlation. */
    [[nodiscard]] std::uint64_t stableId() const noexcept;

    /** @brief Script destroy entry (aliases release). */
    void destroy();
};

}  // namespace eve::growth
