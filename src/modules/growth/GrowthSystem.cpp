#include "growth/GrowthSystem.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace eve::growth {
namespace {

double clampPermille(double value) {
    if (!std::isfinite(value)) return 0.0;
    return std::clamp(value, 0.0, 1000.0);
}

std::uint64_t splitmix64(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

double unitHash(std::uint64_t seed, std::uint64_t stream, std::uint64_t key) {
    const std::uint64_t mixed = splitmix64(seed ^ (stream + 0x9e3779b97f4a7c15ULL) ^ key);
    return static_cast<double>(mixed >> 11) * (1.0 / 9007199254740992.0);  // [0,1)
}

double meterValue(const Organism::VitalNeeds& needs, const std::string& id) {
    auto it = needs.meters.find(id);
    if (it == needs.meters.end()) return 0.0;
    return it->second;
}

bool anyLethalEmpty(const Organism::VitalNeeds& needs, const SpeciesDef& species) {
    bool sawLethal = false;
    bool allLethalEmpty = true;
    for (const NeedMeterDef& need : species.needs) {
        if (!need.lethalWhenEmpty) continue;
        sawLethal = true;
        if (meterValue(needs, need.id) > 0.0) allLethalEmpty = false;
    }
    return sawLethal && allLethalEmpty;
}

bool isStressed(const Organism::VitalNeeds& needs, const SpeciesDef& species) {
    for (const NeedMeterDef& need : species.needs) {
        if (meterValue(needs, need.id) < need.minPermille) return true;
    }
    return false;
}

std::unique_ptr<GrowthWorld>& worldHolder() {
    static std::unique_ptr<GrowthWorld> world = std::make_unique<GrowthWorld>();
    return world;
}

}  // namespace

GrowthWorld& GrowthWorld::instance() { return *worldHolder(); }

void GrowthWorld::resetInstance() {
    std::vector<Organism*> batch = Organism::liveOrganisms();
    for (Organism* organism : batch) {
        if (organism) organism->release();
    }
    worldHolder() = std::make_unique<GrowthWorld>();
}

Result<void> GrowthWorld::setSecondsPerSimDay(double seconds) noexcept {
    if (!(seconds > 0.0) || !std::isfinite(seconds)) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "secondsPerSimDay must be finite and > 0",
                                                       "growth.world.secondsPerSimDay"));
    }
    secondsPerSimDay_ = seconds;
    return Result<void>::success();
}

void GrowthWorld::emit(GrowthEventKind kind, Organism& organism, std::string detail) {
    GrowthEvent event;
    event.kind       = kind;
    event.organismId = organism.stableId();
    event.speciesId  = organism.speciesId();
    event.detail     = std::move(detail);
    events_.push(std::move(event));
}

void GrowthWorld::resetForTests() {
    calendar_.reset();
    events_.clear();
    dayFraction_ = 0.0;
}

Result<void> GrowthWorld::step(const SimulationStep& step) {
    if (step.delta.nanoseconds() < 0) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "SimulationStep.delta must be non-negative", "growth.world.step"));
    }
    const double days = step.delta.seconds() / secondsPerSimDay_;
    return advanceDays(days);
}

Result<void> GrowthWorld::advanceDays(double days) {
    if (!std::isfinite(days) || days < 0.0) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "days must be finite and >= 0",
                                                       "growth.world.advanceDays"));
    }
    if (days == 0.0) return Result<void>::success();

    double remaining = days;
    while (remaining > 0.0) {
        const double       toEdge     = 1.0 - dayFraction_;
        const double       slice      = remaining < toEdge ? remaining : toEdge;
        const std::int64_t dayForHash = calendar_.dayIndex();
        std::vector<Organism*> batch = Organism::liveOrganisms();
        for (Organism* organism : batch) {
            if (!organism || organism->isDead()) continue;
            applyDaySlice(*organism, slice, dayForHash);
        }
        dayFraction_ += slice;
        remaining -= slice;
        if (dayFraction_ + 1e-12 >= 1.0) {
            dayFraction_ = 0.0;
            calendar_.advanceWholeDays(1);
        }
    }
    return Result<void>::success();
}

void GrowthWorld::drainNeeds(Organism& organism, const SpeciesDef& species, double days) {
    auto& needs = *organism.vitalNeeds();
    for (const NeedMeterDef& need : species.needs) {
        const double current = meterValue(needs, need.id);
        needs.meters[need.id] = clampPermille(current - need.drainPerDay * days);
    }
    if (anyLethalEmpty(needs, species)) {
        organism.growth()->dead = true;
        emit(GrowthEventKind::Died, organism, "neglect");
    }
}

