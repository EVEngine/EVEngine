#pragma once
#include "common/Export.h"

#include "common/ECS.h"
#include "common/Result.h"
#include "growth/Species.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::growth {

class GrowthWorld;

/** @brief One harvested or produced item line (no inventory dependency). */
struct YieldReceipt {
    std::string itemId;
    int         count = 0;
};

/**
 * @brief Farm growth short-root entity (plants and animals).
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
        OrganismKind kind              = OrganismKind::Plant;
        int          stageIndex        = 0;
        double       stageProgressDays = 0.0;
        bool         mature            = false;
        bool         dead              = false;
        bool         harvested         = false;
    };

    /** @brief Need meters in permille [0, 1000]. */
    struct VitalNeeds {
        double waterPermille     = 1000.0;
        double foodPermille      = 1000.0;
        double fertilityPermille = 1000.0;
    };

    /** @brief Disease state; empty diseaseId means healthy. */
    struct Health {
        std::string activeDiseaseId;
        double      diseaseDaysLeft  = 0.0;
        double      diseaseGrowthMul = 1.0;
    };

    /** @brief Gestation state for breeding animals. */
    struct Reproduction {
        bool   pregnant              = false;
        double gestationProgressDays = 0.0;
    };

    /** @brief Periodic produce timer for livestock. */
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

    // Component accessors from ENTITY/COMPONENT are non-const; getters follow that model.
    [[nodiscard]] const std::string& speciesId();
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

    [[nodiscard]] double waterPermille();
    [[nodiscard]] double foodPermille();
    [[nodiscard]] double fertilityPermille();

    /**
     * @brief Adds water (plants/animals). Clamped to 1000.
     * @cost Cheap: O(1) meter write + optional event.
     */
    [[nodiscard]] Result<void> waterBy(double permille);
    /**
     * @brief Adds food (animals). Plants reject.
     * @cost Cheap: O(1) meter write + optional event.
     */
    [[nodiscard]] Result<void> feed(double permille);
    /**
     * @brief Adds fertility (plants). Animals reject.
     * @cost Cheap: O(1) meter write + optional event.
     */
    [[nodiscard]] Result<void> fertilize(double permille);

    /**
     * @brief Harvests a mature plant once; returns yield lines without touching inventory.
     * @cost Moderate: copies yield defs and marks harvested; emits one event.
     */
    [[nodiscard]] Result<std::vector<YieldReceipt>> harvest();

    /** @brief Stable ECS id for event correlation. */
    [[nodiscard]] std::uint64_t stableId() const noexcept;

    /** @brief Script destroy entry (aliases release). */
    void destroy();
};

}  // namespace eve::growth
