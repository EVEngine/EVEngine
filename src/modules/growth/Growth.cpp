#include "growth/Growth.h"

#include "common/Diagnostic.h"
#include "common/SquirrelBinding.h"
#include "common/Time.h"
#include "common/Value.h"

#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::growth {

Module_IMPL(Growth, new Growth());

Result<int> Growth::registerSpeciesFromJson(const std::string& json) { return SpeciesRegistry::loadFromJson(json); }

void Growth::clearSpecies() { SpeciesRegistry::clear(); }

bool Growth::hasSpecies(const std::string& id) const { return SpeciesRegistry::has(id); }

int Growth::speciesCount() const { return SpeciesRegistry::count(); }

Result<void> Growth::setDaysPerSeason(int days) { return GrowthWorld::instance().calendar().setDaysPerSeason(days); }

Result<void> Growth::setSecondsPerSimDay(double seconds) {
    return GrowthWorld::instance().setSecondsPerSimDay(seconds);
}

void Growth::setRngSeed(std::uint64_t seed) { GrowthWorld::instance().setRngSeed(seed); }

Result<void> Growth::advanceDays(double days) { return GrowthWorld::instance().advanceDays(days); }

Result<void> Growth::stepSeconds(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "seconds must be finite and >= 0", "growth.stepSeconds"));
    }
    auto delta = Duration::fromSeconds(seconds);
    if (!delta) return Result<void>::failure(delta.status());
    SimulationStep step;
    step.delta = std::move(delta).takeValue();
    step.tick  = SimulationTick::zero();
    return GrowthWorld::instance().step(step);
}

std::int64_t Growth::dayIndex() const { return GrowthWorld::instance().calendar().dayIndex(); }

std::int64_t Growth::year() const { return GrowthWorld::instance().calendar().year(); }

std::string Growth::seasonName() const {
    return GrowthCalendar::seasonName(GrowthWorld::instance().calendar().season());
}

Result<Organism*> Growth::plant(const std::string& speciesId) {
    auto organism = Organism::createOrganism(speciesId);
    if (!organism) return organism;
    Organism* created = organism.value();
    if (created->kind() != OrganismKind::Plant) {
        created->release();
        return Result<Organism*>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "species is not a plant", "growth.plant"));
    }
    return organism;
}

Result<Organism*> Growth::spawnAnimal(const std::string& speciesId) {
    auto organism = Organism::createOrganism(speciesId);
    if (!organism) return organism;
    Organism* created = organism.value();
    if (created->kind() != OrganismKind::Animal) {
        created->release();
        return Result<Organism*>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "species is not an animal", "growth.spawnAnimal"));
    }
    return organism;
}

Result<void> Growth::tryBreed(Organism* carrier, Organism* partner) {
    if (!carrier || !partner) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "breed partners must not be null", "growth.tryBreed"));
    }
    return GrowthWorld::instance().tryBreed(*carrier, *partner);
}

void Growth::clearEvents() { GrowthWorld::instance().events().clear(); }

int Growth::eventCount() const { return GrowthWorld::instance().events().count(); }

std::string Growth::eventKind(int index) const {
    const GrowthEvent* event = GrowthWorld::instance().events().at(index);
    if (!event) return {};
    return GrowthEventLog::kindName(event->kind);
}

std::uint64_t Growth::eventOrganismId(int index) const {
    const GrowthEvent* event = GrowthWorld::instance().events().at(index);
    return event ? event->organismId : 0;
}

std::string Growth::eventSpeciesId(int index) const {
    const GrowthEvent* event = GrowthWorld::instance().events().at(index);
    return event ? event->speciesId : std::string{};
}

std::string Growth::eventDetail(int index) const {
    const GrowthEvent* event = GrowthWorld::instance().events().at(index);
    return event ? event->detail : std::string{};
}

void Growth::resetWorld() {
    SpeciesRegistry::clear();
    GrowthWorld::resetInstance();
}