double GrowthWorld::growthMultiplier(Organism& organism, const SpeciesDef& species) {
    if (organism.isDead()) return 0.0;
    const auto& needs = *organism.vitalNeeds();
    for (const NeedMeterDef& need : species.needs) {
        if (need.requiredForGrowth && meterValue(needs, need.id) < need.minPermille) return 0.0;
    }

    double mul = 1.0;
    const std::string seasonId = calendar_.seasonId();
    if (!species.seasonMulById.empty()) {
        auto it = species.seasonMulById.find(seasonId);
        if (it != species.seasonMulById.end()) mul = it->second;
    } else if (!species.seasonMul.empty()) {
        const int index = calendar_.seasonIndex();
        if (index >= 0 && index < static_cast<int>(species.seasonMul.size())) {
            mul = species.seasonMul[static_cast<std::size_t>(index)];
        } else {
            mul = species.seasonMul.back();
        }
    }
    if (organism.isDiseased()) mul *= organism.health()->diseaseGrowthMul;
    if (!(mul > 0.0) || !std::isfinite(mul)) return 0.0;
    return mul;
}

void GrowthWorld::advanceGrowth(Organism& organism, const SpeciesDef& species, double days) {
    auto& growth = *organism.growth();
    if (growth.dead || growth.mature || growth.harvested) return;
    if (growth.stageIndex < 0 || growth.stageIndex >= static_cast<int>(species.stages.size())) return;

    const double mul = growthMultiplier(organism, species);
    if (mul <= 0.0) return;

    double remaining = days * mul;
    while (remaining > 0.0 && !growth.mature && !growth.dead) {
        const GrowthStageDef& stage = species.stages[static_cast<std::size_t>(growth.stageIndex)];
        const double          need  = stage.durationDays - growth.stageProgressDays;
        if (remaining + 1e-12 < need) {
            growth.stageProgressDays += remaining;
            remaining = 0.0;
            break;
        }
        remaining -= need;
        growth.stageProgressDays = 0.0;
        if (growth.stageIndex + 1 >= static_cast<int>(species.stages.size())) {
            growth.mature = true;
            emit(GrowthEventKind::Matured, organism, stage.id);
            break;
        }
        ++growth.stageIndex;
        emit(GrowthEventKind::StageChanged, organism, species.stages[static_cast<std::size_t>(growth.stageIndex)].id);
    }
}

void GrowthWorld::advanceDisease(Organism& organism, const SpeciesDef& species, double days,
                                 std::int64_t dayIndexForHash) {
    auto& health = *organism.health();
    if (!health.activeDiseaseId.empty()) {
        health.diseaseDaysLeft -= days;
        if (health.diseaseDaysLeft <= 0.0) {
            health.activeDiseaseId.clear();
            health.diseaseDaysLeft  = 0.0;
            health.diseaseGrowthMul = 1.0;
            emit(GrowthEventKind::Cured, organism, {});
        }
        return;
    }

    if (species.diseases.empty() || days <= 0.0) return;
    if (!isStressed(*organism.vitalNeeds(), species)) return;

    const double resist = std::clamp(species.baseDiseaseResist, 0.0, 1.0);
    for (std::size_t i = 0; i < species.diseases.size(); ++i) {
        const DiseaseSusceptibility& disease = species.diseases[i];
        const double                 chance  = std::clamp(disease.chancePerDay * days * (1.0 - resist), 0.0, 1.0);
        const double                 roll    = unitHash(
            rngSeed_, 0xD15EA5EULL,
            organism.stableId() ^ (static_cast<std::uint64_t>(dayIndexForHash) << 8) ^ static_cast<std::uint64_t>(i));
        if (roll < chance) {
            health.activeDiseaseId  = disease.diseaseId;
            health.diseaseDaysLeft  = disease.durationDays;
            health.diseaseGrowthMul = disease.growthMul;
            emit(GrowthEventKind::Diseased, organism, disease.diseaseId);
            break;
        }
    }
}

void GrowthWorld::advanceProduce(Organism& organism, const SpeciesDef& species, double days) {
    if (!organism.isMature() || organism.isDead()) return;
    if (species.produceIntervalDays <= 0.0 || species.produceYield.empty()) return;

    auto& produce = *organism.produce();
    produce.daysSinceProduce += days;
    while (produce.daysSinceProduce + 1e-12 >= species.produceIntervalDays) {
        produce.daysSinceProduce -= species.produceIntervalDays;
        emit(GrowthEventKind::Produced, organism, species.produceYield.front().itemId);
    }
}

Result<void> GrowthWorld::spawnOffspring(Organism& parent, const SpeciesDef& species, int count) {
    if (count <= 0) return Result<void>::success();
    const std::string offspringId = species.reproduction.offspringSpeciesId.empty()
                                        ? species.id
                                        : species.reproduction.offspringSpeciesId;
    for (int i = 0; i < count; ++i) {
        auto child = Organism::createOrganism(offspringId);
        if (!child) {
            emit(GrowthEventKind::Died, parent, "offspring_failed");
            return Result<void>::failure(child.status());
        }
        emit(GrowthEventKind::Offspring, *child.value(), offspringId);
    }

    auto& growth = *parent.growth();
    if (species.reproduction.parentDiesOnBirth) {
        growth.dead = true;
        emit(GrowthEventKind::Died, parent, "fission");
    } else if (species.reproduction.parentResetsStagesOnBirth) {
        growth.stageIndex        = 0;
        growth.stageProgressDays = 0.0;
        growth.mature            = false;
        growth.harvested         = false;
        emit(GrowthEventKind::StageChanged, parent, species.stages.front().id);
    }
    return Result<void>::success();
}

