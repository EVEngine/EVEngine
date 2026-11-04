#pragma once
#include "common/Export.h"

#include "common/Result.h"
#include "common/Time.h"
#include "growth/GrowthCalendar.h"
#include "growth/GrowthEvents.h"
#include "growth/Organism.h"

#include <cstdint>

namespace eve::growth {

/**
 * @brief Owner of calendar, RNG seed, and the sole simulation advance path.
 *
 * Callers inject SimulationStep (or explicit sim days). Wall clock is never read.
 */
class EVENGINE_API_FOUNDATION GrowthWorld {
public:
    GrowthWorld() = default;

    /** @brief Seconds of SimulationStep.delta that equal one simulation day. */
    [[nodiscard]] Result<void> setSecondsPerSimDay(double seconds) noexcept;
    [[nodiscard]] double       secondsPerSimDay() const noexcept { return secondsPerSimDay_; }

    /** @brief Deterministic seed for disease / yield hash streams. */
    void                        setRngSeed(std::uint64_t seed) noexcept { rngSeed_ = seed; }
    [[nodiscard]] std::uint64_t rngSeed() const noexcept { return rngSeed_; }

    [[nodiscard]] GrowthCalendar&       calendar() noexcept { return calendar_; }
    [[nodiscard]] const GrowthCalendar& calendar() const noexcept { return calendar_; }
    [[nodiscard]] GrowthEventLog&       events() noexcept { return events_; }
    [[nodiscard]] const GrowthEventLog& events() const noexcept { return events_; }

    /**
     * @brief Converts a fixed SimulationStep into fractional sim days and advances.
     * @cost Linear in live organism count for the fractional day span.
     */
    [[nodiscard]] Result<void> step(const SimulationStep& step);

    /**
     * @brief Advances every live organism by simulation days (may be fractional).
     * @cost Linear in organism count × whole-day crossings for calendar events.
     */
    [[nodiscard]] Result<void> advanceDays(double days);

    /**
     * @brief Starts gestation on carrier when both are mature, breedable animals.
     * @cost Cheap validation + component write; offspring spawn on later advance.
     */
    [[nodiscard]] Result<void> tryBreed(Organism& carrier, Organism& partner);

    /** @brief Clears organisms' world-facing bookkeeping used by tests (events + calendar). */
    void resetForTests();

    /** @brief Process-wide world used by Organism helpers and the Growth module. */
    [[nodiscard]] static GrowthWorld& instance();
    /** @brief Replaces process instance with a fresh world (tests). */
    static void resetInstance();

private:
    friend class Organism;

    void                 applyDaySlice(Organism& organism, double days, std::int64_t dayIndexForHash);
    void                 drainNeeds(Organism& organism, const SpeciesDef& species, double days);
    [[nodiscard]] double growthMultiplier(Organism& organism, const SpeciesDef& species);
    void                 advanceGrowth(Organism& organism, const SpeciesDef& species, double days);
    void advanceDisease(Organism& organism, const SpeciesDef& species, double days, std::int64_t dayIndexForHash);
    void advanceProduce(Organism& organism, const SpeciesDef& species, double days);
    void advanceGestation(Organism& organism, const SpeciesDef& species, double days);
    void emit(GrowthEventKind kind, Organism& organism, std::string detail);

    GrowthCalendar calendar_;
    GrowthEventLog events_;
    double         secondsPerSimDay_ = 60.0;
    double         dayFraction_      = 0.0;
    std::uint64_t  rngSeed_          = 1;
};

}  // namespace eve::growth
