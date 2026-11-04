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
    growth.kind              = species->kind;
    growth.stageIndex        = 0;
    growth.stageProgressDays = 0.0;
    growth.mature            = false;
    growth.dead              = false;
    growth.harvested         = false;

    auto& needs             = *organism->vitalNeeds();
    needs.waterPermille     = 1000.0;
    needs.foodPermille      = 1000.0;
    needs.fertilityPermille = 1000.0;

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

double Organism::waterPermille() { return vitalNeeds()->waterPermille; }

double Organism::foodPermille() { return vitalNeeds()->foodPermille; }

double Organism::fertilityPermille() { return vitalNeeds()->fertilityPermille; }

Result<void> Organism::waterBy(double permille) {
    if (growth()->dead) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "organism is dead", "growth.organism.waterBy"));
    }
    if (!std::isfinite(permille) || permille < 0.0) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "water amount must be finite and >= 0", "growth.organism.waterBy"));
    }
    vitalNeeds()->waterPermille = clampPermille(vitalNeeds()->waterPermille + permille);
    GrowthWorld::instance().emit(GrowthEventKind::Watered, *this, std::to_string(permille));
    return Result<void>::success();
}

Result<void> Organism::feed(double permille) {
    if (growth()->kind != OrganismKind::Animal) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "only animals can be fed", "growth.organism.feed"));
    }
    if (growth()->dead) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "organism is dead", "growth.organism.feed"));
    }
    if (!std::isfinite(permille) || permille < 0.0) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "feed amount must be finite and >= 0", "growth.organism.feed"));
    }
    vitalNeeds()->foodPermille = clampPermille(vitalNeeds()->foodPermille + permille);
    GrowthWorld::instance().emit(GrowthEventKind::Fed, *this, std::to_string(permille));
    return Result<void>::success();
}

Result<void> Organism::fertilize(double permille) {
    if (growth()->kind != OrganismKind::Plant) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "only plants can be fertilized",
                                                       "growth.organism.fertilize"));
    }
    if (growth()->dead) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "organism is dead", "growth.organism.fertilize"));
    }
    if (!std::isfinite(permille) || permille < 0.0) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "fertilizer amount must be finite and >= 0", "growth.organism.fertilize"));
    }
    vitalNeeds()->fertilityPermille = clampPermille(vitalNeeds()->fertilityPermille + permille);
    GrowthWorld::instance().emit(GrowthEventKind::Fertilized, *this, std::to_string(permille));
    return Result<void>::success();
}

Result<std::vector<YieldReceipt>> Organism::harvest() {
    if (growth()->kind != OrganismKind::Plant) {
        return Result<std::vector<YieldReceipt>>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "only plants can be harvested", "growth.organism.harvest"));
    }
    if (growth()->dead) {
        return Result<std::vector<YieldReceipt>>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "organism is dead", "growth.organism.harvest"));
    }
    if (!growth()->mature) {
        return Result<std::vector<YieldReceipt>>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "plant is not mature", "growth.organism.harvest"));
    }
    if (growth()->harvested) {
        return Result<std::vector<YieldReceipt>>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "plant already harvested", "growth.organism.harvest"));
    }
    const SpeciesDef* species = SpeciesRegistry::find(growth()->speciesId);
    if (!species) {
        return Result<std::vector<YieldReceipt>>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "unknown species", "growth.organism.harvest"));
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
