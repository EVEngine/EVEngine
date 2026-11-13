// Composition/script boundary only; no ECS View, callbacks under locks, or implicit simulation clocks.
#include "map/rpg_spawn/MapRPGSpawn.h"
#include <atomic>
#include <charconv>
#include <cmath>
#include <limits>
#include <optional>
#include "common/Capability.h"
#include "common/ECS.h"
#include "common/ServiceInterfaces.h"
#include "common/SnapshotHash.h"
#include "common/SquirrelBinding.h"
#include "definitions/Definitions.h"
#include "graphics/Graphics.h"
#include "map/Map.h"
#include "map/rpg_spawn/MapRPGPresentation.h"
#include "map/rpg_spawn/RPGMapRuntime.h"
#include "map/rpg_spawn/RPGMapSession.h"
#include "map/spawn/MapSpawnLoad.h"
#include "rpg/RPG.h"
#include "rpg/SimulationEventArchive.h"
#include "rpg/SimulationEvents.h"
#include "rpg/spawn/RPGUnitCatalogue.h"
#include "spawn/SpawnDiagnostics.h"
#include "spawn/definitions/SpawnTemplateSource.h"
namespace eve::map_rpg_spawn {
namespace {
template <class T>
Result<T> unavailable() {
    return Result<T>::failure(
        Diagnostic::error(DiagnosticCode::PreconditionViolation, "Map RPG spawn session is unavailable"));
}
ssq::Table project(HSQUIRRELVM vm, Result<Value> result) {
    return script::projectResult(vm, std::move(result), [](Value value) { return value; });
}
// Process-local delivery identities never repeat across script roots; no simulation state or persisted identity.
Result<std::int64_t> mintReceipt() {
    static std::atomic<std::int64_t> issued{0};
    auto                             current = issued.load(std::memory_order_relaxed);
    while (current != INT64_MAX) {
        if (issued.compare_exchange_weak(current, current + 1, std::memory_order_relaxed))
            return Result<std::int64_t>::success(current + 1);
    }
    return Result<std::int64_t>::failure(Diagnostic::error(DiagnosticCode::Unsupported, "Script receipts exhausted"));
}
std::string_view blockName(spawn::BlockReason reason) {
    switch (reason) {
        case spawn::BlockReason::None: return "none";
        case spawn::BlockReason::BudgetFull: return "budget_full";
        case spawn::BlockReason::PlacementBlocked: return "placement_blocked";
        case spawn::BlockReason::ProviderUnavailable: return "provider_unavailable";
        case spawn::BlockReason::FactoryFailed: return "factory_failed";
        case spawn::BlockReason::UnexpectedRemoval: return "unexpected_removal";
        case spawn::BlockReason::ArithmeticOverflow: return "arithmetic_overflow";
    }
    return "unknown";
}
}  // namespace
struct MapRPGSpawn::Impl {
    std::shared_ptr<ecs::Table>                      world    = std::make_shared<ecs::Table>();
    std::shared_ptr<definitions::DefinitionRegistry> registry = std::make_shared<definitions::DefinitionRegistry>();
    std::shared_ptr<rpg_spawn::RPGUnitCatalogue>     units    = std::make_shared<rpg_spawn::RPGUnitCatalogue>(registry);
    spawn::SpawnTemplateSource                       templates{*registry, *units};
    std::optional<PreparedRPGMapSession>             candidate;
    std::unique_ptr<RPGMapSession>                   session;
    rpg::detail::SimulationEvents                    events;
    std::unique_ptr<RPGMapRuntime>                   runtimeCandidate, runtime;
    std::optional<RPGActivation>                     activation;
    std::optional<RPGRuntimeCheckpoint>              checkpoint;
    std::optional<PreparedRPGRuntimeRestore>         restoration;
    std::optional<RPGEventDelivery>                  delivery;
    std::int64_t                                     receiptSequence = 0;
    mutable bool                                     busy            = false;
    struct Guard {
        bool& value;
        explicit Guard(bool& flag) : value(flag) { value = true; }
        ~Guard() { value = false; }
    };
    Result<void> prepareRuntime(const std::string& unitJson, const std::string& templateJson,
                                const std::string& bindingJson, const std::string& ownerText, std::int64_t seed,
                                double radius) {
        if (busy || candidate || session || runtimeCandidate || runtime || restoration) return unavailable<void>();
        Guard guard(busy);
        auto  owner = PersistentId::parse(ownerText);
        if (!owner || owner->isNil() || seed < 0)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid owner/seed"));
        auto unitsValue = Value::fromJson(unitJson);
        if (!unitsValue) return Result<void>::failure(unitsValue.status());
        auto templatesValue = Value::fromJson(templateJson);
        if (!templatesValue) return Result<void>::failure(templatesValue.status());
        auto binding = Value::fromJson(bindingJson);
        if (!binding) return Result<void>::failure(binding.status());
        const auto* unitArray     = unitsValue.value().getIf<Value::Array>();
        const auto* templateArray = templatesValue.value().getIf<Value::Array>();
        if (!unitArray || !templateArray)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Content arrays required"));
        auto* files = cap::query<service::IFileSystem>();
        if (!files)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "Project filesystem absent"));
        auto prepared = RPGMapRuntime::prepare(*unitArray, *templateArray, std::move(binding).takeValue(), *files,
                                               *owner, std::uint64_t(seed), radius);
        if (!prepared) return Result<void>::failure(prepared.status());
        runtimeCandidate = std::move(prepared).takeValue();
        return Result<void>::success();
    }
    Result<void> commitRuntime() {
        if (busy || !runtimeCandidate || runtime) return unavailable<void>();
        runtime = std::move(runtimeCandidate);
        return Result<void>::success();
    }
    Result<void> prepareRuntimeActivation(const std::string& playersJson, const std::string& eventsJson) {
        if (busy || !runtime) return unavailable<void>();
        Guard guard(busy);
        auto  playersValue = Value::fromJson(playersJson);
        if (!playersValue) return Result<void>::failure(playersValue.status());
        auto eventsValue = Value::fromJson(eventsJson);
        if (!eventsValue) return Result<void>::failure(eventsValue.status());
        auto invalid = [] {
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Expected null/point array and event string array"));
        };
        std::optional<std::vector<spawn::Point>> players;
        if (!playersValue.value().isNull()) {
            const auto* points = playersValue.value().getIf<Value::Array>();
            if (!points || points->size() > 1024) return invalid();
            players.emplace();
            players->reserve(points->size());
            for (const auto& point : *points) {
                const auto* pair = point.getIf<Value::Array>();
                if (!pair || pair->size() != 2) return invalid();
                double coordinates[2];
                for (std::size_t i = 0; i < 2; ++i) {
                    if (const auto* real = (*pair)[i].getIf<double>())
                        coordinates[i] = *real;
                    else if (const auto* integer = (*pair)[i].getIf<std::int64_t>()) {
                        coordinates[i] = static_cast<double>(*integer);
                        if (coordinates[i] >= 0x1p63 || coordinates[i] < -0x1p63 ||
                            static_cast<std::int64_t>(coordinates[i]) != *integer)
                            return invalid();
                    } else
                        return invalid();
                    if (!std::isfinite(coordinates[i])) return invalid();
                }
                players->push_back({coordinates[0], coordinates[1]});
            }
        }
        const auto* names = eventsValue.value().getIf<Value::Array>();
        if (!names || names->size() > 1024) return invalid();
        std::vector<std::string> eventsInput;
        eventsInput.reserve(names->size());
        for (const auto& name : *names) {
            const auto* text = name.getIf<std::string>();
            if (!text) return invalid();
            eventsInput.push_back(*text);
        }
        auto prepared = runtime->prepareActivation(std::move(players), std::move(eventsInput));
        if (!prepared) return Result<void>::failure(prepared.status());
        activation.emplace(std::move(prepared).takeValue());
        return Result<void>::success();
    }
    Result<Value> advanceRuntime(std::int64_t tick, std::int64_t nanoseconds) {
        if (busy || !runtime) return unavailable<Value>();
        if (tick <= 0 || nanoseconds <= 0)
            return Result<Value>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Positive tick/delta required"));
        Guard                guard(busy);
        const SimulationStep step{SimulationTick(std::uint64_t(tick)), Duration(nanoseconds)};
        auto                 advanced = activation ? runtime->advance(step, *activation) : runtime->advance(step);
        if (!advanced) return Result<Value>::failure(advanced.status());
        activation.reset();
        auto report = std::move(advanced).takeValue();
        return Result<Value>::success(Value::object({{"units", std::int64_t(report.units)},
                                                     {"batches", std::int64_t(report.batches)},
                                                     {"diagnostic_count", std::int64_t(report.diagnostics.size())}}),
                                      Status(StatusCode::Applied, std::move(report.diagnostics)));
    }
    Result<Value> captureRuntimeState() const {
        if (busy || !runtime) return unavailable<Value>();
        return runtime->captureState();
    }
    Result<void> freezeRuntime(const std::string& epochText, std::int64_t revision) {
        if (busy || !runtime) return unavailable<void>();
        auto epoch = PersistentId::parse(epochText);
        if (!epoch || epoch->isNil() || revision <= 0)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Non-nil epoch/positive revision required"));
        Guard guard(busy);
        auto  captured = runtime->captureCheckpoint(*epoch, Revision(std::uint64_t(revision)));
        if (!captured) return Result<void>::failure(captured.status());
        checkpoint.emplace(std::move(captured).takeValue());
        return Result<void>::success();
    }
    Result<std::string> sealRuntime() {
        if (busy || !checkpoint) return unavailable<std::string>();
        Guard guard(busy);
        auto  sealed = checkpoint->seal(snapshotContentHashProvider());
        if (!sealed) return Result<std::string>::failure(sealed.status());
        return serializeSnapshotEnvelope(sealed.value());
    }
    Result<void> prepareRuntimeRestore(const std::string& file, const std::string& epochText) {
        if (busy || candidate || session || runtimeCandidate) return unavailable<void>();
        auto epoch = PersistentId::parse(epochText);
        if (!epoch || epoch->isNil())
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Non-nil epoch required"));
        Guard guard(busy);
        auto  hash   = snapshotContentHashProvider();
        auto  parsed = parseSnapshotEnvelope(file, hash);
        if (!parsed) return Result<void>::failure(parsed.status());
        auto prepared = PreparedRPGRuntimeRestore::prepare(std::move(parsed).takeValue(), *epoch, hash);
        if (!prepared) return Result<void>::failure(prepared.status());
        restoration.emplace(std::move(prepared).takeValue());
        return Result<void>::success();
    }
    Result<void> commitRuntimeRestore() {
        if (busy || !restoration || candidate || session || runtimeCandidate) return unavailable<void>();
        Guard guard(busy);
        auto  committed = restoration->commit(runtime);
        if (!committed) return Result<void>::failure(committed.status());
        auto previous = std::move(committed).takeValue();
        activation.reset();
        delivery.reset();
        restoration.reset();
        return Result<void>::success();
    }
    Result<Value> readRuntimeEvents(const std::string& epochText, std::int64_t revision) {
        if (busy || !runtime) return unavailable<Value>();
        auto epoch = PersistentId::parse(epochText);
        if (!epoch || epoch->isNil() || revision <= 0)
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid epoch/revision"));
        Guard guard(busy);
        auto  state = runtime->captureState();
        if (!state) return Result<Value>::failure(state.status());
        const auto    text   = state.value().find("completed")->asString();
        std::uint64_t tick   = 0;
        const auto    parsed = std::from_chars(text.data(), text.data() + text.size(), tick);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
            return Result<Value>::failure(
                Diagnostic::error(DiagnosticCode::InvariantViolation, "Invalid runtime tick"));
        auto observed = runtime->readEvents();
        if (!observed) return Result<Value>::failure(observed.status());
        auto archive = rpg::SimulationEventArchive::capture(observed.value().events(), *epoch,
                                                            Revision(std::uint64_t(revision)), SimulationTick(tick));
        if (!archive) return Result<Value>::failure(archive.status());
        auto sealed = archive.value().seal(snapshotContentHashProvider());
        if (!sealed) return Result<Value>::failure(sealed.status());
        auto file = serializeSnapshotEnvelope(sealed.value());
        if (!file) return Result<Value>::failure(file.status());
        auto issued = mintReceipt();
        if (!issued) return Result<Value>::failure(issued.status());
        auto output = Value::object({{"receipt", issued.value()},
                                     {"snapshot", std::move(file).takeValue()},
                                     {"hash_algorithm", std::string(activeSnapshotHashAlgorithm())}});
        delivery.emplace(std::move(observed).takeValue());
        receiptSequence = issued.value();
        return Result<Value>::success(std::move(output));
    }
    Result<void> acknowledgeRuntimeEvents(std::int64_t receipt) {
        if (busy || !runtime || !delivery || receipt != receiptSequence) return unavailable<void>();
        Guard guard(busy);
        auto  accepted = runtime->acknowledge(*delivery);
        if (!accepted) return accepted;
        delivery.reset();
        return Result<void>::success();
    }
    Result<void> publish(const std::string& json, bool unit) {
        if (busy || runtime || runtimeCandidate || restoration) return unavailable<void>();
        Guard guard(busy);
        auto  value = Value::fromJson(json);
        if (!value) return Result<void>::failure(value.status());
        return unit ? units->publish(value.value()) : templates.publish(value.value());
    }
    Result<void> prepare(map::Map* target, const std::string& bindingJson, const std::string& ownerText,
                         std::int64_t seed, const std::string& contract, double radius) {
        if (busy || runtime || runtimeCandidate || restoration || !target || candidate || session)
            return unavailable<void>();
        Guard guard(busy);
        auto  owner = PersistentId::parse(ownerText);
        if (!owner || seed < 0)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid owner or seed"));
        auto binding = Value::fromJson(bindingJson);
        if (!binding) return Result<void>::failure(binding.status());
        auto* files = cap::query<service::IFileSystem>();
        if (!files)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "Project file service absent"));
        auto prepared = PreparedRPGMapSession::prepare(*target, binding.value(), *files, templates, world, units,
                                                       *owner, static_cast<std::uint64_t>(seed), contract, radius, {});
        if (!prepared) return Result<void>::failure(prepared.status());
        candidate.emplace(std::move(prepared).value());
        return Result<void>::success();
    }
    Result<void> commit(map::Map* target) {
        if (busy || runtime || runtimeCandidate || restoration || !target || !candidate || session)
            return unavailable<void>();
        Guard guard(busy);
        auto  committed = candidate->commit(*target);
        if (!committed) return Result<void>::failure(committed.status());
        session = std::move(committed).value();
        candidate.reset();
        return Result<void>::success();
    }
    Result<Value> advance(map::Map* target, rpg::RPG* domain, std::int64_t tick, std::int64_t nanoseconds) {
        if (busy || !target || !domain || !session) return unavailable<Value>();
        Guard guard(busy);
        if (tick <= 0 || nanoseconds < 0)
            return Result<Value>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid simulation frame"));
        std::vector<rpg::VitalsEvent> frame(domain->vitalsEvents().begin(), domain->vitalsEvents().end());
        const auto                    retained = events.vitals.size();
        frame.insert(frame.end(), events.vitals.begin(), events.vitals.end());
        ecs::ScopedTable                  tableScope(*world);
        rpg::detail::SimulationEventScope eventScope(events);
        auto                              result = session->advance(
            *target, {SimulationTick(static_cast<std::uint64_t>(tick)), Duration(nanoseconds)}, frame, {});
        if (result) events.vitals.erase(events.vitals.begin(), events.vitals.begin() + retained);
        if (!result) return Result<Value>::failure(result.status());
        auto report = std::move(result).value();
        auto value  = Value::object({{"units", static_cast<std::int64_t>(report.units)},
                                     {"batches", static_cast<std::int64_t>(report.batches)},
                                     {"diagnostic_count", static_cast<std::int64_t>(report.diagnostics.size())}});
        return Result<Value>::success(std::move(value), Status(StatusCode::Applied, std::move(report.diagnostics)));
    }
    Result<double> damage(const std::string& subject, double amount) {
        if (busy || (!session && !runtime)) return unavailable<double>();
        Guard      guard(busy);
        const auto id = PersistentId::parse(subject);
        if (!id || id->isNil())
            return Result<double>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Non-nil subject identity required"));
        if (runtime) return runtime->applyDamage(SubjectRef::fromPersistentId(*id), amount);
        rpg::detail::SimulationEventScope eventScope(events);
        auto                              result = session->applyDamage(SubjectRef::fromPersistentId(*id), amount);
        return result;
    }
    Result<Value> captureDiagnostics() const {
        if (busy || !session) return unavailable<Value>();
        Guard guard(busy);
        auto  facts = session->spawning().world().captureDiagnostics();
        if (!facts) return Result<Value>::failure(facts.status());
        Value::Array sources;
        for (const auto& row : facts.value().sources) {
            if (row.state.total > std::uint64_t(std::numeric_limits<std::int64_t>::max()))
                return Result<Value>::failure(
                    Diagnostic::error(DiagnosticCode::Unsupported, "Counter exceeds script integer range"));
            Value::Array subjects;
            for (const auto subject : row.state.subjects) subjects.emplace_back(subject.format());
            const char* phase = row.state.phase == spawn::Phase::Dormant     ? "dormant"
                                : row.state.phase == spawn::Phase::Running   ? "running"
                                : row.state.phase == spawn::Phase::Completed ? "completed"
                                                                             : "faulted";
            sources.push_back(Value::object({{"id", row.state.id},
                                             {"blocked", std::string(blockName(row.state.blocked))},
                                             {"phase", phase},
                                             {"alive", std::int64_t(row.state.alive)},
                                             {"total", std::int64_t(row.state.total)},
                                             {"wave", std::int64_t(row.state.wave)},
                                             {"remaining_ns", row.state.remaining.nanoseconds()},
                                             {"subjects", Value(std::move(subjects))}}));
        }
        return Result<Value>::success(Value::object(
            {{"tick", std::int64_t(facts.value().tick.value())}, {"sources", Value(std::move(sources))}}));
    }
    Result<Value> draw(graphics::Graphics* graphics, float width, float height, float red, float green, float blue,
                       float alpha) {
        if (busy || !graphics || (!session && !runtime)) return unavailable<Value>();
        Guard guard(busy);
        auto  result = runtime ? runtime->draw(*graphics, {width, height, red, green, blue, alpha})
                               : session->draw(*graphics, {width, height, red, green, blue, alpha});
        if (!result) return Result<Value>::failure(result.status());
        return Result<Value>::success(Value(static_cast<std::int64_t>(result.value())));
    }
    Result<void> close(const std::string& policy) {
        if (busy) return unavailable<void>();
        Guard guard(busy);
        if (policy != "remove" && policy != "keep")
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Unknown close policy"));
        if (runtime && policy == "keep")
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Unsupported, "Owned runtime requires remove"));
        if (runtime) {
            auto closed = runtime->close();
            if (!closed) return closed;
            runtime.reset();
        }
        runtimeCandidate.reset();
        activation.reset();
        restoration.reset();
        delivery.reset();
        if (session) {
            rpg::detail::SimulationEventScope eventScope(events);
            auto                              result =
                session->close(policy == "remove" ? spawn::DetachPolicy::RemoveUnits : spawn::DetachPolicy::KeepUnits);
            if (!result) return result;
            session.reset();
        }
        candidate.reset();
        events.vitals.clear();
        return Result<void>::success();
    }
};
MapRPGSpawn::MapRPGSpawn() : impl_(std::make_unique<Impl>()) {}
MapRPGSpawn::~MapRPGSpawn() = default;
Module_IMPL(MapRPGSpawn, new MapRPGSpawn());
void MapRPGSpawn::expose(ssq::Table& table) {
    auto cls = table.addClass(name, MapRPGSpawn::create, false);
    expose(cls);
}
void MapRPGSpawn::expose(ssq::Class& cls) {
    const auto vm = cls.getHandle();
    cls.addFunc("prepareRuntime",
                [vm](MapRPGSpawn* self, const std::string& units, const std::string& templates,
                     const std::string& binding, const std::string& owner, std::int64_t seed, float radius) {
                    return script::projectResult(
                        vm, self ? self->impl_->prepareRuntime(units, templates, binding, owner, seed, radius)
                                 : unavailable<void>());
                });
    cls.addFunc("commitRuntime", [vm](MapRPGSpawn* self) {
        return script::projectResult(vm, self ? self->impl_->commitRuntime() : unavailable<void>());
    });
    cls.addFunc("prepareRuntimeActivation",
                [vm](MapRPGSpawn* self, const std::string& players, const std::string& events) {
                    return script::projectResult(
                        vm, self ? self->impl_->prepareRuntimeActivation(players, events) : unavailable<void>());
                });
    cls.addFunc("advanceRuntime", [vm](MapRPGSpawn* self, std::int64_t tick, std::int64_t nanoseconds) {
        return project(vm, self ? self->impl_->advanceRuntime(tick, nanoseconds) : unavailable<Value>());
    });
    cls.addFunc("captureRuntimeState", [vm](MapRPGSpawn* self) {
        return project(vm, self ? self->impl_->captureRuntimeState() : unavailable<Value>());
    });
    cls.addFunc("freezeRuntime", [vm](MapRPGSpawn* self, const std::string& epoch, std::int64_t revision) {
        return script::projectResult(vm, self ? self->impl_->freezeRuntime(epoch, revision) : unavailable<void>());
    });
    cls.addFunc("sealRuntime", [vm](MapRPGSpawn* self) {
        return script::projectResult(vm, self ? self->impl_->sealRuntime() : unavailable<std::string>(),
                                     [](std::string value) { return value; });
    });
    cls.addFunc("prepareRuntimeRestore", [vm](MapRPGSpawn* self, const std::string& file, const std::string& epoch) {
        return script::projectResult(vm, self ? self->impl_->prepareRuntimeRestore(file, epoch) : unavailable<void>());
    });
    cls.addFunc("commitRuntimeRestore", [vm](MapRPGSpawn* self) {
        return script::projectResult(vm, self ? self->impl_->commitRuntimeRestore() : unavailable<void>());
    });
    cls.addFunc("readRuntimeEvents", [vm](MapRPGSpawn* self, const std::string& epoch, std::int64_t revision) {
        return project(vm, self ? self->impl_->readRuntimeEvents(epoch, revision) : unavailable<Value>());
    });
    cls.addFunc("acknowledgeRuntimeEvents", [vm](MapRPGSpawn* self, std::int64_t receipt) {
        return script::projectResult(vm, self ? self->impl_->acknowledgeRuntimeEvents(receipt) : unavailable<void>());
    });
    cls.addFunc("snapshotHashAlgorithm", [](MapRPGSpawn*) { return std::string(activeSnapshotHashAlgorithm()); });
    cls.addFunc("publishUnit", [vm](MapRPGSpawn* self, const std::string& json) {
        return script::projectResult(vm, self ? self->impl_->publish(json, true) : unavailable<void>());
    });
    cls.addFunc("publishTemplate", [vm](MapRPGSpawn* self, const std::string& json) {
        return script::projectResult(vm, self ? self->impl_->publish(json, false) : unavailable<void>());
    });
    cls.addFunc("prepare", [vm](MapRPGSpawn* self, map::Map* target, const std::string& json, const std::string& owner,
                                std::int64_t seed, const std::string& contract, float radius) {
        return script::projectResult(
            vm, self ? self->impl_->prepare(target, json, owner, seed, contract, radius) : unavailable<void>());
    });
    cls.addFunc("commit", [vm](MapRPGSpawn* self, map::Map* target) {
        return script::projectResult(vm, self ? self->impl_->commit(target) : unavailable<void>());
    });
    cls.addFunc("advance", [vm](MapRPGSpawn* self, map::Map* target, rpg::RPG* domain, std::int64_t tick,
                                std::int64_t nanoseconds) {
        return project(vm, self ? self->impl_->advance(target, domain, tick, nanoseconds) : unavailable<Value>());
    });
    cls.addFunc("damage", [vm](MapRPGSpawn* self, const std::string& subject, float amount) {
        return script::projectResult(vm, self ? self->impl_->damage(subject, amount) : unavailable<double>(),
                                     [](double value) { return value; });
    });
    cls.addFunc("captureDiagnostics", [vm](MapRPGSpawn* self) {
        return project(vm, self ? self->impl_->captureDiagnostics() : unavailable<Value>());
    });
    cls.addFunc("draw", [vm](MapRPGSpawn* self, graphics::Graphics* graphics, float width, float height, float red,
                             float green, float blue, float alpha) {
        return project(
            vm, self ? self->impl_->draw(graphics, width, height, red, green, blue, alpha) : unavailable<Value>());
    });
    cls.addFunc("close", [vm](MapRPGSpawn* self, const std::string& policy) {
        return script::projectResult(vm, self ? self->impl_->close(policy) : unavailable<void>());
    });
}
}  // namespace eve::map_rpg_spawn
