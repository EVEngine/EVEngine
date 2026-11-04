#include "growth/Organism.h"

#include "common/Diagnostic.h"
#include "growth/GrowthSystem.h"

#include <algorithm>
#include <cmath>

namespace eve::growth {
namespace {

double clampPermille(double value) {
    if (!std::isfinite(value)) return 0.0;
    return std::clamp(value, 0.0, 1000.0);
}

double meterOrZero(const Organism::VitalNeeds& needs, const std::string& id) {
    auto it = needs.meters.find(id);
    if (it == needs.meters.end()) return 0.0;
    return it->second;
}

}  // namespace

Result<Organism*> Organism::createOrganism(const std::string& speciesId) {
    const SpeciesDef* species = SpeciesRegistry::find(speciesId);
    if (!species) {
        return Result<Organism*>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "unknown species", "growth.organism.speciesId"));
    }
    Organism* organism       = Organism::create();
    auto&     growth         = *organism->growth();
    growth.speciesId         = species->id;
    growth.kindId            = species->kindId;
    growth.kind              = species->kind;
    growth.stageIndex        = 0;
    growth.stageProgressDays = 0.0;
    growth.mature            = false;
    growth.dead              = false;
    growth.harvested         = false;

    auto& needs = *organism->vitalNeeds();
    needs.meters.clear();
    for (const NeedMeterDef& need : species->needs) needs.meters[need.id] = 1000.0;

    (void)*organism->health();
    (void)*organism->reproduction();
    (void)*organism->produce();
    return Result<Organism*>::success(organism);
}

const std::vector<Organism*>& Organism::liveOrganisms() {
    static std::vector<Organism*> resolved;
    resolved.clear();
    auto view = ecs::View<Organism, Organism::Growth>();
    for (auto it = view.begin(); it != view.end(); ++it) resolved.push_back(it.entity());
    return resolved;
}

const std::string& Organism::speciesId() { return growth()->speciesId; }

const std::string& Organism::kindId() { return growth()->kindId; }

OrganismKind Organism::kind() { return growth()->kind; }

int Organism::stageIndex() { return growth()->stageIndex; }

std::string Organism::stageId() {
    const SpeciesDef* species = SpeciesRegistry::find(growth()->speciesId);
    if (!species) return {};
    const int index = growth()->stageIndex;
    if (index < 0 || index >= static_cast<int>(species->stages.size())) return {};
    return species->stages[static_cast<std::size_t>(index)].id;
}

double Organism::stageProgressDays() { return growth()->stageProgressDays; }

bool Organism::isMature() { return growth()->mature; }

bool Organism::isDead() { return growth()->dead; }

bool Organism::isHarvested() { return growth()->harvested; }

bool Organism::isDiseased() { return !health()->activeDiseaseId.empty(); }

bool Organism::isPregnant() { return reproduction()->pregnant; }

std::string Organism::diseaseId() { return health()->activeDiseaseId; }

double Organism::waterPermille() { return meterOrZero(*vitalNeeds(), "water"); }

double Organism::foodPermille() { return meterOrZero(*vitalNeeds(), "food"); }

double Organism::fertilityPermille() { return meterOrZero(*vitalNeeds(), "fertility"); }

double Organism::needPermille(const std::string& needId) { return meterOrZero(*vitalNeeds(), needId); }

Result<void> Organism::refillNeed(const std::string& needId, double permille) {
    const SpeciesDef* species = SpeciesRegistry::find(growth()->speciesId);
    if (!species) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "unknown species", "growth.organism.refillNeed"));
    }
    if (!species->hasNeed(needId)) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "species does not declare need '" + needId + "'",
                                                       "growth.organism.refillNeed"));
    }
    if (growth()->dead) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "organism is dead", "growth.organism.refillNeed"));
    }
    if (!std::isfinite(permille) || permille < 0.0) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "need amount must be finite and >= 0", "growth.organism.refillNeed"));
    }
    auto& meters  = vitalNeeds()->meters;
    meters[needId] = clampPermille(meterOrZero(*vitalNeeds(), needId) + permille);
    if (needId == "water") {
        GrowthWorld::instance().emit(GrowthEventKind::Watered, *this, std::to_string(permille));
    } else if (needId == "food") {
        GrowthWorld::instance().emit(GrowthEventKind::Fed, *this, std::to_string(permille));
    } else if (needId == "fertility") {
        GrowthWorld::instance().emit(GrowthEventKind::Fertilized, *this, std::to_string(permille));
    } else {
        GrowthWorld::instance().emit(GrowthEventKind::Watered, *this, needId + ":" + std::to_string(permille));
    }
    return Result<void>::success();
}

Result<void> Organism::waterBy(double permille) { return refillNeed("water", permille); }

Result<void> Organism::feed(double permille) { return refillNeed("food", permille); }

Result<void> Organism::fertilize(double permille) { return refillNeed("fertility", permille); }

Result<std::vector<YieldReceipt>> Organism::harvest() {
    if (growth()->dead) {
        return Result<std::vector<YieldReceipt>>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "organism is dead", "growth.organism.harvest"));
    }
    if (!growth()->mature) {
        return Result<std::vector<YieldReceipt>>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "organism is not mature", "growth.organism.harvest"));
    }
    if (growth()->harvested) {
        return Result<std::vector<YieldReceipt>>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "organism already harvested", "growth.organism.harvest"));
    }
    const SpeciesDef* species = SpeciesRegistry::find(growth()->speciesId);
    if (!species) {
        return Result<std::vector<YieldReceipt>>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "unknown species", "growth.organism.harvest"));
    }
    if (species->harvestYield.empty()) {
        return Result<std::vector<YieldReceipt>>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "species has no harvestYield", "growth.organism.harvest"));
    }

    std::vector<YieldReceipt> yields;
    yields.reserve(species->harvestYield.size());
    for (const YieldDef& def : species->harvestYield) {
        const int count = def.minCount + (def.maxCount - def.minCount) / 2;
        yields.push_back(YieldReceipt{def.itemId, count});
    }
    growth()->harvested = true;
    GrowthWorld::instance().emit(GrowthEventKind::Harvested, *this, std::to_string(static_cast<int>(yields.size())));
    return Result<std::vector<YieldReceipt>>::success(std::move(yields));
}

std::uint64_t Organism::stableId() const noexcept {
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(this));
}

void Organism::destroy() { release(); }

}  // namespace eve::growth
