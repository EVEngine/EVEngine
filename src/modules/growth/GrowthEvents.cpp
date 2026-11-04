#include "growth/GrowthEvents.h"

namespace eve::growth {

void GrowthEventLog::push(GrowthEvent event) { events_.push_back(std::move(event)); }

void GrowthEventLog::clear() { events_.clear(); }

int GrowthEventLog::count() const { return static_cast<int>(events_.size()); }

const GrowthEvent* GrowthEventLog::at(int index) const {
    if (index < 0 || index >= count()) return nullptr;
    return &events_[static_cast<std::size_t>(index)];
}

const char* GrowthEventLog::kindName(GrowthEventKind kind) noexcept {
    switch (kind) {
        case GrowthEventKind::StageChanged: return "stage_changed";
        case GrowthEventKind::Matured: return "matured";
        case GrowthEventKind::Produced: return "produced";
        case GrowthEventKind::Harvested: return "harvested";
        case GrowthEventKind::Watered: return "watered";
        case GrowthEventKind::Fed: return "fed";
        case GrowthEventKind::Fertilized: return "fertilized";
        case GrowthEventKind::Diseased: return "diseased";
        case GrowthEventKind::Cured: return "cured";
        case GrowthEventKind::Bred: return "bred";
        case GrowthEventKind::Offspring: return "offspring";
        case GrowthEventKind::Died: return "died";
    }
    return "unknown";
}

}  // namespace eve::growth
