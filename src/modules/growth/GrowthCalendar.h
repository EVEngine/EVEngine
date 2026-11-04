#pragma once
#include "common/Export.h"

#include "common/Result.h"

#include <cstdint>

namespace eve::growth {

/** @brief Four-season farm calendar (domain time, not daynight presentation). */
enum class Season : std::uint8_t { Spring = 0, Summer = 1, Autumn = 2, Winter = 3 };

/**
 * @brief Injected simulation calendar owned by GrowthWorld.
 * @remarks Advances only through GrowthWorld::advanceDays / step — never wall clock.
 */
class EVENGINE_API_FOUNDATION GrowthCalendar {
public:
    /** @brief Days in each season; must be positive. */
    [[nodiscard]] Result<void> setDaysPerSeason(int days) noexcept;
    /** @brief Current days-per-season setting. */
    [[nodiscard]] int daysPerSeason() const noexcept { return daysPerSeason_; }

    /** @brief Absolute simulation day index (0-based). */
    [[nodiscard]] std::int64_t dayIndex() const noexcept { return dayIndex_; }
    /** @brief 0-based year from dayIndex / (4 * daysPerSeason). */
    [[nodiscard]] std::int64_t year() const noexcept;
    /** @brief Current season from dayIndex. */
    [[nodiscard]] Season season() const noexcept;
    /** @brief Day within the current season [0, daysPerSeason). */
    [[nodiscard]] int dayOfSeason() const noexcept;

    /** @brief Advances the calendar by whole simulation days (truncated toward zero). */
    void advanceWholeDays(std::int64_t days) noexcept;
    /** @brief Resets to day 0 (tests). */
    void reset() noexcept;

    /**
     * @brief Stable English season name for scripts and events.
     * @return Non-owning pointer to a static string literal; never null.
     * @ownership Caller does not own the pointer and must not free it.
     * @lifetime Valid for the process lifetime (static storage).
     */
    [[nodiscard]] static const char* seasonName(Season season) noexcept;

private:
    int          daysPerSeason_ = 28;
    std::int64_t dayIndex_      = 0;
};

}  // namespace eve::growth