void GrowthWorld::advanceReproduction(Organism& organism, const SpeciesDef& species, double days) {
    auto& repro = *organism.reproduction();
    const ReproductionDef& def = species.reproduction;

    if (def.mode == ReproductionMode::PairGestation) {
        if (!repro.pregnant) return;
        repro.gestationProgressDays += days;
        if (repro.gestationProgressDays + 1e-12 < def.gestationDays) return;
        repro.pregnant              = false;
        repro.gestationProgressDays = 0.0;
        const int count =
            def.offspringCountMin + (def.offspringCountMax - def.offspringCountMin) / 2;
        spawnOffspring(organism, species, std::max(1, count))
            .ignore("gestation completion already emitted offspring_failed on error");
        return;
    }

    if (def.mode != ReproductionMode::SelfBud && def.mode != ReproductionMode::Fission &&
        def.mode != ReproductionMode::SporeBurst)
        return;
    if (!organism.isMature() || organism.isDead()) return;
    if (!(def.intervalDays > 0.0)) return;

    repro.daysSinceReproduce += days;
    while (repro.daysSinceReproduce + 1e-12 >= def.intervalDays && !organism.isDead()) {
        repro.daysSinceReproduce -= def.intervalDays;
        const int count =
            def.offspringCountMin + (def.offspringCountMax - def.offspringCountMin) / 2;
        emit(GrowthEventKind::Bred, organism, "auto:" + species.kindId);
        spawnOffspring(organism, species, std::max(1, count))
            .ignore("interval reproduction already emitted offspring_failed on error");
        if (organism.isDead() || def.parentResetsStagesOnBirth) break;
    }
}

void GrowthWorld::applyDaySlice(Organism& organism, double days, std::int64_t dayIndexForHash) {
    const SpeciesDef* species = SpeciesRegistry::find(organism.speciesId());
    if (!species || organism.isDead()) return;
    drainNeeds(organism, *species, days);
    if (organism.isDead()) return;
    advanceDisease(organism, *species, days, dayIndexForHash);
    if (organism.isDead()) return;
    advanceGrowth(organism, *species, days);
    advanceProduce(organism, *species, days);
    advanceReproduction(organism, *species, days);
}

Result<void> GrowthWorld::tryBreed(Organism& carrier, Organism& partner) {
    if (&carrier == &partner) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "cannot breed an organism with itself", "growth.world.tryBreed"));
    }
    if (carrier.isDead() || partner.isDead()) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "dead organisms cannot breed", "growth.world.tryBreed"));
    }
    if (!carrier.isMature() || !partner.isMature()) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "both organisms must be mature", "growth.world.tryBreed"));
    }
    const SpeciesDef* species = SpeciesRegistry::find(carrier.speciesId());
    if (!species) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "unknown species", "growth.world.tryBreed"));
    }
    const ReproductionDef& def = species->reproduction;
    if (def.mode != ReproductionMode::PairGestation || !def.requiresPartner) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "species reproduction.mode is not pair_gestation",
                                                       "growth.world.tryBreed"));
    }
    if (def.partnerMustSameSpecies && carrier.speciesId() != partner.speciesId()) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "partners must share a species",
                                                       "growth.world.tryBreed"));
    }
    if (!(def.gestationDays > 0.0)) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "species cannot breed", "growth.world.tryBreed"));
    }
    if (carrier.isPregnant() || partner.isPregnant()) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "a partner is already pregnant", "growth.world.tryBreed"));
    }

    auto& repro                 = *carrier.reproduction();
    repro.pregnant              = true;
    repro.gestationProgressDays = 0.0;
    repro.partnerStableId       = partner.stableId();
    emit(GrowthEventKind::Bred, carrier, partner.speciesId());
    return Result<void>::success();
}

Result<void> GrowthWorld::tryReproduce(Organism& organism) {
    if (organism.isDead()) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "dead organisms cannot reproduce", "growth.world.tryReproduce"));
    }
    if (!organism.isMature()) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "organism must be mature", "growth.world.tryReproduce"));
    }
    const SpeciesDef* species = SpeciesRegistry::find(organism.speciesId());
    if (!species) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "unknown species", "growth.world.tryReproduce"));
    }
    const ReproductionDef& def = species->reproduction;
    if (def.mode != ReproductionMode::SelfBud && def.mode != ReproductionMode::Fission &&
        def.mode != ReproductionMode::SporeBurst) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "species reproduction.mode is not a solo mode",
                                                       "growth.world.tryReproduce"));
    }
    organism.reproduction()->daysSinceReproduce = 0.0;
    emit(GrowthEventKind::Bred, organism, "forced:" + species->kindId);
    const int count = def.offspringCountMin + (def.offspringCountMax - def.offspringCountMin) / 2;
    return spawnOffspring(organism, *species, std::max(1, count));
}

}  // namespace eve::growth
