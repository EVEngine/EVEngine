#include "growth/GrowthCalendar.h"

#include "common/Assert.h"
#include "common/Diagnostic.h"
#include "common/Value.h"

namespace eve::growth {
namespace {

std::vector<SeasonSlotDef> defaultEarthSeasons(int days) {
    return {
        SeasonSlotDef{"spring", days},
        SeasonSlotDef{"summer", days},
        SeasonSlotDef{"autumn", days},
        SeasonSlotDef{"winter", days},
    };
}

}  // namespace

GrowthCalendar::GrowthCalendar() : seasons_(defaultEarthSeasons(28)) {}

Result<void> GrowthCalendar::setSeasonLayout(std::vector<SeasonSlotDef> seasons) noexcept {
    if (seasons.empty()) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "season layout must be non-empty", "growth.calendar.layout"));
    }
    for (const SeasonSlotDef& slot : seasons) {
        if (slot.id.empty() || slot.days <= 0) {
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "each season needs a non-empty id and days > 0",
                                                           "growth.calendar.layout"));
        }
    }
    seasons_ = std::move(seasons);
    return Result<void>::success();
}

Result<void> GrowthCalendar::setSeasonLayoutFromJson(std::string_view json) {
    auto parsed = Value::fromJson(json);
    if (!parsed) return Result<void>::failure(parsed.status());
    Value root = std::move(parsed).takeValue();
    const auto* array = root.getIf<Value::Array>();
    if (!array || array->empty()) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "season layout JSON must be a non-empty array",
                                                       "growth.calendar.layout"));
    }
    std::vector<SeasonSlotDef> seasons;
    seasons.reserve(array->size());
    for (const Value& entry : *array) {
        const auto* object = entry.getIf<Value::Object>();
        if (!object) {
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "season entry must be an object", "growth.calendar.layout"));
        }
        SeasonSlotDef slot;
        auto          idIt = object->find("id");
        if (idIt == object->end() || !idIt->second.getIf<std::string>()) {
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "season.id must be a string", "growth.calendar.layout"));
        }
        slot.id = *idIt->second.getIf<std::string>();
        slot.days = 28;
        auto daysIt = object->find("days");
        if (daysIt != object->end()) {
            if (const auto* integer = daysIt->second.getIf<std::int64_t>()) {
                slot.days = static_cast<int>(*integer);
            } else if (const auto* number = daysIt->second.getIf<double>()) {
                slot.days = static_cast<int>(*number);
            } else {
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "season.days must be an integer", "growth.calendar.layout"));
            }
        }
        seasons.push_back(std::move(slot));
    }
    return setSeasonLayout(std::move(seasons));
}

Result<void> GrowthCalendar::setDaysPerSeason(int days) noexcept {
    if (days <= 0) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "daysPerSeason must be positive", "growth.calendar.daysPerSeason"));
    }
    if (seasons_.empty()) seasons_ = defaultEarthSeasons(days);
    for (SeasonSlotDef& slot : seasons_) slot.days = days;
    return Result<void>::success();
}

int GrowthCalendar::daysPerSeason() const noexcept {
    if (seasons_.empty()) return 28;
    return seasons_.front().days;
}

int GrowthCalendar::seasonCount() const noexcept { return static_cast<int>(seasons_.size()); }

std::int64_t GrowthCalendar::yearLength() const noexcept {
    std::int64_t total = 0;
    for (const SeasonSlotDef& slot : seasons_) total += slot.days;
    EV_ASSERT(total > 0, "season layout must stay positive");
    return total;
}

std::int64_t GrowthCalendar::year() const noexcept { return dayIndex_ / yearLength(); }

int GrowthCalendar::seasonIndex() const noexcept {
    if (seasons_.empty()) return 0;
    std::int64_t dayInYear = dayIndex_ % yearLength();
    for (std::size_t i = 0; i < seasons_.size(); ++i) {
        if (dayInYear < seasons_[i].days) return static_cast<int>(i);
        dayInYear -= seasons_[i].days;
    }
    return static_cast<int>(seasons_.size() - 1);
}

std::string GrowthCalendar::seasonId() const {
    if (seasons_.empty()) return "spring";
    return seasons_[static_cast<std::size_t>(seasonIndex())].id;
}

Season GrowthCalendar::season() const noexcept {
    return static_cast<Season>(seasonIndex() % 4);
}

int GrowthCalendar::dayOfSeason() const noexcept {
    if (seasons_.empty()) return 0;
    std::int64_t dayInYear = dayIndex_ % yearLength();
    for (const SeasonSlotDef& slot : seasons_) {
        if (dayInYear < slot.days) return static_cast<int>(dayInYear);
        dayInYear -= slot.days;
    }
    return 0;
}

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
