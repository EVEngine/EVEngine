#include "rts/RTSAction.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "action/Action.h"
#include "action/ActionCapture.h"
#include "common/Value.h"
#include "definitions/Definitions.h"
#include "rts/RTSActionCapture.h"
#include "rts/RTSArchetype.h"
#include "rts/RTSComponentAccessInternal.h"
#include "weapon/WeaponDefinitionRuntime.h"
#include "weapon/WeaponTypes.h"

// RTSAction.cpp
namespace eve::rts {
namespace {

bool sameHandle(const ecs::EntityHandle& left, const ecs::EntityHandle& right) {
    return left.table == right.table && left.type == right.type && left.id == right.id &&
           left.generation == right.generation;
}

bool terminal(action::ActionPhase phase) {
    return phase == action::ActionPhase::Completed || phase == action::ActionPhase::Cancelled ||
           phase == action::ActionPhase::Failed;
}

std::optional<LogicalId> actionIdFor(const OrderRecord& order) {
    if (!order.definitionId.empty()) return LogicalId::parse(order.definitionId);
    return LogicalId::fromParts("rts", "command." + std::string(orderKindName(order.kind)));
}

}  // namespace

namespace {
struct PendingAction {
    ecs::EntityHandle         unit;
    std::string               orderId;
    action::ActionExecutionId execution;
};
struct FrozenCorrelation {
    action::CapturedActionReference source;
    std::string                     orderId;
    action::ActionExecutionId       execution;
};
template <class T>
Result<T> correlationFailure(const char* message) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::Conflict, message, "rts.action.checkpoint"));
}
Result<void> validateCorrelations(const action::ActionCapture& actions, std::span<const FrozenCorrelation> pending) {
    const auto                rows = actions.executions();
    action::ActionExecutionId previous;
    for (std::size_t i = 0; i < pending.size(); ++i) {
        const auto& row = pending[i];
        if (row.execution.isZero() || row.execution <= previous || !row.source.subject.isValid())
            return correlationFailure<void>("Pending execution references must be ordered and unique");
        previous         = row.execution;
        const auto found = std::lower_bound(
            rows.begin(), rows.end(), row.execution,
            [](const auto& value, action::ActionExecutionId id) { return value.execution->id() < id; });
        if (found == rows.end() || found->execution->id() != row.execution || !found->source ||
            found->source->subject != row.source.subject || found->source->presence != row.source.presence)
            return correlationFailure<void>("Pending source/reference differs from the native action");
        if (row.source.presence == action::ActionEntityPresence::Present)
            for (std::size_t j = 0; j < i; ++j)
                if (pending[j].source.presence == action::ActionEntityPresence::Present &&
                    pending[j].source.subject == row.source.subject)
                    return correlationFailure<void>("One present Unit has multiple pending orders");
    }
    return Result<void>::success();
}
bool matchesUnit(ecs::EntityHandle handle, SubjectRef subject, ecs::Table& world) {
    if (handle.table != &world) return false;
    auto* unit = dynamic_cast<Unit*>(ecs::try_get(handle));
    if (!unit) return false;
    const auto* identity = detail::existingComponent<Unit::Identity>(*unit);
    return identity && identity->subject == subject;
}
}  // namespace
struct ActionAdapter::Impl {
    using Pending = PendingAction;

    explicit Impl(action::ActionRuntime& runtimeValue) : runtime(runtimeValue) {}

    action::ActionRuntime& runtime;
    std::vector<Pending>   pending;
};