void Growth::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Growth::create, false);
    expose(cls);

    auto organism =
        table.addClass<Organism>("Organism", std::function<Organism*()>([]() -> Organism* { return nullptr; }), false);
    organism.addFunc("destroy", &Organism::destroy);
    organism.addFunc("speciesId", &Organism::speciesId);
    organism.addFunc("stageIndex", &Organism::stageIndex);
    organism.addFunc("stageId", &Organism::stageId);
    // SSQ has no double marshalling — project meters/progress as float (same pattern as RPG xp).
    organism.addFunc("stageProgressDays", [](Organism* self) -> float {
        return self ? static_cast<float>(self->stageProgressDays()) : 0.f;
    });
    organism.addFunc("isMature", &Organism::isMature);
    organism.addFunc("isDead", &Organism::isDead);
    organism.addFunc("isHarvested", &Organism::isHarvested);
    organism.addFunc("isDiseased", &Organism::isDiseased);
    organism.addFunc("isPregnant", &Organism::isPregnant);
    organism.addFunc("diseaseId", &Organism::diseaseId);
    organism.addFunc("waterPermille", [](Organism* self) -> float {
        return self ? static_cast<float>(self->waterPermille()) : 0.f;
    });
    organism.addFunc("foodPermille", [](Organism* self) -> float {
        return self ? static_cast<float>(self->foodPermille()) : 0.f;
    });
    organism.addFunc("fertilityPermille", [](Organism* self) -> float {
        return self ? static_cast<float>(self->fertilityPermille()) : 0.f;
    });
    organism.addFunc("kindName", [](Organism* self) {
        if (!self) return std::string{};
        return self->kind() == OrganismKind::Animal ? std::string("animal") : std::string("plant");
    });
    organism.addFunc("waterBy", [vm = organism.getHandle()](Organism* self, float amount) {
        if (!self) {
            return eve::script::projectResult(
                vm, Result<void>::failure(
                        Diagnostic::error(DiagnosticCode::InvalidArgument, "organism is null", "growth.organism")));
        }
        return eve::script::projectResult(vm, self->waterBy(static_cast<double>(amount)));
    });
    organism.addFunc("feed", [vm = organism.getHandle()](Organism* self, float amount) {
        if (!self) {
            return eve::script::projectResult(
                vm, Result<void>::failure(
                        Diagnostic::error(DiagnosticCode::InvalidArgument, "organism is null", "growth.organism")));
        }
        return eve::script::projectResult(vm, self->feed(static_cast<double>(amount)));
    });
    organism.addFunc("fertilize", [vm = organism.getHandle()](Organism* self, float amount) {
        if (!self) {
            return eve::script::projectResult(
                vm, Result<void>::failure(
                        Diagnostic::error(DiagnosticCode::InvalidArgument, "organism is null", "growth.organism")));
        }
        return eve::script::projectResult(vm, self->fertilize(static_cast<double>(amount)));
    });
    organism.addFunc("harvest", [vm = organism.getHandle()](Organism* self) {
        if (!self) {
            return eve::script::projectResult(
                vm,
                Result<std::vector<YieldReceipt>>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "organism is null", "growth.organism")),
                [](std::vector<YieldReceipt>) { return eve::Value(); });
        }
        return eve::script::projectResult(vm, self->harvest(), [](std::vector<YieldReceipt> yields) {
            eve::Value::Array rows;
            rows.reserve(yields.size());
            for (YieldReceipt& line : yields) {
                eve::Value::Object row;
                row.emplace("itemId", eve::Value(std::move(line.itemId)));
                row.emplace("count", eve::Value(static_cast<std::int64_t>(line.count)));
                rows.emplace_back(eve::Value(std::move(row)));
            }
            return eve::Value(std::move(rows));
        });
    });
}

