#include "growth/GrowthCalendar.h"

#include "common/Assert.h"
#include "common/Diagnostic.h"

namespace eve::growth {

Result<void> GrowthCalendar::setDaysPerSeason(int days) noexcept {
    if (days <= 0) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "daysPerSeason must be positive", "growth.calendar.daysPerSeason"));
    }
    daysPerSeason_ = days;
    return Result<void>::success();
}

std::int64_t GrowthCalendar::year() const noexcept {
    const std::int64_t yearLength = static_cast<std::int64_t>(daysPerSeason_) * 4;
    EV_ASSERT(yearLength > 0, "daysPerSeason must stay positive");
    return dayIndex_ / yearLength;
}

Season GrowthCalendar::season() const noexcept {
    const std::int64_t yearLength = static_cast<std::int64_t>(daysPerSeason_) * 4;
    const std::int64_t dayInYear  = dayIndex_ % yearLength;
    return static_cast<Season>(static_cast<int>(dayInYear / daysPerSeason_));
}

int GrowthCalendar::dayOfSeason() const noexcept { return static_cast<int>(dayIndex_ % daysPerSeason_); }

void GrowthCalendar::advanceWholeDays(std::int64_t days) noexcept {
    if (days <= 0) return;
    dayIndex_ += days;
}

void GrowthCalendar::reset() noexcept { dayIndex_ = 0; }

const char* GrowthCalendar::seasonName(Season season) noexcept {
    switch (season) {
        case Season::Spring: return "spring";
        case Season::Summer: return "summer";
        case Season::Autumn: return "autumn";
        case Season::Winter: return "winter";
    }
    return "spring";
}

}  // namespace eve::growth