struct RTSActionCapture::Impl {
    explicit Impl(action::ActionCapture value) : actions(std::move(value)) {}
    action::ActionCapture          actions;
    std::vector<FrozenCorrelation> pending;
};
struct PreparedRTSActionRestore::Impl {
    explicit Impl(action::PreparedActionRestore value) : actions(std::move(value)) {}
    action::PreparedActionRestore actions;
    std::weak_ptr<ecs::Table>     world;
    std::vector<PendingAction>    pending;
    std::vector<SubjectRef>       subjects;
};
RTSActionCapture::RTSActionCapture(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RTSActionCapture::~RTSActionCapture()                                      = default;
RTSActionCapture::RTSActionCapture(RTSActionCapture&&) noexcept            = default;
RTSActionCapture& RTSActionCapture::operator=(RTSActionCapture&&) noexcept = default;
std::optional<std::reference_wrapper<const action::ActionCapture>> RTSActionCapture::actions() const noexcept {
    if (!impl_) return std::nullopt;
    return std::cref(impl_->actions);
}
std::size_t              RTSActionCapture::pendingCount() const noexcept { return impl_ ? impl_->pending.size() : 0; }
Result<SnapshotEnvelope> RTSActionCapture::seal(Revision revision, SimulationTick tick,
                                                const SnapshotHashProvider& hash) const {
    using R = Result<SnapshotEnvelope>;
    if (!impl_ || revision.isZero())
        return correlationFailure<SnapshotEnvelope>("Moved action capture or zero save revision");
    auto valid = validateCorrelations(impl_->actions, impl_->pending);
    if (!valid) return R::failure(valid.status());
    const auto   epoch = impl_->actions.saveEpoch();
    Value::Array pending;
    for (const auto& row : impl_->pending)
        pending.push_back(Value::Array{row.source.subject.format(), int(row.source.presence), row.orderId,
                                       std::to_string(row.execution.value())});
    // Freeze every outer fact before the first unknown digest callback.
    auto actions = impl_->actions.seal(revision, tick, hash);
    if (!actions) return R::failure(actions.status());
    auto nested = snapshotEnvelopeValue(actions.value());
    if (!nested) return R::failure(nested.status());
    return makeSnapshotEnvelope(
        "eve.rts-actions", *LogicalId::fromParts("rts", "actions"), SchemaVersion(1), epoch, revision, tick,
        Value::object({{"actions", std::move(nested).takeValue()}, {"pending", std::move(pending)}}), hash);
}
Result<RTSActionCapture> RTSActionCapture::parse(SnapshotEnvelope snapshot, PersistentId epoch,
                                                 const SnapshotHashProvider& hash) {
    using R       = Result<RTSActionCapture>;
    auto verified = verifySnapshotEnvelope(snapshot, hash);
    if (!verified) return R::failure(verified.status());
    if (snapshot.schemaVersion != SchemaVersion(1))
        return R::failure(Diagnostic::error(DiagnosticCode::UnknownVersion, "Unknown RTS actions version"));
    if (snapshot.type != "eve.rts-actions" || snapshot.schema != *LogicalId::fromParts("rts", "actions") ||
        epoch.isNil() || snapshot.instanceId != epoch || snapshot.revision.isZero())
        return correlationFailure<RTSActionCapture>("RTS actions save boundary mismatch");
    const auto* root = snapshot.payload.getIf<Value::Object>();
    if (!root || root->size() != 2 || !root->contains("actions") || !root->contains("pending") ||
        !root->at("pending").isArray())
        return correlationFailure<RTSActionCapture>("Unknown or missing RTS actions fields");
    auto nested = parseSnapshotEnvelopeValue(root->at("actions"), hash);
    if (!nested) return R::failure(nested.status());
    if (nested.value().instanceId != epoch || nested.value().revision != snapshot.revision ||
        nested.value().tick != snapshot.tick)
        return correlationFailure<RTSActionCapture>("Nested actions use a different save boundary");
    auto actions = action::ActionCapture::parse(std::move(nested).takeValue(), epoch, hash);
    if (!actions) return R::failure(actions.status());
    auto result = std::make_unique<Impl>(std::move(actions).takeValue());
    for (const auto& value : *root->at("pending").getIf<Value::Array>()) {
        const auto* row = value.getIf<Value::Array>();
        if (!row || row->size() != 4 || !(*row)[0].isString() || !(*row)[1].isInt64() || (*row)[1].asInt() < 0 ||
            (*row)[1].asInt() > 1 || !(*row)[2].isString() || !(*row)[3].isString())
            return correlationFailure<RTSActionCapture>("Malformed RTS pending correlation");
        const auto& sourceText = *(*row)[0].getIf<std::string>();
        const auto  source     = PersistentId::parse(sourceText);
        if (!source || source->isNil() || source->format() != sourceText)
            return correlationFailure<RTSActionCapture>("Noncanonical RTS pending subject");
        const auto&   idText = *(*row)[3].getIf<std::string>();
        std::uint64_t id     = 0;
        const auto    parsed = std::from_chars(idText.data(), idText.data() + idText.size(), id);
        if (parsed.ec != std::errc{} || parsed.ptr != idText.data() + idText.size() || std::to_string(id) != idText)
            return correlationFailure<RTSActionCapture>("Noncanonical RTS execution reference");
        result->pending.push_back(
            {{SubjectRef::fromPersistentId(*source), action::ActionEntityPresence((*row)[1].asInt())},
             *(*row)[2].getIf<std::string>(),
             action::ActionExecutionId(id)});
    }
    auto valid = validateCorrelations(result->actions, result->pending);
    if (!valid) return R::failure(valid.status());
    return R::success(RTSActionCapture(std::move(result)));
}
PreparedRTSActionRestore::PreparedRTSActionRestore(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
PreparedRTSActionRestore::~PreparedRTSActionRestore()                                              = default;
PreparedRTSActionRestore::PreparedRTSActionRestore(PreparedRTSActionRestore&&) noexcept            = default;
PreparedRTSActionRestore& PreparedRTSActionRestore::operator=(PreparedRTSActionRestore&&) noexcept = default;

Result<RTSActionCapture> ActionAdapter::capture(PersistentId epoch, std::shared_ptr<ecs::Table> world,
                                                std::span<const action::ActionEntityBinding> bindings) const {
    using R      = Result<RTSActionCapture>;
    auto actions = impl_->runtime.capture(epoch, world, bindings);
    if (!actions) return R::failure(actions.status());
    auto candidate = std::make_unique<RTSActionCapture::Impl>(std::move(actions).takeValue());
    candidate->pending.reserve(impl_->pending.size());
    for (const auto& pending : impl_->pending) {
        const auto* original = impl_->runtime.find(pending.execution);
        if (!original || !original->request().source || !sameHandle(*original->request().source, pending.unit))
            return correlationFailure<RTSActionCapture>("Pending order has no matching native action source");
        const auto rows = candidate->actions.executions();
        const auto row  = std::find_if(rows.begin(), rows.end(),
                                       [&](const auto& value) { return value.execution->id() == pending.execution; });
        if (row == rows.end() || !row->source)
            return correlationFailure<RTSActionCapture>("Pending order is absent from frozen native actions");
        if (row->source->presence == action::ActionEntityPresence::Present &&
            !matchesUnit(pending.unit, row->source->subject, *world))
            return correlationFailure<RTSActionCapture>("Pending order source identity changed");
        candidate->pending.push_back({*row->source, pending.orderId, pending.execution});
    }
    auto valid = validateCorrelations(candidate->actions, candidate->pending);
    if (!valid) return R::failure(valid.status());
    return R::success(RTSActionCapture(std::move(candidate)));
}
Result<PreparedRTSActionRestore> PreparedRTSActionRestore::prepare(
    const RTSActionCapture& captured, std::shared_ptr<ecs::Table> world,
    std::span<const action::ActionEntityBinding> bindings) {
    using R = Result<PreparedRTSActionRestore>;
    if (!captured.impl_) return correlationFailure<PreparedRTSActionRestore>("RTS action capture was moved");
    auto valid = validateCorrelations(captured.impl_->actions, captured.impl_->pending);
    if (!valid) return R::failure(valid.status());
    auto actions = action::PreparedActionRestore::prepare(captured.impl_->actions, world, bindings);
    if (!actions) return R::failure(actions.status());
    auto candidate   = std::make_unique<Impl>(std::move(actions).takeValue());
    candidate->world = world;
    candidate->pending.reserve(captured.impl_->pending.size());
    candidate->subjects.reserve(captured.impl_->pending.size());
    for (const auto& row : captured.impl_->pending) {
        ecs::EntityHandle unit;
        if (row.source.presence == action::ActionEntityPresence::Present) {
            const auto binding = std::find_if(bindings.begin(), bindings.end(),
                                              [&](const auto& value) { return value.subject == row.source.subject; });
            if (binding == bindings.end() || !matchesUnit(binding->entity, row.source.subject, *world))
                return correlationFailure<PreparedRTSActionRestore>(
                    "Pending order requires the actual restored RTS identity");
            unit = binding->entity;
        }
        candidate->pending.push_back({unit, row.orderId, row.execution});
        candidate->subjects.push_back(row.source.subject);
    }
    return R::success(PreparedRTSActionRestore(std::move(candidate)));
}
Result<void> ActionAdapter::restore(PreparedRTSActionRestore&& prepared) {
    if (!prepared.impl_) return correlationFailure<void>("RTS action restore token consumed");
    auto world = prepared.impl_->world.lock();
    if (!world || world->is_deferred())
        return correlationFailure<void>("RTS action target world retired or active View");
    for (std::size_t i = 0; i < prepared.impl_->pending.size(); ++i) {
        const auto handle = prepared.impl_->pending[i].unit;
        if (handle.table && !matchesUnit(handle, prepared.impl_->subjects[i], *world))
            return correlationFailure<void>("Pending order source incarnation/identity changed after prepare");
    }
    auto restored = impl_->runtime.restore(std::move(prepared.impl_->actions));
    if (!restored) return Result<void>::failure(restored.status());
    impl_->pending.swap(prepared.impl_->pending);
    prepared.impl_.reset();
    return Result<void>::success(Status::success(StatusCode::Applied));
}

ActionAdapter::ActionAdapter(action::ActionRuntime& runtime) : impl_(std::make_unique<Impl>(runtime)) {}

ActionAdapter::~ActionAdapter() = default;

void ActionAdapter::clear() noexcept {
    if (impl_) impl_->pending.clear();
}

Result<ActionExecutionResult> ActionAdapter::execute(Unit& unit, const OrderRecord& order, const SimulationStep& step) {
    if (step.delta.nanoseconds() < 0)
        return Result<ActionExecutionResult>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "RTS action step delta must be non-negative", "step.delta"));
    if (!impl_)
        return Result<ActionExecutionResult>::failure(
            Diagnostic::error(DiagnosticCode::InvariantViolation, "RTS action adapter is not initialized", "adapter"));

    const ecs::EntityHandle unitHandle = ecs::handle_of(&unit);
    auto                    pendingForUnit =
        std::find_if(impl_->pending.begin(), impl_->pending.end(),
                     [&unitHandle](const Impl::Pending& pending) { return sameHandle(pending.unit, unitHandle); });

    if (pendingForUnit != impl_->pending.end() && pendingForUnit->orderId != order.id) {
        const auto* oldExecution = impl_->runtime.find(pendingForUnit->execution);
        if (oldExecution != nullptr && !terminal(oldExecution->phase())) {
            auto cancelled = impl_->runtime.cancel(pendingForUnit->execution, step.tick);
            if (!cancelled) return Result<ActionExecutionResult>::failure(cancelled.status());
        }
        impl_->pending.erase(pendingForUnit);
        pendingForUnit = impl_->pending.end();
    }

    action::ActionExecutionId executionId;
    if (pendingForUnit == impl_->pending.end()) {
        const auto logicalId = actionIdFor(order);
        if (!logicalId)
            return Result<ActionExecutionResult>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "RTS order definition is not a valid LogicalId",
                                  "order.definitionId"));

        action::ActionDefinition definition;
        definition.id = *logicalId;

        action::ActionRequest request;
        request.actionId      = definition.id;
        request.source             = unitHandle;
        const auto* sourceIdentity = detail::existingComponent<Unit::Identity>(unit);
        if (!sourceIdentity || !sourceIdentity->subject.isValid())
            return Result<ActionExecutionResult>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "RTS action source requires an existing stable identity"));
        request.sourceSubject = sourceIdentity->subject;
        request.requestedTick = step.tick;

        // Prepare correlation storage before publishing the native execution.
        // A failed allocation must not leave an untracked submitted action.
        std::string orderId = order.id;
        if (impl_->pending.size() == impl_->pending.capacity()) {
            const auto available = impl_->pending.max_size() - impl_->pending.size();
            if (available == 0)
                return Result<ActionExecutionResult>::failure(Diagnostic::error(
                    DiagnosticCode::PreconditionViolation, "RTS pending action capacity exhausted", "adapter.pending"));
            const auto growth = std::min(available, std::max(std::size_t(1), impl_->pending.size()));
            impl_->pending.reserve(impl_->pending.size() + growth);
        }
        auto submitted = impl_->runtime.submit(std::move(definition), std::move(request));
        if (!submitted) return Result<ActionExecutionResult>::failure(submitted.status());
        executionId = std::move(submitted).takeValue();
        impl_->pending.push_back({unitHandle, std::move(orderId), executionId});
    } else {
        executionId = pendingForUnit->execution;
    }

    auto advanced = impl_->runtime.advance(executionId, step.tick, step.delta);
    if (!advanced) {
        impl_->pending.erase(
            std::remove_if(impl_->pending.begin(), impl_->pending.end(),
                           [&executionId](const Impl::Pending& pending) { return pending.execution == executionId; }),
            impl_->pending.end());
        return Result<ActionExecutionResult>::failure(advanced.status());
    }

    const auto summary   = std::move(advanced).takeValue();
    bool       completed = summary.phase == action::ActionPhase::Completed;
    if (order.kind == OrderKind::Move && !unit.motion()->arrived) completed = false;
    if (completed) {
        impl_->pending.erase(
            std::remove_if(impl_->pending.begin(), impl_->pending.end(),
                           [&executionId](const Impl::Pending& pending) { return pending.execution == executionId; }),
            impl_->pending.end());
    }

    const ActionExecutionResult result{completed ? ActionDisposition::Completed : ActionDisposition::Pending};
    return Result<ActionExecutionResult>::success(
        result, Status::success(completed ? StatusCode::Applied : StatusCode::Pending));
}

}  // namespace eve::rts