void Growth::expose(ssq::Class& cls) {
    cls.addFunc("registerSpeciesFromJson", [vm = cls.getHandle()](Growth* self, const std::string& json) {
        auto projector = [](int count) { return eve::Value(static_cast<std::int64_t>(count)); };
        if (!self) {
            return eve::script::projectResult(vm,
                                              Result<int>::failure(Diagnostic::error(
                                                  DiagnosticCode::InvalidArgument, "growth module is null", "growth")),
                                              projector);
        }
        return eve::script::projectResult(vm, self->registerSpeciesFromJson(json), projector);
    });
    cls.addFunc("clearSpecies", &Growth::clearSpecies);
    cls.addFunc("hasSpecies", &Growth::hasSpecies);
    cls.addFunc("speciesCount", &Growth::speciesCount);
    cls.addFunc("setDaysPerSeason", [vm = cls.getHandle()](Growth* self, int days) {
        if (!self) {
            return eve::script::projectResult(
                vm, Result<void>::failure(
                        Diagnostic::error(DiagnosticCode::InvalidArgument, "growth module is null", "growth")));
        }
        return eve::script::projectResult(vm, self->setDaysPerSeason(days));
    });
    cls.addFunc("setSecondsPerSimDay", [vm = cls.getHandle()](Growth* self, float seconds) {
        if (!self) {
            return eve::script::projectResult(
                vm, Result<void>::failure(
                        Diagnostic::error(DiagnosticCode::InvalidArgument, "growth module is null", "growth")));
        }
        return eve::script::projectResult(vm, self->setSecondsPerSimDay(static_cast<double>(seconds)));
    });
    cls.addFunc("setRngSeed", [](Growth* self, int seed) {
        if (self) self->setRngSeed(static_cast<std::uint64_t>(seed));
    });
    cls.addFunc("advanceDays", [vm = cls.getHandle()](Growth* self, float days) {
        if (!self) {
            return eve::script::projectResult(
                vm, Result<void>::failure(
                        Diagnostic::error(DiagnosticCode::InvalidArgument, "growth module is null", "growth")));
        }
        return eve::script::projectResult(vm, self->advanceDays(static_cast<double>(days)));
    });
    cls.addFunc("stepSeconds", [vm = cls.getHandle()](Growth* self, float seconds) {
        if (!self) {
            return eve::script::projectResult(
                vm, Result<void>::failure(
                        Diagnostic::error(DiagnosticCode::InvalidArgument, "growth module is null", "growth")));
        }
        return eve::script::projectResult(vm, self->stepSeconds(static_cast<double>(seconds)));
    });
    cls.addFunc("dayIndex", &Growth::dayIndex);
    cls.addFunc("year", &Growth::year);
    cls.addFunc("seasonName", &Growth::seasonName);
    // ECS organism pointers match RPGActor: return borrowed instances; script calls destroy().
    cls.addFunc("plant", [](Growth* self, const std::string& speciesId) -> Organism* {
        if (!self) return nullptr;
        auto created = self->plant(speciesId);
        if (!created) return nullptr;
        return created.value();
    });
    cls.addFunc("spawnAnimal", [](Growth* self, const std::string& speciesId) -> Organism* {
        if (!self) return nullptr;
        auto created = self->spawnAnimal(speciesId);
        if (!created) return nullptr;
        return created.value();
    });
    cls.addFunc("tryBreed", [vm = cls.getHandle()](Growth* self, Organism* carrier, Organism* partner) {
        if (!self) {
            return eve::script::projectResult(
                vm, Result<void>::failure(
                        Diagnostic::error(DiagnosticCode::InvalidArgument, "growth module is null", "growth")));
        }
        return eve::script::projectResult(vm, self->tryBreed(carrier, partner));
    });
    cls.addFunc("clearEvents", &Growth::clearEvents);
    cls.addFunc("eventCount", &Growth::eventCount);
    cls.addFunc("eventKind", &Growth::eventKind);
    cls.addFunc("eventOrganismId", [](Growth* self, int index) -> int64_t {
        return self ? static_cast<int64_t>(self->eventOrganismId(index)) : int64_t{0};
    });
    cls.addFunc("eventSpeciesId", &Growth::eventSpeciesId);
    cls.addFunc("eventDetail", &Growth::eventDetail);
    cls.addFunc("resetWorld", &Growth::resetWorld);
}

}  // namespace eve::growth
