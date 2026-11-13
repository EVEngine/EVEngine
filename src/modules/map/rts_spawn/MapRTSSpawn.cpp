// Script boundary: preparation-only capability lookup; actual simulation services are owned and typed.
#include "map/rts_spawn/MapRTSSpawn.h"
#include <optional>
#include "common/Capability.h"
#include "common/ServiceInterfaces.h"
#include "common/SnapshotHash.h"
#include "common/SquirrelBinding.h"
#include "map/rts_spawn/RTSMapRuntime.h"
#include "map/rts_spawn/RTSRuntimeCheckpoint.h"
#include "map/rts_spawn/RTSRuntimeRestore.h"
#include "rts/RTSTypes.h"
namespace eve::map_rts_spawn {
namespace {
template <class T>
Result<T> unavailable() {
    return Result<T>::failure(
        Diagnostic::error(DiagnosticCode::PreconditionViolation, "RTS map runtime is unavailable or busy"));
}
struct Guard {
    bool& busy;
    explicit Guard(bool& value) : busy(value) { busy = true; }
    ~Guard() { busy = false; }
};
}  // namespace
struct MapRTSSpawn::Impl {
    std::unique_ptr<RTSMapRuntime>              candidate, runtime;
    std::shared_ptr<const RTSRuntimeCheckpoint> checkpoint;
    std::optional<PreparedRTSRuntimeRestore>    restoration;
    bool                                        busy = false;
    Result<void> prepare(const std::string& content, const std::string& templateJson, const std::string& bindingJson,
                         const std::string& ownerText, const std::string& factionText, std::int64_t seed, float targetX,
                         float targetY) {
        if (busy || candidate || runtime || restoration) return unavailable<void>();
        Guard guard(busy);
        auto  owner = PersistentId::parse(ownerText), faction = PersistentId::parse(factionText);
        if (!owner || owner->isNil() || !faction || faction->isNil() || seed < 0)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid owner/faction/seed"));
        auto binding = Value::fromJson(bindingJson);
        if (!binding) return Result<void>::failure(binding.status());
        auto templates = Value::fromJson(templateJson);
        if (!templates) return Result<void>::failure(templates.status());
        if (!templates.value().isArray())
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Templates must be an array"));
        std::vector<Value> values;
        values.reserve(templates.value().arraySize());
        for (std::size_t i = 0; i < templates.value().arraySize(); ++i) values.push_back(templates.value().at(i));
        auto command    = std::make_shared<rts::CommandSpec>();
        command->kind   = rts::OrderKind::AttackMove;
        command->target = {targetX, targetY};
        auto valid      = command->validate();
        if (!valid) return valid;
        auto* files = cap::query<service::IFileSystem>();
        if (!files)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "Project filesystem absent"));
        auto prepared =
            RTSMapRuntime::prepare(content, std::move(values), std::move(binding).takeValue(), *files,
                                   SubjectRef::fromPersistentId(*faction), *owner, std::uint64_t(seed), command);
        if (!prepared) return Result<void>::failure(prepared.status());
        candidate = std::move(prepared).takeValue();
        return Result<void>::success();
    }
    Result<void> commit() {
        if (busy || !candidate || runtime) return unavailable<void>();
        runtime = std::move(candidate);
        return Result<void>::success();
    }
    Result<Value> advance(std::int64_t tick, std::int64_t nanoseconds) {
        if (busy || !runtime) return unavailable<Value>();
        if (tick <= 0 || nanoseconds <= 0)
            return Result<Value>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Positive tick/delta required"));
        Guard guard(busy);
        auto  advanced = runtime->advance({SimulationTick(std::uint64_t(tick)), Duration(nanoseconds)});
        if (!advanced) return Result<Value>::failure(advanced.status());
        auto report = std::move(advanced).takeValue();
        return Result<Value>::success(Value::object({{"units", std::int64_t(report.units)},
                                                     {"batches", std::int64_t(report.batches)},
                                                     {"diagnostic_count", std::int64_t(report.diagnostics.size())}}),
                                      Status(StatusCode::Applied, std::move(report.diagnostics)));
    }
    Result<Value> captureState() const {
        if (busy || !runtime) return unavailable<Value>();
        return runtime->captureState();
    }
    Result<void> freezeRuntime(const std::string& epochText, std::int64_t revision) {
        if (busy || !runtime) return unavailable<void>();
        auto epoch = PersistentId::parse(epochText);
        if (!epoch || epoch->isNil() || revision <= 0)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Non-nil epoch and positive revision required"));
        Guard guard(busy);
        auto  frozen = runtime->captureCheckpoint(*epoch, Revision(std::uint64_t(revision)));
        if (!frozen) return Result<void>::failure(frozen.status());
        checkpoint = std::make_shared<const RTSRuntimeCheckpoint>(std::move(frozen).takeValue());
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
        if (busy || candidate) return unavailable<void>();
        auto epoch = PersistentId::parse(epochText);
        if (!epoch || epoch->isNil())
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Non-nil epoch required"));
        Guard guard(busy);
        auto  hash     = snapshotContentHashProvider();
        auto  envelope = parseSnapshotEnvelope(file, hash);
        if (!envelope) return Result<void>::failure(envelope.status());
        auto facts = RTSRuntimeCheckpoint::parse(std::move(envelope).takeValue(), *epoch, hash);
        if (!facts) return Result<void>::failure(facts.status());
        auto prepared = PreparedRTSRuntimeRestore::prepare(
            std::make_shared<const RTSRuntimeCheckpoint>(std::move(facts).takeValue()), hash);
        if (!prepared) return Result<void>::failure(prepared.status());
        restoration.emplace(std::move(prepared).takeValue());
        return Result<void>::success();
    }
    Result<void> commitRuntimeRestore() {
        if (busy || candidate || !restoration) return unavailable<void>();
        Guard guard(busy);
        auto  committed = restoration->commit(runtime);
        if (!committed) return committed;
        restoration.reset();
        return Result<void>::success();
    }
    Result<void> close() {
        if (busy) return unavailable<void>();
        Guard guard(busy);
        if (runtime) {
            auto closed = runtime->close();
            if (!closed) return closed;
            runtime.reset();
        }
        candidate.reset();
        restoration.reset();
        return Result<void>::success();
    }
};
MapRTSSpawn::MapRTSSpawn() : impl_(std::make_unique<Impl>()) {}
MapRTSSpawn::~MapRTSSpawn() = default;
Module_IMPL(MapRTSSpawn, new MapRTSSpawn());
void MapRTSSpawn::expose(ssq::Table& table) {
    auto cls = table.addClass(name, MapRTSSpawn::create, false);
    expose(cls);
}
void MapRTSSpawn::expose(ssq::Class& cls) {
    const auto vm = cls.getHandle();
    cls.addFunc("freezeRuntime", [vm](MapRTSSpawn* self, const std::string& epoch, std::int64_t revision) {
        return script::projectResult(vm, self ? self->impl_->freezeRuntime(epoch, revision) : unavailable<void>());
    });
    cls.addFunc("sealRuntime", [vm](MapRTSSpawn* self) {
        return script::projectResult(vm, self ? self->impl_->sealRuntime() : unavailable<std::string>(),
                                     [](std::string value) { return value; });
    });
    cls.addFunc("prepareRuntimeRestore", [vm](MapRTSSpawn* self, const std::string& file, const std::string& epoch) {
        return script::projectResult(vm, self ? self->impl_->prepareRuntimeRestore(file, epoch) : unavailable<void>());
    });
    cls.addFunc("commitRuntimeRestore", [vm](MapRTSSpawn* self) {
        return script::projectResult(vm, self ? self->impl_->commitRuntimeRestore() : unavailable<void>());
    });
    cls.addFunc("snapshotHashAlgorithm", [](MapRTSSpawn*) { return std::string(activeSnapshotHashAlgorithm()); });
    cls.addFunc("prepare", [vm](MapRTSSpawn* self, const std::string& content, const std::string& templates,
                                const std::string& binding, const std::string& owner, const std::string& faction,
                                std::int64_t seed, float targetX, float targetY) {
        return script::projectResult(
            vm, self ? self->impl_->prepare(content, templates, binding, owner, faction, seed, targetX, targetY)
                     : unavailable<void>());
    });
    cls.addFunc("commit", [vm](MapRTSSpawn* self) {
        return script::projectResult(vm, self ? self->impl_->commit() : unavailable<void>());
    });
    cls.addFunc("advance", [vm](MapRTSSpawn* self, std::int64_t tick, std::int64_t nanoseconds) {
        return script::projectResult(vm, self ? self->impl_->advance(tick, nanoseconds) : unavailable<Value>(),
                                     [](Value value) { return value; });
    });
    cls.addFunc("captureState", [vm](MapRTSSpawn* self) {
        return script::projectResult(vm, self ? self->impl_->captureState() : unavailable<Value>(),
                                     [](Value value) { return value; });
    });
    cls.addFunc("close", [vm](MapRTSSpawn* self) {
        return script::projectResult(vm, self ? self->impl_->close() : unavailable<void>());
    });
}
}  // namespace eve::map_rts_spawn