// RTSArchetype.cpp
namespace eve::rts {
namespace {

const Value* field(const Value::Object& object, std::string_view name) {
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

Result<Value::Object> definitionObject(definitions::DefinitionRegistry& registry, std::string_view type,
                                       const LogicalId& id) {
    if (!id.isValid() || id.name().empty())
        return Result<Value::Object>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "RTS root requires a logical definition",
                              std::string(type) + ".definition", {}, "rts.archetype"));
    auto resolved = registry.resolve(std::string(type), std::string(id.name()));
    if (!resolved) return Result<Value::Object>::failure(resolved.status());
    auto parsed = Value::fromJson(resolved.value().get().json);
    if (!parsed) return Result<Value::Object>::failure(parsed.status());
    auto        value  = std::move(parsed).takeValue();
    const auto* object = value.getIf<Value::Object>();
    if (object == nullptr)
        return Result<Value::Object>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                "RTS definition must be a JSON object",
                                                                std::string(type) + ".json", {}, "rts.archetype"));
    return Result<Value::Object>::success(*object);
}

Result<float> number(const Value::Object& object, std::string_view name, float fallback,
                     float minimum = -std::numeric_limits<float>::max()) {
    const Value* source = field(object, name);
    double       value  = fallback;
    if (source != nullptr) {
        if (const auto* real = source->getIf<double>())
            value = *real;
        else if (const auto* integer = source->getIf<std::int64_t>())
            value = static_cast<double>(*integer);
        else
            return Result<float>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "RTS archetype numeric field must be a number",
                                                            std::string(name), {}, "rts.archetype"));
    }
    if (!std::isfinite(value) || value < minimum || value > std::numeric_limits<float>::max())
        return Result<float>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                        "RTS archetype numeric field is outside its valid range",
                                                        std::string(name), {}, "rts.archetype"));
    return Result<float>::success(static_cast<float>(value));
}

Result<int> integer(const Value::Object& object, std::string_view name, int fallback, int minimum = 0) {
    const Value* source = field(object, name);
    std::int64_t value  = fallback;
    if (source != nullptr) {
        if (const auto* exact = source->getIf<std::int64_t>())
            value = *exact;
        else
            return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                          "RTS archetype integer field must be an integer",
                                                          std::string(name), {}, "rts.archetype"));
    }
    if (value < minimum || value > std::numeric_limits<int>::max())
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                      "RTS archetype integer field is outside its valid range",
                                                      std::string(name), {}, "rts.archetype"));
    return Result<int>::success(static_cast<int>(value));
}

Result<bool> boolean(const Value::Object& object, std::string_view name, bool fallback) {
    const Value* source = field(object, name);
    if (source == nullptr) return Result<bool>::success(fallback);
    const auto* value = source->getIf<bool>();
    if (value == nullptr)
        return Result<bool>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "RTS archetype boolean field must be boolean", std::string(name),
                                                       {}, "rts.archetype"));
    return Result<bool>::success(*value);
}

Result<std::string> text(const Value::Object& object, std::string_view name, std::string fallback = {}) {
    const Value* source = field(object, name);
    if (source == nullptr) return Result<std::string>::success(std::move(fallback));
    const auto* value = source->getIf<std::string>();
    if (value == nullptr)
        return Result<std::string>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                              "RTS archetype text field must be a string",
                                                              std::string(name), {}, "rts.archetype"));
    return Result<std::string>::success(*value);
}

Result<TagSet> tags(const Value::Object& object, std::string_view name) {
    TagSet       result;
    const Value* source = field(object, name);
    if (source == nullptr) return Result<TagSet>::success(std::move(result));
    const auto* array = source->getIf<Value::Array>();
    if (array == nullptr)
        return Result<TagSet>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                         "RTS archetype tags must be an array", std::string(name), {},
                                                         "rts.archetype"));
    for (std::size_t index = 0; index < array->size(); ++index) {
        const auto* value = (*array)[index].getIf<std::string>();
        if (value == nullptr)
            return Result<TagSet>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "RTS archetype tag must be a string",
                                  std::string(name) + "[" + std::to_string(index) + "]", {}, "rts.archetype"));
        auto added = result.add(*value);
        if (!added) return Result<TagSet>::failure(added.status());
    }
    return Result<TagSet>::success(std::move(result));
}

}  // namespace

