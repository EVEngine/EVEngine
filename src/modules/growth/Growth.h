#pragma once
#include "common/Export.h"

#include "common/Module.h"
#include "common/Result.h"
#include "growth/GrowthSystem.h"
#include "growth/Organism.h"
#include "growth/Species.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::growth {

/**
 * @brief Farm growth module facade (`eve.Growth`): species registry + world step.
 */
class EVENGINE_API_FOUNDATION Growth : public Module {
public:
    Module_REG(Growth);
    Growth()           = default;
    ~Growth() override = default;

    /** @brief Registers species from a JSON object or array. */
    [[nodiscard]] Result<int> registerSpeciesFromJson(const std::string& json);
    /** @brief Clears species (tests / demos). */
    void               clearSpecies();
    [[nodiscard]] bool hasSpecies(const std::string& id) const;
    [[nodiscard]] int  speciesCount() const;

    [[nodiscard]] Result<void> setDaysPerSeason(int days);
    /**
     * @brief Configures a custom season layout from JSON (alien year cycles).
     * @cost Cheap: parses a small season table.
     */
    [[nodiscard]] Result<void> setSeasonLayoutFromJson(const std::string& json);
    [[nodiscard]] Result<void> setSecondsPerSimDay(double seconds);
    void                       setRngSeed(std::uint64_t seed);
    [[nodiscard]] Result<void> advanceDays(double days);
    [[nodiscard]] Result<void> stepSeconds(double seconds);

    [[nodiscard]] std::int64_t dayIndex() const;
    [[nodiscard]] std::int64_t year() const;
    [[nodiscard]] std::string  seasonName() const;

    /** @brief Spawns any registered species (plant, animal, or custom lifeform). */
    [[nodiscard]] Result<Organism*> spawn(const std::string& speciesId);
    /** @brief Convenience: spawn when species.kindId == "plant". */
    [[nodiscard]] Result<Organism*> plant(const std::string& speciesId);
    /** @brief Convenience: spawn when species.kindId == "animal". */
    [[nodiscard]] Result<Organism*> spawnAnimal(const std::string& speciesId);
    [[nodiscard]] Result<void>      tryBreed(Organism* carrier, Organism* partner);
    /** @brief Solo reproduction for self_bud / fission / spore_burst species. */
    [[nodiscard]] Result<void> tryReproduce(Organism* organism);

    void                        clearEvents();
    [[nodiscard]] int           eventCount() const;
    [[nodiscard]] std::string   eventKind(int index) const;
    [[nodiscard]] std::uint64_t eventOrganismId(int index) const;
    [[nodiscard]] std::string   eventSpeciesId(int index) const;
    [[nodiscard]] std::string   eventDetail(int index) const;

    /** @brief Resets calendar, events, and species for demos/tests. */
    void resetWorld();
};

}  // namespace eve::growth
