#pragma once
#include "common/Export.h"

#include "common/Result.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eve::growth {

/** @brief One named season slot in a configurable calendar layout. */
struct SeasonSlotDef {
    std::string id;
    int         days = 28;
};

/**
 * @brief Legacy four-season index kept for Earth-farm defaults and older tests.
 * @remarks Prefer seasonIndex()/seasonId() when the calendar layout is custom.
 */
enum class Season : std::uint8_t { Spring = 0, Summer = 1, Autumn = 2, Winter = 3 };

/**
 * @brief Injected simulation calendar owned by GrowthWorld.
 * @remarks Advances only through GrowthWorld::advanceDays / step — never wall clock.
 *          Season count and names are configurable (alien year cycles welcome).
 */
class EVENGINE_API_FOUNDATION GrowthCalendar {
public:
    GrowthCalendar();

    /**
     * @brief Replaces the season layout (must be non-empty; each slot days &gt; 0).
     * @cost Cheap: copies a small season table.
     */
    [[nodiscard]] Result<void> setSeasonLayout(std::vector<SeasonSlotDef> seasons) noexcept;
    /**
     * @brief Parses `[{"id":"bloom","days":40},…]` and applies setSeasonLayout.
     * @cost Cheap JSON parse of a small array.
     */
    [[nodiscard]] Result<void> setSeasonLayoutFromJson(std::string_view json);
    /**
     * @brief Sets every slot's day count (keeps current ids). Positive only.
     * @remarks With the default Earth layout this matches the old 4×N year.
     */
    [[nodiscard]] Result<void> setDaysPerSeason(int days) noexcept;
    /** @brief Days of the first season slot (legacy helper). */
    [[nodiscard]] int daysPerSeason() const noexcept;

    [[nodiscard]] const std::vector<SeasonSlotDef>& seasonLayout() const noexcept { return seasons_; }
    [[nodiscard]] int                               seasonCount() const noexcept;

    /** @brief Absolute simulation day index (0-based). */
    [[nodiscard]] std::int64_t dayIndex() const noexcept { return dayIndex_; }
    /** @brief 0-based year from dayIndex / yearLength. */
    [[nodiscard]] std::int64_t year() const noexcept;
    /** @brief Index into seasonLayout() for the current day. */
    [[nodiscard]] int seasonIndex() const noexcept;
    /** @brief Current season id string (never empty when layout is valid). */
    [[nodiscard]] std::string seasonId() const;
    /**
     * @brief Legacy Season enum when layout is the default 4 Earth seasons; else maps by index%4.
     * @deprecated Prefer seasonIndex()/seasonId() for custom calendars.
     */
    [[nodiscard]] Season season() const noexcept;
    /** @brief Day within the current season [0, slot.days). */
    [[nodiscard]] int dayOfSeason() const noexcept;

    /** @brief Advances the calendar by whole simulation days (truncated toward zero). */
    void advanceWholeDays(std::int64_t days) noexcept;
    /** @brief Resets to day 0 (tests). */
    void reset() noexcept;

    /**
     * @brief Stable English season name for the default Earth layout.
     * @return Non-owning pointer to a static string literal; never null.
     * @ownership Caller does not own the pointer and must not free it.
     * @lifetime Valid for the process lifetime (static storage).
     * @nullable Never.
     */
    [[nodiscard]] static const char* seasonName(Season season) noexcept;

private:
    [[nodiscard]] std::int64_t yearLength() const noexcept;

    std::vector<SeasonSlotDef> seasons_;
    std::int64_t               dayIndex_ = 0;
};

}  // namespace eve::growth