struct PreparedUnitArchetype::Impl {
    LogicalId                              definition;
    definitions::Definition                source;
    std::optional<definitions::Definition> weapon;
    float                                  health;
    float                                  speed;
    float                                  radius;
    float                                  sight;
    float                                  detection;
    float                                  radar;
    float                                  jamming;
    float                                  range;
    std::string                            role;
    std::string                            weaponType;
    TagSet                                 targetTags;
    float                                  capacity;
    std::string                            gatherResource;
    float                                  gather;
    float                                  buildRate;
    float                                  repairRate;
    float                                  captureRate;
    float                                  shield;
    float                                  shieldRegen;
    float                                  shieldDelay;
    int                                    transport;
    std::string                            airborne;
    float                                  suppression;
    float                                  suppressionRecovery;
    float                                  suppressedSpeed;
    float                                  suppressedDamage;
    bool                                   retreatEnabled;
    float                                  retreatThreshold;
    float                                  retreatDistance;
    float                                  moraleRange;
    float                                  moraleFactor;
    float                                  moraleRecovery;
    float                                  veteranThreshold;
    float                                  eliteThreshold;
    float                                  veteranDamage;
    float                                  eliteDamage;
    float                                  veteranHealth;
    float                                  eliteHealth;
    float                                  commandRange;
    int                                    commandCapacity;
    int                                    commandCost;
    int                                    commandPriority;
    float                                  commandJamming;
    bool                                   commandRequired;
    bool                                   relayUplink;
    float                                  outOfCommandSpeed;
    float                                  outOfCommandDamage;
    float                                  ammoCapacity;
    float                                  ammoRange;
    float                                  ammoRate;
    bool                                   ammoRelay;
    bool                                   autoResupply;
    float                                  autoThreshold;
    float                                  reserveAmmo;
    float                                  supplyPriority;
    float                                  firingTurnRate;
    float                                  firingArc;
    float                                  firingHeight;
    float                                  targetHeight;
};
PreparedUnitArchetype::PreparedUnitArchetype(std::shared_ptr<const Impl> impl) : impl_(std::move(impl)) {}
PreparedUnitArchetype::~PreparedUnitArchetype()                                                = default;
PreparedUnitArchetype::PreparedUnitArchetype(const PreparedUnitArchetype&) noexcept            = default;
PreparedUnitArchetype& PreparedUnitArchetype::operator=(const PreparedUnitArchetype&) noexcept = default;
PreparedUnitArchetype::PreparedUnitArchetype(PreparedUnitArchetype&&) noexcept                 = default;
PreparedUnitArchetype& PreparedUnitArchetype::operator=(PreparedUnitArchetype&&) noexcept      = default;
std::optional<std::reference_wrapper<const definitions::Definition>> PreparedUnitArchetype::source() const noexcept {
    if (!impl_) return std::nullopt;
    return std::cref(impl_->source);
}
std::optional<std::reference_wrapper<const definitions::Definition>> PreparedUnitArchetype::weaponSource()
    const noexcept {
    if (!impl_ || !impl_->weapon) return std::nullopt;
    return std::cref(*impl_->weapon);
}
Result<Value> PreparedUnitArchetype::encodeState() const {
    if (!impl_)
        return Result<Value>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, "Unit archetype moved"));
    auto record = [](const definitions::Definition& source) {
        return Value(Value::Array{source.type, source.id, std::int64_t(source.version.value()),
                                  std::to_string(source.generation.value()), source.json});
    };
    return Result<Value>::success(Value::object({{"schema", "rts.unit-archetype"},
                                                 {"version", 1},
                                                 {"unit", record(impl_->source)},
                                                 {"weapon", impl_->weapon ? record(*impl_->weapon) : Value{}}}));
}
Result<PreparedUnitArchetype> PreparedUnitArchetype::prepareState(const Value& state) {
    using R          = Result<PreparedUnitArchetype>;
    const auto* root = state.getIf<Value::Object>();
    if (!root || root->size() != 4 || !root->contains("schema") || !root->contains("version") ||
        !root->contains("unit") || !root->contains("weapon"))
        return R::failure(Diagnostic::error(DiagnosticCode::ParseError, "Unknown or missing archetype fields"));
    const auto* schema = root->at("schema").getIf<std::string>();
    if (!schema || *schema != "rts.unit-archetype")
        return R::failure(Diagnostic::error(DiagnosticCode::ParseError, "Invalid archetype schema"));
    if (!root->at("version").isInt64() || root->at("version").asInt() != 1)
        return R::failure(Diagnostic::error(DiagnosticCode::UnknownVersion, "Unknown archetype version"));
    auto record = [](const Value& value) -> Result<definitions::Definition> {
        using D           = Result<definitions::Definition>;
        const auto* tuple = value.getIf<Value::Array>();
        if (!tuple || tuple->size() != 5 || !(*tuple)[0].isString() || !(*tuple)[1].isString() ||
            !(*tuple)[2].isInt64() || !(*tuple)[3].isString() || !(*tuple)[4].isString() || (*tuple)[2].asInt() < 1 ||
            (*tuple)[2].asInt() > std::numeric_limits<std::uint32_t>::max())
            return D::failure(Diagnostic::error(DiagnosticCode::ParseError, "Invalid archetype source tuple"));
        const auto&   counter    = *(*tuple)[3].getIf<std::string>();
        std::uint64_t generation = 0;
        const auto    parsed     = std::from_chars(counter.data(), counter.data() + counter.size(), generation);
        if (parsed.ec != std::errc{} || parsed.ptr != counter.data() + counter.size() || !generation ||
            std::to_string(generation) != counter)
            return D::failure(Diagnostic::error(DiagnosticCode::ParseError, "Invalid canonical source generation"));
        return D::success(definitions::Definition{*(*tuple)[0].getIf<std::string>(), *(*tuple)[1].getIf<std::string>(),
                                                  SchemaVersion(static_cast<std::uint32_t>((*tuple)[2].asInt())),
                                                  Generation(generation), *(*tuple)[4].getIf<std::string>()});
    };
    auto source = record(root->at("unit"));
    if (!source) return R::failure(source.status());
    std::optional<definitions::Definition> weapon;
    if (!root->at("weapon").isNull()) {
        auto parsed = record(root->at("weapon"));
        if (!parsed) return R::failure(parsed.status());
        weapon = std::move(parsed).takeValue();
    }
    return prepareSources(source.value(), weapon ? &*weapon : nullptr);
}
Result<PreparedUnitArchetype> PreparedUnitArchetype::prepare(definitions::DefinitionRegistry& registry,
                                                             LogicalId                        definition) {
    using R = Result<PreparedUnitArchetype>;
    if (!definition.isValid() || definition.namespaceName() != "unit")
        return R::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Expected unit logical definition"));
    auto resolved = registry.resolve("unit", std::string(definition.name()));
    if (!resolved) return R::failure(resolved.status());
    const auto source = resolved.value().get();
    if (source.version != SchemaVersion(1))
        return R::failure(Diagnostic::error(DiagnosticCode::Unsupported, "Unsupported unit archetype schema version"));
    auto parsed = Value::fromJson(source.json);
    if (!parsed) return R::failure(parsed.status());
    const auto* object = parsed.value().getIf<Value::Object>();
    if (!object) return R::failure(Diagnostic::error(DiagnosticCode::ParseError, "Unit source must be an object"));
    auto name = text(*object, "weaponType");
    if (!name) return R::failure(name.status());
    std::optional<definitions::Definition> weapon;
    if (!name.value().empty()) {
        auto resolvedWeapon = registry.resolve("weapon", name.value());
        if (!resolvedWeapon) return R::failure(resolvedWeapon.status());
        weapon = resolvedWeapon.value().get();
    }
    return prepareSources(source, weapon ? &*weapon : nullptr);
}
Result<PreparedUnitArchetype> PreparedUnitArchetype::prepareSources(const definitions::Definition& source,
                                                                    const definitions::Definition* weapon) {
    using R      = Result<PreparedUnitArchetype>;
    auto logical = LogicalId::fromParts(source.type, source.id);
    if (!logical || source.type != "unit" || source.generation.isZero())
        return R::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Exact unit source identity/generation required"));
    if (source.version != SchemaVersion(1))
        return R::failure(Diagnostic::error(DiagnosticCode::Unsupported, "Unsupported unit archetype schema version"));
    auto definition = *logical;
    auto parsed     = Value::fromJson(source.json);
    if (!parsed) return R::failure(parsed.status());
    const auto* view = parsed.value().getIf<Value::Object>();
    if (!view) return R::failure(Diagnostic::error(DiagnosticCode::ParseError, "Unit source must be an object"));
    const auto& object     = *view;
    auto health     = number(object, "health", 1.0f, 0.0f);
    if (!health) return Result<PreparedUnitArchetype>::failure(health.status());
    auto speed      = number(object, "speed", 1.0f, 0.0f);
    if (!speed) return Result<PreparedUnitArchetype>::failure(speed.status());
    auto radius     = number(object, "radius", 0.5f, 0.0f);
    if (!radius) return Result<PreparedUnitArchetype>::failure(radius.status());
    auto sight      = number(object, "sightRange", 0.0f, 0.0f);
    if (!sight) return Result<PreparedUnitArchetype>::failure(sight.status());
    auto detection  = number(object, "detectionRange", 0.0f, 0.0f);
    if (!detection) return Result<PreparedUnitArchetype>::failure(detection.status());
    auto radar      = number(object, "radarRange", 0.0f, 0.0f);
    if (!radar) return Result<PreparedUnitArchetype>::failure(radar.status());
    auto jamming    = number(object, "jammingRange", 0.0f, 0.0f);
    if (!jamming) return Result<PreparedUnitArchetype>::failure(jamming.status());
    auto range      = number(object, "attackRange", 0.0f, 0.0f);
    if (!range) return Result<PreparedUnitArchetype>::failure(range.status());
    auto role       = text(object, "role");
    if (!role) return Result<PreparedUnitArchetype>::failure(role.status());
    auto weaponType = text(object, "weaponType");
    if (!weaponType) return Result<PreparedUnitArchetype>::failure(weaponType.status());
    auto targetTags = tags(object, "targetTags");
    if (!targetTags) return Result<PreparedUnitArchetype>::failure(targetTags.status());

    auto capacity            = number(object, "cargoCapacity", role.value() == "worker" ? 10.0f : 0.0f, 0.0f);
    if (!capacity) return Result<PreparedUnitArchetype>::failure(capacity.status());
    auto gatherResource      = text(object, "gatherResource", role.value() == "worker" ? "minerals" : "");
    if (!gatherResource) return Result<PreparedUnitArchetype>::failure(gatherResource.status());
    if (!gatherResource.value().empty()) {
        auto validResource = resource::ResourceId::parse(gatherResource.value());
        if (!validResource) return Result<PreparedUnitArchetype>::failure(validResource.status());
    }
    auto gather              = number(object, "gatherRate", role.value() == "worker" ? 5.0f : 0.0f, 0.0f);
    if (!gather) return Result<PreparedUnitArchetype>::failure(gather.status());
    auto buildRate           = number(object, "buildRate", 1.0f, 0.0f);
    if (!buildRate) return Result<PreparedUnitArchetype>::failure(buildRate.status());
    auto repairRate          = number(object, "repairRate", 0.0f, 0.0f);
    if (!repairRate) return Result<PreparedUnitArchetype>::failure(repairRate.status());
    auto captureRate         = number(object, "captureRate", 0.0f, 0.0f);
    if (!captureRate) return Result<PreparedUnitArchetype>::failure(captureRate.status());
    auto shield              = number(object, "shieldCapacity", 0.0f, 0.0f);
    if (!shield) return Result<PreparedUnitArchetype>::failure(shield.status());
    auto shieldRegen         = number(object, "shieldRegenRate", 0.0f, 0.0f);
    if (!shieldRegen) return Result<PreparedUnitArchetype>::failure(shieldRegen.status());
    auto shieldDelay         = number(object, "shieldRegenDelay", 0.0f, 0.0f);
    if (!shieldDelay) return Result<PreparedUnitArchetype>::failure(shieldDelay.status());
    auto transport           = integer(object, "transportCapacity", 0);
    if (!transport) return Result<PreparedUnitArchetype>::failure(transport.status());
    auto airborne            = text(object, "targetDomain");
    if (!airborne) return Result<PreparedUnitArchetype>::failure(airborne.status());
    auto suppression         = number(object, "suppressionCapacity", 0.0f, 0.0f);
    if (!suppression) return Result<PreparedUnitArchetype>::failure(suppression.status());
    auto suppressionRecovery = number(object, "suppressionRecoveryRate", 0.0f, 0.0f);
    if (!suppressionRecovery) return Result<PreparedUnitArchetype>::failure(suppressionRecovery.status());
    auto suppressedSpeed     = number(object, "suppressedSpeedFactor", 1.0f, 0.0f);
    if (!suppressedSpeed) return Result<PreparedUnitArchetype>::failure(suppressedSpeed.status());
    auto suppressedDamage    = number(object, "suppressedDamageFactor", 1.0f, 0.0f);
    if (!suppressedDamage) return Result<PreparedUnitArchetype>::failure(suppressedDamage.status());
    auto retreatEnabled      = boolean(object, "suppressionRetreatEnabled", false);
    if (!retreatEnabled) return Result<PreparedUnitArchetype>::failure(retreatEnabled.status());
    auto retreatThreshold    = number(object, "suppressionRetreatThreshold", 0.8f, 0.0f);
    if (!retreatThreshold) return Result<PreparedUnitArchetype>::failure(retreatThreshold.status());
    auto retreatDistance     = number(object, "suppressionRetreatDistance", 6.0f, 0.0f);
    if (!retreatDistance) return Result<PreparedUnitArchetype>::failure(retreatDistance.status());
    auto moraleRange         = number(object, "moraleAuraRange", 0.0f, 0.0f);
    if (!moraleRange) return Result<PreparedUnitArchetype>::failure(moraleRange.status());
    auto moraleFactor        = number(object, "moraleAuraSuppressionFactor", 1.0f, 0.0f);
    if (!moraleFactor) return Result<PreparedUnitArchetype>::failure(moraleFactor.status());
    auto moraleRecovery      = number(object, "moraleAuraRecoveryBonus", 0.0f, 0.0f);
    if (!moraleRecovery) return Result<PreparedUnitArchetype>::failure(moraleRecovery.status());
    auto veteranThreshold    = number(object, "veteranThreshold", 0.0f, 0.0f);
    if (!veteranThreshold) return Result<PreparedUnitArchetype>::failure(veteranThreshold.status());
    auto eliteThreshold      = number(object, "eliteThreshold", 0.0f, 0.0f);
    if (!eliteThreshold) return Result<PreparedUnitArchetype>::failure(eliteThreshold.status());
    auto veteranDamage       = number(object, "veteranDamageFactor", 1.0f, 0.0f);
    if (!veteranDamage) return Result<PreparedUnitArchetype>::failure(veteranDamage.status());
    auto eliteDamage         = number(object, "eliteDamageFactor", 1.0f, 0.0f);
    if (!eliteDamage) return Result<PreparedUnitArchetype>::failure(eliteDamage.status());
    auto veteranHealth       = number(object, "veteranHealthFactor", 1.0f, 0.0f);
    if (!veteranHealth) return Result<PreparedUnitArchetype>::failure(veteranHealth.status());
    auto eliteHealth         = number(object, "eliteHealthFactor", 1.0f, 0.0f);
    if (!eliteHealth) return Result<PreparedUnitArchetype>::failure(eliteHealth.status());
    auto commandRange        = number(object, "commandRange", 0.0f, 0.0f);
    if (!commandRange) return Result<PreparedUnitArchetype>::failure(commandRange.status());
    auto commandCapacity     = integer(object, "commandCapacity", 0);
    if (!commandCapacity) return Result<PreparedUnitArchetype>::failure(commandCapacity.status());
    auto commandCost         = integer(object, "commandCost", 1);
    if (!commandCost) return Result<PreparedUnitArchetype>::failure(commandCost.status());
    auto commandPriority     = integer(object, "commandPriority", 0);
    if (!commandPriority) return Result<PreparedUnitArchetype>::failure(commandPriority.status());
    auto commandJamming      = number(object, "commandJammingRange", 0.0f, 0.0f);
    if (!commandJamming) return Result<PreparedUnitArchetype>::failure(commandJamming.status());
    auto commandRequired     = boolean(object, "requiresCommand", false);
    if (!commandRequired) return Result<PreparedUnitArchetype>::failure(commandRequired.status());
    auto relayUplink         = boolean(object, "commandRelayRequiresUplink", false);
    if (!relayUplink) return Result<PreparedUnitArchetype>::failure(relayUplink.status());
    auto outOfCommandSpeed   = number(object, "outOfCommandSpeedFactor", 1.0f, 0.0f);
    if (!outOfCommandSpeed) return Result<PreparedUnitArchetype>::failure(outOfCommandSpeed.status());
    auto outOfCommandDamage  = number(object, "outOfCommandDamageFactor", 1.0f, 0.0f);
    if (!outOfCommandDamage) return Result<PreparedUnitArchetype>::failure(outOfCommandDamage.status());
    auto ammoCapacity        = number(object, "ammoSupplyCapacity", 0.0f, 0.0f);
    if (!ammoCapacity) return Result<PreparedUnitArchetype>::failure(ammoCapacity.status());
    auto ammoRange           = number(object, "ammoSupplyRange", 0.0f, 0.0f);
    if (!ammoRange) return Result<PreparedUnitArchetype>::failure(ammoRange.status());
    auto ammoRate            = number(object, "ammoSupplyRate", 0.0f, 0.0f);
    if (!ammoRate) return Result<PreparedUnitArchetype>::failure(ammoRate.status());
    auto ammoRelay           = boolean(object, "ammoRelayEnabled", false);
    if (!ammoRelay) return Result<PreparedUnitArchetype>::failure(ammoRelay.status());
    auto autoResupply        = boolean(object, "autoResupplyEnabled", false);
    if (!autoResupply) return Result<PreparedUnitArchetype>::failure(autoResupply.status());
    auto autoThreshold       = number(object, "autoResupplyThreshold", 0.5f, 0.0f);
    if (!autoThreshold) return Result<PreparedUnitArchetype>::failure(autoThreshold.status());
    auto reserveAmmo         = number(object, "reserveAmmoCapacity", 0.0f, 0.0f);
    if (!reserveAmmo) return Result<PreparedUnitArchetype>::failure(reserveAmmo.status());
    auto supplyPriority      = number(object, "supplyPriority", 1.0f, 0.0f);
    if (!supplyPriority) return Result<PreparedUnitArchetype>::failure(supplyPriority.status());
    auto firingTurnRate      = number(object, "turretTurnRate", 0.0f, 0.0f);
    if (!firingTurnRate) return Result<PreparedUnitArchetype>::failure(firingTurnRate.status());
    auto firingArc           = number(object, "firingArc", 0.01f, 0.0f);
    if (!firingArc) return Result<PreparedUnitArchetype>::failure(firingArc.status());
    auto firingHeight        = number(object, "firingHeight", 1.0f, 0.000001f);
    if (!firingHeight) return Result<PreparedUnitArchetype>::failure(firingHeight.status());
    auto targetHeight        = number(object, "targetHeight", 1.0f, 0.000001f);
    if (!targetHeight) return Result<PreparedUnitArchetype>::failure(targetHeight.status());

    auto impl        = std::make_shared<Impl>();
    impl->definition = std::move(definition);
    impl->source     = std::move(source);
    if (!weaponType.value().empty()) {
        if (!weapon || weapon->type != "weapon" || weapon->id != weaponType.value() || weapon->generation.isZero())
            return Result<PreparedUnitArchetype>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "Exact referenced weapon source required"));
        if (weapon->version != SchemaVersion(1))
            return Result<PreparedUnitArchetype>::failure(
                Diagnostic::error(DiagnosticCode::Unsupported, "Unsupported captured weapon schema version"));
        auto admitted = weapon::parseWeaponDefinition(*weapon);
        if (!admitted) return Result<PreparedUnitArchetype>::failure(admitted.status());
        impl->weapon = *weapon;
    }
    if (weaponType.value().empty() && weapon)
        return Result<PreparedUnitArchetype>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Unreferenced weapon source in unit recipe"));
    impl->health              = std::move(health).takeValue();
    impl->speed               = std::move(speed).takeValue();
    impl->radius              = std::move(radius).takeValue();
    impl->sight               = std::move(sight).takeValue();
    impl->detection           = std::move(detection).takeValue();
    impl->radar               = std::move(radar).takeValue();
    impl->jamming             = std::move(jamming).takeValue();
    impl->range               = std::move(range).takeValue();
    impl->role                = std::move(role).takeValue();
    impl->weaponType          = std::move(weaponType).takeValue();
    impl->targetTags          = std::move(targetTags).takeValue();
    impl->capacity            = std::move(capacity).takeValue();
    impl->gatherResource      = std::move(gatherResource).takeValue();
    impl->gather              = std::move(gather).takeValue();
    impl->buildRate           = std::move(buildRate).takeValue();
    impl->repairRate          = std::move(repairRate).takeValue();
    impl->captureRate         = std::move(captureRate).takeValue();
    impl->shield              = std::move(shield).takeValue();
    impl->shieldRegen         = std::move(shieldRegen).takeValue();
    impl->shieldDelay         = std::move(shieldDelay).takeValue();
    impl->transport           = std::move(transport).takeValue();
    impl->airborne            = std::move(airborne).takeValue();
    impl->suppression         = std::move(suppression).takeValue();
    impl->suppressionRecovery = std::move(suppressionRecovery).takeValue();
    impl->suppressedSpeed     = std::move(suppressedSpeed).takeValue();
    impl->suppressedDamage    = std::move(suppressedDamage).takeValue();
    impl->retreatEnabled      = std::move(retreatEnabled).takeValue();
    impl->retreatThreshold    = std::move(retreatThreshold).takeValue();
    impl->retreatDistance     = std::move(retreatDistance).takeValue();
    impl->moraleRange         = std::move(moraleRange).takeValue();
    impl->moraleFactor        = std::move(moraleFactor).takeValue();
    impl->moraleRecovery      = std::move(moraleRecovery).takeValue();
    impl->veteranThreshold    = std::move(veteranThreshold).takeValue();
    impl->eliteThreshold      = std::move(eliteThreshold).takeValue();
    impl->veteranDamage       = std::move(veteranDamage).takeValue();
    impl->eliteDamage         = std::move(eliteDamage).takeValue();
    impl->veteranHealth       = std::move(veteranHealth).takeValue();
    impl->eliteHealth         = std::move(eliteHealth).takeValue();
    impl->commandRange        = std::move(commandRange).takeValue();
    impl->commandCapacity     = std::move(commandCapacity).takeValue();
    impl->commandCost         = std::move(commandCost).takeValue();
    impl->commandPriority     = std::move(commandPriority).takeValue();
    impl->commandJamming      = std::move(commandJamming).takeValue();
    impl->commandRequired     = std::move(commandRequired).takeValue();
    impl->relayUplink         = std::move(relayUplink).takeValue();
    impl->outOfCommandSpeed   = std::move(outOfCommandSpeed).takeValue();
    impl->outOfCommandDamage  = std::move(outOfCommandDamage).takeValue();
    impl->ammoCapacity        = std::move(ammoCapacity).takeValue();
    impl->ammoRange           = std::move(ammoRange).takeValue();
    impl->ammoRate            = std::move(ammoRate).takeValue();
    impl->ammoRelay           = std::move(ammoRelay).takeValue();
    impl->autoResupply        = std::move(autoResupply).takeValue();
    impl->autoThreshold       = std::move(autoThreshold).takeValue();
    impl->reserveAmmo         = std::move(reserveAmmo).takeValue();
    impl->supplyPriority      = std::move(supplyPriority).takeValue();
    impl->firingTurnRate      = std::move(firingTurnRate).takeValue();
    impl->firingArc           = std::move(firingArc).takeValue();
    impl->firingHeight        = std::move(firingHeight).takeValue();
    impl->targetHeight        = std::move(targetHeight).takeValue();
    return Result<PreparedUnitArchetype>::success(PreparedUnitArchetype(std::move(impl)));
}
Result<void> PreparedUnitArchetype::apply(Unit& unit, const CapturedArchetypeWeaponFactory& weaponFactory) const {
    if (!impl_ || unit.definition()->id != impl_->definition)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "Unit does not match prepared archetype", "unit.definition"));
    weapon::WeaponEntity* weapon = nullptr;
    if (!impl_->weaponType.empty()) {
        if (!weaponFactory)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "RTS unit definition requires a weapon factory",
                                                           "unit.weaponType", {}, "rts.archetype"));
        auto created = weaponFactory(*impl_->weapon, unit.identity()->subject.persistentId());
        if (!created) return Result<void>::failure(created.status());
        weapon = std::move(created).takeValue();
        if (weapon == nullptr)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvariantViolation, "Captured weapon factory returned no entity", "unit.weaponType"));
    }

    unit.motion()->speed                     = impl_->speed;
    unit.motion()->airborne                  = impl_->airborne == "air";
    unit.crowd()->radius                     = impl_->radius;
    unit.vision()->sightRange                = impl_->sight;
    unit.vision()->detectionRange            = impl_->detection;
    unit.vision()->radarRange                = impl_->radar;
    unit.vision()->jammingRange              = impl_->jamming;
    unit.combat()->acquisitionRange          = std::max(impl_->sight, impl_->range);
    unit.combat()->engagementRange           = impl_->range;
    unit.combat()->leashRange                = std::max(impl_->sight, impl_->range) * 1.5f;
    unit.durability()->state.subject         = unit.identity()->subject;
    unit.durability()->state.health          = impl_->health;
    unit.durability()->state.maxHealth       = impl_->health;
    unit.durability()->alive                 = impl_->health > 0.0f;
    unit.worker()->capacity                  = impl_->capacity;
    unit.worker()->resourceType              = impl_->gatherResource;
    unit.worker()->gatherRate                = impl_->gather;
    unit.worker()->buildRate                 = impl_->buildRate;
    unit.worker()->repairRate                = impl_->repairRate;
    unit.worker()->autoAssign                = impl_->role == "worker";
    unit.capture()->rate                     = impl_->captureRate;
    unit.shield()->capacity                  = impl_->shield;
    unit.shield()->value                     = impl_->shield;
    unit.shield()->regenRate                 = impl_->shieldRegen;
    unit.shield()->regenDelay                = impl_->shieldDelay;
    unit.containment()->capacity             = static_cast<std::size_t>(impl_->transport);
    unit.morale()->capacity                  = impl_->suppression;
    unit.morale()->recoveryRate              = impl_->suppressionRecovery;
    unit.morale()->suppressedSpeedFactor     = impl_->suppressedSpeed;
    unit.morale()->suppressedDamageFactor    = impl_->suppressedDamage;
    unit.morale()->retreatEnabled            = impl_->retreatEnabled;
    unit.morale()->retreatThreshold          = impl_->retreatThreshold;
    unit.morale()->retreatDistance           = impl_->retreatDistance;
    unit.morale()->auraRange                 = impl_->moraleRange;
    unit.morale()->auraSuppressionFactor     = impl_->moraleFactor;
    unit.morale()->auraRecoveryBonus         = impl_->moraleRecovery;
    unit.veterancy()->veteranThreshold       = impl_->veteranThreshold;
    unit.veterancy()->eliteThreshold         = impl_->eliteThreshold;
    unit.veterancy()->veteranDamageFactor    = impl_->veteranDamage;
    unit.veterancy()->eliteDamageFactor      = impl_->eliteDamage;
    unit.veterancy()->veteranHealthFactor    = impl_->veteranHealth;
    unit.veterancy()->eliteHealthFactor      = impl_->eliteHealth;
    unit.command()->range                    = impl_->commandRange;
    unit.command()->capacity                 = impl_->commandCapacity;
    unit.command()->cost                     = impl_->commandCost;
    unit.command()->priority                 = impl_->commandPriority;
    unit.command()->jammingRange             = impl_->commandJamming;
    unit.command()->requiresCommand          = impl_->commandRequired;
    unit.command()->relayRequiresUplink      = impl_->relayUplink;
    unit.command()->outOfCommandSpeedFactor  = impl_->outOfCommandSpeed;
    unit.command()->outOfCommandDamageFactor = impl_->outOfCommandDamage;
    unit.supply()->capacity                  = std::max(impl_->ammoCapacity, impl_->reserveAmmo);
    unit.supply()->stock                     = impl_->ammoCapacity;
    unit.supply()->range                     = impl_->ammoRange;
    unit.supply()->transferRate              = impl_->ammoRate;
    unit.supply()->relayEnabled              = impl_->ammoRelay;
    unit.supply()->autoDispatch              = impl_->autoResupply;
    unit.supply()->autoThreshold             = impl_->autoThreshold;
    unit.supply()->priority                  = impl_->supplyPriority;
    unit.combat()->turnRateDegrees           = impl_->firingTurnRate;
    unit.combat()->aimToleranceDegrees       = impl_->firingArc;
    unit.combat()->firingHeight              = impl_->firingHeight;
    unit.combat()->targetHeight              = impl_->targetHeight;
    unit.tags()->values                      = impl_->targetTags;
    if (weapon != nullptr) {
        auto link = WeaponLink::bind(ecs::handle_of(weapon));
        if (!link) return Result<void>::failure(link.status());
        unit.weapon()->link = std::move(link).takeValue();
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> RTSArchetypeMaterializer::apply(definitions::DefinitionRegistry& registry, Building& building,
                                             const ArchetypeWeaponFactory& weaponFactory) {
    auto objectResult = definitionObject(registry, "building", building.definition()->id);
    if (!objectResult) return Result<void>::failure(objectResult.status());
    const auto object          = std::move(objectResult).takeValue();
    auto       health          = number(object, "health", 1.0f, 0.0f);
    auto       radius          = number(object, "radius", 0.5f, 0.0f);
    auto       buildTime       = number(object, "buildTime", 0.0f, 0.0f);
    auto       sight           = number(object, "sightRange", 0.0f, 0.0f);
    auto       weaponType      = text(object, "weaponType");
    auto       garrison        = integer(object, "garrisonCapacity", 0);
    auto       capturable      = boolean(object, "capturable", false);
    auto       captureTime     = number(object, "captureTime", 5.0f, 0.000001f);
    auto       powerProduced   = number(object, "powerProvided", 0.0f, 0.0f);
    auto       powerUsed       = number(object, "powerUsed", 0.0f, 0.0f);
    auto       powerPriority   = integer(object, "powerPriority", 0);
    auto       influence       = number(object, "buildInfluenceRadius", 0.0f, 0.0f);
    auto       incomeResource  = text(object, "incomeResource");
    auto       dropoffResources = tags(object, "dropoffResources");
    if (!dropoffResources) return Result<void>::failure(dropoffResources.status());
    for (const auto& name : dropoffResources.value().values()) {
        auto validResource = resource::ResourceId::parse(name);
        if (!validResource) return Result<void>::failure(validResource.status());
    }
    auto       incomeRate      = number(object, "incomeRate", 0.0f, 0.0f);
    auto       repairCost      = number(object, "repairCostPerHealth", 0.0f, 0.0f);
    auto       repairResource  = text(object, "repairResource", "minerals");
    auto       shield          = number(object, "shieldCapacity", 0.0f, 0.0f);
    auto       shieldRegen     = number(object, "shieldRegenRate", 0.0f, 0.0f);
    auto       shieldDelay     = number(object, "shieldRegenDelay", 0.0f, 0.0f);
    auto       garrisonBonus   = number(object, "garrisonDamageBonus", 0.0f, 0.0f);
    auto       ammoCapacity    = number(object, "ammoSupplyCapacity", 0.0f, 0.0f);
    auto       ammoRange       = number(object, "ammoSupplyRange", 0.0f, 0.0f);
    auto       ammoRate        = number(object, "ammoSupplyRate", 0.0f, 0.0f);
    auto       ammoResource    = text(object, "ammoProductionResource");
    auto       ammoCost        = number(object, "ammoProductionCost", 0.0f, 0.0f);
    auto       ammoProduction  = number(object, "ammoProductionRate", 0.0f, 0.0f);
    auto       commandRange    = number(object, "commandRange", 0.0f, 0.0f);
    auto       commandCapacity = integer(object, "commandCapacity", 0);
    auto       commandJamming  = number(object, "commandJammingRange", 0.0f, 0.0f);
    auto       turretTurn      = number(object, "turretTurnRate", 0.0f, 0.0f);
    auto       firingArc       = number(object, "firingArc", 0.01f, 0.0f);
    auto       firingHeight    = number(object, "firingHeight", 2.0f, 0.000001f);
    auto       targetHeight    = number(object, "targetHeight", 1.5f, 0.000001f);
    if (!health || !radius || !buildTime || !sight || !weaponType || !garrison || !capturable || !captureTime ||
        !powerProduced || !powerUsed || !powerPriority || !influence || !incomeResource || !incomeRate || !repairCost ||
        !repairResource || !shield || !shieldRegen || !shieldDelay || !garrisonBonus || !ammoCapacity || !ammoRange ||
        !ammoRate || !ammoResource || !ammoCost || !ammoProduction || !commandRange || !commandCapacity ||
        !commandJamming || !turretTurn || !firingArc || !firingHeight || !targetHeight)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "RTS building definition contains an invalid field", "building",
                                                       {}, "rts.archetype"));

    weapon::WeaponEntity* weapon = nullptr;
    if (!weaponType.value().empty()) {
        if (!weaponFactory)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "RTS building definition requires a weapon factory",
                                                           "building.weaponType", {}, "rts.archetype"));
        auto created = weaponFactory(weaponType.value(), building.identity()->subject.persistentId());
        if (!created) return Result<void>::failure(created.status());
        weapon = std::move(created).takeValue();
    }

    building.placement()->placed                    = true;
    building.construction()->buildTimeSeconds       = buildTime.value();
    building.integrity()->state.subject             = building.identity()->subject;
    building.integrity()->state.health              = health.value();
    building.integrity()->state.maxHealth           = health.value();
    building.integrity()->alive                     = health.value() > 0.0f;
    building.integrity()->repairCostPerHealth       = repairCost.value();
    building.integrity()->repairResource            = std::move(repairResource).takeValue();
    building.shield()->capacity                     = shield.value();
    building.shield()->value                        = shield.value();
    building.shield()->regenRate                    = shieldRegen.value();
    building.shield()->regenDelay                   = shieldDelay.value();
    building.vision()->sightRange                   = sight.value();
    building.garrison()->capacity                   = static_cast<std::size_t>(garrison.value());
    building.garrison()->damageBonusPerOccupant     = garrisonBonus.value();
    building.capture()->capturable                  = capturable.value();
    building.capture()->durationSeconds             = captureTime.value();
    building.infrastructure()->powerProduced        = powerProduced.value();
    building.infrastructure()->powerConsumed        = powerUsed.value();
    building.infrastructure()->powerPriority        = powerPriority.value();
    building.infrastructure()->buildInfluenceRadius = influence.value();
    building.infrastructure()->incomeResource       = std::move(incomeResource).takeValue();
    building.infrastructure()->incomeRate           = incomeRate.value();
    building.supply()->capacity                     = ammoCapacity.value();
    building.supply()->stock                        = ammoCapacity.value();
    building.supply()->range                        = ammoRange.value();
    building.supply()->transferRate                 = ammoRate.value();
    building.supply()->productionResource           = std::move(ammoResource).takeValue();
    building.supply()->productionCostPerRound       = static_cast<std::int64_t>(std::ceil(ammoCost.value()));
    building.supply()->productionRate               = ammoProduction.value();
    building.command()->range                       = commandRange.value();
    building.command()->capacity                    = commandCapacity.value();
    building.command()->jammingRange                = commandJamming.value();
    building.command()->active                      = commandRange.value() > 0.0f;
    building.combat()->firingHeight                 = firingHeight.value();
    building.combat()->targetHeight                 = targetHeight.value();
    building.dropoff()->radius                      = radius.value();
    if (const auto dropoff = boolean(object, "dropoff", false); dropoff && dropoff.value())
        building.dropoff()->acceptedResources = field(object, "dropoffResources")
                                                    ? dropoffResources.value().values()
                                                    : std::vector<std::string>{"minerals"};
    if (weapon != nullptr) {
        auto link = WeaponLink::bind(ecs::handle_of(weapon));
        if (!link) return Result<void>::failure(link.status());
        building.weapon()->link                = std::move(link).takeValue();
        building.combat()->acquisitionRange    = sight.value();
        building.combat()->engagementRange     = sight.value();
        building.combat()->turnRateDegrees     = turretTurn.value();
        building.combat()->aimToleranceDegrees = firingArc.value();
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

}  // namespace eve::rts
